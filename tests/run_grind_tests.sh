#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu
cd "$(dirname "$0")/.."

CC=${CC:-cc}
CFLAGS="-std=c99 -O1 -g -Wall -Wextra -Werror -UNDEBUG ${GRIND_SAN_FLAGS:-}"
OUT=build/grind-host
CORE=firmware/src/grind
mkdir -p "$OUT"

run_test() {
    suite=$1
    shift
    $CC $CFLAGS -I"$CORE" "tests/grind/test_$suite.c" "$@" -o "$OUT/test_$suite"
    "$OUT/test_$suite"
}

run_rng() {
    "${PYTHON:-python3}" tools/grind/rng_reference.py > "$OUT/rng-reference.txt"
    cmp tests/grind/golden/rng-v1.txt "$OUT/rng-reference.txt"
    run_test rng "$CORE/rng.c"
}

run_pattern_test() {
    run_test "$1" "$CORE/grid.c" "$CORE/rules.c" "$CORE/core.c" "$CORE/locks.c" \
        "$CORE/rng.c" "$CORE/serialize.c"
}

run_golden() {
    "${PYTHON:-python3}" tools/grind/check_vectors.py
    run_pattern_test golden
}

case "${1:-all}" in
    grid) run_test grid "$CORE/grid.c" ;;
    rng) run_rng ;;
    codec) run_test codec "$CORE/serialize.c" "$CORE/grid.c" ;;
    rules) run_test rules "$CORE/rules.c" "$CORE/grid.c" ;;
    locks) run_test locks "$CORE/locks.c" ;;
    morph|properties) run_pattern_test "$1" ;;
    golden) run_golden ;;
    all)
        run_test grid "$CORE/grid.c"
        run_rng
        run_test codec "$CORE/serialize.c" "$CORE/grid.c"
        run_test rules "$CORE/rules.c" "$CORE/grid.c"
        run_test locks "$CORE/locks.c"
        run_pattern_test morph
        run_pattern_test properties
        run_golden
        ;;
    *) echo "unknown suite: $1" >&2; exit 2 ;;
esac
