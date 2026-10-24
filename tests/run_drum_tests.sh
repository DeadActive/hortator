#!/bin/sh
# Host suite of the drum fork (no hardware, no toolchain): generated headers, drum_test, guards.
set -e
cd "$(dirname "$0")/.."
PY="${PYTHON:-python3}"
"$PY" tools/build.py --gen-only >/dev/null
"$PY" tools/gen_presets.py --check                 # firmware/src/preset_bank.h = tools/presets.json
OUT=build/host
mkdir -p "$OUT"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/drum_test" tests/drum_test.c -lm
"$OUT/drum_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/sound_pack_test" tests/sound_pack_test.c -lm
"$OUT/sound_pack_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/usb_audio_test" tests/usb_audio_test.c -lm
"$OUT/usb_audio_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/midi_clock_test" tests/midi_clock_test.c -lm
"$OUT/midi_clock_test"
cc -O1 -g -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/metro_test" tests/metro_test.c -lm   # (UBSan: the click in the mix)
"$OUT/metro_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ifirmware/src -o "$OUT/led_dim_test" tests/led_dim_test.c   # (the key backlight on the real scan)
"$OUT/led_dim_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ifirmware/src -o "$OUT/enc_test" tests/enc_test.c   # (the knobs on the real scan, #126)
"$OUT/enc_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/perform_test" tests/perform_test.c -lm
"$OUT/perform_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/motion_test" tests/motion_test.c -lm
"$OUT/motion_test"
cc -O1 -g -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all -Wall -Wno-unused-function \
    -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/lock_test" tests/lock_test.c -lm   # (locks: ASan + UBSan)
"$OUT/lock_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/song_test" tests/song_test.c -lm
"$OUT/song_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/phys_test" tests/phys_test.c -lm
"$OUT/phys_test"
cc -O1 -g -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/filter_test" tests/filter_test.c -lm   # (UBSan: an overflow in the mix aborts: the master limiter hides it otherwise)
"$OUT/filter_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/fm_test" tests/fm_test.c -lm
"$OUT/fm_test"
cc -O1 -g -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/ktrx_test" tests/ktrx_test.c -lm   # (UBSan: Q31 envelopes, 64-bit pitch)
"$OUT/ktrx_test"
cc -O1 -g -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/sdtrx_test" tests/sdtrx_test.c -lm   # (UBSan: Q31 envelopes, the tanh's 64-bit products)
"$OUT/sdtrx_test"
cc -O1 -g -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/b2trx_test" tests/b2trx_test.c -lm   # (UBSan: Q31 envelopes, the shapers)
"$OUT/b2trx_test"
cc -O1 -g -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/trxperc_test" tests/trxperc_test.c -lm   # (UBSan: Q31 envelopes, the warp, 64-bit pitch)
"$OUT/trxperc_test"
cc -O1 -g -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/trxn_test" tests/trxn_test.c -lm   # (UBSan: Q30 filters, Q27 / Q30 envelopes)
"$OUT/trxn_test"
cc -O1 -g -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/trxmetal_test" tests/trxmetal_test.c -lm   # (UBSan: Q30 filters, 64-bit envelopes)
"$OUT/trxmetal_test"
cc -O1 -g -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/bdefm_test" tests/bdefm_test.c -lm   # (UBSan: Q31 envelopes, 64-bit FM increments, Q30 high-passes)
"$OUT/bdefm_test"
cc -O1 -g -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/efmn_test" tests/efmn_test.c -lm   # (UBSan: Q31 envelopes, 64-bit noise FM)
"$OUT/efmn_test"
cc -O1 -g -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/efmmetal_test" tests/efmmetal_test.c -lm   # (UBSan: FM phases mod 2^32, 64-bit deviation)
"$OUT/efmmetal_test"
cc -O1 -g -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/acid_test" tests/acid_test.c -lm   # (UBSan: Review Focus 3)
"$OUT/acid_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/bench_host" tests/bench_host.c -lm
"$OUT/bench_host"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -DDM_QCHECK -Ibuild/gen -Ifirmware/src -o "$OUT/drum_test_q" tests/drum_test.c -lm
"$OUT/drum_test_q" > "$OUT/drum_test_q.txt" || { grep FAIL "$OUT/drum_test_q.txt"; exit 1; }
echo "drum_test with Q24 overflow checks: all passed"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/drumsim" tests/drumsim.c -lm
"$OUT/drumsim" build/drum_renders >/dev/null && echo "renders: build/drum_renders"
rm -rf build/ui_shots
mkdir -p build/ui_shots/engines build/ui_shots/seq build/ui_shots/grids build/ui_shots/comp build/ui_shots/lfo build/ui_shots/reson build/ui_shots/perform build/ui_shots/motion build/ui_shots/song build/ui_shots/name build/ui_shots/phys build/ui_shots/filter build/ui_shots/fm build/ui_shots/ktrx build/ui_shots/trxdrums build/ui_shots/acid build/ui_shots/bt build/ui_shots/fixes build/ui_shots/learn build/ui_shots/presets build/ui_shots/voicegraph build/ui_shots/locks build/ui_shots/themes/md-red build/ui_shots/themes/red-on-black build/ui_shots/themes/white-on-blue build/ui_shots/themes/black-on-green build/ui_shots/themes/blue-on-white build/ui_shots/themes/green-on-black
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/ui_test" tests/ui_test.c -lm
"$OUT/ui_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/accel_test" tests/accel_test.c -lm   # (the knob acceleration vs upstream's)
"$OUT/accel_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/gfx_test" tests/gfx_test.c -lm
"$OUT/gfx_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -DFELUCCA_BLE=1 -Ibuild/gen -Ifirmware/src -o "$OUT/bt_core_test" tests/bt_core_test.c -lm
"$OUT/bt_core_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -DFELUCCA_BLE=1 -Ibuild/gen -Ifirmware/src -o "$OUT/smc_test" tests/smc_test.c -lm
"$OUT/smc_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -DFELUCCA_BLE=1 -Ibuild/gen -Ifirmware/src -o "$OUT/bt_page_test" tests/bt_page_test.c -lm
"$OUT/bt_page_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -DFELUCCA_BLE=1 -Ibuild/gen -Ifirmware/src -o "$OUT/redraw_test" tests/redraw_test.c -lm   # (the LCD traffic per frame)
"$OUT/redraw_test"
cc -O1 -g -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all -Wall -Wno-unused-function \
    -Wno-int-to-pointer-cast -Wno-macro-redefined -DFELUCCA_BLE=1 -Ibuild/gen -Ifirmware/src -o "$OUT/learn_test" tests/learn_test.c -lm   # (MIDI LEARN: ASan + UBSan)
"$OUT/learn_test"
cc -O1 -g -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all -Wall -Wno-unused-function \
    -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/preset_test" tests/preset_test.c -lm   # (presets: ASan + UBSan)
"$OUT/preset_test"
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/audio_isr_test" tests/audio_isr_test.c -lm
"$OUT/audio_isr_test"
cc -O1 -g -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all -Wall -Wno-unused-function \
    -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/boot_test" tests/boot_test.c -lm
"$OUT/boot_test" > "$OUT/boot_test.txt" 2>&1 || { tail -30 "$OUT/boot_test.txt"; exit 1; }
tail -1 "$OUT/boot_test.txt"
cc -O1 -g -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast \
    -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/comp_overflow" tests/comp_overflow.c -lm
"$OUT/comp_overflow"
"$PY" -c "import glob,PIL.Image as I; [I.open(p).resize((480,480),I.NEAREST).save(p[:-4]+'.png') for p in glob.glob('build/ui_shots/**/*.ppm', recursive=True)]" && rm -f build/ui_shots/*.ppm build/ui_shots/engines/*.ppm build/ui_shots/seq/*.ppm build/ui_shots/grids/*.ppm build/ui_shots/comp/*.ppm build/ui_shots/lfo/*.ppm build/ui_shots/reson/*.ppm build/ui_shots/perform/*.ppm build/ui_shots/motion/*.ppm build/ui_shots/song/*.ppm build/ui_shots/name/*.ppm build/ui_shots/phys/*.ppm build/ui_shots/filter/*.ppm build/ui_shots/fm/*.ppm build/ui_shots/acid/*.ppm build/ui_shots/bt/*.ppm build/ui_shots/fixes/*.ppm build/ui_shots/learn/*.ppm build/ui_shots/presets/*.ppm build/ui_shots/voicegraph/*.ppm build/ui_shots/locks/*.ppm build/ui_shots/themes/*.ppm build/ui_shots/themes/*/*.ppm && echo "screens: build/ui_shots" && "$PY" tools/theme_sheet.py
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
if sh tests/fetch_daisysp.sh >/dev/null 2>&1; then
    DS=build/drum_ref/DaisySP/Source
    cc -O2 -w -c -o "$OUT/phys_fixed.o" tests/phys_fixed.c
    c++ -O2 -std=c++14 -w -I"$DS" -I"$DS/Utility" -o "$OUT/phys_ref" tests/phys_ref.cpp \
        "$DS/PhysicalModeling/modalvoice.cpp" "$DS/PhysicalModeling/resonator.cpp" "$DS/Filters/svf.cpp" \
        "$DS/Dynamics/crossfade.cpp" "$OUT/phys_fixed.o"
    "$OUT/phys_ref" | tail -1
else
    echo "phys_ref: SKIPPED (offline: tests/fetch_daisysp.sh could not fetch DaisySP)"
    FID=" (fidelity SKIPPED: offline)"
fi
sh tests/guard_test.sh
python3 tools/check_untouched.py
echo "ALL DRUM HOST TESTS PASSED$FID"
