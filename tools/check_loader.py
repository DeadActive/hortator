#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""The update loader (build/loader/ota.bin) is byte-identical to the one the FM-1 runs: its sha256 must be
tools/frozen_base.txt's LOADER_SHA256 (moving the pin is the user's decision).
  check_loader.py [BUILD_DIR]      check_loader.py --selftest [BUILD_DIR]"""
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def pinned():
    for ln in (ROOT / "tools/frozen_base.txt").read_text().splitlines():
        if ln.startswith("LOADER_SHA256 "):
            return ln.split()[1]
    raise SystemExit("tools/frozen_base.txt: no LOADER_SHA256")


def check(data):
    return hashlib.sha256(data).hexdigest() == pinned()


def main(argv):
    selftest = "--selftest" in argv
    args = [a for a in argv if a != "--selftest"]
    build = Path(args[0]) if args else ROOT / "build"
    data = (build / "loader" / "ota.bin").read_bytes()
    if selftest:
        bad = bytearray(data)
        bad[len(bad) // 2] ^= 1
        ok = check(data) and not check(bytes(bad))
        print(f"loader pin selftest: one flipped bit is {'caught' if ok else 'NOT caught'}")
        return 0 if ok else 1
    ok = check(data)
    print(f"loader: {len(data)} B, sha256 {hashlib.sha256(data).hexdigest()[:16]}... "
          f"{'= the pinned loader' if ok else 'DIFFERS from the pinned loader (tools/frozen_base.txt)'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
