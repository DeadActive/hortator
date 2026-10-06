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
mkdir -p build/ui_shots/engines build/ui_shots/seq build/ui_shots/grids build/ui_shots/comp build/ui_shots/lfo build/ui_shots/reson
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/ui_test" tests/ui_test.c -lm
"$OUT/ui_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/audio_isr_test" tests/audio_isr_test.c -lm
"$OUT/audio_isr_test"
cc -O1 -g -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all -Wall -Wno-unused-function \
    -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/boot_test" tests/boot_test.c -lm
"$OUT/boot_test" > "$OUT/boot_test.txt" 2>&1 || { tail -30 "$OUT/boot_test.txt"; exit 1; }
tail -1 "$OUT/boot_test.txt"
cc -O1 -g -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast \
    -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/comp_overflow" tests/comp_overflow.c -lm
"$OUT/comp_overflow"
"$PY" -c "import glob,PIL.Image as I; [I.open(p).resize((480,480),I.NEAREST).save(p[:-4]+'.png') for p in glob.glob('build/ui_shots/**/*.ppm', recursive=True)]" && rm -f build/ui_shots/*.ppm build/ui_shots/engines/*.ppm build/ui_shots/seq/*.ppm build/ui_shots/grids/*.ppm build/ui_shots/comp/*.ppm build/ui_shots/lfo/*.ppm build/ui_shots/reson/*.ppm && echo "screens: build/ui_shots"
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
    python3 tools/gen_grids_tables.py "$REF/eurorack/grids/resources.cc" "$OUT/grids_tables.h"
    cmp -s "$OUT/grids_tables.h" firmware/src/grids_tables.h || { echo "FAIL  grids_tables.h differs from the reference (tools/gen_grids_tables.py)"; exit 1; }
    E="$REF/eurorack"
    c++ -std=c++11 -O1 -w -Itests/grids_stub -I"$E" -I. -o "$REF/grids_ref" tests/grids_ref.cc "$E/grids/pattern_generator.cc" \
        "$E/grids/resources.cc" "$E/avrlib/random.cc"
    "$REF/grids_ref" "$REF/grids_ref.bin"
    cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -Itests \
        -o "$OUT/grids_fidelity" tests/grids_fidelity.c -lm
    "$OUT/grids_fidelity" "$REF/grids_ref.bin"
    python3 tools/gen_comp_tables.py "$REF/eurorack/streams/resources.cc" "$OUT/comp_tables.h"
    cmp -s "$OUT/comp_tables.h" firmware/src/comp_tables.h || { echo "FAIL  comp_tables.h differs from the reference (tools/gen_comp_tables.py)"; exit 1; }
    python3 tools/gen_comp_tables.py "$REF/eurorack/streams/resources.cc" "$REF/comp_lp31k.h" --lp31k
    c++ -std=c++11 -O1 -w -DTEST -I"$E" -I. -o "$REF/comp_ref" tests/comp_ref.cc "$E/streams/compressor.cc" "$E/streams/resources.cc"
    "$REF/comp_ref" "$REF/comp_ref.bin"
    cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -Itests -I"$REF" \
        -o "$OUT/comp_fidelity" tests/comp_fidelity.c -lm
    "$OUT/comp_fidelity" "$REF/comp_ref.bin"
else
    echo "fidelity: SKIPPED (offline: tests/fetch_ref.sh could not fetch the reference)"
    FID=" (fidelity SKIPPED: offline)"
fi
sh tests/guard_test.sh
python3 tools/check_untouched.py
echo "ALL DRUM HOST TESTS PASSED$FID"
