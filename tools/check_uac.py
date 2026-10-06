#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""USB audio is on in the built app (FELUCCA_UAC defaults to 1 since roadmap §3.2): usb_app.c's audio ring and state
are in build/felucca.elf and the console's UAC lines in build/felucca.bin, so a default that flips back is caught.

  python3 tools/check_uac.py build
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import elf_syms  # noqa: E402


def check(build):
    build = Path(build)
    names = {s[2] for s in elf_syms.Elf(str(build / "felucca.elf")).syms}
    errors = [f"{build / 'felucca.elf'}: no {n} (USB audio not compiled in: FELUCCA_UAC off?)"
              for n in ("ua_ring", "uac") if n not in names]
    if b"uac_pkts\0" not in (build / "felucca.bin").read_bytes():
        errors.append(f"{build / 'felucca.bin'}: no 'uac_pkts' (the console's USB audio lines)")
    return errors


def main(argv):
    if len(argv) != 1:
        print(__doc__)
        return 2
    errors = check(argv[0])
    for e in errors:
        print("  FAIL", e)
    if not errors:
        print("  ok   USB audio compiled in (ua_ring, uac; console uac_* lines)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
