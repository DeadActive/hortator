#!/bin/sh
# check_untouched.py: passes on the tree, fails when frozen code changes (on a copy)
set -e
cd "$(dirname "$0")/.."
python3 tools/check_untouched.py >/dev/null || { echo "guard: clean tree reported dirty"; exit 1; }
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cp -R firmware "$TMP/firmware"
echo '/* x */' >> "$TMP/firmware/src/ota.c"
if python3 tools/check_untouched.py --tree "$TMP" >/dev/null; then echo "guard: ota.c change not caught"; exit 1; fi
cp firmware/src/ota.c "$TMP/firmware/src/ota.c"
sed -i.bak 's/fm1_enter_uboot();/fm1_reboot();/' "$TMP/firmware/src/main.c"
if python3 tools/check_untouched.py --tree "$TMP" >/dev/null; then echo "guard: main.c recovery change not caught"; exit 1; fi
cp firmware/src/main.c "$TMP/firmware/src/main.c"
sed -i.bak 's/^#define ST_MAGIC 0x554C4546u/#define ST_MAGIC 0x4D524446u/' "$TMP/firmware/src/storage.c"
python3 tools/check_untouched.py --tree "$TMP" >/dev/null || { echo "guard: allowed ST_MAGIC change rejected"; exit 1; }
echo "guard: ok"
