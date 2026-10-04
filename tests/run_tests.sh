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
$CC -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry" "$OUT/ota_test" "$PKG"
# the "other app": the first half of this image (the rest erased), so the install must rewrite app sectors
head -c $(( $(wc -c < build/felucca.bin) / 2 )) build/felucca.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
$CC -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader: other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" "$PKG"
run "drum suite (models, mix, sequencer, UI, guards)" sh tests/run_drum_tests.sh
run "target cost of the render loops" python3 tests/target_budget.py build/felucca.dis tests/target_budget.txt
if [ -f build/upstream/build/felucca.dis ]; then
    run "frozen code in the binary = upstream's (H2)" python3 tools/compare_upstream.py build build/upstream/build
    run "H2 self-test (changed effects are caught)" python3 tools/compare_upstream.py --selftest build build/upstream/build
else
    echo "== H2/H3 SKIPPED: no upstream build (sh tools/upstream_build.sh)"
    UPSKIP=" (H2/H3 SKIPPED: no upstream build)"
fi
run "installer CLI (fm1_install.py) against a simulated FM-1" python3 tests/install_test.py
if command -v node >/dev/null 2>&1; then
    run "web pages: editor protocol, samples, packages, update protocol" node web/test_web.mjs
else
    echo "== skip web tests (no node)"
fi
[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED$UPSKIP" || { echo "HOST TESTS FAILED"; exit 1; }
