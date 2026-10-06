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
