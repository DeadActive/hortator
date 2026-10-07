#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
# Fetch DaisySP (Electrosmith, MIT) for the PHYS reference test only (tests/phys_ref.cpp): never built into the
# firmware, never committed. Pinned commit, into build/drum_ref/DaisySP.
#   exit 0: there; exit 1: cannot be fetched (offline)
set -e
cd "$(dirname "$0")/.."
DAISY=2c72eaf9eac5fc0dca1919d65d606da907832618
D=build/drum_ref/DaisySP
[ "${DRUM_REF_OFFLINE:-0}" = 1 ] && exit 1
[ -f "$D/.ok-$DAISY" ] && exit 0
rm -rf "$D"
mkdir -p build/drum_ref
git clone -q --filter=blob:none --no-checkout https://github.com/electro-smith/DaisySP.git "$D" || exit 1
git -C "$D" checkout -q "$DAISY" || exit 1
touch "$D/.ok-$DAISY"
