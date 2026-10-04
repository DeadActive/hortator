#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""H3 (docs/superpowers/specs/2026-10-05-device-readiness-design.md): worst-case stack of the main path
(fm1_cstart, user stack) and of the interrupts (isr_timer5 + isr_alnk0 nested: timer5 runs below the audio
priority, the audio ISR can preempt it; system stack), from the target listing.
A function's frame: every push "[--sp] = {list}" (4 B per register) and every "sp += -N" in it (a
conservative sum). Calls: "call N <f ...>" and a "goto" into another function (a tail call). An indirect
"call rN": every .ram_text function (FL_FAR) and every address-taken .text function (its address in
text.bin / data.bin / ramtext.bin, or loaded by an instruction) that makes no indirect call itself.
Any other instruction that writes sp, a call that cannot be resolved, or recursion: FAIL.
  stack_depth.py BUILD_DIR [--compare UPSTREAM_BUILD_DIR]      stack_depth.py --selftest"""
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import dis_parse  # noqa: E402

USER_STACK = 0x01C7A000 - 0x01C74100                 # app.ld _ustack_lo .. _ustack_top
SYS_STACK = 0x01C7C000 - 0x01C7A100                  # _sstack_lo .. _sstack_top
LIMIT = 0.75
PUSH = re.compile(r"^\[--sp\] = (?:\{([^}]*)\}|(\w+))$")
POP = re.compile(r"^\{[^}]*\} = \[sp\+\+\]$")
ADJ = re.compile(r"^sp \+= (-?\d+)$")
CALL = re.compile(r"^call -?\d+ <([^+ >]+)")
GOTO = re.compile(r"\bgoto -?\d+ <([^+ >]+)")
ICALL = re.compile(r"^call r\d+$")
LOAD = re.compile(r"<([^+ :>]+) : ([0-9a-f]+) >")
WRITES_SP = re.compile(r"(^|[^\w])sp\s*(\+|-)?=(?!=)")


class StackError(Exception):
    pass


def regs(lst):
    n = 0
    for part in lst.split(","):
        part = part.strip()
        m = re.match(r"r(\d+)-r(\d+)$", part)
        n += abs(int(m.group(1)) - int(m.group(2))) + 1 if m else 1
    return n


def frame(name, insns):
    size = 0
    for _, t in insns:
        m = PUSH.match(t)
        if m:
            size += 4 * regs(m.group(1)) if m.group(1) else 4
            continue
        m = ADJ.match(t)
        if m:
            size += max(0, -int(m.group(1)))
            continue
        if POP.match(t) or ("sp" in t and not WRITES_SP.search(t)):
            continue
        if re.match(r"^sp = -?\d+(\s|$)", t):                 # the stack pointer set to a stack top (start-up)
            continue
        if WRITES_SP.search(t):
            raise StackError(f"{name}: unknown stack instruction: {t}")
    return size


def address_taken(funcs, starts, blobs):
    taken = set()
    by_addr = {a: n for n, a in starts.items()}
    for blob in blobs:
        for i in range(0, len(blob) - 3, 2):
            v = struct.unpack_from("<I", blob, i)[0]
            if v in by_addr:
                taken.add(by_addr[v])
    for insns in funcs.values():
        for _, t in insns:
            if CALL.match(t):
                continue
            for m in LOAD.finditer(t):
                if m.group(1) in funcs and int(m.group(2), 16) == starts.get(m.group(1)):
                    taken.add(m.group(1))
    return taken


def analyse(funcs, blobs):
    starts = {n: ins[0][0] for n, ins in funcs.items() if ins}
    frames = {n: frame(n, ins) for n, ins in funcs.items()}
    indirect = {n for n, ins in funcs.items() if any(ICALL.match(t) for _, t in ins)}
    ram = {n for n, a in starts.items() if 0x01C00000 <= a < 0x01D00000}
    taken = address_taken(funcs, starts, blobs)
    entries = {n for n in funcs if n in ("_start", "fm1_cstart") or n.startswith("isr_")}   # reached from the vectors
    targets = (ram | taken) - entries                # a pointer call back into the path is skipped in deep()
    by_addr = {a: n for n, a in starts.items()}
    edges, iedges = {}, {}
    for n, ins in funcs.items():
        e, ie = set(), set()
        for a, t in ins:
            m = CALL.match(t)
            if m:
                if m.group(1) not in funcs:
                    raise StackError(f"{n}: call to an unknown function {m.group(1)}")
                e.add(m.group(1))
                continue
            mu = re.match(r"^call (-?\d+)$", t)          # unannotated: the target is address + 4 + N
            if mu:
                tgt = by_addr.get(a + 4 + int(mu.group(1)))
                if tgt is None:
                    raise StackError(f"{n}: call to an unknown address {a + 4 + int(mu.group(1)):#x}")
                e.add(tgt)
                continue
            if t.startswith("call") and not ICALL.match(t):
                raise StackError(f"{n}: unknown call form: {t}")
            m = GOTO.search(t)
            if m and m.group(1) != n and m.group(1) in funcs:
                e.add(m.group(1))
            if ICALL.match(t):
                ie |= targets
        edges[n], iedges[n] = e, ie
    memo, onpath = {}, set()

    def deep(n):
        if n in memo:
            return memo[n]
        if n in onpath:
            raise StackError(f"recursion through {n}")
        onpath.add(n)
        best, path = 0, []
        for c in list(edges.get(n, ())) + [c for c in iedges.get(n, ()) if c not in onpath]:
            d, p = deep(c)
            if d > best:
                best, path = d, p
        onpath.discard(n)
        memo[n] = (frames[n] + best, [n] + path)
        return memo[n]
    return deep


def report(build):
    d = Path(build)
    funcs = dis_parse.functions(d / "felucca.dis")
    blobs = [(d / f).read_bytes() for f in ("text.bin", "data.bin", "ramtext.bin") if (d / f).exists()]
    deep = analyse(funcs, blobs)
    main_b, main_p = deep("fm1_cstart")
    t5_b, t5_p = deep("isr_timer5")
    au_b, au_p = deep("isr_alnk0")
    return main_b, main_p, t5_b + au_b, (t5_p, au_p)


def _listing(lines):
    p = Path("build/host/stack_selftest.dis")
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text("\n".join(lines) + "\n")
    return dis_parse.functions(p)


def _insn(addr, text):
    return " %x:    00 00             \t%s" % (addr, text)


def selftest():
    ok = True
    try:                                             # an unknown stack form fails
        analyse(_listing(["a:", _insn(0x1000, "[--sp] = {rets, r5-r4}"), _insn(0x1002, "sp = r3")]), [])
        print("stack_depth selftest: an unknown stack form was NOT caught")
        ok = False
    except StackError as e:
        print(f"stack_depth selftest: an unknown stack form is caught ({e})")
    try:                                             # an unknown call form fails
        analyse(_listing(["a:", _insn(0x1000, "call [r3+4]")]), [])
        print("stack_depth selftest: an unknown call form was NOT caught")
        ok = False
    except StackError as e:
        print(f"stack_depth selftest: an unknown call form is caught ({e})")
    cases = (
        ("an unannotated call (address + 4 + N) reaches its callee",
         ["a:", _insn(0x1000, "[--sp] = {rets, r4}"), _insn(0x1002, "call 10"), "b:", _insn(0x1010, "sp += -100"),
          _insn(0x1012, "rts")], "a", 8 + 100),
        ("a brace-less push counts its register",
         ["a:", _insn(0x1000, "[--sp] = rets"), _insn(0x1002, "sp += -8")], "a", 4 + 8),
        ("a jump-table label stays inside its function",
         ["a:", _insn(0x1000, "[--sp] = {rets, r4}"), ".GJTI0_1:", _insn(0x1004, "sp += -40")], "a", 8 + 40),
        ("an indirect call reaches a function that itself calls indirectly",
         ["a:", _insn(0x1000, "[--sp] = {rets, r4}"), _insn(0x1002, "call r2"),
          "b:", _insn(0x1010, "sp += -200"), _insn(0x1012, "call r3"), _insn(0x1014, "rts")], "a", 8 + 200),
    )
    for what, lines, root, want in cases:
        funcs = _listing(lines)
        blob = struct.pack("<I", 0x1010)             # b's address in data: b is address-taken
        try:
            got = analyse(funcs, [blob])(root)[0]
        except StackError as e:
            got = f"error: {e}"
        print(f"stack_depth selftest: {what}: {'ok' if got == want else f'NOT ok (got {got}, want {want})'}")
        ok &= got == want
    return 0 if ok else 1


def main(argv):
    if argv[:1] == ["--selftest"]:
        return selftest()
    try:
        mb, mp, sb, sp = report(argv[0])
    except StackError as e:
        print("FAIL  " + str(e))
        return 1
    print(f"stack: main path {mb} B of {USER_STACK} ({100 * mb / USER_STACK:.0f} %): {' > '.join(mp[:12])}")
    print(f"stack: interrupts {sb} B of {SYS_STACK} ({100 * sb / SYS_STACK:.0f} %): timer5 {' > '.join(sp[0][:6])}; "
          f"audio {' > '.join(sp[1][:10])}")
    if len(argv) > 2 and argv[1] == "--compare":
        try:
            ub, _, us, _ = report(argv[2])
            print(f"stack: upstream Felucca: main {ub} B, interrupts {us} B")
        except StackError as e:
            print("info  upstream not analysed: " + str(e))
    ok = mb <= LIMIT * USER_STACK and sb <= LIMIT * SYS_STACK
    print("stack: within 75 % of both stacks" if ok else "FAIL  stack: over 75 % of a stack")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
