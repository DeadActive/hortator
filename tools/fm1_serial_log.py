#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# BLE MIDI: 2026 DEADACTIVE (felucca-ble ble-kit's, unchanged but for this docstring's menu)
"""Log the FM-1's USB serial console (Hortator: always on) with timestamps, across reboots: the port goes
away when the FM-1 resets and comes back, the logger waits for it. On every (re)connect it sends `crash`, `ble` and
`fml` (the crash record and the Bluetooth state right after a reset, before AUTOSTART starts Bluetooth again), then
the poll commands (--poll, default `ble`) every PERIOD seconds while connected. Needs pyserial (pip install pyserial).
For a stall or a lag around a link drop: --period 2 --poll ble,status (`ble`: the main loop's longest period and the
longest Bluetooth task run since the last `ble`, each link's state; `status`: audio_max_us, audio_late, cpu_pct).

  fm1_serial_log.py LOGFILE [--port /dev/cu.usbmodemXXXX] [--period 30] [--poll ble,status]
"""
import argparse
import glob
import time

try:
    import serial
except ImportError:                                 # (--help works without it)
    serial = None


def stamp():
    return time.strftime("%H:%M:%S ") + f"{time.time() % 1:.3f}"[1:]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", help="the file the lines are appended to")
    ap.add_argument("--port", help="the serial port (default: the first /dev/cu.usbmodem*)")
    ap.add_argument("--period", type=float, default=30.0, help="seconds between polls (0: none)")
    ap.add_argument("--poll", default="ble", help="console commands sent each period, comma separated (ble,status)")
    a = ap.parse_args()
    if serial is None:
        raise SystemExit("fm1_serial_log: pyserial is needed (pip install pyserial)")
    with open(a.log, "a", buffering=1) as f:
        def log(s):
            f.write(f"{stamp()} {s}\n")
        log("== logger started")
        while True:
            ports = [a.port] if a.port else sorted(glob.glob("/dev/cu.usbmodem*"))
            if not ports or not glob.glob(ports[0]):
                time.sleep(0.2)
                continue
            try:
                sp = serial.Serial(ports[0], 115200, timeout=0.1)
            except Exception:                       # (not ready yet)
                time.sleep(0.3)
                continue
            log(f"== connected {ports[0]}")
            buf = b""
            try:
                time.sleep(0.3)
                sp.write(b"\r\ncrash\r\n")
                time.sleep(0.3)
                sp.write(b"ble\r\nfml\r\n")
                last = time.time()
                while True:
                    d = sp.read(4096)
                    if d:
                        buf += d
                        while b"\n" in buf:
                            line, buf = buf.split(b"\n", 1)
                            log(line.rstrip(b"\r").decode("latin-1"))
                    if a.period and time.time() - last > a.period:
                        sp.write("".join(c.strip() + "\r\n" for c in a.poll.split(",") if c.strip()).encode())
                        last = time.time()
            except Exception as e:
                if buf:
                    log(buf.decode("latin-1"))
                log(f"== disconnected ({type(e).__name__}: {e})")
                try:
                    sp.close()
                except Exception:
                    pass
                time.sleep(0.2)


if __name__ == "__main__":
    main()
