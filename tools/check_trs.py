#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""TRS MIDI is on in the built app (FELUCCA_UART defaults to 1 since stage 2 step 4): midi_uart.c's ring and state
are in build/felucca.elf and the console's UART lines in build/felucca.bin, so a default that flips back is caught.

  python3 tools/check_trs.py build
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import elf_syms  # noqa: E402


def check(build):
    build = Path(build)
    names = {s[2] for s in elf_syms.Elf(str(build / "felucca.elf")).syms}
    errors = [f"{build / 'felucca.elf'}: no {n} (midi_uart.c not compiled in: FELUCCA_UART off?)"
              for n in ("um_ring", "um") if n not in names]
    if b"uart_rx_bytes\0" not in (build / "felucca.bin").read_bytes():
        errors.append(f"{build / 'felucca.bin'}: no 'uart_rx_bytes' (the console's TRS status lines)")
    return errors


def main(argv):
    if len(argv) != 1:
        print(__doc__)
        return 2
    errors = check(argv[0])
    for e in errors:
        print("  FAIL", e)
    if not errors:
        print("  ok   TRS MIDI input compiled in (um_ring, um; console uart_rx_* lines)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
