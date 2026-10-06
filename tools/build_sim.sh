#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
# The web simulator (docs/SIMULATOR.md): generated headers, the headless host test, the wasm (emscripten in
# Docker), the page, the node glue test. Everything is written under build/sim/.
#   tools/build_sim.sh              everything
#   tools/build_sim.sh --host-only  generated headers + host test only (no Docker, no node)
set -euo pipefail
cd "$(dirname "$0")/.."
PY=${PYTHON:-python3}
OUT=build/sim
GEN=$OUT/gen
mkdir -p "$GEN" "$OUT/host"

# 1. generated headers (tools/build.py's list, written into build/sim/gen) and the input matrix keymap
"$PY" -c 'import sys; sys.path.insert(0, "tools"); import build; from pathlib import Path; build.GEN = Path(sys.argv[1]).resolve(); build.generate()' "$GEN" >/dev/null
awk '/static const int8_t FM1_KEYMAP/,/^};/' firmware/hal/fm1_input.h > "$GEN/sim_keymap.h"
grep -q FM1_KEYMAP "$GEN/sim_keymap.h" || { echo "build_sim: FM1_KEYMAP not found in firmware/hal/fm1_input.h" >&2; exit 1; }

# 2. host test: the sim core in plain cc, same sources
CFLAGS=(-O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -I"$GEN" -Ifirmware/src -Itests)
cc "${CFLAGS[@]}" -o "$OUT/host/sim_test" tests/sim_test.c -lm
"$OUT/host/sim_test"

[ "${1:-}" = "--host-only" ] && exit 0

# 3. wasm: emscripten in Docker (pinned), the same sim core, no JS glue (web/sim/engine.js loads it)
IMG=emscripten/emsdk:6.0.11
if ! docker info >/dev/null 2>&1; then
    [ "$(uname)" = Darwin ] && open -a Docker
    for _ in $(seq 60); do docker info >/dev/null 2>&1 && break; sleep 1; done
fi
EXPORTS=_sim_init,_sim_render,_sim_frame,_sim_btn,_sim_key,_sim_enc,_sim_master,_sim_leds,_sim_fb,_sim_audio,_sim_flash
EXPORTS=$EXPORTS,_sim_flash_reset,_sim_flash_dirty,_sim_flash_clean,_sim_playing
docker run --rm -v "$PWD":/src -w /src -u "$(id -u):$(id -g)" -e EM_CACHE=/src/$OUT/emcache "$IMG" \
    emcc -O2 -sSTANDALONE_WASM --no-entry -sINITIAL_MEMORY=8MB -sSTACK_SIZE=256KB -sEXPORTED_FUNCTIONS="$EXPORTS" \
    -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined \
    -I"$GEN" -Ifirmware/src -Itests -o "$OUT/fm1sim.wasm" tests/sim_core.c

# 4. the page next to it, then the glue test on the built wasm
cp web/sim/* "$OUT/"
node --test-reporter=dot tests/sim_glue.mjs "$OUT"
echo "build_sim: $OUT ready (cd $OUT && python3 -m http.server 8001)"
