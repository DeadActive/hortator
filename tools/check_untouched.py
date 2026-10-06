#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Drum machine fork: 2026 DEADACTIVE
"""Fail when code the USB update / recovery path depends on differs from the frozen baseline (tools/frozen_base.txt:
upstream Felucca 1e838e1, or a fork baseline tag = 1e838e1 + cited upstream hunks).

  tools/check_untouched.py [--tree DIR]     DIR: a copy of the repo root (default: the repo)

Frozen: hal/, loader/, ota.c, usb.c (the loader's), usb_app.c (the app's), crt0.S, app.ld byte for byte; storage.c except the ST_MAGIC
line; main.c except felucca_init() and the two boot-title lines; the boot-guard tail of core.h."""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def frozen_base():
    """The baseline tag, checked against the commit tools/frozen_base.txt pins (FROZEN_BASE_FILE: tests only)."""
    src = Path(os.environ.get("FROZEN_BASE_FILE", ROOT / "tools/frozen_base.txt"))
    for ln in src.read_text().splitlines():
        f = ln.split()
        if f[:1] == ["FROZEN_BASE"]:
            if len(f) != 3:
                raise SystemExit(f"{src}: FROZEN_BASE <tag> <commit>")
            r = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--verify", "-q", f"{f[1]}^{{commit}}"],
                               capture_output=True, text=True)
            if r.returncode or r.stdout.strip() != f[2]:
                raise SystemExit(f"check_untouched: tag {f[1]} is {r.stdout.strip() or 'missing'}, "
                                 f"pinned {f[2]} (moving the baseline is the user's decision)")
            return f[1]
    raise SystemExit(f"{src}: no FROZEN_BASE")


BASE = frozen_base()
FROZEN_DIRS = ["firmware/hal", "firmware/loader"]
FROZEN_FILES = ["firmware/src/ota.c", "firmware/src/usb.c", "firmware/src/usb_app.c", "firmware/crt0.S", "firmware/app.ld"]
MAGIC = re.compile(r"^#define ST_MAGIC 0x[0-9A-Fa-f]{8}u\b")
TITLE = re.compile(r"draw_text_box\(0, 1[03]0, 240, &FONT_[LS], \"")


def upstream(path):
    r = subprocess.run(["git", "-C", str(ROOT), "show", f"{BASE}:{path}"], capture_output=True)
    return r.stdout.decode() if r.returncode == 0 else None


def upstream_tree(d):
    r = subprocess.run(["git", "-C", str(ROOT), "ls-tree", "-r", "--name-only", BASE, d],
                       capture_output=True, text=True, check=True)
    return r.stdout.split()


def strip_main(text):
    """main.c without felucca_init() (and its comment line) and the boot-title lines"""
    out, skip = [], False
    for ln in text.splitlines():
        if ln.startswith("static void felucca_init(void)"):
            skip = True
            if out and out[-1].startswith("/* power-on"):
                out.pop()
        if skip:
            if ln == "}":
                skip = False
            continue
        if TITLE.search(ln):
            continue
        out.append(ln)
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", type=Path, default=ROOT)
    tree = ap.parse_args().tree
    print(f"check_untouched: baseline {BASE}")
    bad = []
    files = FROZEN_FILES + [f for d in FROZEN_DIRS for f in upstream_tree(d)]
    for f in files:
        p = tree / f
        if not p.exists() or p.read_bytes().decode(errors="replace") != upstream(f):
            bad.append(f"{f}: differs from {BASE}")
    up = upstream("firmware/src/storage.c").splitlines()
    cur = (tree / "firmware/src/storage.c").read_text().splitlines()
    if len(up) != len(cur) or any(a != b and not (MAGIC.match(a) and MAGIC.match(b)) for a, b in zip(up, cur)):
        bad.append("firmware/src/storage.c: differs beyond the ST_MAGIC line")
    if strip_main(upstream("firmware/src/main.c")) != strip_main((tree / "firmware/src/main.c").read_text()):
        bad.append("firmware/src/main.c: differs outside felucca_init() and the boot titles")
    if upstream("firmware/src/core.h").splitlines()[-5:] != (tree / "firmware/src/core.h").read_text().splitlines()[-5:]:
        bad.append("firmware/src/core.h: the boot-guard tail (last 5 lines) changed")
    for b in bad:
        print("FROZEN", b)
    print("check_untouched: " + ("FAILED" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
