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
    print("check_update_path: selftest ok")


# gate 2a: the update path's sources are the last release's (spec amendment 8). Modified or deleted files fail;
# added files only as listed (the BLE build's new HAL header and context switch: spec amendment 2)
SOURCES = ["firmware/src/usb.c", "firmware/src/usb_app.c", "firmware/src/ota.c", "firmware/src/storage.c",
           "firmware/src/libc.c", "firmware/hal", "firmware/loader"]
NEW_OK = {"firmware/hal/fm1_ble_hal.h", "firmware/hal/fm1_ctx.h", "firmware/hal/fm1_ctx.S"}


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
        if not (status == "A" and path in NEW_OK):
            errors.append(f"update path source {path} differs from {base} ({status})")
    return errors


def main(argv):
    if argv[1:2] == ["--sources"]:
        base = argv[2] if len(argv) > 2 else last_release()
        errors = source_errors(base)
        for e in errors:
            print("gate 2a:", e)
        if not errors:
            print(f"update path sources: identical to {base}")
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
