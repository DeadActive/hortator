#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
# Builds upstream Felucca 1e838e1 (the origin of the frozen code) in build/upstream (a detached git
# worktree, ignored) with the same toolchain, for tools/compare_upstream.py and tools/stack_depth.py.
# Re-running is free when it is already built.
set -e
cd "$(dirname "$0")/.."
UP=build/upstream
REV=$(git rev-parse 1e838e1)
if [ -f "$UP/build/felucca.dis" ] && [ "$(git -C "$UP" rev-parse HEAD 2>/dev/null)" = "$REV" ]; then
    echo "upstream build: up to date ($UP)"
    exit 0
fi
git worktree remove --force "$UP" 2>/dev/null || rm -rf "$UP"
git worktree prune
git worktree add --detach "$UP" "$REV" >/dev/null
for i in 1 2; do                                     # Docker's first start after a pause can fail once
    (cd "$UP" && PYTHON="$PWD/../../.venv/bin/python" ./build.sh > build.log 2>&1) && break
done
[ -f "$UP/build/felucca.dis" ] || { tail -20 "$UP/build.log"; exit 1; }
echo "upstream build: $UP/build"
