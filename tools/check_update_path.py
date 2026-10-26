#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# BLE MIDI: 2026 DEADACTIVE (from fm1-lsdj 548ce73; Hortator)
"""Gate 2 (spec 2026-10-08 BLE, part 1): the update path in a BLE build.
  2a: its sources are the last release's (`drum-v*`): check_update_path.py --sources [BASE]
  2b: its machine code equals the reference core's: the same core object linked with empty BLE entry points
      (tools/build.py links it in every BLE build: build/felucca_ref.dis; build.py calls compare())
      check_update_path.py build/felucca.dis build/felucca_ref.dis
  check_update_path.py --selftest

Functions compared: every function whose name matches PATTERNS, plus everything in .ram_text (the flash
driver). Instructions are compared as text with addresses and encodings dropped and branch / load targets
normalised to symbol names, so moving code around is fine and changing it is not."""
import re
import sys

PATTERNS = [r"^usb_", r"^ep[0-4]_", r"^e0_", r"^sie_", r"^sysex_byte$", r"^midi_in_event$", r"^midi_enqueue$",
            r"^ota_", r"^fm1_timer5_irq$", r"^fm1_cstart$", r"^fl_", r"^st_", r"^uac_",
            # the main loop: the update session, the UBOOT key and the SysEx UBOOT request are inlined into it
            r"^fm1_main$", r"^enter_uboot$", r"^fm1_enter_uboot$", r"^fm1_wdt_feed$"]
# (not cdc_task: the console's commands inline into it and change with them; the CDC transport is in usb_poll)
LABEL = re.compile(r"^([A-Za-z_.$][\w.$]*):\s*$")
INSN = re.compile(r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2} )+\s*\t(.*)$")
COMMENT = re.compile(r"\s*#\s*<[^>]*>")          # objdump's "# <sym : addr>" notes after a constant
BARE = re.compile(r"(?<![\w<+-])(\d{8})(?![\w>])")   # an 8-digit decimal constant (RAM / XIP addresses have 8)
ANNOT = re.compile(r"(-?\d+) <([^:>]+?)\s*:\s*([0-9a-f]+)\s*>", re.I)


def normalise(text, fn):
    """`N <sym+off : addr>` operands, independent of where things were placed: a call keeps its callee's name; a
    branch inside the function keeps its offset; any other RAM / XIP address is a data address, `<addr>` (objdump
    names the same address differently from build to build, e.g. _bss_end / an empty pool after it); anything else
    is a plain constant objdump annotated with the nearest symbol, and stays the number N"""
    def one(m):
        num, sym, addr = m.groups()
        sym = sym.strip()
        if not 0x01C00000 <= int(addr, 16) < 0x02100000:
            return num
        if text.lstrip().startswith("call"):
            return f"<{sym.split('+')[0]}>"
        if sym == fn or sym.startswith(fn + "+"):
            return f"<{sym[len(fn):] or '+0'}>"
        return "<addr>"

    def bare(m):                    # an unannotated constant in the RAM / XIP range: an address (it moves)
        return "<addr>" if 0x01C00000 <= int(m.group(1)) < 0x02100000 else m.group(0)
    return BARE.sub(bare, ANNOT.sub(one, COMMENT.sub("", text))).strip()


def functions(text):
    out, name = {}, None
    for line in text.splitlines():
        m = LABEL.match(line)
        if m and m.group(1).startswith("."):  # a local label (a jump table's .GJTIS…): inside the function
            continue
        if m:
            name = m.group(1)
            out[name] = []
            continue
        m = INSN.match(line)
        if m and name:
            out[name].append(normalise(m.group(1), name))
        elif line.startswith("| ") and name:      # a reference file's instruction line (render)
            out[name].append(line[2:].rstrip("\n"))
    for body in out.values():                     # alignment padding after the last instruction is not code
        while body and body[-1] == "nop":
            body.pop()
    return out


def selected(funcs):
    return {n: body for n, body in funcs.items() if any(re.search(p, n) for p in PATTERNS)}


def render(funcs):
    return "".join(f"{n}:\n" + "".join(f"| {i}\n" for i in funcs[n]) for n in sorted(funcs))


def compare(dis_text, ref_text):
    now, ref = selected(functions(dis_text)), functions(ref_text)
    errors = [f"missing in this build: {n}" for n in ref if n not in now]
    errors += [f"changed: {n}" for n in ref if n in now and now[n] != ref[n]]
    errors += [f"new update-path function (not in the reference): {n}" for n in now if n not in ref]
    return errors


def selftest():
    dis = ("usb_poll:\n  2000000:    00 49    \tr0 = 1\n  2000002:    80 00    \trts\n"
           "other:\n  2000010:    80 00    \trts\n")
    ref = render(selected(functions(dis)))
    assert compare(dis, ref) == [], "identical listing flagged"
    assert compare(dis.replace("r0 = 1", "r0 = 2"), ref), "changed instruction not caught"
    assert compare(dis.replace("usb_poll:", "usb_poll2:"), ref), "missing function not caught"
    moved = dis.replace("2000000:", "2000100:")
    assert compare(moved, ref) == [], "moved code flagged"
    # objdump's real forms: upper-case offsets, relative call / branch numbers that change with layout, constants
    # annotated with a nonsense nearest symbol (keep the constant), address constants (normalise to the symbol)
    a = ("usb_x:\n  2000000:    00 49    \tif (r1 == 0) goto 38 <usb_x+0x2A : 2002d6c >\n"
         "  2000002:    00 49    \tcall -40632 <memset : 2001fae >\n"
         "  2000004:    00 49    \tr3 = -20000 <_data_load+0xFFFFFFFFFDFEADC8 : ffffffffffffb1e0 >\n"
         "  2000006:    00 49    \tr0 = 33720110 <usb_x.D1 : 202872e >\n")
    b = (a.replace("2002d6c", "2003faa").replace("-40632", "-50000").replace("2001fae", "2003fae")
          .replace("FFFFFFFFFDFEADC8", "FFFFFFFFFDF84D5C").replace("33720110", "33800000").replace("202872e", "2035f00"))
    ref2 = render(selected(functions(a)))
    assert compare(b, ref2) == [], "layout-only changes flagged"
    assert compare(b.replace("-20000", "-20004"), ref2), "a changed constant not caught"
    assert compare(b.replace("<memset", "<memcpy"), ref2), "a changed call target not caught"
    c1 = "usb_y:\n  2000000:    00 49    \tr1 = -12711  # <_data_load+0xFFFFFFFFFDFECA40 : ffffffffffffce58 >\n"
    ref3 = render(selected(functions(c1)))
    assert compare(c1.replace("FDFECA40", "FDF869D4"), ref3) == [], "a comment annotation flagged"
    assert compare(c1.replace("-12711", "-12712"), ref3), "the constant before a comment not caught"
    # the same address may carry several names (an empty pool after _bss_end): addresses compare by role, branches
    # inside the function by offset
    d1 = ("usb_z:\n  2000000:    00 49    \tr3 = 29410364 <_bss_end : 1c0c43c >\n"
          "  2000002:    00 49    \tgoto 10 <usb_z+0x2A : 200002a >\n")
    ref4 = render(selected(functions(d1)))
    assert compare(d1.replace("_bss_end", "tws_bulk_pool_end"), ref4) == [], "an address alias flagged"
    assert compare(d1.replace("usb_z+0x2A", "usb_z+0x2C"), ref4), "a moved branch target not caught"
    # alignment padding after a function's last instruction (objdump puts it in the function) is not code: what
    # follows a function in its section decides it, so it comes and goes with the layout; a nop inside is code
    e1 = ("usb_w:\n  2000000:    00 49    \tr0 = 1\n  2000002:    80 00    \trts\n  2000004:    00 00    \tnop\n"
          "other:\n  2000010:    80 00    \trts\n")
    ref5 = render(selected(functions(e1)))
    assert compare(e1.replace("  2000004:    00 00    \tnop\n", ""), ref5) == [], "trailing padding flagged"
    e2 = e1.replace("\tr0 = 1\n", "\tr0 = 1\n  2000001:    00 00    \tnop\n")
    assert compare(e2, ref5), "a nop inside the function not caught"
    # the main loop holds the inlined update session, the UBOOT key and the SysEx UBOOT request (review #1)
    f1 = ("fm1_main:\n  2000000:    00 49    \tcall -40 <ota_prog : 2001fae >\n  2000002:    80 00    \trts\n")
    ref6 = render(selected(functions(f1)))
    assert compare(f1.replace("<ota_prog", "<ota_show"), ref6), "a changed call in fm1_main not caught"
    for fn in ("enter_uboot", "fm1_enter_uboot", "fm1_wdt_feed"):
        assert selected({fn: ["rts"]}), f"{fn} not compared"
    # a jump table's local labels (.GJTIS…) are inside the function: a change after them is caught
    g1 = ("usb_j:\n  2000000:    00 49    \tr0 = 1\n.GJTIS7_0_0_:\n  2000002:    00 49    \tr1 = 2\n"
          "  2000004:    80 00    \trts\n")
    ref7 = render(selected(functions(g1)))
    assert compare(g1.replace("r1 = 2", "r1 = 3"), ref7), "a change after a jump table's label not caught"
    assert compare(g1.replace(".GJTIS7_0_0_", ".GJTIS9_0_0_"), ref7) == [], "a renumbered local label flagged"
    # a bare constant in the RAM / XIP range (objdump leaves some address loads unannotated, e.g. inside `if (...)
    # {` forms) is an address: it moves with the layout; a constant outside the range is compared
    h1 = "usb_k:\n  2000000:    00 49    \tif (r0 != 7) {\n  2000002:    00 49    \tr1 = 33848701\n"
    ref8 = render(selected(functions(h1)))
    assert compare(h1.replace("33848701", "33884012"), ref8) == [], "a moved unannotated address flagged"
    h2 = "usb_k:\n  2000000:    00 49    \tr1 = 4096\n"
    assert compare(h2.replace("4096", "4097"), render(selected(functions(h2)))), "a changed small constant not caught"
    real = globals()["git"]          # gate 2a's rule for added files, with a fake git
    try:
        globals()["git"] = lambda *a: "A\tfirmware/hal/fm1_ble_hal.h\n"
        assert source_errors("x") == [], "an allowed new HAL file flagged"
        globals()["git"] = lambda *a: "M\tfirmware/src/ota.c\nA\tfirmware/hal/fm1_new.h\n"
        assert len(source_errors("x")) == 2, "a changed source / an unlisted new file not caught"
    finally:
        globals()["git"] = real
    # main.c: the recorded backlight hook (frozen-base-8) and nothing else, exactly once, in its place
    base = ("void fm1_timer5_irq(void)\n{\n    fm1_timer5_ack();\n" + HOOK_AFTER + "    {   /* ms */\n    }\n"
            "    if (felucca_dbg.in_audio)\n        return;\n}\n")
    hooked = base.replace(HOOK_AFTER, HOOK_AFTER + HOOK_LINE)
    cases = {"the hook": (hooked, True),
             "the hook and another change": (hooked.replace("return;", "return; /* x */"), False),
             "the hook twice": (hooked.replace(HOOK_LINE, HOOK_LINE * 2), False),
             "the hook before fm1_input_tick": (base.replace(HOOK_AFTER, HOOK_LINE + HOOK_AFTER), False),
             "the hook after the in_audio return": (base.replace("        return;\n", "        return;\n" + HOOK_LINE), False),
             "another hook line": (base.replace(HOOK_AFTER, HOOK_AFTER + HOOK_LINE.replace("led_dim_tick", "led_dim_tock")), False),
             "another change, no hook": (base.replace("fm1_timer5_ack();", "fm1_timer5_ack(); x();"), False),
             "a comment changed with the hook": (hooked.replace("/* ms */", "/* us */"), False)}
    real_wt = globals()["worktree_text"]
    try:
        for what, (cur, ok) in cases.items():
            globals()["git"] = lambda *a, b=base: b if a[0] == "show" else "M\tfirmware/src/main.c\n"
            globals()["worktree_text"] = lambda path, c=cur: c
            assert (source_errors("x") == []) == ok, f"main.c, {what}: {'flagged' if ok else 'not caught'}"
        globals()["git"] = lambda *a: hooked if a[0] == "show" else "M\tfirmware/src/main.c\n"
        globals()["worktree_text"] = lambda path: hooked.replace(HOOK_LINE, HOOK_LINE * 2)
        assert source_errors("x"), "a release with the hook: a second hook line not caught"
        globals()["worktree_text"] = lambda path: hooked
        globals()["git"] = lambda *a: hooked if a[0] == "show" else ""
        assert source_errors("x") == [], "a release with the hook, main.c equal: flagged"
        globals()["git"] = lambda *a: base if a[0] == "show" else "M\tfirmware/src/storage.c\n"
        assert source_errors("x"), "the main.c allowance excused another file"
    finally:
        globals()["git"] = real
        globals()["worktree_text"] = real_wt
    # usb_app.c: the recorded USB product string (frozen-base-9) and nothing else
    ub = ("#ifdef FELUCCA_LOADER\n" + STR2_LOADER + "#else\n" + STR2_OLD + "#endif\n")
    named = ub.replace(STR2_OLD, STR2_NEW)
    cases = {"the new name": (named, True),
             "the new name and another change": (named.replace("#endif", "#endif /* x */"), False),
             "the loader's string changed": (ub.replace(STR2_LOADER, STR2_LOADER.replace("'F', 0", "'H', 0")), False),
             "another name": (ub.replace(STR2_OLD, STR2_NEW.replace("'r', 0}", "'R', 0}")), False),
             "the new name twice": (named.replace("#endif", STR2_NEW + "#endif"), False)}
    try:
        for what, (cur, ok) in cases.items():
            globals()["git"] = lambda *a, b=ub: b if a[0] == "show" else "M\tfirmware/src/usb_app.c\n"
            globals()["worktree_text"] = lambda path, c=cur: c
            assert (source_errors("x") == []) == ok, f"usb_app.c, {what}: {'flagged' if ok else 'not caught'}"
        globals()["git"] = lambda *a: named if a[0] == "show" else "M\tfirmware/src/usb_app.c\n"
        globals()["worktree_text"] = lambda path: named.replace(STR2_NEW, STR2_OLD)
        assert source_errors("x"), "a release with the new name: the old name back not caught"
        globals()["git"] = lambda *a: ub if a[0] == "show" else "M\tfirmware/src/ota.c\n"
        globals()["worktree_text"] = lambda path: named
        assert source_errors("x"), "the usb_app.c allowance excused another file"
    finally:
        globals()["git"] = real
        globals()["worktree_text"] = real_wt
    # storage.c: the recorded lock-bank objects (frozen-base-10) and nothing else
    sb = ("/* SPDX */\n" + ST_MAP_OLD + ST_ENUM_OLD + "\nstatic uint32_t st_sector(uint32_t obj, uint32_t copy)\n{\n"
          + ST_SECTOR_OLD + "    return 0;\n}\n")
    locked = sb.replace(ST_MAP_OLD, ST_MAP_NEW).replace(ST_ENUM_OLD, ST_ENUM_NEW).replace(ST_SECTOR_OLD, ST_SECTOR_NEW)
    cases = {"the lock banks": (locked, True),
             "the lock banks and another change": (locked.replace("return 0;", "return 1;"), False),
             "only the enum hunk": (sb.replace(ST_ENUM_OLD, ST_ENUM_NEW), False),
             "another address": (locked.replace("0xD4000u +", "0xD5000u +"), False),
             "the lock banks twice": (locked.replace(ST_SECTOR_NEW, ST_SECTOR_NEW + ST_SECTOR_NEW.replace(ST_SECTOR_OLD, "")),
                                      False)}
    try:
        for what, (cur, ok) in cases.items():
            globals()["git"] = lambda *a, b=sb: b if a[0] == "show" else "M\tfirmware/src/storage.c\n"
            globals()["worktree_text"] = lambda path, c=cur: c
            assert (source_errors("x") == []) == ok, f"storage.c, {what}: {'flagged' if ok else 'not caught'}"
        globals()["git"] = lambda *a: locked if a[0] == "show" else ""
        globals()["worktree_text"] = lambda path: locked
        assert source_errors("x") == [], "a release with the lock banks, storage.c equal: flagged"
        globals()["git"] = lambda *a: locked if a[0] == "show" else "M\tfirmware/src/storage.c\n"
        globals()["worktree_text"] = lambda path: sb
        assert source_errors("x"), "a release with the lock banks: the old text back not caught"
        globals()["git"] = lambda *a: sb if a[0] == "show" else "M\tfirmware/src/ota.c\n"
        globals()["worktree_text"] = lambda path: locked
        assert source_errors("x"), "the storage.c allowance excused another file"
    finally:
        globals()["git"] = real
        globals()["worktree_text"] = real_wt
    # storage.c: the recorded variation objects (frozen-base-11) and nothing else, over 0.24.0's text (the lock banks)
    vb = ("/* SPDX */\n" + SV_MAP_OLD + SV_ENUM_OLD + "\nstatic uint32_t st_sector(uint32_t obj, uint32_t copy)\n{\n"
          + SV_SECTOR_OLD + "        return 0xD4000u;\n    return 0;\n}\n")
    varied = vb.replace(SV_MAP_OLD, SV_MAP_NEW).replace(SV_ENUM_OLD, SV_ENUM_NEW).replace(SV_SECTOR_OLD, SV_SECTOR_NEW)
    cases = {"the variations": (varied, True),
             "the variations and another change": (varied.replace("return 0;", "return 1;"), False),
             "only the enum hunk": (vb.replace(SV_ENUM_OLD, SV_ENUM_NEW), False),
             "another address": (varied.replace("0xC0000u +", "0xC1000u +"), False),
             "the variations twice": (varied.replace(SV_SECTOR_NEW, SV_SECTOR_NEW + SV_SECTOR_NEW.replace(SV_SECTOR_OLD, "")),
                                      False)}
    try:
        for what, (cur, ok) in cases.items():
            globals()["git"] = lambda *a, b=vb: b if a[0] == "show" else "M\tfirmware/src/storage.c\n"
            globals()["worktree_text"] = lambda path, c=cur: c
            assert (source_errors("x") == []) == ok, f"storage.c, {what}: {'flagged' if ok else 'not caught'}"
        globals()["git"] = lambda *a: varied if a[0] == "show" else ""
        globals()["worktree_text"] = lambda path: varied
        assert source_errors("x") == [], "a release with the variations, storage.c equal: flagged"
        globals()["git"] = lambda *a: varied if a[0] == "show" else "M\tfirmware/src/storage.c\n"
        globals()["worktree_text"] = lambda path: vb
        assert source_errors("x"), "a release with the variations: the old text back not caught"
        globals()["git"] = lambda *a: vb if a[0] == "show" else "M\tfirmware/src/ota.c\n"
        globals()["worktree_text"] = lambda path: varied
        assert source_errors("x"), "the storage.c variations allowance excused another file"
    finally:
        globals()["git"] = real
        globals()["worktree_text"] = real_wt
    print("check_update_path: selftest ok")


# gate 2a: the update path's sources are the last release's (spec amendment 8). Modified or deleted files fail;
# added files only as listed (the BLE build's new HAL header and context switch: spec amendment 2)
SOURCES = ["firmware/src/usb.c", "firmware/src/usb_app.c", "firmware/src/ota.c", "firmware/src/storage.c",
           "firmware/src/libc.c", "firmware/hal", "firmware/loader",
           # the main loop, the start-up and the memory map (frozen): gate 2b links the same core object twice, so a
           # change here would otherwise reach a BLE device build unseen (review 1b, I3)
           "firmware/src/main.c", "firmware/crt0.S", "firmware/app.ld"]
NEW_OK = {"firmware/hal/fm1_ble_hal.h", "firmware/hal/fm1_ctx.h", "firmware/hal/fm1_ctx.S"}
# the one recorded change to main.c a release may not have yet (user decision 2026-10-12, frozen-base-8, spec
# 2026-10-11-fixes §12): the key backlight's hook, this exact line right after fm1_timer5_irq's fm1_input_tick(), and
# nothing else. Once a release has it, main.c must equal that release's again (a second hook line fails).
MAIN_C = "firmware/src/main.c"
HOOK_AFTER = "    fm1_input_tick();\n"
HOOK_LINE = "    led_dim_tick();                             /* the dimmed LEDs (led_dim.c): the column the next tick lights */\n"


# the one recorded change to usb_app.c a release may not have yet (user decision 2026-10-23, frozen-base-9): the app's
# USB product string "Felucca" -> "Hortator" (STR2, the line after #else; the FELUCCA_LOADER string above it stays),
# and nothing else. Once a release has it, usb_app.c must equal that release's again.
USB_APP_C = "firmware/src/usb_app.c"
STR2_LOADER = ("static const uint8_t STR2[] = {30, 3, 'F', 0, 'e', 0, 'l', 0, 'u', 0, 'c', 0, 'c', 0, 'a', 0, ' ', 0, "
               "'U', 0, 'p', 0,\n                               'd', 0, 'a', 0, 't', 0, 'e', 0};\n")
STR2_OLD = "static const uint8_t STR2[] = {16, 3, 'F', 0, 'e', 0, 'l', 0, 'u', 0, 'c', 0, 'c', 0, 'a', 0};\n"
STR2_NEW = "static const uint8_t STR2[] = {18, 3, 'H', 0, 'o', 0, 'r', 0, 't', 0, 'a', 0, 't', 0, 'o', 0, 'r', 0};\n"


# the one recorded change to storage.c a release may not have yet (user decision 2026-10-24, frozen-base-10, option D of
# docs/superpowers/specs/2026-10-24-parameter-locks-design.md): the lock-bank objects OBJ_LOCKS0..3 at 0xD4000..0xDBFFF
# (the map comment, the object enum and st_sector's first test), and nothing else. Once a release has it, storage.c
# must equal that release's again.
STORAGE_C = "firmware/src/storage.c"
ST_MAP_OLD = ("/* flash map (FL_DATA 0x97000..0xDFFFF, FL_GLOB 0xFC000..): settings 0xFC000, projects 0x97000..0x9EFFF,\n"
              " * user sample slots 0xA0000..0xDBFFF (eng_sample.c), user preset banks 0xDC000..0xDFFFF (upreset.c) */\n")
ST_MAP_NEW = ("/* flash map (FL_DATA 0x97000..0xDFFFF, FL_GLOB 0xFC000..): settings 0xFC000, projects 0x97000..0x9EFFF,\n"
              " * user sample slots 0xA0000..0xD3FFF (eng_sample.c), lock banks 0xD4000..0xDBFFF (project.c),\n"
              " * user preset banks 0xDC000..0xDFFFF (upreset.c) */\n")
ST_ENUM_OLD = "enum { OBJ_SETTINGS, OBJ_PROJECT0, OBJ_UPRESET0 = OBJ_PROJECT0 + 4, OBJ_COUNT = OBJ_UPRESET0 + 2 };\n"
ST_ENUM_NEW = ("enum { OBJ_SETTINGS, OBJ_PROJECT0, OBJ_UPRESET0 = OBJ_PROJECT0 + 4, OBJ_LOCKS0 = OBJ_UPRESET0 + 2,\n"
               "       OBJ_COUNT = OBJ_LOCKS0 + 4 };\n")
ST_SECTOR_OLD = "    if (obj >= OBJ_UPRESET0)\n"
ST_SECTOR_NEW = ("    if (obj >= OBJ_LOCKS0)\n"
                 "        return 0xD4000u + (obj - OBJ_LOCKS0) * 2u * ST_SECTOR + copy * ST_SECTOR;\n" + ST_SECTOR_OLD)
ST_HUNKS = ((ST_MAP_OLD, ST_MAP_NEW), (ST_ENUM_OLD, ST_ENUM_NEW), (ST_SECTOR_OLD, ST_SECTOR_NEW))


def storage_locks_only(base_text, cur_text):
    """cur_text is base_text with each recorded storage.c hunk replaced once by its lock-bank text, nothing else"""
    if "OBJ_LOCKS0" in base_text or any(base_text.count(old) != 1 for old, _ in ST_HUNKS):
        return False
    want = base_text
    for old, new in ST_HUNKS:
        want = want.replace(old, new)
    return cur_text == want


# the one recorded change to storage.c past 0.24.0 a release may not have yet (owner decision 2026-10-26, frozen-base-11,
# docs/superpowers/specs/2026-10-26-cue-variations-fill-copy-design.md): the variation objects OBJ_VAR0..3 at
# 0xC0000..0xC7FFF, cut from USR2 (the map comment, the object enum and st_sector's first test), and nothing else.
SV_MAP_OLD = ST_MAP_NEW
SV_MAP_NEW = ("/* flash map (FL_DATA 0x97000..0xDFFFF, FL_GLOB 0xFC000..): settings 0xFC000, projects 0x97000..0x9EFFF,\n"
              " * user sample slots 0xA0000..0xD3FFF (eng_sample.c) but 0xC0000..0xC7FFF the variations (project.c),\n"
              " * lock banks 0xD4000..0xDBFFF (project.c), user preset banks 0xDC000..0xDFFFF (upreset.c) */\n")
SV_ENUM_OLD = ST_ENUM_NEW
SV_ENUM_NEW = ("enum { OBJ_SETTINGS, OBJ_PROJECT0, OBJ_UPRESET0 = OBJ_PROJECT0 + 4, OBJ_LOCKS0 = OBJ_UPRESET0 + 2,\n"
               "       OBJ_VAR0 = OBJ_LOCKS0 + 4, OBJ_COUNT = OBJ_VAR0 + 4 };\n")
SV_SECTOR_OLD = "    if (obj >= OBJ_LOCKS0)\n"
SV_SECTOR_NEW = ("    if (obj >= OBJ_VAR0)\n"
                 "        return 0xC0000u + (obj - OBJ_VAR0) * 2u * ST_SECTOR + copy * ST_SECTOR;\n" + SV_SECTOR_OLD)
SV_HUNKS = ((SV_MAP_OLD, SV_MAP_NEW), (SV_ENUM_OLD, SV_ENUM_NEW), (SV_SECTOR_OLD, SV_SECTOR_NEW))


def storage_vars_only(base_text, cur_text):
    """cur_text is base_text (with the lock banks) with each recorded hunk replaced once by its variations text"""
    if "OBJ_VAR0" in base_text or any(base_text.count(old) != 1 for old, _ in SV_HUNKS):
        return False
    want = base_text
    for old, new in SV_HUNKS:
        want = want.replace(old, new)
    return cur_text == want


def usb_name_only(base_text, cur_text):
    """cur_text is base_text with its one app product string line renamed to the recorded one, nothing else"""
    if STR2_NEW in base_text or base_text.count(STR2_OLD) != 1:
        return False
    return cur_text == base_text.replace(STR2_OLD, STR2_NEW)


def main_hook_only(base_text, cur_text):
    """cur_text is base_text with the recorded hook line added after its one fm1_input_tick() line, nothing else"""
    if HOOK_LINE in base_text or base_text.count(HOOK_AFTER) != 1:
        return False
    return cur_text == base_text.replace(HOOK_AFTER, HOOK_AFTER + HOOK_LINE)


def worktree_text(path):
    from pathlib import Path
    p = Path(__file__).resolve().parents[1] / path
    return p.read_text() if p.exists() else None


def git(*a):
    import subprocess
    from pathlib import Path
    r = subprocess.run(["git", *a], cwd=Path(__file__).resolve().parents[1], capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(f"check_update_path: git {' '.join(a)}: {r.stderr.strip()}")
    return r.stdout


def last_release():
    """the newest drum-v* tag merged into HEAD (each release moves gate 2a's base)"""
    return git("describe", "--tags", "--abbrev=0", "--match", "drum-v*").strip()


def source_errors(base):
    errors = []
    for ln in git("diff", "--name-status", base, "--", *SOURCES).splitlines():
        status, path = ln.split("\t", 1)
        if status == "A" and path in NEW_OK:
            continue
        if status == "M" and path == MAIN_C and main_hook_only(git("show", f"{base}:{path}"), worktree_text(path) or ""):
            continue
        if status == "M" and path == USB_APP_C and usb_name_only(git("show", f"{base}:{path}"), worktree_text(path) or ""):
            continue
        if status == "M" and path == STORAGE_C and storage_locks_only(git("show", f"{base}:{path}"),
                                                                       worktree_text(path) or ""):
            continue
        if status == "M" and path == STORAGE_C and storage_vars_only(git("show", f"{base}:{path}"),
                                                                      worktree_text(path) or ""):
            continue
        errors.append(f"update path source {path} differs from {base} ({status})")
    return errors


def source_note(base):
    """what gate 2a allowed beyond the release's sources ("" for nothing)"""
    changed = git("diff", "--name-only", base, "--", MAIN_C, USB_APP_C, STORAGE_C).split()
    return ((" + main.c's recorded backlight hook (frozen-base-8)" if MAIN_C in changed else "") +
            (" + usb_app.c's recorded USB name Hortator (frozen-base-9)" if USB_APP_C in changed else "") +
            (" + storage.c's recorded lock-bank objects (frozen-base-10)" if STORAGE_C in changed and
             "OBJ_LOCKS0" not in git("show", f"{base}:{STORAGE_C}") else "") +
            (" + storage.c's recorded variation objects (frozen-base-11)" if STORAGE_C in changed and
             "OBJ_LOCKS0" in git("show", f"{base}:{STORAGE_C}") else ""))


def main(argv):
    if argv[1:2] == ["--sources"]:
        base = argv[2] if len(argv) > 2 else last_release()
        errors = source_errors(base)
        for e in errors:
            print("gate 2a:", e)
        if not errors:
            print(f"update path sources: identical to {base}{source_note(base)}")
        return 1 if errors else 0
    if argv[1:] == ["--selftest"]:
        selftest()
        return 0
    errors = compare(open(argv[1]).read(), render(selected(functions(open(argv[2]).read()))))
    for e in errors:
        print("update path:", e)
    print("update path: unchanged" if not errors else f"update path: {len(errors)} difference(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
