#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""H2 (docs/superpowers/specs/2026-10-05-device-readiness-design.md): the frozen code in the drum build's binary
does what upstream Felucca's does (tools/upstream_build.sh builds upstream with the same toolchain).
  compare_upstream.py OURS_BUILD_DIR UPSTREAM_BUILD_DIR      compare_upstream.py --selftest OURS UPSTREAM
(a build dir holds felucca.dis and felucca.elf.) Frozen functions: every function defined in firmware/hal/*,
firmware/src/{usb,ota,storage}.c, the asm entry labels (crt0.S, hal/*.S), and fm1_cstart.
A frozen function passes when:
 1. its instructions are identical, with data resolved by name (the compiler's merged-globals block by its
    member and offset), strings by their text, other constants by value; or
 2. (data layout and scheduling differ: the compiler places the globals of the whole program, so a variable's
    offset, and the instructions forming its address, change with code outside the frozen files) its effects
    are identical: the memory it reads and writes (resolved, with width), its hardware-register constants, its
    calls, branches and returns; or
 3. it is on INLINED (reviewed by the user's decision): the compiler inlined differently, and the effects are
    those of upstream's function with the listed callee(s) inlined ("union"), or are all found in upstream's
    function, which has the callee inlined ("prefix", fm1_cstart: upstream inlines fm1_main).
Anything else, or a frozen function in upstream's binary but not in ours: FAIL."""
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import dis_parse  # noqa: E402
from elf_syms import Elf  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
C_FILES = sorted((ROOT / "firmware/hal").glob("*.h")) + [ROOT / "firmware/src" / f for f in ("usb.c", "ota.c", "storage.c")]
S_FILES = [ROOT / "firmware/crt0.S"] + sorted((ROOT / "firmware/hal").glob("*.S"))
CDEF = re.compile(r"^(?:static\s+|inline\s+|RAMFN\s+|FM1_INLINE\s+|RAMINL\s+|__attribute__\(\([^)]*\)\)\s+)*"
                  r"[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\([^;]*$")
ANNOT = re.compile(r"(-?\d+ )?<([^<>:]+?) : ([0-9a-f]+) >")
SETC = re.compile(r"^(r\d+) = (-?\d+|0x[0-9A-Fa-f]+)(?: <[^>]*>)?\s*#?$")
COPY = re.compile(r"^(r\d+) = (r\d+)$")
ADDI = re.compile(r"^(r\d+) = (r\d+) \+ (-?\d+)\s*#?$")
INCI = re.compile(r"^(r\d+) \+= (-?\d+)$")
DEST = re.compile(r"^(r\d+) (?:[-+*/&|^]|<<|>>)?= ")
POPR = re.compile(r"^\{([^}]*)\} = \[sp\+\+\]$")
MEMOP = re.compile(r"(?<![\w\]])([bhd]?)\[(r\d+)(?:\+(-?\d+)|\+(r\d+)(?:<<\d)?)?\]")
CALL = re.compile(r"^call -?\d+ <([^+ >]+)")
RAM = (0x01C00000, 0x01D00000)
XIP = (0x02000000, 0x02400000)
# reviewed (user decision, 2026-10-05): the compiler inlines these differently in the two programs
INLINED = {"ota_send_msg": ("union", ("ota_wire_send",)), "fm1_cstart": ("prefix", ("fm1_main",))}


def frozen_names():
    names = {"fm1_cstart"}
    for f in C_FILES:
        for line in f.read_text(errors="replace").splitlines():
            m = CDEF.match(line)
            if m and m.group(1) not in ("if", "while", "for", "switch", "return", "sizeof"):
                names.add(m.group(1))
    for f in S_FILES:
        for line in f.read_text(errors="replace").splitlines():
            m = re.match(r"^\s*([A-Za-z_]\w*):", line) or re.match(r"^\s*FM1_ISR\s+([A-Za-z_]\w*)", line)
            if m:
                names.add(m.group(1))
    return names


def _int(s):
    return int(s, 16) if s.lower().startswith("0x") else int(s)


class Prog:
    def __init__(self, build):
        self.funcs = dis_parse.functions(Path(build) / "felucca.dis")
        self.elf = Elf(Path(build) / "felucca.elf")
        self.starts = {n: i[0][0] for n, i in self.funcs.items() if i}

    def value_name(self, fn, sym, v):
        base = sym.split("+")[0]
        if base == fn:
            return "<" + sym + ">"
        if base in self.funcs and "+" not in sym and self.starts.get(base) == v:
            return "<" + base + ">"
        if RAM[0] <= v < RAM[1] or XIP[0] <= v < XIP[1]:
            return "<" + (self.elf.cstring(v) or self.elf.name_of(v) or
                          "DATA:" + (self.elf.bytes_at(v, 8) or b"").hex()) + ">"
        return "<0x%x>" % v

    def walk(self, fn):
        """(normalised text, effect tokens) per instruction of fn, tracking constant registers"""
        regs, out = {}, []
        for _, t in self.funcs[fn]:
            toks = []
            text = ANNOT.sub(lambda m: self.value_name(fn, m.group(2).strip(), int(m.group(3), 16) & 0xFFFFFFFF), t)

            def mem(m):
                w, ra, off, rb = m.group(1), m.group(2), m.group(3), m.group(4)
                target = "ptr"
                if w == "d" and rb is None:                  # a doubleword: two words, at off and off + 4
                    o = int(off) if off else 0
                    names = [self.elf.name_of(regs[ra] + o + k) if ra in regs else None for k in (0, 4)]
                    names = [n or "ptr" for n in names]
                    store = t.strip().startswith(m.group(0)) and re.match(r"^\S+\s*=", t.strip())
                    for n in names:
                        toks.append(("st" if store else "ld") + "w:" + n)
                    return "d[" + ra + "->" + "/".join(names) + "]"
                if ra in regs:
                    if rb is None:
                        target = self.elf.name_of(regs[ra] + (int(off) if off else 0)) or "ptr"
                    elif rb in regs and "<<" not in m.group(0):
                        target = self.elf.name_of(regs[ra] + regs[rb]) or "ptr"
                    else:
                        target = (self.elf.name_of(regs[ra]) or "ptr").split("+")[0] + "[i]"
                elif rb is not None and rb in regs:
                    target = (self.elf.name_of(regs[rb]) or "ptr").split("+")[0] + "[i]"
                if target == "ptr[i]":                       # an unresolved pointer, however it is indexed
                    target = "ptr"
                store = t.strip().startswith(m.group(0)) and re.match(r"^\S+\s*[-+&|^]?=", t.strip())
                if store and re.match(r"^\S+\s*[-+&|^]=", t.strip()):
                    toks.append("ld" + (w or "w") + ":" + target)   # read-modify-write: a read and a write
                toks.append(("st" if store else "ld") + (w or "w") + ":" + target)
                return w + "[" + ra + "->" + target + "]" if target != "ptr" else m.group(0)
            text = MEMOP.sub(mem, text)
            m = CALL.match(t) or re.search(r"\bgoto -?\d+ <([^+ >]+) : ", t)
            if m and (CALL.match(t) or (m.group(1) != fn and m.group(1) in self.funcs)):
                toks.append("call:" + m.group(1))             # a goto into another function: a tail call
            elif re.match(r"^call r\d+$", t):
                toks.append("icall")
            if t.startswith(("if ", "ifs ")):
                toks.append("br")
            if t == "rts" or (POPR.match(t) and "pc" in t):
                toks.append("ret")
            for k in re.findall(r"\b0x[0-9A-Fa-f]+\b|(?<![\w+-])-?\d{5,}\b", t):
                v = _int(k) & 0xFFFFFFFF
                if v >= 0x10000 and not (RAM[0] <= v < RAM[1] or XIP[0] <= v < XIP[1]) and "<" not in t:
                    toks.append("k:%x" % v)
            for am in ANNOT.finditer(t):
                v = int(am.group(3), 16) & 0xFFFFFFFF
                if 0x10000 <= v < RAM[0]:
                    toks.append("k:%x" % v)
            m1, m2, m3, m4 = SETC.match(t), COPY.match(t), ADDI.match(t), INCI.match(t)
            if m1:
                regs[m1.group(1)] = _int(m1.group(2)) & 0xFFFFFFFF
            elif m2:
                if m2.group(2) in regs:
                    regs[m2.group(1)] = regs[m2.group(2)]
                else:
                    regs.pop(m2.group(1), None)
            elif m3:
                if m3.group(2) in regs:
                    regs[m3.group(1)] = (regs[m3.group(2)] + int(m3.group(3))) & 0xFFFFFFFF
                else:
                    regs.pop(m3.group(1), None)
            elif m4:
                if m4.group(1) in regs:
                    regs[m4.group(1)] = (regs[m4.group(1)] + int(m4.group(2))) & 0xFFFFFFFF
            else:
                d = DEST.match(t)
                if d:
                    regs.pop(d.group(1), None)
                if t.startswith("call"):                 # r0..r3 are the callee's to change; r4.. are saved
                    for r in ("r0", "r1", "r2", "r3"):
                        regs.pop(r, None)
                elif POPR.match(t):
                    regs.clear()
            out.append((text, toks))
        return out

    def text(self, fn):
        return [x for x, _ in self.walk(fn)]

    def effects(self, fn):
        c = Counter()
        for _, toks in self.walk(fn):
            c.update(toks)
        return c


def judge(ours, up, name):
    """None if the frozen function passes, else why"""
    if name not in ours.funcs:
        return "in upstream's binary, not in ours (inlined or removed)"
    if ours.text(name) == up.text(name):
        return None
    eo, eu = ours.effects(name), up.effects(name)
    if eo == eu:
        return None
    mode = INLINED.get(name)
    if mode and mode[0] == "union":                 # upstream's, the listed callees inlined, and recursively every
        want = Counter(eu)                           # callee upstream calls that our binary no longer calls here
        todo = list(mode[1])
        while todo:
            callee = todo.pop()
            k = want.pop("call:" + callee, 0)
            ce = up.effects(callee)
            ce.pop("ret", None)
            for _ in range(k):
                want.update(ce)
            todo += [c[5:] for c in ce if c.startswith("call:") and c not in eo and c[5:] in up.funcs
                     and c[5:] not in todo]
        eo2, want2 = Counter(eo), Counter(want)
        eo2.pop("ret", None)
        want2.pop("ret", None)
        if eo2 == want2:
            return None
        return f"effects differ from upstream's with {', '.join(mode[1])} inlined: ours-only {dict(eo2 - want2)}, upstream-only {dict(want2 - eo2)}"
    if mode and mode[0] == "prefix":
        mine = Counter(eo)
        for callee in mode[1]:
            mine.pop("call:" + callee, None)
        mine.pop("ret", None)
        extra = mine - eu
        return None if not extra else f"effects not in upstream's (with {', '.join(mode[1])} inlined): {dict(extra)}"
    return f"effects differ: ours-only {dict(eo - eu)}, upstream-only {dict(eu - eo)}"


def compare(ours, up):
    frozen = frozen_names()
    bad, exact, effects, reviewed, only_ours = [], 0, 0, 0, []
    for name in sorted(frozen):
        if name not in up.funcs:
            if name in ours.funcs:
                only_ours.append(name)
            continue
        inlined_into = [k for k, (mode, callees) in INLINED.items() if mode == "union" and name in callees]
        if name not in ours.funcs and inlined_into and all(judge(ours, up, k) is None for k in inlined_into):
            reviewed += 1                            # inlined into a listed caller, whose effects include it
            continue
        why = judge(ours, up, name)
        if why:
            bad.append(f"{name}: {why}")
        elif name in ours.funcs and ours.text(name) == up.text(name):
            exact += 1
        elif ours.effects(name) == up.effects(name):
            effects += 1
        else:
            reviewed += 1
    return bad, exact, effects, reviewed, only_ours


def selftest(ours, up):
    """an effect change in a frozen function must FAIL: a hardware-register constant, then a call target"""
    frozen = sorted(n for n in frozen_names() if n in ours.funcs and n in up.funcs and ours.text(n) == up.text(n))
    ok = True
    for kind, find, change in (("a hardware-register constant", re.compile(r"\b0x1[0-9A-Fa-f]{4}\b"),
                                lambda s: s.replace(s[s.index("0x"):s.index("0x") + 7], "0x1FFF0", 1)),
                               ("a call target", CALL, None)):
        victim = next((n for n in frozen if any(find.search(t) for _, t in ours.funcs[n])), None)
        if not victim:
            print(f"compare_upstream selftest: no frozen function with {kind}")
            ok = False
            continue
        saved = list(ours.funcs[victim])
        i = next(i for i, (_, t) in enumerate(saved) if find.search(t))
        a, t = saved[i]
        if change:
            t2 = change(t)
        else:                                        # another function, its name and its address
            other = next(n for n in sorted(ours.starts) if n != victim and n not in t)   # any other function
            t2 = re.sub(r"<[^<>]+ : [0-9a-f]+ >", "<%s : %x >" % (other, ours.starts[other]), t, count=1)
            t2 = re.sub(r"^call -?\d+ <", "call 0 <", t2)
        ours.funcs[victim][i] = (a, t2)
        why = judge(ours, up, victim)
        ours.funcs[victim] = saved
        print(f"compare_upstream selftest: {kind} changed in {victim} is {'caught' if why else 'NOT caught'}")
        ok &= bool(why)
    return 0 if ok else 1


def main(argv):
    st = argv[:1] == ["--selftest"]
    if st:
        argv = argv[1:]
    ours, up = Prog(argv[0]), Prog(argv[1])
    if st:
        return selftest(ours, up)
    bad, exact, effects, reviewed, only_ours = compare(ours, up)
    for b in bad:
        print("FAIL  " + b)
    if only_ours:
        print("info  frozen helpers only in our binary (our code calls them now): " + ", ".join(only_ours))
    print(f"compare_upstream: {exact} frozen functions identical, {effects} identical in effect (data layout), "
          f"{reviewed} on the reviewed inlining list, {len(bad)} different")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
