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

$CC -o "$OUT/storage_test" tests/storage_test.c
run "flash storage (A/B, torn writes)" "$OUT/storage_test"
$CC -o "$OUT/midi_uart_test" tests/midi_uart_test.c
run "TRS MIDI parser" "$OUT/midi_uart_test"
$CC -o "$OUT/usb_midi_test" tests/usb_midi_test.c
run "USB-MIDI driver (usb_app.c): back-pressure, packet checks, SysEx, realtime, overflow" "$OUT/usb_midi_test"
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
run "update loader = the pinned one (tools/frozen_base.txt)" python3 tools/check_loader.py build
run "loader pin self-test (a changed loader is caught)" python3 tools/check_loader.py --selftest build
run "storage erase: IRQs off before the audio is silenced (st_save)" python3 tools/check_erase_order.py build/felucca.dis
run "erase-order self-test (the old order is caught)" python3 tools/check_erase_order.py --selftest
run "version: the app and the installer carry VERSION.txt (docs/VERSIONING.md)" python3 tools/version.py check build
run "version tool self-test (bump, the build check)" python3 tools/version.py --selftest
run "TRS MIDI input on in the built app (FELUCCA_UART, console lines)" python3 tools/check_trs.py build
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
if command -v node >/dev/null 2>&1; then
    run "web pages: editor protocol, samples, packages, update protocol" node web/test_web.mjs
else
    echo "== skip web tests (no node)"
fi
[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED$UPSKIP" || { echo "HOST TESTS FAILED"; exit 1; }
