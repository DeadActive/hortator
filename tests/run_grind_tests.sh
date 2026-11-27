#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu
cd "$(dirname "$0")/.."

WASM=${GRIND_WASM:-0}
if [ "$WASM" = 1 ]; then
    CC=${CC:-emcc}
    OUT=build/grind-wasm
else
    CC=${CC:-cc}
    OUT=build/grind-host
fi
CFLAGS="-std=c99 -O1 -g -Wall -Wextra -Werror -UNDEBUG ${GRIND_SAN_FLAGS:-}"
CORE=firmware/src/grind
mkdir -p "$OUT"

run_test() {
    suite=$1
    shift
    if [ "$WASM" = 1 ]; then
        case "$suite" in
            rng) set -- "$@" --embed-file tests/grind/golden/rng-v1.txt@/tests/grind/golden/rng-v1.txt ;;
            rules) set -- "$@" --embed-file tests/grind/golden/styles-v1.csv@/tests/grind/golden/styles-v1.csv ;;
            golden) set -- "$@" --embed-file tests/grind/golden/patterns-v1.bin@/tests/grind/golden/patterns-v1.bin ;;
        esac
        $CC $CFLAGS -sEXIT_RUNTIME=1 -sSTACK_SIZE=256KB -I"$CORE" "tests/grind/test_$suite.c" "$@" -o "$OUT/test_$suite.js"
        "${NODE:-node}" "$OUT/test_$suite.js"
    else
        $CC $CFLAGS -I"$CORE" "tests/grind/test_$suite.c" "$@" -o "$OUT/test_$suite"
        "$OUT/test_$suite"
    fi
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

# The native adapter suites compile the real firmware sources (tests/drum_host.h) with the upstream host flags,
# including its sanitizer exception: the firmware's fixed-point code shifts negative values left.
run_native() {
    suite=$1
    [ "$WASM" = 1 ] && { echo "$suite: native fixture is host-only" >&2; return 0; }
    "${PYTHON:-python3}" tools/build.py --gen-only > /dev/null
    $CC -std=gnu11 -O1 -g -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Wno-date-time         ${GRIND_SAN_FLAGS:+${GRIND_SAN_FLAGS} -fno-sanitize=shift-base} -UNDEBUG -Ibuild/gen -Ifirmware/src -Itests -I"$CORE"         "tests/grind/test_$suite.c" -o "$OUT/test_$suite" -lm
    "$OUT/test_$suite"
}

case "${1:-all}" in
    adapter) run_native adapter ;;
    cue) run_native cue ;;
    ui) run_native ui ;;
    controller) run_test controller "$CORE/controller.c" "$CORE/grid.c" "$CORE/rules.c" "$CORE/core.c" "$CORE/locks.c" \
        "$CORE/rng.c" "$CORE/serialize.c" ;;
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
        run_test controller "$CORE/controller.c" "$CORE/grid.c" "$CORE/rules.c" "$CORE/core.c" "$CORE/locks.c" \
            "$CORE/rng.c" "$CORE/serialize.c"
        run_native adapter
        run_native cue
        run_native ui
        ;;
    *) echo "unknown suite: $1" >&2; exit 2 ;;
esac
