#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
# The web simulator (docs/SIMULATOR.md): generated headers, the headless host test, the wasm (emscripten in
# Docker), the page, the node glue test. Everything is written under build/sim/.
#   tools/build_sim.sh                  the firmware in this checkout
#   tools/build_sim.sh --ref main       the firmware of a commit / branch (exported into build/sim/src, with this
#                                       checkout's simulator files laid over it; no branch is touched)
#   tools/build_sim.sh --host-only      generated headers + host test only (no Docker, no node); combines with --ref
set -euo pipefail
cd "$(dirname "$0")/.."
PY=${PYTHON:-python3}
export PYTHONDONTWRITEBYTECODE=1                      # no __pycache__ next to tools/build.py: output stays in build/sim
OUT=build/sim
GEN=$OUT/gen
HOST_ONLY=0 REF=
while [ $# -gt 0 ]; do
    case "$1" in
        --host-only) HOST_ONLY=1 ;;
        --ref) REF=${2:?--ref needs a commit or branch}; shift ;;
        *) echo "build_sim: unknown option $1" >&2; exit 2 ;;
    esac
    shift
done
mkdir -p "$GEN" "$OUT/host"

# 0. the source root: this checkout, or REF's tree with the simulator's files (tests/sim_*, web/sim) laid over it
ROOT=.
if [ -n "$REF" ]; then
    ROOT=$OUT/src
    rm -rf "$ROOT"
    mkdir -p "$ROOT"
    git archive "$REF" | tar -x -C "$ROOT"
    cp tests/sim_* "$ROOT/tests/"
    mkdir -p "$ROOT/web/sim" "$ROOT/tools" "$ROOT/docs"
    cp web/sim/* "$ROOT/web/sim/"
    cp tools/build_sim.sh "$ROOT/tools/"
    cp docs/SIMULATOR.md "$ROOT/docs/"
    echo "build_sim: firmware from $REF ($(git rev-parse --short "$REF")), simulator from $(git rev-parse --short HEAD)"
fi
VERSION=$(cat "$ROOT/VERSION.txt" 2>/dev/null || echo DEV)
DEFS=(-DFELUCCA_VERSION="\"DRUM-$VERSION\"")         # ABOUT shows the firmware's version, as tools/build.py does

# 1. generated headers (tools/build.py's list, written into build/sim/gen) and the input matrix keymap
"$PY" -c 'import sys; sys.path.insert(0, sys.argv[2] + "/tools"); import build; from pathlib import Path; build.GEN = Path(sys.argv[1]).resolve(); build.generate()' "$GEN" "$ROOT" >/dev/null
awk '/static const int8_t FM1_KEYMAP/,/^};/' "$ROOT/firmware/hal/fm1_input.h" > "$GEN/sim_keymap.h"
grep -q FM1_KEYMAP "$GEN/sim_keymap.h" || { echo "build_sim: FM1_KEYMAP not found in firmware/hal/fm1_input.h" >&2; exit 1; }

# 2. host test: the sim core in plain cc, same sources (run from the source root: it reads felucca.c)
CFLAGS=(-O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -I"$GEN" -I"$ROOT/firmware/src" -I"$ROOT/tests")
cc "${CFLAGS[@]}" "${DEFS[@]}" -o "$OUT/host/sim_test" "$ROOT/tests/sim_test.c" -lm
TEST_BIN=$PWD/$OUT/host/sim_test
(cd "$ROOT" && "$TEST_BIN")

[ "$HOST_ONLY" = 1 ] && exit 0

# 3. wasm: emscripten in Docker (pinned), the same sim core, no JS glue (web/sim/engine.js loads it)
IMG=emscripten/emsdk:6.0.11
if ! docker info >/dev/null 2>&1; then
    [ "$(uname)" = Darwin ] && open -a Docker
    for _ in $(seq 60); do docker info >/dev/null 2>&1 && break; sleep 1; done
fi
EXPORTS=_sim_init,_sim_render,_sim_frame,_sim_btn,_sim_key,_sim_enc,_sim_master,_sim_leds,_sim_key_leds,_sim_fb,_sim_audio,_sim_flash
EXPORTS=$EXPORTS,_sim_flash_reset,_sim_flash_dirty,_sim_flash_clean,_sim_playing
docker run --rm -v "$PWD":/src -w /src -u "$(id -u):$(id -g)" -e EM_CACHE=/src/$OUT/emcache "$IMG" \
    emcc -O2 -sSTANDALONE_WASM --no-entry -sINITIAL_MEMORY=8MB -sSTACK_SIZE=256KB -sEXPORTED_FUNCTIONS="$EXPORTS" \
    -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined "${DEFS[@]}" \
    -I"$GEN" -I"$ROOT/firmware/src" -I"$ROOT/tests" -o "$OUT/fm1sim.wasm" "$ROOT/tests/sim_core.c"

# 4. the page next to it, with the source it was built from (GPL-3.0: the page links source.tar.gz), then the glue
#    test on the built wasm
cp "$ROOT"/web/sim/* "$OUT/"
if [ -n "$REF" ]; then
    (cd "$OUT" && tar -czf source.tar.gz src)
else
    git archive --format=tar.gz --prefix="fm1-drum-sim-$(git rev-parse --short HEAD)/" -o "$OUT/source.tar.gz" HEAD
fi
[ -n "$REF" ] || [ -z "$(git status --porcelain -- firmware tests web tools)" ] ||
    echo "build_sim: uncommitted changes: source.tar.gz is HEAD, not this build (commit before publishing)" >&2
OUT_ABS=$PWD/$OUT
(cd "$ROOT" && node --test-reporter=dot tests/sim_glue.mjs "$OUT_ABS")
echo "build_sim: $OUT ready, firmware DRUM-$VERSION (cd $OUT && python3 -m http.server 8001)"
