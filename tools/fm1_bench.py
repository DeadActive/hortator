#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""BENCH: the same performance cases (firmware/src/bench.c) on the host and on a connected FM-1, side by side.

  tools/fm1_bench.py --yes [--port /dev/cu.usbmodemNNNN]   run on the FM-1 (console `bench yes`) and the host
  tools/fm1_bench.py --log build/bench/device.log         a saved FM-1 run against the host
  tools/fm1_bench.py --selftest

On the FM-1: ~4 min, the speaker silent; the pattern in its memory is lost and it restarts after (flash, saved
projects untouched). --yes is required for that. The FM-1 must run a build of this tree (the case names are
compared). Only `status` and `bench yes` are sent; reading uses the standard library (no pyserial).

Out: build/bench/report.md and report.csv; build/bench/device.log (the FM-1's lines), build/bench/host.txt.
Columns: host instructions / sample (tests/bench_host measure); the FM-1's render time per sample, its average and
worst CPU % of an audio half (as SYSTEM CPU), late halves; ns on the FM-1 per host instruction (where it departs
from the run's median, the host predicts the FM-1 badly: flash cache, divides)."""
import argparse
import glob
import os
import re
import select
import statistics
import subprocess
import sys
import termios
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "bench"
HALF_FRAMES = 256
DEV_LINE = re.compile(r"^bench (\d+) (\S+) halves (\d+) sum_us (\d+) max_us (\d+) late (\d+)\s*$")
START = re.compile(r"^bench start (\d+) period_us (\d+)\s*$")
HOST_LINE = re.compile(r"^host (\d+) (\S+) ([0-9.]+)\s*$")


def parse_device(text):
    """-> (period_us, n, {idx: (name, halves, sum_us, max_us, late)}, done)"""
    period, n, rows, done = None, None, {}, False
    for ln in text.splitlines():
        ln = ln.strip().lstrip("> ")
        m = START.match(ln)
        if m:
            n, period = int(m[1]), int(m[2])
        m = DEV_LINE.match(ln)
        if m:
            rows[int(m[1])] = (m[2], int(m[3]), int(m[4]), int(m[5]), int(m[6]))
        if ln == "bench done":
            done = True
    return period, n, rows, done


def parse_host(text):
    return {int(m[1]): (m[2], float(m[3])) for m in map(HOST_LINE.match, text.splitlines()) if m}


def table(period, dev, host):
    """-> rows: (idx, name, host_ips, us_per_sample, avg_pct, max_pct, late, ns_per_instr)"""
    out = []
    for i in sorted(host):
        name, ips = host[i]
        if i not in dev:
            out.append((i, name, ips, None, None, None, None, None))
            continue
        dname, halves, sum_us, max_us, late = dev[i]
        if dname != name:
            raise SystemExit(f"fm1_bench: case {i} is {dname!r} on the FM-1, {name!r} here: install a build of this tree")
        avg = sum_us / halves
        ups = avg / HALF_FRAMES
        out.append((i, name, ips, ups, 100 * avg / period, 100 * max_us / period, late,
                    1000 * ups / ips if ips else None))
    return out


def report(rows, period):
    med = statistics.median([r[7] for r in rows if r[7]]) if any(r[7] for r in rows) else None
    md = ["# BENCH: host vs FM-1", "",
          f"Audio half: {period} us (256 samples). CPU % = render time / half, as SYSTEM CPU. "
          + (f"Median {med:.2f} ns on the FM-1 per host instruction; 'vs median' > 1: slower on the FM-1 than the "
             "host predicts." if med else ""), "",
          "| case | host instr/sample | FM-1 us/sample | FM-1 CPU avg | FM-1 CPU worst | late | ns/instr | vs median |",
          "|---|---|---|---|---|---|---|---|"]
    csv = ["case,name,host_instr_per_sample,fm1_us_per_sample,fm1_cpu_avg_pct,fm1_cpu_worst_pct,late,ns_per_instr"]
    for i, name, ips, ups, avg, mx, late, ns in rows:
        if ups is None:
            md.append(f"| {name} | {ips:.0f} | - | - | - | - | - | - |")
            csv.append(f"{i},{name},{ips:.1f},,,,,")
            continue
        md.append(f"| {name} | {ips:.0f} | {ups:.2f} | {avg:.0f} % | {mx:.0f} % | {late} | {ns:.2f} | "
                  f"{ns / med:.2f} |")
        csv.append(f"{i},{name},{ips:.1f},{ups:.3f},{avg:.1f},{mx:.1f},{late},{ns:.3f}")
    return "\n".join(md) + "\n", "\n".join(csv) + "\n"


def host_run():
    """build tests/bench_host and measure (build/gen made by tools/build.py's generate, as build_sim.sh)"""
    OUT.mkdir(parents=True, exist_ok=True)
    gen = ROOT / "build" / "gen"
    if not (gen / "felucca_logo.h").exists():
        sys.path.insert(0, str(ROOT / "tools"))
        import build
        build.GEN = gen
        build.generate()
    exe = OUT / "bench_host"
    subprocess.run(["cc", "-O2", "-w", f"-I{gen}", f"-I{ROOT / 'firmware/src'}", "-o", str(exe),
                    str(ROOT / "tests/bench_host.c"), "-lm"], check=True)
    print("host: measuring (about a minute)", flush=True)
    text = subprocess.run([str(exe), "measure"], check=True, capture_output=True, text=True).stdout
    (OUT / "host.txt").write_text(text)
    return text


class Port:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        a = termios.tcgetattr(self.fd)
        a[0] = a[1] = a[3] = 0
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        a[4] = a[5] = termios.B115200
        termios.tcsetattr(self.fd, termios.TCSANOW, a)

    def read(self, t):
        out, end = b"", time.time() + t
        while time.time() < end:
            if select.select([self.fd], [], [], 0.05)[0]:
                try:
                    out += os.read(self.fd, 4096)
                except (BlockingIOError, OSError):
                    break
        return out.decode("latin-1")

    def send(self, cmd):
        assert cmd in ("status", "bench yes"), cmd       # nothing else is ever sent
        os.write(self.fd, (cmd + "\r\n").encode())


def device_run(port):
    ports = [port] if port else sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        raise SystemExit("fm1_bench: no /dev/cu.usbmodem* (is the FM-1 connected and running the drum firmware?)")
    p = Port(ports[0])
    p.read(0.3)
    p.send("status")
    st = p.read(1.0)
    if "fm1-drums" not in st:
        raise SystemExit(f"fm1_bench: {ports[0]} does not answer as the FM-1 drum firmware")
    print(f"FM-1 on {ports[0]}: running the cases (~4 min, the speaker silent; it restarts after)", flush=True)
    p.send("bench yes")
    text, last, end = "", time.time(), time.time() + 20 * 60
    while time.time() < end:
        chunk = p.read(1.0)
        if chunk:
            text += chunk
            last = time.time()
            for ln in chunk.splitlines():
                if DEV_LINE.match(ln.strip()):
                    print("  " + ln.strip(), flush=True)
        if "bench done" in text:
            break
        if time.time() - last > 60:
            raise SystemExit("fm1_bench: the FM-1 stopped answering (partial log in build/bench/device.log)")
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "device.log").write_text(text)
    return text


def selftest():
    dev = ("bench start 3 period_us 5804\r\n> bench 0 idle halves 344 sum_us 240000 max_us 800 late 0\r\n"
           "bench 1 K808 halves 344 sum_us 1032000 max_us 3100 late 0\r\nbench done\r\n")
    host = "host 0 idle 290.0\nhost 1 K808 1333.0\nhost 2 K808+fx 2038.0\n"
    period, n, d, done = parse_device(dev)
    assert (period, n, done) == (5804, 3, True) and d[1] == ("K808", 344, 1032000, 3100, 0)
    rows = table(period, d, parse_host(host))
    i, name, ips, ups, avg, mx, late, ns = rows[1]
    assert abs(ups - 1032000 / 344 / 256) < 1e-9 and abs(avg - 100 * 3000 / 5804) < 1e-9
    assert abs(mx - 100 * 3100 / 5804) < 1e-9 and abs(ns - 1000 * ups / 1333) < 1e-9
    assert rows[2][3] is None                                # a case the FM-1 did not report
    md, csv = report(rows, period)
    assert "| K808 | 1333 | 11.72 | 52 % | 53 % | 0 |" in md and csv.count("\n") == 4
    try:
        table(period, {1: ("K909", 344, 1, 1, 0)}, parse_host(host))
        raise AssertionError("a name mismatch must stop")
    except SystemExit:
        pass
    print("fm1_bench selftest: ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--yes", action="store_true", help="run on the FM-1 (its pattern in memory is lost)")
    ap.add_argument("--port")
    ap.add_argument("--log", help="a saved FM-1 run (build/bench/device.log) instead of running it")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.log and not a.yes:
        raise SystemExit("fm1_bench: --yes runs the cases on the FM-1 (its pattern in memory is lost, it restarts "
                         "after); or --log FILE")
    dev_text = Path(a.log).read_text() if a.log else device_run(a.port)
    period, n, dev, done = parse_device(dev_text)
    if not period:
        raise SystemExit("fm1_bench: no 'bench start' line from the FM-1")
    host = parse_host(host_run())
    if n != len(host):
        raise SystemExit(f"fm1_bench: {n} cases on the FM-1, {len(host)} here: install a build of this tree")
    md, csv = report(table(period, dev, host), period)
    (OUT / "report.md").write_text(md)
    (OUT / "report.csv").write_text(csv)
    print(md)
    print(f"report: {OUT / 'report.md'}" + ("" if done else "  (the FM-1's run was incomplete)"))


if __name__ == "__main__":
    main()
