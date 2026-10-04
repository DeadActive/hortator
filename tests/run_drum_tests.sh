#!/bin/sh
# Host suite of the drum fork (no hardware, no toolchain): generated headers, drum_test, guards.
set -e
cd "$(dirname "$0")/.."
PY="${PYTHON:-python3}"
"$PY" tools/build.py --gen-only >/dev/null
OUT=build/host
mkdir -p "$OUT"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/drum_test" tests/drum_test.c -lm
"$OUT/drum_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/drumsim" tests/drumsim.c -lm
"$OUT/drumsim" build/drum_renders >/dev/null && echo "renders: build/drum_renders"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -DUI_NO_DRAW -DUI_NO_PROJECT -Ibuild/gen -Ifirmware/src -o "$OUT/ui_test" tests/ui_test.c -lm
"$OUT/ui_test"
sh tests/guard_test.sh
python3 tools/check_untouched.py
echo "ALL DRUM HOST TESTS PASSED"
