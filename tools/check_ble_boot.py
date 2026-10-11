#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# BLE MIDI: 2026 DEADACTIVE (after fm1-lsdj 548ce73's gate 4; Hortator)
"""Gate 3 (spec): no Bluetooth start-up reachable at boot. The graph is built from felucca.dis: an edge for every
function named in an instruction's `<name[+off] : addr>` operand (calls, tail gotos, and the address loads of the
BLE unit's long calls, `rN = … <fn : …>` then `call rN`). Walked from fm1_cstart, every isr_* entry and the fault
handlers; reaching a START function is an error, except through ALLOWED (the user's trigger: the console's
`ble start`, part 2's Bluetooth page), and ALLOWED itself may be referenced only from TRIGGERS. A trigger in CALLERS
may be referenced only from its listed functions: the page's BLUETOOTH ON from the page's input, and from AUTOSTART's
bt_autostart (the user's saved choice, amendment 2026-10-12 of spec 2026-10-10 §2), itself only from ui_input (the
main loop's UI, 5 s after power-on), never from felucca_init / drum_boot_init / the service hook / an ISR.

  check_ble_boot.py build/felucca.dis
  check_ble_boot.py --selftest"""
import re
import sys

START = {"ble_start", "btstack_init", "btctrler_task_init", "bt_ble_init", "bt_master_ble_init"}
ALLOWED = {"ble_user_start"}
TRIGGERS = {"ble_console", "bt_page_on"}   # the only functions that may reference ALLOWED (the user's commands:
                                           # the console's `ble start`, the Bluetooth page's BLUETOOTH ON)
CALLERS = {"bt_page_on": {"bt_page_input", "menu_input", "ui_input", "bt_autostart"},   # a trigger in the core: only
           "bt_autostart": {"ui_input"}}   # from the page's input (or the functions it is inlined into) and AUTOSTART
                                           # (amendment 2026-10-12: 5 s after power-on, from the main loop's UI only),
                                           # never the service hook, the boot or an ISR
ROOTS = ("fm1_cstart", "fm1_fatal_common", "fm1_fault_c")
LABEL = re.compile(r"^([A-Za-z_.$][\w.$]*):\s*$")
REF = re.compile(r"<([A-Za-z_.$][\w.$]*)(?:\+[^:>]*)?\s*:\s*[0-9a-f]+\s*>", re.I)


def graph(text):
    g, name = {}, None
    for line in text.splitlines():
        m = LABEL.match(line)
        if m and not m.group(1).startswith("."):  # (a local label, e.g. a jump table's .GJTIS…, stays in its function)
            name = m.group(1)
            g[name] = set()
        elif name:
            g[name].update(r.strip() for r in REF.findall(line))
    return {n: {r for r in refs if r in g} for n, refs in g.items()}     # functions only (labels of the listing)


def reachable_starts(text):
    g = graph(text)
    todo = [r for r in g if r in ROOTS or r.startswith("isr_")]
    seen, bad = set(), set()
    while todo:
        f = todo.pop()
        if f in seen or f in ALLOWED:
            continue
        seen.add(f)
        if f in START:
            bad.add(f)
        todo.extend(g.get(f, ()))
    for f, refs in g.items():           # the trigger from anywhere but the user's command is a start at boot
        if f not in TRIGGERS and f not in ALLOWED:
            bad.update(f"{a} (from {f})" for a in refs & ALLOWED)
        bad.update(f"{t} (from {f})" for t in refs & CALLERS.keys() if f != t and f not in CALLERS[t])
    return sorted(bad)


def selftest():
    def fn(name, *body):
        return f"{name}:\n" + "".join(f"  2000000:    00 49    \t{b}\n" for b in body)
    boot = fn("fm1_cstart", "call -4 <fm1_main : 2000010 >") + fn("fm1_main", "call -8 <ed_service : 2000020 >")
    assert reachable_starts(boot + fn("ed_service", "call -4 <ble_start : 2000030 >") + fn("ble_start", "rts")) == \
        ["ble_start"], "a direct call to ble_start not caught"
    ok = boot + fn("ed_service", "call -4 <ble_console : 2000030 >") + \
        fn("ble_console", "call -4 <ble_user_start : 2000038 >") + fn("ble_user_start", "call -4 <ble_start : 2000040 >")
    assert reachable_starts(ok + fn("ble_start", "rts")) == [], "the allowed trigger flagged"
    # the trigger itself is allowed only from the user's command (the console; part 2: the Bluetooth page): the
    # service hook calling it at boot (a saved "on") must stop the build
    hook = boot + fn("ed_service", "call -4 <ble_user_start : 2000030 >") + fn("ble_user_start",
                                                                                "call -4 <ble_start : 2000040 >")
    assert reachable_starts(hook + fn("ble_start", "rts")) == ["ble_user_start (from ed_service)"], \
        "the trigger called from outside the console not caught"
    page = boot + fn("ed_service", "call -4 <ui_input : 2000030 >") + fn("ui_input", "call -4 <bt_page_on : 2000038 >") + \
        fn("bt_page_on", "call -4 <ble_user_start : 2000040 >") + fn("ble_user_start", "call -4 <ble_start : 2000048 >")
    assert reachable_starts(page + fn("ble_start", "rts")) == [], "the Bluetooth page's BLUETOOTH ON flagged"
    # the page's trigger itself only from the page's input (inlined into menu_input / ui_input): from the service hook
    # or the boot (a saved "on") it is a start at boot
    for hook in ("ed_service", "felucca_init"):
        bad = boot + fn(hook, "call -4 <bt_page_on : 2000038 >") + \
            fn("bt_page_on", "call -4 <ble_user_start : 2000040 >") + fn("ble_user_start", "call -4 <ble_start : 2000048 >")
        assert reachable_starts(bad + fn("ble_start", "rts")) == [f"bt_page_on (from {hook})"], \
            f"the page's trigger called from {hook} not caught"
    # AUTOSTART (amendment 2026-10-12): bt_autostart may reference bt_page_on, and only ui_input (the main loop's UI,
    # 5 s after power-on) may call bt_autostart; from the boot, the service hook or an ISR it is a start at boot
    chain = fn("bt_autostart", "call -4 <bt_page_on : 2000040 >") + \
        fn("bt_page_on", "call -4 <ble_user_start : 2000048 >") + fn("ble_user_start", "call -4 <ble_start : 2000050 >") + \
        fn("ble_start", "rts")
    auto = boot + fn("ed_service", "call -4 <ui_input : 2000030 >") + fn("ui_input", "call -4 <bt_autostart : 2000038 >")
    assert reachable_starts(auto + chain) == [], "AUTOSTART from ui_input flagged"
    for hook in ("felucca_init", "drum_boot_init", "ed_service", "menu_input"):
        bad = boot + fn(hook, "call -4 <bt_autostart : 2000038 >")
        assert reachable_starts(bad + chain) == [f"bt_autostart (from {hook})"], f"AUTOSTART from {hook} not caught"
    isr = fn("isr_timer5", "call -4 <bt_autostart : 2000038 >")
    assert reachable_starts(boot + isr + chain) == ["bt_autostart (from isr_timer5)"], "AUTOSTART from an ISR not caught"
    lng = boot + fn("felucca_init", "r0 = 33554496 <bt_autostart : 2000038 >", "call r0")
    assert reachable_starts(lng + chain) == ["bt_autostart (from felucca_init)"], "AUTOSTART's address load not caught"
    # bt_autostart is no trigger: it may not reference ble_user_start itself; nor may any other new function call
    # the page's trigger
    direct = boot + fn("ed_service", "call -4 <ui_input : 2000030 >") + fn("ui_input", "call -4 <bt_autostart : 2000038 >") + \
        fn("bt_autostart", "call -4 <ble_user_start : 2000048 >") + fn("ble_user_start", "call -4 <ble_start : 2000050 >") + \
        fn("ble_start", "rts")
    assert reachable_starts(direct) == ["ble_user_start (from bt_autostart)"], "AUTOSTART calling ble_user_start not caught"
    other = auto + chain + fn("ui_draw", "call -4 <bt_page_on : 2000040 >")
    assert reachable_starts(other) == ["bt_page_on (from ui_draw)"], "another caller of the page's trigger not caught"
    lng = boot + fn("ed_service", "r0 = 33554496 <ble_start : 2000040 >", "call r0") + fn("ble_start", "rts")
    assert reachable_starts(lng) == ["ble_start"], "a long call (address load) not caught"
    isr = fn("isr_timer5", "call -4 <fm1_timer5_irq : 2000050 >") + fn("fm1_timer5_irq",
                                                                     "goto 4 <btstack_init+0x4 : 2000064 >")
    assert reachable_starts(isr + fn("btstack_init", "rts")) == ["btstack_init"], "an ISR path not caught"
    data = boot + fn("ed_service", "r1 = 29411328 <ble_heap : 1c0c800 >") + fn("ble_start", "rts")
    assert reachable_starts(data) == [], "a data symbol followed as a call"
    # a jump table's local labels (.GJTIS…, .GJTIE…) are inside the function: its calls after them count
    jt = boot + fn("ed_service", "call -4 <ble_console : 2000030 >") + fn("ble_console", "goto 8 <.GJTIE1_0_0_+0x8 : "
                                                                          "2000048 >") + \
        fn(".GJTIS1_0_0_", "call -4 <ble_start : 2000060 >") + fn("ble_start", "rts")
    assert reachable_starts(jt) == ["ble_start"], "a call after a jump table's label not caught"
    print("check_ble_boot: selftest ok")


def main(argv):
    if argv[1:] == ["--selftest"]:
        selftest()
        return 0
    bad = reachable_starts(open(argv[1]).read())
    if bad:
        print("gate 3: BT start-up reachable at boot:", ", ".join(bad))
        return 1
    print("gate 3: no BT start-up reachable at boot (only through", ", ".join(sorted(ALLOWED)) + ")")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
