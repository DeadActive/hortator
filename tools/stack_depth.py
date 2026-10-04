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
PUSH = re.compile(r"^\[--sp\] = \{([^}]*)\}$")
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
            size += 4 * regs(m.group(1))
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
    targets = (ram | taken) - indirect - entries
    edges = {}
    for n, ins in funcs.items():
        e = set()
        for _, t in ins:
            m = CALL.match(t)
            if m:
                if m.group(1) not in funcs:
                    raise StackError(f"{n}: call to an unknown function {m.group(1)}")
                e.add(m.group(1))
            m = GOTO.search(t)
            if m and m.group(1) != n and m.group(1) in funcs:
                e.add(m.group(1))
            if ICALL.match(t):
                e |= targets
        edges[n] = e
    memo, onpath = {}, set()

    def deep(n):
        if n in memo:
            return memo[n]
        if n in onpath:
            raise StackError(f"recursion through {n}")
        onpath.add(n)
        best, path = 0, []
        for c in edges.get(n, ()):
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


def selftest():
    lines = ["a:", " 1000:    00 00             \t[--sp] = {rets, r5-r4}", " 1002:    00 00             \tsp += -16",
             " 1004:    00 00             \tcall 2 <b : 1010 >", "b:", " 1010:    00 00             \tsp = r3"]
    p = Path("build/host/stack_selftest.dis")
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text("\n".join(lines) + "\n")
    try:
        analyse(dis_parse.functions(p), [])
    except StackError as e:
        print(f"stack_depth selftest: an unknown stack form is caught ({e})")
        return 0
    print("stack_depth selftest: an unknown stack form was NOT caught")
    return 1


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
