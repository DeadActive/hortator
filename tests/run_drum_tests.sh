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
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -DDM_QCHECK -Ibuild/gen -Ifirmware/src -o "$OUT/drum_test_q" tests/drum_test.c -lm
"$OUT/drum_test_q" > "$OUT/drum_test_q.txt" || { grep FAIL "$OUT/drum_test_q.txt"; exit 1; }
echo "drum_test with Q24 overflow checks: all passed"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/drumsim" tests/drumsim.c -lm
"$OUT/drumsim" build/drum_renders >/dev/null && echo "renders: build/drum_renders"
rm -rf build/ui_shots
mkdir -p build/ui_shots/engines build/ui_shots/seq
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/ui_test" tests/ui_test.c -lm
"$OUT/ui_test"
"$PY" -c "import glob,PIL.Image as I; [I.open(p).resize((480,480),I.NEAREST).save(p[:-4]+'.png') for p in glob.glob('build/ui_shots/**/*.ppm', recursive=True)]" && rm -f build/ui_shots/*.ppm build/ui_shots/engines/*.ppm build/ui_shots/seq/*.ppm && echo "screens: build/ui_shots"
REF=build/drum_ref
FID=""
if sh tests/fetch_ref.sh >/dev/null 2>&1; then
    if [ ! -f "$REF/metrics.bin" ] || [ tests/drum_ref.cc -nt "$REF/metrics.bin" ] ||
       [ tests/drum_ref_grid.h -nt "$REF/metrics.bin" ] || [ tests/drum_metrics.h -nt "$REF/metrics.bin" ]; then
        E="$REF/eurorack"
        c++ -std=c++11 -O2 -DTEST -w -I"$E" -Itests -o "$REF/drum_ref" tests/drum_ref.cc "$E/plaits/resources.cc" \
            "$E/stmlib/dsp/units.cc" "$E/stmlib/utils/random.cc"
        "$REF/drum_ref" selftest > "$REF/selftest.txt" || { tail -5 "$REF/selftest.txt"; exit 1; }
        tail -1 "$REF/selftest.txt"
        "$REF/drum_ref" grid "$REF/metrics.bin"
        mkdir -p build/drum_renders
        "$REF/drum_ref" wav build/drum_renders
    fi
    cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -Itests \
        -o "$OUT/drum_fidelity" tests/drum_fidelity.c -lm
    "$OUT/drum_fidelity" "$REF/metrics.bin"
else
    echo "fidelity: SKIPPED (offline: tests/fetch_ref.sh could not fetch the reference)"
    FID=" (fidelity SKIPPED: offline)"
fi
sh tests/guard_test.sh
python3 tools/check_untouched.py
echo "ALL DRUM HOST TESTS PASSED$FID"
