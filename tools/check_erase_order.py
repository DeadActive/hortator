# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""The storage erase order in the built app (docs/superpowers/specs/2026-10-06-storage-hardening-design.md §5).
In st_save, where felucca.c's st_erase is inlined, IRQs go off (cli) before the audio buffer is zeroed and come
back on (sti) only after the erase call. Zeroing first lets the audio ISR refill abuf before the cli, and the DMA
then loops that chunk for the whole erase (a buzz on SAVE). ota_erase (frozen ota.c) keeps the old order.
  python3 tools/check_erase_order.py build/felucca.dis
  python3 tools/check_erase_order.py --selftest"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dis_parse import functions  # noqa: E402

ZERO_RE = re.compile(r"^\[r\d+\+r\d+<<2\] = r\d+$")    # the zero loop's indexed word store

# st_save's erase in the frozen-base-3 build (zeroing first): the self-test's failing case
OLD = ["r0 = 0", "r1 = 29392936 <abuf : 1c08028 >", "r2 = 0", "[r1+r2<<2] = r0", "r2 += 1",
       "if (r2 != 1024) goto -12 <st_save+0x98 : 2005bea >", "cli",
       "r0 = 29360824 <fl_erase4k_ram : 1c002b8 >", "[sp+68] = r0", "r2 = [sp+68]", "r1 = sp + 64", "call r2",
       "csync", "sti", "if (r0 != 0) goto 166 <st_save+0x162 : 2005cb4 >"]
NEW = OLD[6:7] + OLD[:6] + OLD[7:]                       # cli first: upstream's order


def check(text):
    """None when the order is right, else what was found."""
    ab = next((i for i, t in enumerate(text) if "<abuf " in t), None)
    er = next((i for i, t in enumerate(text) if "<fl_erase4k_ram " in t), None)
    if ab is None or er is None:
        return "no inlined st_erase (abuf, fl_erase4k_ram) in st_save"
    zero = next((i for i in range(ab, er) if ZERO_RE.match(text[i])), None)
    if zero is None:
        return "no abuf zero loop before the erase"
    cli = [i for i in range(zero) if text[i] == "cli"]
    if not cli or any(text[i] == "sti" for i in range(cli[-1], zero)):
        return "abuf zeroed with IRQs on (the cli comes after it): the audio ISR can refill it"
    if any(text[i] == "sti" for i in range(zero, er)):
        return "IRQs back on between the zeroing and the erase"
    call = next((i for i in range(er, len(text)) if text[i].startswith("call")), None)
    if call is None or "sti" not in text[call + 1:]:
        return "no sti after the erase call"
    return None


def selftest():
    cases = [("frozen-base-3's order", OLD, False), ("upstream's order", NEW, True),
             ("sti before the erase", NEW[:7] + ["sti"] + NEW[7:], False),
             ("no sti after the erase", [t for t in NEW if t != "sti"], False)]
    bad = 0
    for name, text, good in cases:
        ok = (check(text) is None) == good
        print("%-28s %s" % (name, "ok" if ok else "FAIL"))
        bad += not ok
    return bad


def main(argv):
    if argv[1:] == ["--selftest"]:
        return 1 if selftest() else 0
    st = functions(argv[1]).get("st_save")
    msg = "st_save not found in %s" % argv[1] if st is None else check([t for _, t in st])
    if msg:
        print("erase order: FAIL: " + msg)
        return 1
    print("erase order: st_save zeroes the audio buffer with IRQs off, through the erase")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
