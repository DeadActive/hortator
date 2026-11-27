#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run one Foundation CI lane locally or on Actions. Keep evidence in the build tree.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

lane=${1:-}
evidence=build/grind-evidence
mkdir -p "$evidence" || exit 1
result=0
lane_status=PASS
native_tool_dir=
trap 'if [[ -n $native_tool_dir ]]; then rm -rf -- "$native_tool_dir"; fi' EXIT

report() {
    local status=$1 name=$2 scope=${3:-required}
    printf '%s: %s\n' "$status" "$name"
    if [[ -n ${GITHUB_STEP_SUMMARY:-} ]]; then
        printf -- '- **%s** %s\n' "$status" "$name" >> "$GITHUB_STEP_SUMMARY"
    fi
    if [[ ${GITHUB_ACTIONS:-} == true && ( $status == FAIL || $status == BLOCKED ) ]]; then
        local level=warning details=$name
        if [[ $status == FAIL ]]; then level=error; fi
        if [[ -f "$evidence/$name.log" ]]; then
            if [[ $status == BLOCKED ]]; then
                details=$(grep -m1 '^BLOCKED:' "$evidence/$name.log" || printf '%s' "$name")
            else
                details=$(tail -n 8 "$evidence/$name.log")
            fi
        fi
        details=${details//%/%25}
        details=${details//$'\r'/%0D}
        details=${details//$'\n'/%0A}
        printf '::%s title=Foundation %s::%s\n' "$level" "$status" "${details:0:1600}"
    fi
    if [[ $scope == diagnostic ]]; then return 0; fi
    case $status in
        FAIL) result=1 ;;
        BLOCKED) if (( result == 0 )); then result=2; fi ;;
    esac
}

capture() {
    local name=$1
    shift
    "$@" 2>&1 | tee "$evidence/$name.log"
}

check() {
    local name=$1 status=0
    shift
    capture "$name" "$@" || status=$?
    case $status in
        0) report PASS "$name" ;;
        2)
            if grep -q '^BLOCKED:' "$evidence/$name.log"; then
                report BLOCKED "$name"
            else
                report FAIL "$name"
            fi
            ;;
        *) report FAIL "$name" ;;
    esac
}

# The upstream scripts sometimes exit 0 even when a required check did not run.
# An unavailable instruction counter is expected in the functional lanes.
suite() {
    local name=$1 performance=$2 status=0
    shift 2
    local log="$evidence/$name.log" skipped
    capture "$name" "$@" || status=$?
    if [[ $performance == diagnostic || $performance == optional ]] && (( status == 2 )) &&
        grep -q '^BLOCKED: performance: host instruction counter ' "$log" &&
        ! grep -qE '^FAIL([:[:space:]]|$)' "$log"; then
        report SKIPPED "$name interrupted instruction measurement; retrying functional suite"
        if [[ $performance == diagnostic ]]; then lane_status=SKIPPED; fi
        status=0
        capture "$name-retry" env GRIND_DISABLE_PERFORMANCE=1 GRIND_REQUIRE_PERFORMANCE=0 "$@" || status=$?
        cat "$evidence/$name-retry.log" >> "$log" || { report FAIL "$name retry evidence"; return; }
        log="$evidence/$name-retry.log"
    fi
    if [[ $name == native-host ]] && grep -Fq 'official V15 restore: skipped' "$log"; then
        report SKIPPED 'official stock recovery fixture (requires M-VAVE V15)'
    fi
    if (( status != 0 )); then
        if grep -qE '^FAIL([:[:space:]]|$)' "$log"; then
            report FAIL "$name"
        elif (( status == 2 )) && grep -q '^BLOCKED:' "$log"; then
            report BLOCKED "$name"
        else
            report FAIL "$name"
        fi
        return
    fi
    skipped=$(grep -iE '^SKIPPED:|^== (H2/H3 )?skip|^[[:alnum:]_.-]+: skipped([[:space:]]|$)' "$log" || true)
    if printf '%s\n' "$skipped" | grep -q '^SKIPPED: historical:'; then
        report SKIPPED "$name historical comparisons"
        skipped=$(printf '%s\n' "$skipped" | grep -v '^SKIPPED: historical:' || true)
    fi
    if [[ $performance == optional || $performance == diagnostic ]]; then
        local perf_skips
        perf_skips=$(printf '%s\n' "$skipped" | grep '^SKIPPED: performance:' || true)
        if [[ -n $perf_skips ]]; then
            report SKIPPED "$name instruction counts"
            if [[ $performance == diagnostic ]]; then lane_status=SKIPPED; fi
        fi
        skipped=$(printf '%s\n' "$skipped" | grep -v '^SKIPPED: performance:' || true)
    elif [[ $performance == wasm ]]; then
        if grep -Fq 'packages: skipped (run ./build.sh first)' "$log"; then
            report SKIPPED 'native package checks (covered by the native lane)'
            skipped=$(printf '%s\n' "$skipped" | grep -Fv 'packages: skipped (run ./build.sh first)' || true)
        fi
    fi
    if [[ -n $skipped ]]; then
        printf '%s\n' "$skipped"
        report BLOCKED "$name has unrun required checks"
    elif [[ $lane_status != SKIPPED ]]; then
        report PASS "$name"
    fi
}

native_audit() {
    local facts="$evidence/T03/native.json" native_facts
    if ! mkdir -p "$evidence/T03" ||
        ! python3 tools/grind/build_audit.py --resources "$evidence/resources.json" > "$facts"; then
        rm -f -- "$facts"
        report FAIL 'collect T03 native layout evidence' diagnostic
        return
    fi
    if [[ -n ${GITHUB_OUTPUT:-} ]]; then
        native_facts=$(python3 -c 'import json,sys; print(json.dumps(json.load(sys.stdin), separators=(",", ":")))' \
            < "$facts") && printf 't03_native=%s\n' "$native_facts" >> "$GITHUB_OUTPUT" ||
            report FAIL 'publish T03 native evidence status' diagnostic
    fi
}

native() {
    rm -f -- "$evidence/T03/native.json" || report FAIL 'clear stale T03 native evidence' diagnostic
    if [[ -z ${GRIND_JIELI_ARCHIVE:-} || -z ${AC79_SDK:-} ]]; then
        report BLOCKED 'native inputs (GRIND_JIELI_ARCHIVE and AC79_SDK)'
        return
    fi
    native_tool_dir=$(mktemp -d "$PWD/$evidence/jieli.XXXXXX") || { report FAIL 'native scratch directory'; return; }
    check native-inputs python3 tools/grind/native_inputs.py "$GRIND_JIELI_ARCHIVE" "$AC79_SDK" "$native_tool_dir"
    if (( result != 0 )); then return; fi
    export JIELI_TOOLCHAIN="$native_tool_dir/jieli-linux-toolchains-20260730.1"
    AC79_SDK=$(cd "$AC79_SDK" && pwd) || { report FAIL 'SDK path'; return; }
    export AC79_SDK FELUCCA_BLE=0 DRUM_SITE=0 JIELI_DOCKER=0
    export PATH="$PWD/.venv/bin:$PATH"
    if [[ ! -x .venv/bin/python ]]; then
        report BLOCKED 'native project .venv (requires Pillow)'
        return
    fi
    local package=build/felucca-UNTESTED.fwsc
    rm -f -- "$package" || { report FAIL 'remove stale test package'; return; }
    check native-build env DRUM_PACKAGE=1 ./build.sh
    if [[ ! -s $package ]] || (( result != 0 )); then
        report BLOCKED 'native host suite (test package unavailable)'
        return
    fi
    if ! sha256sum "$package" build/felucca.bin build/loader/ota.bin | tee "$evidence/native-sha256.txt"; then
        report FAIL 'native artifact hashes'
    fi
    local historical_status=0
    local -a upstream_build=()
    capture historical-ref python3 tools/check_untouched.py --historical-ref || historical_status=$?
    case $historical_status in
        0)
            if capture frozen-baseline sh tools/upstream_build.sh; then
                report PASS frozen-baseline
                upstream_build=(--upstream-build-dir build/upstream/build)
            else
                report FAIL frozen-baseline
            fi
            ;;
        3) report SKIPPED frozen-baseline ;;
        *) report FAIL historical-ref ;;
    esac
    local resource_status=0
    python3 tools/grind/resource_report.py --build-dir build \
        --build-log "$evidence/native-build.log" "${upstream_build[@]}" \
        > "$evidence/resources.json" || resource_status=$?
    cat "$evidence/resources.json"
    case $resource_status in
        0) report PASS resources ;;
        2) report BLOCKED resources ;;
        *) report FAIL resources ;;
    esac
    native_audit
    if ! git describe --tags --abbrev=0 --match 'drum-v*' >/dev/null 2>&1; then
        report BLOCKED 'native host suite (missing reachable drum-v release tags)'
    elif ! command -v rsync >/dev/null 2>&1; then
        report BLOCKED 'native host suite requires rsync'
    else
        suite native-host optional env DRUM_PACKAGE=1 sh tests/run_tests.sh
    fi
}

t03() {
    mkdir -p "$evidence/T03" || { report FAIL 'prepare T03 evidence'; return; }
    if ! python3 tools/grind/build_audit.py > "$evidence/T03/audit.json" 2> "$evidence/T03/audit.log"; then
        rm -f -- "$evidence/T03/audit.json"
        cat "$evidence/T03/audit.log"
        report FAIL 'assemble T03 audit'
        return
    fi
    check T03/audit python3 tools/grind/audit.py --complete "$evidence/T03/audit.json"
}

case $lane in
    host-core)
        check audit-tests python3 -m unittest discover -s tests/grind -p 'test_*.py' -v
        if [[ -f tests/run_grind_tests.sh ]]; then
            check grind-core env GRIND_SAN_FLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all' sh tests/run_grind_tests.sh all
        else
            report SKIPPED 'GRIND C core (planned after Foundation)'
        fi
        ;;
    host-upstream)
        suite host-upstream optional sh tests/run_drum_tests.sh
        ;;
    performance)
        suite performance diagnostic env GRIND_REQUIRE_PERFORMANCE=0 sh tests/run_drum_tests.sh
        ;;
    wasm)
        if [[ -f tests/run_grind_tests.sh ]]; then
            if mkdir -p build/grind-wasm; then
                check grind-core-wasm docker run --rm -v "$PWD":/src -w /src -u "$(id -u):$(id -g)" \
                    -e GRIND_WASM=1 -e EM_CACHE=/src/build/grind-wasm/emcache emscripten/emsdk:6.0.11 \
                    sh tests/run_grind_tests.sh all
            else
                report FAIL 'prepare Grind WASM output'
            fi
        fi
        check wasm-preview bash tools/build_sim.sh --preview
        preview_status=$result
        suite web-regression wasm node web/test_web.mjs
        check web-editor node web/editor/test_editor.mjs
        browser=${GRIND_CHROMIUM:-$(command -v google-chrome || command -v chromium || true)}
        if (( preview_status != 0 )); then
            report BLOCKED 'browser smoke requires successful WASM preview build'
        elif [[ -x $browser && -f build/grind-browser/node_modules/playwright-core/index.mjs ]]; then
            check browser-smoke env GRIND_CHROMIUM="$browser" node tests/grind/browser_smoke.mjs
        else
            report BLOCKED 'browser smoke requires Chromium and Playwright Core'
        fi
        ;;
    native) native ;;
    t03) t03 ;;
    license) check assets python3 tools/grind/check_assets.py --source-only ;;
    *) printf 'usage: %s {host-core|host-upstream|performance|wasm|native|license|t03}\n' "$0" >&2; exit 1 ;;
esac
if [[ -n ${GITHUB_OUTPUT:-} ]]; then
    case $result in
        0) printf 'status=%s\n' "$lane_status" >> "$GITHUB_OUTPUT" ;;
        1) printf 'status=FAIL\n' >> "$GITHUB_OUTPUT" ;;
        2) printf 'status=BLOCKED\n' >> "$GITHUB_OUTPUT" ;;
    esac
fi
exit "$result"
