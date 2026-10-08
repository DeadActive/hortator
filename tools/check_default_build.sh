#!/bin/sh
# Gate 5 (spec 2026-10-08 BLE, amendment 6): the default build (no FELUCCA_BLE) of this tree is byte-identical to
# BASE's (default drum-v0.14.0), both built now (felucca.bin embeds the build date: ABOUT).
#   tools/check_default_build.sh [BASE]
set -e
cd "$(dirname "$0")/.."
BASE=${1:-drum-v0.14.0}
bld() {                                         # a build, retried (up to 5) on Docker's "exec format error"
    for i in 1 2 3 4 5; do
        if out=$("$@" 2>&1); then return 0; fi
        echo "$out" | grep -q "exec format error" || { echo "$out" | tail -20; return 1; }
    done
    echo "$out" | tail -20; return 1
}
W=build/gate5/base
git worktree remove --force "$W" 2>/dev/null || rm -rf "$W"
git worktree prune
mkdir -p build/gate5
git worktree add -q --detach "$W" "$BASE"
trap 'git worktree remove --force "$W" 2>/dev/null; true' EXIT
(cd "$W" && ln -sf "$OLDPWD/.venv" .venv 2>/dev/null; bld ./build.sh)
bld env -u FELUCCA_BLE -u FELUCCA_CORE_REF ./build.sh
if cmp -s "$W/build/felucca.bin" build/felucca.bin; then
    echo "gate 5: the default build is byte-identical to $BASE ($(wc -c < build/felucca.bin) B)"
else
    echo "gate 5: the default build differs from $BASE"; exit 1
fi
