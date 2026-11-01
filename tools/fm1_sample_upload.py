#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Drum machine fork: 2026 DEADACTIVE
"""Hortator's user sample slots (USR1..USR3) from the command line, over USB-MIDI. The web editor's Samples tab
does the same with a waveform, trim and preview; this is for scripts.

  fm1_sample_upload.py info
  fm1_sample_upload.py load SLOT NAME file.wav[:ROOT[:LO-HI]] ...   (SLOT 1..3)
  fm1_sample_upload.py clear SLOT
  fm1_sample_upload.py rename SLOT NAME
  fm1_sample_upload.py backup SLOT out.fsmp
  fm1_sample_upload.py restore SLOT file.fsmp
  fm1_sample_upload.py build NAME OUT_PREFIX file.wav[...] ...      (no device: writes OUT_PREFIX.fsmp)

Each file becomes one zone: mono, 22050 Hz, IMA ADPCM (sampleio.py, the same encoder as the built-in sets and the
web editor). ROOT is a MIDI note (default: from the file name, C4 = 60, else 60); without LO-HI the zones split the
keyboard between their roots. USR1 holds 79.5 KiB of data (about 7.4 s at 22050 Hz), USR2 and USR3 47.5 KiB each.
The FM-1 must be stopped. Protocol: web/EDITOR_PROTOCOL.md §2.7 (Hortator editor protocol 1.3, SMP_INFO ..
SMP_RENAME). Needs mido (and a backend such as python-rtmidi) for the device commands.
"""
import re
import sys
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402

HDR = [0x7D, 0x48, 0x54]                            # F0 7D 48 54 cmd seq ... F7
PORT = re.compile(r"hortator|felucca|fm-1", re.I)   # the name only says whom to ask: INFO's answer decides
INFO, SMP_INFO, SMP_BEGIN, SMP_DATA, SMP_END, SMP_READ, SMP_CLEAR, SMP_RENAME = 1, 21, 22, 23, 24, 25, 26, 27
CAP_SAMPLES = 0x80
SLOT_MAX = [0x14000 - 512, 0xC000 - 512, 0xC000 - 512]
DATA_OFF, HDR_LEN, PIECE = 512, 480, 256


def pack7(b):
    """groups of up to 7 bytes, each preceded by their top bits"""
    out = []
    for i in range(0, len(b), 7):
        g = b[i:i + 7]
        out.append(sum(((x >> 7) & 1) << j for j, x in enumerate(g)))
        out += [x & 0x7F for x in g]
    return out


def unpack7(b):
    out = bytearray()
    for i in range(0, len(b), 8):
        g = b[i:i + 8]
        out += bytes(x | ((g[0] >> j) & 1) << 7 for j, x in enumerate(g[1:]))
    return bytes(out)


def u35(v):
    return [(v >> (7 * i)) & 0x7F for i in range(5)]


def r35(b, i):
    return sum(b[i + k] << (7 * k) for k in range(5))


def rstr(b, i):
    j = b.index(0, i)
    return bytes(b[i:j]).decode("ascii", "replace"), j + 1


def crc16(b):
    """poly 0x1021, init 0 (ota.c ota_crc16: the update record's)"""
    c = 0
    for x in b:
        c ^= x << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def loader_windows(data):
    """data offsets whose slot window (a 4 KiB sector's +0xF00..+0xF4F, past the data's end 0xFF) could read as an
    update record for the update loader's sweep (web/EDITOR_PROTOCOL.md Appendix): the FM-1 refuses those pieces"""
    hits = []
    for d in range(0xF00 - DATA_OFF, len(data), 0x1000):
        w = bytes(data[d:d + 80]).ljust(80, b"\xff")
        v = w[0] | w[1] << 8
        if v and v == crc16(w[2:]):
            hits.append(d)
    return hits


class Link:
    def __init__(self):
        try:
            import mido
        except ImportError:
            sys.exit("needs mido: pip install mido python-rtmidi")
        self.mido, self.seq = mido, 0
        outs = [n for n in mido.get_output_names() if PORT.search(n) and not re.search(r"update", n, re.I)]
        ins = [n for n in mido.get_input_names() if PORT.search(n) and not re.search(r"update", n, re.I)]
        if not outs or not ins:
            sys.exit("no FM-1 MIDI port: connect the FM-1 (running Hortator 0.28.0 or later) by USB")
        self.o = mido.open_output(outs[0])
        self.i = mido.open_input(ins[0])
        r = self.req(INFO, [], 1.0)
        magic, k = rstr(r, 0)
        fw, k = rstr(r, k)
        caps = r[k + 2] | r[k + 3] << 7
        if magic != "HORTATOR":
            sys.exit("not a Hortator FM-1")
        if not caps & CAP_SAMPLES:
            sys.exit(f"this FM-1 runs Hortator {fw}: update it to use the sample slots")

    def req(self, cmd, args, timeout=2.0, tries=3):
        """one request (its tries keep the seq: the FM-1 answers a repeated write once); the reply's fields after rc"""
        self.seq = (self.seq + 1) % 127
        for _ in range(tries):
            for _m in self.i.iter_pending():
                pass
            self.o.send(self.mido.Message("sysex", data=HDR + [cmd, self.seq] + list(args)))
            t0 = time.time()
            while time.time() - t0 < timeout:
                for m in self.i.iter_pending():
                    d = list(m.data) if m.type == "sysex" else []
                    if d[:5] == HDR + [cmd, self.seq]:
                        if d[5]:
                            raise RuntimeError(f"refused: {rstr(d, 7)[0]} (rc {d[5]}, detail {d[6]})")
                        return d[6:]
                time.sleep(0.002)
        raise TimeoutError(f"no reply to command {cmd}")

    def info(self, k):
        r = self.req(SMP_INFO, [k], 0.3)
        name, i = rstr(r, 2)
        return dict(valid=r[1], name=name, len=r35(r, i), crc=r35(r, i + 5), max=r35(r, i + 10), nz=r[i + 15],
                    n=r35(r, i + 16), rate=r35(r, i + 21))

    def write(self, k, hdr, data):
        self.req(SMP_BEGIN, [k] + u35(len(data)) + u35(zlib.crc32(data)))
        for off in range(0, len(data), PIECE):
            self.req(SMP_DATA, [k] + u35(off) + pack7(data[off:off + PIECE]))
            print(f"\r  {min(off + PIECE, len(data))} / {len(data)}", end="", flush=True)
        r = self.req(SMP_END, [k] + pack7(hdr[:HDR_LEN]))
        print()
        return r[1]

    def read(self, k):
        i = self.info(k)
        if not i["valid"]:
            sys.exit(f"USR{k + 1} is empty")
        out = bytearray()
        for off in range(0, DATA_OFF + i["len"], PIECE):
            r = self.req(SMP_READ, [k] + u35(off) + u35(i["crc"]), 0.5)
            out += unpack7(r[8:])
        return bytes(out)


def build(name, specs):
    """specs: "file.wav[:ROOT[:LO-HI]]" -> (header, ADPCM data); a loader window: the first zone moved by a sample"""
    zones = []
    for spec in specs:
        parts = spec.split(":")
        path = Path(parts[0])
        root = int(parts[1]) if len(parts) > 1 and parts[1] else sio.note_from_name(path.stem)
        lo, hi = (int(v) for v in parts[2].split("-")) if len(parts) > 2 else (None, None)
        sr, x = sio.read_any_wav(path)
        x = sio.resample(x, sr, sio.SLOT_RATE)
        if not x:
            raise ValueError(f"{path}: no audio")
        zones.append((sio.to_int16(x), root, lo, hi))
    for shift in range(64):
        z0 = zones[0]
        hdr, data = sio.user_slot(name, [(z0[0][shift:],) + z0[1:]] + zones[1:])
        if not loader_windows(data):
            return hdr, data
    raise ValueError("this sample cannot be written safely: trim it a little")


def fsmp(hdr, data):
    return bytes(hdr[:HDR_LEN]) + b"\xff" * (DATA_OFF - HDR_LEN) + bytes(data)


def main():
    a = sys.argv[1:]
    if not a or a[0] not in ("info", "load", "clear", "erase", "rename", "backup", "restore", "build"):
        sys.exit(__doc__)
    cmd = a[0]
    if cmd == "build":
        if len(a) < 4:
            sys.exit(__doc__)
        hdr, data = build(a[1], a[3:])
        Path(a[2] + ".fsmp").write_bytes(fsmp(hdr, data))
        print(f"{a[1]}: {len(data)} B ADPCM, {hdr[6]} zones -> {a[2]}.fsmp")
        return
    k = None
    if cmd != "info":
        if len(a) < 2 or a[1] not in ("1", "2", "3"):
            sys.exit("SLOT is 1, 2 or 3")
        k = int(a[1]) - 1
    if cmd == "load":
        if len(a) < 4:
            sys.exit(__doc__)
        try:
            hdr, data = build(a[2], a[3:])
        except (OSError, ValueError) as e:
            sys.exit(f"cannot build the slot: {e}")
        if len(data) > SLOT_MAX[k]:
            sys.exit(f"too long for USR{k + 1}: {len(data)} B of ADPCM, it holds {SLOT_MAX[k]} B")
    link = Link()
    if cmd == "info":
        for s in range(3):
            i = link.info(s)
            print(f"USR{s + 1}: " + (f"{i['name']}, {i['nz']} zone(s), {i['len']} of {i['max']} B, "
                                     f"{i['n'] / (i['rate'] * 44100 / 65536):.2f} s" if i["valid"] else f"empty ({i['max']} B)"))
    elif cmd in ("clear", "erase"):
        link.req(SMP_CLEAR, [k])
        print(f"USR{k + 1} cleared")
    elif cmd == "rename":
        nm = re.sub(r"[^\x20-\x7E]", "", " ".join(a[2:]).upper())[:8].strip() or "SAMPLE"
        link.req(SMP_RENAME, [k] + list(nm.encode()) + [0])
        print(f"USR{k + 1}: {nm}")
    elif cmd == "backup":
        Path(a[2]).write_bytes(link.read(k))
        print(f"USR{k + 1} -> {a[2]}")
    elif cmd == "restore":
        b = Path(a[2]).read_bytes()
        n = int.from_bytes(b[16:20], "little")
        if b[:4] != b"FSMP" or len(b) != DATA_OFF + n or zlib.crc32(b[DATA_OFF:]) != int.from_bytes(b[20:24], "little"):
            sys.exit("not a sample backup (.fsmp), or damaged")
        if n > SLOT_MAX[k] or loader_windows(b[DATA_OFF:]):
            sys.exit(f"cannot be written to USR{k + 1} (too big, or not safely)")
        print("ok" if link.write(k, b[:HDR_LEN], b[DATA_OFF:]) else "FAILED")
    else:
        print(f"USR{k + 1} {a[2]}: {len(data)} B ADPCM, {hdr[6]} zones")
        t0 = time.time()
        ok = link.write(k, hdr, data)
        print(f"  {time.time() - t0:.1f} s, end:", "ok" if ok else "FAILED")


if __name__ == "__main__":
    try:
        main()
    except TimeoutError as e:
        sys.exit(f"{e}: is Hortator running, and no other app using its MIDI port?")
    except RuntimeError as e:
        sys.exit(str(e))
