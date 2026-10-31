#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
# The web editor's device data (tests/editor_ids.c) -> web/editor/device_data.json, for its mock device.
#   tools/gen_editor_data.sh            write it
#   tools/gen_editor_data.sh --check    fail when the committed file differs (the firmware's tables moved)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PY=${PYTHON:-python3}
[ -x "$ROOT/.venv/bin/python" ] && PY=${PYTHON:-$ROOT/.venv/bin/python}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
"$PY" -c 'import sys; sys.path.insert(0, sys.argv[2] + "/tools"); import build; from pathlib import Path; build.GEN = Path(sys.argv[1]).resolve(); build.generate()' "$T" "$ROOT" >/dev/null
awk '/static const int8_t FM1_KEYMAP/,/^};/' "$ROOT/firmware/hal/fm1_input.h" > "$T/sim_keymap.h"
cc -O0 -w -I"$T" -I"$ROOT/firmware/src" -I"$ROOT/tests" -o "$T/editor_ids" "$ROOT/tests/editor_ids.c" -lm
(cd "$ROOT" && "$T/editor_ids") > "$T/device_data.json"
if [ "$1" = "--check" ]; then
    cmp -s "$T/device_data.json" "$ROOT/web/editor/device_data.json" ||
        { echo "web/editor/device_data.json is stale: run tools/gen_editor_data.sh" >&2; exit 1; }
    echo "web/editor/device_data.json matches the firmware"
else
    mkdir -p "$ROOT/web/editor"
    cp "$T/device_data.json" "$ROOT/web/editor/device_data.json"
    echo "wrote web/editor/device_data.json"
fi
