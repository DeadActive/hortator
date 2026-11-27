#!/bin/sh
# Protected HAL/loader source and historical comparison negative fixtures.
set -e
cd "$(dirname "$0")/.."
PY=${PYTHON:-python3}
GUARD=tools/check_untouched.py
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

"$PY" "$GUARD" --available-reference
mkdir "$TMP/available" "$TMP/available/firmware"
cp -R firmware/hal firmware/loader "$TMP/available/firmware/"
mkdir -p "$TMP/available/firmware/src"
cp firmware/src/main.c "$TMP/available/firmware/src/main.c"
printf '\n/* guard negative fixture */\n' >> "$TMP/available/firmware/hal/fm1_audio.h"
status=0
"$PY" "$GUARD" --available-reference --tree "$TMP/available" > "$TMP/available.log" 2>&1 || status=$?
if [ "$status" -ne 1 ] || ! grep -Fq 'FAIL: available source: firmware/hal/fm1_audio.h differs' "$TMP/available.log"; then
    cat "$TMP/available.log"
    echo "FAIL: available source guard did not reject a protected HAL change"
    exit 1
fi
echo "PASS: guard rejects a protected HAL change"
cp firmware/hal/fm1_audio.h "$TMP/available/firmware/hal/fm1_audio.h"
sed -i.bak 's/fm1_enter_uboot();/fm1_reboot();/' "$TMP/available/firmware/src/main.c"
status=0
"$PY" "$GUARD" --available-reference --tree "$TMP/available" > "$TMP/recovery.log" 2>&1 || status=$?
if [ "$status" -ne 1 ] || ! grep -Fq 'FAIL: available source: firmware/src/main.c differs' "$TMP/recovery.log"; then
    cat "$TMP/recovery.log"
    echo "FAIL: available source guard did not reject a boot recovery change"
    exit 1
fi
echo "PASS: guard rejects a boot recovery change"

status=0
"$PY" "$GUARD" --historical-ref >/dev/null || status=$?
case "$status" in
    0) ;;
    3) exit 0 ;;
    *) exit 1 ;;
esac
"$PY" "$GUARD" || { echo "FAIL: historical source guard rejected clean tree"; exit 1; }
cp -R firmware "$TMP/firmware"
echo '/* x */' >> "$TMP/firmware/src/ota.c"
if "$PY" "$GUARD" --tree "$TMP" >/dev/null; then echo "guard: ota.c change not caught"; exit 1; fi
cp firmware/src/ota.c "$TMP/firmware/src/ota.c"
echo '/* x */' >> "$TMP/firmware/src/usb_app.c"
if "$PY" "$GUARD" --tree "$TMP" >/dev/null; then echo "guard: usb_app.c change not caught"; exit 1; fi
cp firmware/src/usb_app.c "$TMP/firmware/src/usb_app.c"
sed -i.bak 's/fm1_enter_uboot();/fm1_reboot();/' "$TMP/firmware/src/main.c"
if "$PY" "$GUARD" --tree "$TMP" >/dev/null; then echo "guard: main.c recovery change not caught"; exit 1; fi
cp firmware/src/main.c "$TMP/firmware/src/main.c"
sed -i.bak 's/^#define ST_MAGIC 0x554C4546u/#define ST_MAGIC 0x4D524446u/' "$TMP/firmware/src/storage.c"
"$PY" "$GUARD" --tree "$TMP" >/dev/null || { echo "guard: allowed ST_MAGIC change rejected"; exit 1; }
sed 's/^FROZEN_BASE \([^ ]*\) [0-9a-f]*/FROZEN_BASE \1 1e838e1bad0000000000000000000000000000000/' tools/frozen_base.txt > "$TMP/frozen_base.txt"
if FROZEN_BASE_FILE="$TMP/frozen_base.txt" "$PY" "$GUARD" >/dev/null 2>&1; then
    echo "guard: a baseline tag that moved from its pinned commit not caught"; exit 1
fi
echo "guard: ok"
