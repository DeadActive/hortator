#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
# Builds the frozen baseline (tools/frozen_base.txt: upstream 1e838e1 or a fork baseline tag) in build/upstream
# (a detached git worktree, ignored) with the same toolchain, for tools/compare_upstream.py and
# tools/stack_depth.py.
# Re-running is free when it is already built.
set -e
cd "$(dirname "$0")/.."
UP=build/upstream
BASE=$(awk '$1 == "FROZEN_BASE" { print $2 }' tools/frozen_base.txt)
PIN=$(awk '$1 == "FROZEN_BASE" { print $3 }' tools/frozen_base.txt)
REV=$(git rev-parse -q --verify "$BASE^{commit}" || true)
if [ -z "$PIN" ] || [ "$REV" != "$PIN" ]; then
    echo "baseline build: tag $BASE is ${REV:-missing}, pinned ${PIN:-nothing} (tools/frozen_base.txt)" >&2
    exit 1
fi
if [ -f "$UP/build/felucca.dis" ] && [ "$(git -C "$UP" rev-parse HEAD 2>/dev/null)" = "$REV" ]; then
    echo "baseline build ($BASE): up to date ($UP)"
    exit 0
fi
git worktree remove --force "$UP" 2>/dev/null || rm -rf "$UP"
git worktree prune
git worktree add --detach "$UP" "$REV" >/dev/null
for i in 1 2; do                                     # Docker's first start after a pause can fail once
    (cd "$UP" && PYTHON="$PWD/../../.venv/bin/python" ./build.sh > build.log 2>&1) && break
done
[ -f "$UP/build/felucca.dis" ] || { tail -20 "$UP/build.log"; exit 1; }
echo "baseline build ($BASE): $UP/build"
