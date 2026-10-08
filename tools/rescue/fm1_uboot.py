#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
# Ported routines (the CRC cipher, the UBOOT1.00 / LoaderV2 command codes and framing) from jl-uboot-tool,
# MIT, Copyright (c) 2023 Andrey Grigoryev (kagaimiq), <https://github.com/kagaimiq/jl-uboot-tool>;
# LICENSES/MIT-jl-uboot-tool.txt. Protocol facts: FM-1-transporter docs/PROTOCOL.md (kurogedelic).
"""Back up and restore an FM-1's flash from a Mac through the chip's mask-ROM boot mode (UBOOT1.00), with no
extra hardware. Full instructions: docs/RECOVERY.md.

  sudo python3 tools/rescue/fm1_uboot.py backup  --loader wl82loader.bin OUT.bin
  sudo python3 tools/rescue/fm1_uboot.py restore --loader wl82loader.bin FM-1.fwsc BACKUP.bin [--write]

The FM-1 must be in UBOOT (USB 4C4A:8057 "WL80UBOOT1.00"). macOS's mass-storage driver holds the device, so
the tool needs sudo to detach it. Each run is one boot-mode session: when it ends, the FM-1 resets and boots
its flash, so enter UBOOT again before the next run.

backup  reads the whole 1 MiB twice and saves it only if both reads match. It sends no flash erase or write.
restore puts M-VAVE's stock V15 app back. It is a dry run unless --write, and refuses unless the package is stock
        V15 (sha256 db1642b2...), its image's head equals the device's, and the device's head and data
        ([0, 0x4000) and [0x93000, 1 MiB)) equal the backup. It writes only whole 4 KiB sectors in
        [0x4000, 0x93000) that differ (erase, 512 B writes, read back; 3 tries each), never a block or the
        chip, never the head or the chip key, then reads everything back: [0, 0x93000) must be the stock
        image and the rest the backup. An interrupted --write can simply be run again.
"""
import argparse
import ctypes.util
import hashlib
import os
import struct
import sys
import time

try:
    import usb.backend.libusb1
    import usb.core
    import usb.util
except ImportError:
    usb = None                  # (the host test runs without pyusb: it fakes the transport)

VID, PID = 0x4C4A, 0x8057
LOADER_ADDR, LOADER_ARG = 0x1C02000, 0x0001          # loader in RAM; argument: target SPI NOR
LOADER_LEN = 24064                                     # jl-uboot-tool data/loaderblobs/usb/wl82loader.bin
FLASH_SIZE, CHUNK, SECTOR = 0x100000, 512, 0x1000
APP_LO, APP_HI = 0x4000, 0x93000
EXPECT_KEY, EXPECT_ID = 0x980F, 0x856014              # FM-1 chip key; 1 MiB SPI NOR
STOCK_V15 = "db1642b2"                                 # sha256 prefix of M-VAVE's FM-1.fwsc (V15)

CMD_WRITE_MEMORY = 0xFB06      # ROM UBOOT1.00: RAM only (ram_write guards the range)
CMD_JUMP = 0xFB08              # ROM UBOOT1.00
CMD_READ_FLASH = 0xFD05        # loader
CMD_READ_KEY = 0xFC09          # loader
CMD_ONLINE = 0xFC0A            # loader
CMD_ERASE_SECTOR = 0xFB01      # loader, 4 KiB: restore --write only
CMD_WRITE_FLASH = 0xFB04       # loader: restore --write only
READ_ONLY = (CMD_WRITE_MEMORY, CMD_JUMP, CMD_READ_FLASH, CMD_READ_KEY, CMD_ONLINE)


def crc16(data, crc=0):        # CRC-16/XMODEM (jl_crc16)
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def crc_cipher(buf, key=0xFFFFFFFF):   # jl_crc_cipher ("MengLi"), jl-uboot-tool jltech/cipher.py
    magic = "孟黎我爱你，玉林".encode("gb2312")
    buf = bytearray(buf)
    c = crc16((key >> 16).to_bytes(2, "little"), key & 0xFFFF)
    for i in range(len(buf)):
        c = crc16(magic[i % len(magic):i % len(magic) + 1], c)
        buf[i] ^= c & 0xFF
    return bytes(buf)


def backend():
    for path in (ctypes.util.find_library("usb-1.0"), "/opt/homebrew/lib/libusb-1.0.0.dylib",
                 "/usr/local/lib/libusb-1.0.0.dylib"):
        if path and (os.path.exists(path) or not path.startswith("/")):
            be = usb.backend.libusb1.get_backend(find_library=lambda x, p=path: p)
            if be is not None:
                return be
    sys.exit("fm1_uboot: libusb not found (brew install libusb)")


class UBoot:
    """USB mass-storage Bulk-Only Transport to UBOOT1.00 and then the loader"""

    def __init__(self):
        if usb is None:
            sys.exit("fm1_uboot: needs pyusb (python3 -m pip install pyusb) and libusb (brew install libusb)")
        self.dev = usb.core.find(idVendor=VID, idProduct=PID, backend=backend())
        if self.dev is None:
            sys.exit("no WL80UBOOT (4C4A:8057) device: put the FM-1 in boot mode first (docs/RECOVERY.md)")
        intf = self.dev.get_active_configuration()[(0, 0)]
        try:
            if self.dev.is_kernel_driver_active(intf.bInterfaceNumber):
                self.dev.detach_kernel_driver(intf.bInterfaceNumber)
        except NotImplementedError:
            pass
        except usb.core.USBError as e:
            sys.exit(f"cannot take the device from macOS ({e}): run with sudo")
        usb.util.claim_interface(self.dev, intf.bInterfaceNumber)
        out = lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT
        self.ep_in = usb.util.find_descriptor(intf, custom_match=lambda e: not out(e))
        self.ep_out = usb.util.find_descriptor(intf, custom_match=out)
        self.tag = 1

    def xfer(self, cdb, data_out=None, in_len=0, timeout=10000):
        tag, self.tag = self.tag, self.tag + 1
        n = len(data_out) if data_out is not None else in_len
        cbw = struct.pack("<IIIBBB", 0x43425355, tag, n, 0x80 if in_len else 0, 0, len(cdb)) + cdb.ljust(16, b"\0")
        self.ep_out.write(cbw, timeout)
        data = b""
        if data_out is not None:
            self.ep_out.write(data_out, timeout)
        while len(data) < in_len:
            data += bytes(self.ep_in.read(in_len - len(data), timeout))
        sig, rtag, _, status = struct.unpack("<IIIB", bytes(self.ep_in.read(13, timeout)))
        if sig != 0x53425355 or rtag != tag or status:
            raise RuntimeError(f"command {cdb[:2].hex()} failed (status {status})")
        return data

    def jl(self, cmd, args=b"", data_out=None, in_len=0, writes=False):
        assert cmd in READ_ONLY or writes, "flash-changing command outside restore --write"
        return self.xfer((cmd.to_bytes(2, "big") + args).ljust(16, b"\xff"), data_out, in_len)

    def cmd(self, cmd, args=b"", writes=False):
        r = self.jl(cmd, args, in_len=16, writes=writes)
        if int.from_bytes(r[:2], "big") != cmd:
            raise RuntimeError(f"command {cmd:04X}: response {r.hex()}")
        return r[2:]

    def ram_write(self, addr, data):
        assert 0x01C00000 <= addr and addr + len(data) <= 0x01C80000, "not RAM"
        self.jl(CMD_WRITE_MEMORY, addr.to_bytes(4, "big") + len(data).to_bytes(2, "big") + b"\0"
                + crc16(data).to_bytes(2, "little"), data_out=data)

    def start(self, loader):
        inq = self.xfer(bytes([0x12, 0, 0, 0, 36, 0]), in_len=36)
        if inq[16:25] != b"UBOOT1.00":
            sys.exit(f"not the mask-ROM UBOOT1.00 ({inq[8:32]!r}): stopping")
        for off in range(0, len(loader), 512):           # the blob is already ciphered: sent as is
            self.ram_write(LOADER_ADDR + off, loader[off:off + 512])
        self.jl(CMD_JUMP, LOADER_ADDR.to_bytes(4, "big") + LOADER_ARG.to_bytes(2, "big"), in_len=16)
        time.sleep(0.2)
        r = self.cmd(CMD_READ_KEY, (0xAC6900).to_bytes(4, "big"))
        key = int.from_bytes(crc_cipher(r[4:6][::-1]), "little")
        r = self.cmd(CMD_ONLINE)
        typ, fid = r[0], int.from_bytes(r[2:6], "little")
        print(f"chip key 0x{key:04X}, flash type {typ} id 0x{fid:06X}")
        if key != EXPECT_KEY or typ != 3 or fid != EXPECT_ID:
            sys.exit(f"not an FM-1 as expected (key 0x{EXPECT_KEY:04X}, flash 3 / 0x{EXPECT_ID:06X}): stopping")

    def read(self, lo=0, n=FLASH_SIZE):
        out = bytearray()
        for a in range(lo, lo + n, CHUNK):
            out += self.jl(CMD_READ_FLASH, a.to_bytes(4, "big") + CHUNK.to_bytes(2, "big"), in_len=CHUNK)
        return bytes(out)

    def write_sector(self, addr, data):
        assert addr % SECTOR == 0 and APP_LO <= addr and addr + SECTOR <= APP_HI and len(data) == SECTOR
        self.cmd(CMD_ERASE_SECTOR, addr.to_bytes(4, "big"), writes=True)
        for off in range(0, SECTOR, CHUNK):
            c = data[off:off + CHUNK]
            self.jl(CMD_WRITE_FLASH, (addr + off).to_bytes(4, "big") + len(c).to_bytes(2, "big") + b"\0"
                    + crc16(c).to_bytes(2, "little"), data_out=c, writes=True)


def load_loader(path):
    blob = open(path, "rb").read()
    if len(blob) != LOADER_LEN:
        sys.exit(f"{path}: {len(blob)} B, expected jl-uboot-tool's wl82loader.bin ({LOADER_LEN} B)")
    return blob


def do_backup(a, uboot=None):
    loader = load_loader(a.loader)
    d = (uboot or UBoot)()
    d.start(loader)
    reads = []
    for k in range(2):
        t0 = time.time()
        reads.append(d.read())
        print(f"read {k + 1}: {time.time() - t0:.1f} s, sha256 {hashlib.sha256(reads[-1]).hexdigest()}")
    if reads[0] != reads[1]:
        sys.exit("the two reads differ: no backup saved")
    with open(a.out, "wb") as f:
        f.write(reads[0])
    print(f"saved {a.out}; nothing was written to the FM-1")


def stock_image(pkg_path, head):
    pkg = open(pkg_path, "rb").read()
    if not hashlib.sha256(pkg).hexdigest().startswith(STOCK_V15):
        sys.exit(f"{pkg_path} is not M-VAVE's stock V15 FM-1.fwsc (sha256 {STOCK_V15}...): refusing")
    at = pkg.find(head[:0x200])
    if at < 0 or len(pkg) < at + APP_HI or pkg[at:at + APP_LO] != head[:APP_LO]:
        sys.exit("no flash image with this FM-1's head in the package: refusing")
    return pkg[at:at + APP_HI]


def do_restore(a, uboot=None):
    backup = open(a.backup, "rb").read()
    if len(backup) != FLASH_SIZE:
        sys.exit(f"{a.backup}: not a 1 MiB backup")
    target = stock_image(a.package, backup)
    expect = target + backup[APP_HI:]
    loader = load_loader(a.loader)
    d = (uboot or UBoot)()
    d.start(loader)
    now = d.read()
    if now[:APP_LO] != backup[:APP_LO] or now[APP_HI:] != backup[APP_HI:]:
        sys.exit("the FM-1's head or data differ from the backup: refusing (make a new backup of this FM-1)")
    todo = [s for s in range(APP_LO, APP_HI, SECTOR) if now[s:s + SECTOR] != target[s:s + SECTOR]]
    print(f"{len(todo)} of {(APP_HI - APP_LO) // SECTOR} app sectors differ from stock V15")
    if not a.write:
        print("DRY RUN: nothing written (add --write)")
        return
    t0 = time.time()
    for i, s in enumerate(todo):
        for _ in range(3):
            d.write_sector(s, target[s:s + SECTOR])
            if d.read(s, SECTOR) == target[s:s + SECTOR]:
                break
            print(f"  sector 0x{s:05X}: verify failed, again")
        else:
            sys.exit(f"sector 0x{s:05X} failed 3 times: STOP. Keep the FM-1 in boot mode and run restore again.")
        if i % 16 == 15 or i + 1 == len(todo):
            print(f"  {i + 1}/{len(todo)} sectors, {time.time() - t0:.1f} s")
    final = d.read()
    if final != expect:
        bad = [hex(s) for s in range(0, FLASH_SIZE, SECTOR) if final[s:s + SECTOR] != expect[s:s + SECTOR]]
        sys.exit(f"FINAL CHECK FAILED in sectors {bad[:8]}: keep the FM-1 in boot mode and run restore again")
    print("done: stock V15 app written and verified. Unplug USB or power-cycle the FM-1.")


def main(argv=None, uboot=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="what", required=True)
    b = sub.add_parser("backup", help="read the whole flash twice into a file (writes nothing)")
    b.add_argument("--loader", required=True, help="jl-uboot-tool data/loaderblobs/usb/wl82loader.bin")
    b.add_argument("out")
    r = sub.add_parser("restore", help="put the stock V15 app back (dry run unless --write)")
    r.add_argument("--loader", required=True, help="jl-uboot-tool data/loaderblobs/usb/wl82loader.bin")
    r.add_argument("package", help="M-VAVE's stock FM-1.fwsc (V15)")
    r.add_argument("backup", help="this FM-1's backup from `backup`")
    r.add_argument("--write", action="store_true", help="really write (else: check and report only)")
    a = ap.parse_args(argv)
    (do_backup if a.what == "backup" else do_restore)(a, uboot)


if __name__ == "__main__":
    main()
