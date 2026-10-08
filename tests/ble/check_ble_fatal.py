#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""ble_fatal (firmware/src/ble/ble_port.c) runs from a library assert, often with interrupts off: it may only record,
stop the audio and reboot. Anything that waits on interrupts (the console: con_putc waits on fm1_ms while it feeds
the watchdog) would hang there for good. Checks that its body calls nothing else."""
import re
import sys
from pathlib import Path

ALLOWED = {"core_audio_stop", "core_reboot"}
src = (Path(__file__).resolve().parents[2] / "firmware/src/ble/ble_port.c").read_text()
m = re.search(r"^static void ble_fatal\([^)]*\)\n\{\n(.*?)^\}", src, re.M | re.S)
if not m:
    sys.exit("check_ble_fatal: ble_fatal not found in ble_port.c")
body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
calls = set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", body)) - ALLOWED
if calls:
    print(f"FAIL  ble_fatal calls {sorted(calls)}: only {sorted(ALLOWED)} (interrupts may be off)")
    sys.exit(1)
print("ok    ble_fatal only records, stops the audio and reboots")
