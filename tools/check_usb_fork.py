#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""The app's USB driver is firmware/src/usb_app8.c: the frozen usb_app.c plus blocks marked USB8, insertions only.
Without those blocks (each from a line holding "/* USB8 { */" to a line holding "/* } USB8 */", both included) it
must be usb_app.c byte for byte: every line of the frozen driver is there, unchanged and in order.
  tools/check_usb_fork.py            tools/check_usb_fork.py --selftest"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
B, E = "/* USB8 { */", "/* } USB8 */"


def strip(text):
    """(the text without its USB8 blocks, the number of blocks), or (None, an error) for a broken marker"""
    out, depth, n = [], 0, 0
    for ln in text.split("\n"):
        if B in ln:
            if depth:
                return None, "a USB8 block inside another"
            depth, n = 1, n + 1
        if not depth:
            out.append(ln)
        if E in ln:
            if not depth:
                return None, "an end marker without a start"
            depth = 0
    if depth:
        return None, "a USB8 block without its end"
    return "\n".join(out), n


def check(fork, frozen):
    s, n = strip(fork)
    if s is None:
        return [n]
    if s != frozen:
        a, b = s.split("\n"), frozen.split("\n")
        for i, (x, y) in enumerate(zip(a, b)):
            if x != y:
                return [f"usb_app8.c without its blocks, line {i + 1}: {x!r}; usb_app.c: {y!r}"]
        return [f"usb_app8.c without its blocks has {len(a)} lines, usb_app.c {len(b)}"]
    return []


def main(argv):
    frozen = (ROOT / "firmware/src/usb_app.c").read_text()
    fork = (ROOT / "firmware/src/usb_app8.c").read_text()
    if argv == ["--selftest"]:
        assert check(fork, frozen) == [], "the fork itself"
        assert check(fork.replace("sie_wr(S_INDEX, 4);", "sie_wr(S_INDEX, 5);", 1), frozen), "a changed line"
        assert check(fork.replace(E, "", 1), frozen), "an open block"
        assert check(fork + "\nint x;", frozen), "an addition outside a block"
        assert check(fork.replace("    uac_stream(0);\n", "", 1), frozen), "a removed line"
        print("check_usb_fork self-test: ok (a changed, removed or added line, an open block are caught)")
        return 0
    errors = check(fork, frozen)
    for e in errors:
        print("  FAIL", e)
    if not errors:
        print(f"  ok   usb_app8.c = usb_app.c + {strip(fork)[1]} USB8 blocks (insertions only)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
