#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""tools/rescue/fm1_uboot.py against a simulated FM-1 in boot mode (UBOOT1.00 + the loader, NOR flash: erase sets
0xFF, a write can only clear bits). No pyusb, no device. Checks: backup sends no flash erase or write and saves
exactly the flash; restore refuses a non-stock package, a different FM-1 (head or data) and a wrong loader; the dry
run changes nothing; --write leaves stock V15 in [0, 0x93000) and the data untouched; an interrupted --write is
resumed by running restore again; nothing outside [0x4000, 0x93000) is ever erased or written."""
import hashlib
import io
import os
import random
import sys
import tempfile
from contextlib import redirect_stdout

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "rescue"))
import fm1_uboot as U  # noqa: E402

rng = random.Random(1)
rnd = lambda n: bytes(rng.getrandbits(8) for _ in range(n))
HEAD = rnd(U.APP_LO)
STOCK_APP = rnd(U.APP_HI - U.APP_LO)
DATA = rnd(U.FLASH_SIZE - U.APP_HI)
PROBE_APP = rnd(U.APP_HI - U.APP_LO)                  # what the FM-1 runs before the restore
PKG = b"UFW-HEADER" + b"\0" * 0x40A + HEAD + STOCK_APP + b"ota.bin..."   # the image sits at 0x414, as in V15
LOADER = rnd(U.LOADER_LEN)


class Fake(U.UBoot):
    def __init__(self, flash, fail_after=None):
        self.flash, self.loader, self.ram, self.log, self.fail_after, self.tag = flash, False, {}, [], fail_after, 1

    def xfer(self, cdb, data_out=None, in_len=0, timeout=0):
        if cdb[0] == 0x12:
            return (b"\0" * 8 + b"WL82    " + b"UBOOT1.00       " + b"1.00")[:in_len]
        cmd, a = int.from_bytes(cdb[:2], "big"), cdb[2:]
        self.log.append(cmd)
        echo = cmd.to_bytes(2, "big")
        if cmd == U.CMD_WRITE_MEMORY:
            addr = int.from_bytes(a[:4], "big")
            assert int.from_bytes(a[7:9], "little") == U.crc16(data_out)
            self.ram[addr] = data_out
            return b""
        if cmd == U.CMD_JUMP:
            assert int.from_bytes(a[:4], "big") == U.LOADER_ADDR and b"".join(
                self.ram[k] for k in sorted(self.ram)) == LOADER
            self.loader = True
            return echo.ljust(16, b"\0")
        assert self.loader, "loader command before the jump"
        if cmd == U.CMD_READ_KEY:
            k = U.crc_cipher((0x980F).to_bytes(2, "little"))[::-1]
            return (echo + b"\0\0\0\0" + k).ljust(16, b"\0")
        if cmd == U.CMD_ONLINE:
            return (echo + bytes([3, 0]) + (0x856014).to_bytes(4, "little")).ljust(16, b"\0")
        addr = int.from_bytes(a[:4], "big")
        if cmd == U.CMD_READ_FLASH:
            return bytes(self.flash[addr:addr + int.from_bytes(a[4:6], "big")])
        assert U.APP_LO <= addr < U.APP_HI, f"flash change outside the app area: {cmd:04X} at {addr:#x}"
        if cmd == U.CMD_ERASE_SECTOR:
            if self.fail_after is not None:
                if self.fail_after == 0:
                    raise RuntimeError("cable pulled")
                self.fail_after -= 1
            self.flash[addr:addr + U.SECTOR] = b"\xff" * U.SECTOR
            return echo.ljust(16, b"\0")
        if cmd == U.CMD_WRITE_FLASH:
            assert int.from_bytes(a[7:9], "little") == U.crc16(data_out)
            for i, b in enumerate(data_out):
                self.flash[addr + i] &= b
            return b""
        raise AssertionError(f"unexpected command {cmd:04X}")


def run(args, fake):
    out = io.StringIO()
    try:
        with redirect_stdout(out):
            U.main(args, uboot=lambda: fake)
        return 0, out.getvalue()
    except SystemExit as e:
        return 1, out.getvalue() + str(e.code)
    except RuntimeError as e:                          # a transfer that broke off (the cable)
        return 2, out.getvalue() + str(e)


def check(name, ok):
    print(("ok    " if ok else "FAIL  ") + name)
    if not ok:
        sys.exit(1)


def main():
    tmp = tempfile.mkdtemp()
    p = lambda n: os.path.join(tmp, n)
    open(p("loader.bin"), "wb").write(LOADER)
    open(p("bad_loader.bin"), "wb").write(LOADER[:100])
    open(p("pkg.fwsc"), "wb").write(PKG)
    open(p("other.fwsc"), "wb").write(PKG + b"x")
    U.STOCK_V15 = hashlib.sha256(PKG).hexdigest()[:8]   # (the real one is M-VAVE's file, not in this tree)
    device = bytearray(HEAD + PROBE_APP + DATA)
    before = bytes(device)

    f = Fake(device)
    rc, _ = run(["backup", "--loader", p("loader.bin"), p("backup.bin")], f)
    check("backup saves the flash exactly", rc == 0 and open(p("backup.bin"), "rb").read() == before)
    check("backup sends no flash erase or write", not ({U.CMD_ERASE_SECTOR, U.CMD_WRITE_FLASH} & set(f.log))
          and bytes(device) == before)
    rc, msg = run(["backup", "--loader", p("bad_loader.bin"), p("x.bin")], Fake(device))
    check("a loader that is not wl82loader.bin is refused", rc == 1 and "expected" in msg)

    base = ["restore", "--loader", p("loader.bin")]
    rc, msg = run(base + [p("other.fwsc"), p("backup.bin")], Fake(device))
    check("a package that is not stock V15 is refused", rc == 1 and "not M-VAVE" in msg and bytes(device) == before)
    other = bytearray(device)
    other[U.APP_HI + 5] ^= 1
    open(p("other_backup.bin"), "wb").write(bytes(other))
    rc, msg = run(base + [p("pkg.fwsc"), p("other_backup.bin")], Fake(device))
    check("a backup of another state (data area) is refused", rc == 1 and "refusing" in msg
          and bytes(device) == before)

    f = Fake(device)
    rc, msg = run(base + [p("pkg.fwsc"), p("backup.bin")], f)
    check("dry run: reports 143 sectors and changes nothing", rc == 0 and "143 of 143" in msg and "DRY RUN" in msg
          and bytes(device) == before and not ({U.CMD_ERASE_SECTOR, U.CMD_WRITE_FLASH} & set(f.log)))

    rc, msg = run(base + [p("pkg.fwsc"), p("backup.bin"), "--write"], Fake(device, fail_after=40))
    check("an interrupted write stops", rc == 2 and bytes(device[:U.APP_LO]) == HEAD and
          bytes(device[U.APP_HI:]) == DATA)
    rc, msg = run(base + [p("pkg.fwsc"), p("backup.bin"), "--write"], Fake(device))
    check("running restore again finishes it", rc == 0 and "done" in msg and "103 of 143" in msg)
    check("result: stock app, head and data untouched", bytes(device) == HEAD + STOCK_APP + DATA)
    rc, msg = run(base + [p("pkg.fwsc"), p("backup.bin"), "--write"], Fake(device))
    check("a restored FM-1 needs no writes", rc == 0 and "0 of 143" in msg)
    print("fm1_uboot: all checks passed")


if __name__ == "__main__":
    main()
