#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
# The web simulator (docs/SIMULATOR.md): generated headers, the headless host test, the wasm (emscripten in
# Docker), the page, the node glue test. Everything is written under build/sim/.
#   tools/build_sim.sh                  the firmware in this checkout
#   tools/build_sim.sh --ref main       the firmware of a commit / branch (exported into build/sim/src, with this
#                                       checkout's simulator files laid over it; no branch is touched)
#   tools/build_sim.sh --host-only      generated headers + host test only (no Docker, no node); combines with --ref
#   tools/build_sim.sh --preview        also build/sim/preview/: the site's layout (the landing page at /, the installer
#                                       at webapp/installer/, assembled as web/make_site.py does, with no firmware
#                                       package: it shows "could not load" and cannot install)
#   tools/build_sim.sh --ref main --preview --package FILE.fwsc
#                                       the preview with a firmware package already built (tools/build.py,
#                                       DRUM_PACKAGE=1): copied in, the site assembled by make_site.py; its installer
#                                       can flash an FM-1
set -euo pipefail
cd "$(dirname "$0")/.."
PY=${PYTHON:-python3}
export PYTHONDONTWRITEBYTECODE=1                      # no __pycache__ next to tools/build.py: output stays in build/sim
OUT=build/sim
GEN=$OUT/gen
HOST_ONLY=0 REF= PREVIEW=0 PACKAGE=
while [ $# -gt 0 ]; do
    case "$1" in
        --host-only) HOST_ONLY=1 ;;
        --preview) PREVIEW=1 ;;
        --package) PACKAGE=${2:?--package needs a .fwsc file}; shift ;;
        --ref) REF=${2:?--ref needs a commit or branch}; shift ;;
        *) echo "build_sim: unknown option $1" >&2; exit 2 ;;
    esac
    shift
done
mkdir -p "$GEN" "$OUT/host"
if [ -n "$PACKAGE" ] && { [ "$PREVIEW" = 0 ] || [ ! -f "$PACKAGE" ]; }; then
    echo "build_sim: --package needs --preview and an existing .fwsc ($PACKAGE)" >&2
    exit 2
fi

# 0. the source root: this checkout, or REF's tree with the simulator's files (tests/sim_*, web/sim) laid over it
ROOT=.
if [ -n "$REF" ]; then
    ROOT=$OUT/src
    rm -rf "$ROOT"
    mkdir -p "$ROOT"
    git archive "$REF" | tar -x -C "$ROOT"
    cp tests/sim_* "$ROOT/tests/"
    cp web/index_pkg.html "$ROOT/web/"                  # the restyled installer (docs/SIMULATOR.md)
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
# the landing page's reel: the same core, a scripted performance recorded (tests/sim_record.c)
cc "${CFLAGS[@]}" "${DEFS[@]}" -o "$OUT/host/sim_record" "$ROOT/tests/sim_record.c" -lm
"$OUT/host/sim_record" "$OUT/reel.bin" "$OUT/reel.hash" "$OUT/reel.json"
gzip -9 -n -f -k "$OUT/reel.bin"

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
(cd "$ROOT" && node --test-reporter=dot tests/sim_glue.mjs "$OUT_ABS" && node --test-reporter=dot tests/sim_site.mjs)

# 5. preview: the site's layout, to look at (and, with --package, to install from)
if [ "$PREVIEW" = 1 ]; then
    P=$OUT/preview
    rm -rf "$P"
    mkdir -p "$P/webapp/installer"
    if [ -n "$PACKAGE" ]; then
        cp "$PACKAGE" "$OUT/package.fwsc"                 # a copy: the original is never touched
        LABEL=$(basename "$PACKAGE" .fwsc | sed -nE 's/^felucca-(drum-[0-9.]+)-([0-9a-f]+(-dirty)?)$/\1+\2/p')
        LABEL=${LABEL:-drum-$VERSION}
        echo "build_sim: preview with the firmware package $(basename "$PACKAGE") ($LABEL)"
        "$PY" - "$ROOT/web" "$OUT/package.fwsc" "$LABEL" "$P" <<'PY'
import sys
sys.path.insert(0, sys.argv[1])
import make_site                                     # the site exactly as web/make_site.py makes it
make_site.main(sys.argv[2], sys.argv[3], sys.argv[4])
PY
        cp "$ROOT"/web/sim/* "$OUT/fm1sim.wasm" "$OUT/source.tar.gz" "$OUT/reel.bin.gz" "$OUT/reel.json" "$P/"   # the landing page at /
    else
    cp "$ROOT"/web/sim/* "$OUT/fm1sim.wasm" "$OUT/source.tar.gz" "$OUT/reel.bin.gz" "$OUT/reel.json" "$P/"
    "$PY" - "$ROOT/web" "$P/webapp/installer/index.html" "DRUM-$VERSION" <<'PY'
import json, sys
from pathlib import Path
web, out, version = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
sys.path.insert(0, str(web))
from make_site import strip_module                   # the site's own assembly (web/make_site.py main)
html = (web / "index_pkg.html").read_text(encoding="utf-8")
lib = strip_module((web / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + strip_module((web / "fm1ota.js").read_text(encoding="utf-8"))
meta = json.dumps({"version": version + " (preview)", "product": "FM-1_900", "pkg": "../../firmware/preview-has-no-package.fwsc"})
out.write_text(html.replace("/*LIB*/", lib).replace("/*META*/", meta), encoding="utf-8")
PY
    fi
    echo "build_sim: preview $P (cd $P && python3 -m http.server 8001)"
fi
echo "build_sim: $OUT ready, firmware DRUM-$VERSION (cd $OUT && python3 -m http.server 8001)"
