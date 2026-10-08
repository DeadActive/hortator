#!/bin/sh
# Gate 5 (spec 2026-10-08 BLE; form chosen by the user 2026-10-09): the default build (no FELUCCA_BLE) does not depend
# on the Bluetooth code. This tree's default build is byte-identical to the same tree's with every Bluetooth file
# deleted, both built now (felucca.bin embeds the build date: ABOUT). A default build that includes, links or reads a
# BT file outside the #if FELUCCA_BLE guards fails here (it does not build, or builds differently).
#   tools/check_default_build.sh
set -e
cd "$(dirname "$0")/.."
BT="firmware/src/ble firmware/src/core_ble_api.c firmware/hal/fm1_ble_hal.h firmware/hal/fm1_ctx.h
    firmware/hal/fm1_ctx.S firmware/app_ble.ld"
bld() {                                         # a build, retried (up to 5) on Docker's "exec format error"
    for i in 1 2 3 4 5; do
        if out=$("$@" 2>&1); then return 0; fi
        echo "$out" | grep -q "exec format error" || { echo "$out" | tail -20; return 1; }
    done
    echo "$out" | tail -20
    return 1
}
W=build/gate5/nobt
rm -rf build/gate5
mkdir -p "$W"
git ls-files -z | rsync -a --from0 --files-from=- ./ "$W/"     # the tracked files as they are in the working tree
for p in $BT; do
    [ -e "$W/$p" ] || { echo "gate 5: $p is not in the tree (update tools/check_default_build.sh's list)"; exit 1; }
    rm -rf "${W:?}/$p"
done
ln -s "$PWD/.venv" "$W/.venv" 2>/dev/null || true
(cd "$W" && bld env -u FELUCCA_BLE -u FELUCCA_CORE_REF ./build.sh) || {
    echo "gate 5: the default build fails without the Bluetooth files"; exit 1; }
bld env -u FELUCCA_BLE -u FELUCCA_CORE_REF ./build.sh
if cmp -s "$W/build/felucca.bin" build/felucca.bin; then
    echo "gate 5: the default build is byte-identical without the Bluetooth files ($(wc -c < build/felucca.bin) B)"
    rm -rf build/gate5
else
    echo "gate 5: the default build differs without the Bluetooth files: $(cmp "$W/build/felucca.bin" build/felucca.bin \
        | head -1); kept in $W"
    exit 1
fi
