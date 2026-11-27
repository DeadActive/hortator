#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
# Builds the exact historical baseline (tools/frozen_base.txt) in build/upstream
# (a detached git worktree, ignored) with the same toolchain, for tools/compare_upstream.py and
# tools/stack_depth.py.
# Re-running is free when the recorded exact build still matches. Use --verify to only check it.
set -e
cd "$(dirname "$0")/.."
UP=build/upstream
PY=${PYTHON:-python3}
REV=$("$PY" tools/check_untouched.py --historical-ref)
ARTIFACTS="felucca.dis felucca.elf text.bin data.bin ramtext.bin"
STAMP="$UP/build/.grind-frozen-artifacts"
fingerprint() {
    "$PY" -c 'from hashlib import sha256
from pathlib import Path
import sys

print(sys.argv[1])
for name in sys.argv[3:]:
    path = Path(sys.argv[2]) / name
    if path.is_symlink() or not path.is_file() or path.stat().st_size == 0:
        raise SystemExit(f"missing or empty historical build artifact: {path}")
    print(name, sha256(path.read_bytes()).hexdigest())' "$REV" "$UP/build" $ARTIFACTS
}
valid_build() {
    [ -e "$UP/.git" ] && [ -s "$STAMP" ] &&
    [ "$(git --no-replace-objects -C "$UP" rev-parse HEAD)" = "$REV" ] &&
    git --no-replace-objects -C "$UP" diff --quiet "$REV" -- &&
    actual_stamp=$(fingerprint) && [ "$(cat "$STAMP")" = "$actual_stamp" ]
}
if [ "${1:-}" = "--verify" ]; then
    if valid_build; then
        exit 0
    fi
    echo "FAIL: historical: H2/H3 need an unchanged build of $REV (sh tools/upstream_build.sh)" >&2
    exit 1
fi
if valid_build; then
    echo "baseline build ($REV): up to date ($UP)"
    exit 0
fi
git worktree remove --force "$UP" 2>/dev/null || rm -rf "$UP"
git worktree prune
git --no-replace-objects worktree add --detach "$UP" "$REV" >/dev/null
built=0
for i in 1 2; do                                     # Docker's first start after a pause can fail once
    for artifact in $ARTIFACTS; do
        rm -f "$UP/build/$artifact"
    done
    if (cd "$UP" && PYTHON="$PWD/../../.venv/bin/python" ./build.sh > build.log 2>&1) &&
       actual_stamp=$(fingerprint); then
        printf '%s\n' "$actual_stamp" > "$STAMP"
        built=1
        break
    fi
done
[ "$built" -eq 1 ] || { tail -20 "$UP/build.log"; exit 1; }
echo "baseline build ($REV): $UP/build"
