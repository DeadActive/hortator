#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
# Fetch the references: Mutable Instruments' drum code (MIT) for M1-C and Grids with avrlib (GPL-3.0-or-later)
# for M2, for the host fidelity tests only:
# never built into the firmware, never committed. Pinned commits, sparse, into build/drum_ref/eurorack
# (stmlib inside it, where the sources include it from).
#   tests/fetch_ref.sh       exit 0: the reference is there; exit 1: it cannot be fetched (offline)
#   DRUM_REF_OFFLINE=1       behave as offline (tests the SKIPPED path)
set -e
cd "$(dirname "$0")/.."
EURO=08460a69a7e1f7a81c5a2abcc7189c9a6b7208d4
STM=e3bd7c9cc00e4364166f9905c0509b6ffd0535ec
AVR=276b2887e4110ca913294fcbb313163dfb28a448
D=build/drum_ref/eurorack
[ "${DRUM_REF_OFFLINE:-0}" = 1 ] && exit 1
[ -f "$D/.ok-$EURO-$STM-$AVR" ] && exit 0
rm -rf "$D"
mkdir -p build/drum_ref
git clone -q --filter=blob:none --no-checkout https://github.com/pichenettes/eurorack.git "$D" || exit 1
git -C "$D" sparse-checkout set --no-cone '/plaits/dsp/' '/plaits/resources.h' '/plaits/resources.cc' '/grids/' || exit 1
git -C "$D" checkout -q "$EURO" || exit 1
git clone -q --filter=blob:none --no-checkout https://github.com/pichenettes/stmlib.git "$D/stmlib" || exit 1
git -C "$D/stmlib" checkout -q "$STM" || exit 1
git clone -q --filter=blob:none --no-checkout https://github.com/pichenettes/avril.git "$D/avrlib" || exit 1
git -C "$D/avrlib" checkout -q "$AVR" || exit 1
touch "$D/.ok-$EURO-$STM-$AVR"
