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
    A union entry may record its callee's pointer argument (PTR_ARG, reviewed: the caller passes that object):
    inlined, the callee's accesses through the pointer are that object's, resolved or not.
Anything else, or a frozen function in upstream's binary but not in ours: FAIL.
Both builds compile the app with global merging off (tools/build.py, user decision 2026-10-06), so data is named
alike in both."""
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
# reviewed facts about an inlined callee's pointer argument (user decision 2026-10-06): inlined, its accesses
# through the pointer ("ptr") are accesses to the object the caller passes. ota_send_msg calls
# ota_wire_send(ota_wire, w) (firmware/src/ota.c)
PTR_ARG = {("ota_send_msg", "ota_wire_send"): "ota_wire"}


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
        self.by_addr = {a: n for n, a in self.starts.items()}

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
        regs, out, pending = {}, [], None

        def track(t):
            self._track(regs, t)
        for addr, t in self.funcs[fn]:
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
                if target == "ptr" and ra in regs and rb is None:   # a known address that is no object: hardware
                    v = (regs[ra] + (int(off) if off else 0)) & 0xFFFFFFFF
                    if not (RAM[0] <= v < RAM[1] or XIP[0] <= v < XIP[1]):
                        target = "hw:%x" % v
                store = t.strip().startswith(m.group(0)) and re.match(r"^\S+\s*[-+&|^]?=", t.strip())
                imm = re.match(r"^\S+\s*=\s*(0x[0-9A-Fa-f]+|-?\d+)$", t.strip())
                if store and imm:                            # a stored immediate: its value is an effect
                    target += "=%x" % (_int(imm.group(1)) & 0xFFFFFFFF)
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
            else:
                mu = re.match(r"^call (-?\d+)$", t)      # a call the listing does not annotate: address + 4 + N
                if mu:
                    toks.append("call:" + self.by_addr.get(addr + 4 + int(mu.group(1)), "?%x" % (addr + 4 + int(mu.group(1)))))
            mb = re.match(r"^(ifs?) \((.*)\) (?:goto|\{)", t)
            if mb:                                       # a branch and its condition (registers abstracted)
                cond = re.sub(r"\br\d+\b", "R", ANNOT.sub("", mb.group(2)))
                toks.append(mb.group(1) + ":" + re.sub(r"\s+", " ", cond).strip())
            ma = re.match(r"^r\d+ (?:= r\d+ )?(\||&|\^|<<|>>)=? ?(0x[0-9A-Fa-f]+|-?\d+)$", t)
            if ma:                                       # a mask or a shift: its immediate is an effect
                toks.append("alu:%s%x" % (ma.group(1), _int(ma.group(2)) & 0xFFFFFFFF))
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
            if pending is not None:                      # the second instruction of a "#" bundle has run: now the
                track(pending)                           # first one's register write takes effect
                pending = None
            if t.rstrip().endswith("#"):                 # a bundle: its partner reads the registers before this write
                pending = t
            else:
                track(t)
            out.append((text, toks))
        return out

    def _track(self, regs, t):
        """the register-tracking effect of one instruction"""
        if True:
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
        bounds = set()
        while todo:
            callee = todo.pop()
            k = want.pop("call:" + callee, 0)
            ce = up.effects(callee)
            bound = PTR_ARG.get((name, callee))
            if bound:                                # its pointer argument, known once inlined (PTR_ARG)
                bc = Counter()
                for e, c in ce.items():
                    bc[re.sub(r":ptr(?=$|[+=])", ":" + bound, e)] += c
                ce = bc
                bounds.add(bound)
            ce.pop("ret", None)
            for _ in range(k):
                want.update(ce)
            todo += [c[5:] for c in ce if c.startswith("call:") and c not in eo and c[5:] in up.funcs
                     and c[5:] not in todo]
        eo2, want2 = Counter(eo), Counter(want)
        eo2.pop("ret", None)
        want2.pop("ret", None)
        for b in bounds:                             # a bound pointer access our analysis left unresolved ("ptr")
            for e in list((want2 - eo2).elements()):  # is that object's access (PTR_ARG): op and the rest the same
                p = re.sub(r":" + re.escape(b) + r"(?=$|[+=])", ":ptr", e, count=1)
                if p != e and eo2[p] > want2[p]:
                    eo2[p] -= 1
                    eo2[e] += 1
        if eo2 == want2:
            return None
        return f"effects differ from upstream's with {', '.join(mode[1])} inlined: ours-only {dict(eo2 - want2)}, upstream-only {dict(want2 - eo2)}"
    if mode and mode[0] == "prefix":                # ours, in order, is how upstream's begins (it then goes on
        drop = {"ret"} | {"call:" + c for c in mode[1]}   # with the inlined callee)
        seq_o = [k for _, toks in ours.walk(name) for k in toks if k not in drop]
        seq_u = [k for _, toks in up.walk(name) for k in toks]
        if Counter(seq_o) == Counter(seq_u[:len(seq_o)]):   # the same effects, scheduling aside
            return None
        i = next((i for i in range(len(seq_o)) if i >= len(seq_u) or seq_o[i] != seq_u[i]), len(seq_o))
        return f"not upstream's beginning (with {', '.join(mode[1])} inlined) at effect {i}: ours {seq_o[i:i+3]} upstream {seq_u[i:i+3]}"
    return f"effects differ: ours-only {dict(eo - eu)}, upstream-only {dict(eu - eo)}"


def compare(ours, up):
    frozen = frozen_names()
    bad, exact, effects, reviewed, only_ours = [], 0, 0, 0, []
    compare.source_only = sorted(n for n in frozen if n not in up.funcs and n not in ours.funcs)
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


MUTATIONS = (                                         # (what, a line it applies to, the change)
    ("an inverted branch", re.compile(r"^ifs? \(.*(==|!=)"), lambda t: t.replace("==", "\0").replace("!=", "==").replace("\0", "!=")),
    ("a stored constant + 1", re.compile(r"^[bhd]?\[[^]]+\] = (0x[0-9A-Fa-f]+|\d+)$"),
     lambda t: re.sub(r"(0x[0-9A-Fa-f]+|\d+)$", lambda m: str(_int(m.group(1)) + 1), t)),
    ("a hardware-register offset", re.compile(r"\[r\d+\+\d+\]"),
     lambda t: re.sub(r"\[(r\d+)\+(\d+)\]", lambda m: "[%s+%d]" % (m.group(1), int(m.group(2)) + 4), t, count=1)),
    ("a bit mask", re.compile(r"^r\d+ [|&^]= (0x[0-9A-Fa-f]+|\d+)$"),
     lambda t: re.sub(r"(0x[0-9A-Fa-f]+|\d+)$", lambda m: hex(_int(m.group(1)) ^ 0x100), t)),
)


def _mutate(prog, name, find, change, only_hw=False):
    """the first line of name that find matches (only_hw: an access through a register holding a hardware
    address), changed; returns the saved body or None"""
    saved = list(prog.funcs[name])
    regs = {}
    for i, (a, t) in enumerate(saved):
        m1 = SETC.match(t)
        if m1:
            regs[m1.group(1)] = _int(m1.group(2)) & 0xFFFFFFFF
        if not find.search(t):
            continue
        if only_hw:
            m = re.search(r"\[(r\d+)\+\d+\]", t)
            if not m or not (0x10000 <= regs.get(m.group(1), 0) < RAM[0]):
                continue
        prog.funcs[name][i] = (a, change(t))
        return saved
    return None


def selftest(ours, up):
    """effect changes in frozen functions must FAIL, in the functions that pass on identical text and in those
    that pass only on identical effects (their text differs by data layout)"""
    frozen = sorted(n for n in frozen_names() if n in ours.funcs and n in up.funcs)
    exact = [n for n in frozen if ours.text(n) == up.text(n)]
    by_effects = [n for n in frozen if n not in exact and n not in INLINED and judge(ours, up, n) is None]
    ok = True
    for what, find, change in MUTATIONS:
        tried = caught = 0
        for name in exact[:20] + by_effects:
            saved = _mutate(ours, name, find, change, only_hw=what == "a hardware-register offset")
            if saved is None:
                continue
            tried += 1
            caught += judge(ours, up, name) is not None
            ours.funcs[name] = saved
        print(f"compare_upstream selftest: {what}: caught in {caught} of {tried} functions")
        ok &= tried > 0 and caught == tried
    victim = next(n for n in exact if any(CALL.match(t) for _, t in ours.funcs[n]))   # an unannotated call to another
    saved = list(ours.funcs[victim])
    i = next(i for i, (_, t) in enumerate(saved) if CALL.match(t))
    a, t = saved[i]
    other = next(n for n in sorted(ours.starts) if n != victim and n not in t and not n.startswith("."))
    ours.funcs[victim][i] = (a, "call %d" % (ours.starts[other] - a - 4))
    why = judge(ours, up, victim)
    ours.funcs[victim] = saved
    print(f"compare_upstream selftest: an unannotated call to another function in {victim} is {'caught' if why else 'NOT caught'}")
    ok &= bool(why)
    saved = list(ours.funcs["fm1_cstart"])                # prefix mode: a deleted store
    k = next(i for i, (_, t) in enumerate(saved) if re.match(r"^\[r\d+\+\d+\] = ", t))
    del ours.funcs["fm1_cstart"][k]
    why = judge(ours, up, "fm1_cstart")
    ours.funcs["fm1_cstart"] = saved
    print(f"compare_upstream selftest: a store deleted from fm1_cstart is {'caught' if why else 'NOT caught'}")
    ok &= bool(why)
    for key, obj in list(PTR_ARG.items()):           # a pointer binding to the wrong object (PTR_ARG) must fail
        PTR_ARG[key] = "ota_msg"
        why = key[0] in ours.funcs and judge(ours, up, key[0])
        PTR_ARG[key] = obj
        print(f"compare_upstream selftest: {key[1]}'s pointer bound to the wrong object in {key[0]} is "
              f"{'caught' if why else 'NOT caught'}")
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
    print(f"info  {len(compare.source_only)} frozen functions exist in neither binary on their own (inlined into "
          f"other code, e.g. {', '.join(compare.source_only[:6])}): their source is checked (check_untouched), "
          f"not their machine code")
    print(f"compare_upstream: {exact} frozen functions identical, {effects} identical in effect (data layout), "
          f"{reviewed} on the reviewed inlining list, {len(bad)} different")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
