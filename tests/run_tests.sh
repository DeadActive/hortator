#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Drum machine fork: 2026 DEADACTIVE
# Host tests of the drum firmware (no hardware). Run from anywhere after a packaged build:
#   DRUM_PACKAGE=1 ./build.sh && tests/run_tests.sh
# The package is only for the update-protocol tests; it is never installed (M1).
set -e
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
cd "$(dirname "$0")/.."
OUT=build/host
PKG=build/felucca-UNTESTED.fwsc
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"
fail=0
UPSKIP=""
run() { echo "== $1"; shift; "$@" || fail=1; }

[ -f "$PKG" ] || { echo "run DRUM_PACKAGE=1 ./build.sh first"; exit 1; }
if grep -q "^ble_service:" build/felucca.dis 2>/dev/null; then
    echo "build/ holds a BLE build: H2 / H3, the budgets and the package tests are for the default build."
    echo "Run DRUM_PACKAGE=1 ./build.sh first."
    exit 1
fi

$CC -o "$OUT/storage_test" tests/storage_test.c
run "flash storage (A/B, torn writes)" "$OUT/storage_test"
$CC -o "$OUT/midi_uart_test" tests/midi_uart_test.c
run "TRS MIDI parser" "$OUT/midi_uart_test"
$CC -o "$OUT/usb_midi_test" tests/usb_midi_test.c
run "USB-MIDI driver (usb_app.c): back-pressure, packet checks, SysEx, realtime, overflow" "$OUT/usb_midi_test"
HALF=$(sed -n 's/^#define HALF_FRAMES \([0-9]*\).*/\1/p' firmware/src/core.h)
$CC -DT_CDC=1 -DHALF_FRAMES=$HALF -o "$OUT/uac_test" tests/uac_test.c
run "USB audio input: descriptors (with CDC), ring and packets" "$OUT/uac_test"
$CC -DT_CDC=0 -DHALF_FRAMES=$HALF -o "$OUT/uac_test_nocdc" tests/uac_test.c
run "USB audio input: descriptors (without CDC), ring and packets" "$OUT/uac_test_nocdc"
$CC -o "$OUT/input_test" tests/input_test.c
run "keys and buttons: fast press, long release, bouncy contacts, glitches; encoders (#23)" "$OUT/input_test"
$CC -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry" "$OUT/ota_test" "$PKG"
# the "other app": the first half of this image (the rest erased), so the install must rewrite app sectors
head -c $(( $(wc -c < build/felucca.bin) / 2 )) build/felucca.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
$CC -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader: other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" "$PKG"
run "drum suite (models, mix, sequencer, UI, guards)" sh tests/run_drum_tests.sh
run "target cost of the render loops" python3 tests/target_budget.py build/felucca.dis tests/target_budget.txt
run "Mac recovery tool against a simulated FM-1 in boot mode (docs/RECOVERY.md)" python3 tests/fm1_uboot_test.py
run "update loader = the pinned one (tools/frozen_base.txt)" python3 tools/check_loader.py build
run "loader pin self-test (a changed loader is caught)" python3 tools/check_loader.py --selftest build
run "storage erase: IRQs off before the audio is silenced (st_save)" python3 tools/check_erase_order.py build/felucca.dis
run "erase-order self-test (the old order is caught)" python3 tools/check_erase_order.py --selftest
run "version: the app and the installer carry VERSION.txt (docs/VERSIONING.md)" python3 tools/version.py check build
run "version tool self-test (bump, the build check)" python3 tools/version.py --selftest
run "TRS MIDI input on in the built app (FELUCCA_UART, console lines)" python3 tools/check_trs.py build
run "USB audio input on in the built app (FELUCCA_UAC, console lines)" python3 tools/check_uac.py build
if [ -f build/upstream/build/felucca.dis ]; then
    run "frozen code in the binary = upstream's (H2)" python3 tools/compare_upstream.py build build/upstream/build
    run "H2 self-test (changed effects are caught)" python3 tools/compare_upstream.py --selftest build build/upstream/build
    run "worst-case stack (H3)" python3 tools/stack_depth.py build --compare build/upstream/build
else
    echo "== H2/H3 SKIPPED: no upstream build (sh tools/upstream_build.sh)"
    UPSKIP=" (H2/H3 SKIPPED: no upstream build)"
fi
run "H3 self-test (an unknown stack form is caught)" python3 tools/stack_depth.py --selftest
run "installer CLI (fm1_install.py) against a simulated FM-1" python3 tests/install_test.py
run "BENCH report (fm1_bench.py): the FM-1's lines against the host's" python3 tools/fm1_bench.py --selftest
for t in ble_midi ble_scan ble_central ble_vm; do $CC -o "$OUT/${t}_test" "tests/ble/${t}_test.c"; done
$CC -Wno-deprecated-declarations -Ifirmware/src/ble -o "$OUT/ble_os_test" tests/ble/ble_os_test.c
run "BLE-MIDI parser" "$OUT/ble_midi_test"
run "BLE scan table" "$OUT/ble_scan_test"
run "BLE central state machine, UUID match, monitor text" "$OUT/ble_central_test"
run "BLE stored RF calibration (config store)" "$OUT/ble_vm_test"
run "BLE OS layer (host back end)" "$OUT/ble_os_test"
run "BLE fault path: records and reboots, never waits on interrupts" python3 tests/ble/check_ble_fatal.py
run "gate 2 tool self-test (update path: machine code, sources)" python3 tools/check_update_path.py --selftest
run "gate 2a: the update path's sources = the last release's" python3 tools/check_update_path.py --sources
run "gate 3 tool self-test (BT start-up reachable at boot)" python3 tools/check_ble_boot.py --selftest
if command -v node >/dev/null 2>&1; then
    run "web pages: editor protocol, samples, packages, update protocol" node web/test_web.mjs
else
    echo "== skip web tests (no node)"
fi
# last: it rebuilds build/ (the default build, the same bytes as the packaged one when it passes)
run "gate 5: the default build byte-identical without the Bluetooth files" sh tools/check_default_build.sh
[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED$UPSKIP" || { echo "HOST TESTS FAILED"; exit 1; }
