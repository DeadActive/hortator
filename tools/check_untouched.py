#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Drum machine fork: 2026 DEADACTIVE
"""Fail when code the USB update / recovery path depends on differs from the frozen baseline (tools/frozen_base.txt:
upstream Felucca 1e838e1, or a fork baseline tag = 1e838e1 + cited upstream hunks, and the user's approved fork lines:
frozen-base-8 main.c's led_dim_tick(), frozen-base-9 usb_app.c's app product string "Hortator", 2026-10-23;
frozen-base-10 storage.c's lock-bank objects OBJ_LOCKS0..3 at 0xD4000..0xDBFFF, 2026-10-24;
frozen-base-11 storage.c's variation objects OBJ_VAR0..3 at 0xC0000..0xC7FFF, 2026-10-26).

  tools/check_untouched.py [--tree DIR]     historical source comparison
  tools/check_untouched.py --available-reference [--tree DIR]
                                          protected HAL/loader source at the available Hortator pin
  tools/check_untouched.py --historical-ref
                                          exact historical commit SHA, if available

DIR is a copy of the repo root (default: the repo). An unavailable historical object
returns 3. A wrong existing tag or a failed comparison returns 1.

Frozen: hal/, loader/, ota.c, usb.c (the loader's), usb_app.c (the app's), crt0.S, app.ld byte for byte; storage.c except the ST_MAGIC
line; main.c except felucca_init() and the boot titles (two lines, or the draw_boot_title() call); the boot-guard tail of core.h."""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AVAILABLE_PIN = "44c9ad4aa57bebf63f412bd552e57143c30b4065"
FROZEN_DIRS = ["firmware/hal", "firmware/loader"]
FROZEN_FILES = ["firmware/src/ota.c", "firmware/src/usb.c", "firmware/src/usb_app.c", "firmware/crt0.S", "firmware/app.ld"]
MAGIC = re.compile(r"^#define ST_MAGIC 0x[0-9A-Fa-f]{8}u\b")
TITLE = re.compile(r"draw_text_box\(0, 1[03]0, 240, &FONT_[LS], \"|^    draw_boot_title\(\);")   # (the logo)


class HistoricalUnavailable(Exception):
    """The exact historical object is missing and the tag does not exist."""


def git(*args, **kwargs):
    return subprocess.run(["git", "--no-replace-objects", "-C", str(ROOT), *args], capture_output=True, **kwargs)


def historical_reference():
    src = Path(os.environ.get("FROZEN_BASE_FILE", ROOT / "tools/frozen_base.txt"))
    fields = [line.split() for line in src.read_text().splitlines()]
    pins = [row for row in fields if row[:1] == ["FROZEN_BASE"]]
    if len(pins) != 1 or len(pins[0]) != 3:
        raise ValueError(f"{src}: expected one FROZEN_BASE <tag> <commit> entry")
    _, tag, pin = pins[0]
    ref = f"refs/tags/{tag}"
    if not re.fullmatch("[0-9a-f]{40}", pin) or git("check-ref-format", ref).returncode:
        raise ValueError(f"{src}: invalid historical tag or commit pin")

    tag_status = git("show-ref", "--verify", "--quiet", ref).returncode
    if tag_status == 0:
        peeled = git("rev-parse", "--verify", "-q", f"{ref}^{{commit}}", text=True)
        if peeled.returncode or peeled.stdout.strip() != pin:
            raise ValueError(f"tag {tag} resolves to {peeled.stdout.strip() or 'a non-commit object'}, expected {pin}")
    elif tag_status == 1:
        obj = git("cat-file", "--batch-check=%(objectname) %(objecttype)", input=f"{pin}\n", text=True, check=True)
        if obj.stdout.strip() == f"{pin} missing":
            raise HistoricalUnavailable(f"{tag} at {pin} is unavailable")
        if obj.stdout.strip() != f"{pin} commit":
            raise ValueError(f"pinned {pin} is not a Git commit")
    else:
        raise ValueError(f"cannot resolve Git tag {tag} (exit {tag_status})")
    return tag, pin


def upstream(base, path):
    return git("show", f"{base}:{path}", check=True).stdout.decode()


def upstream_tree(base, directory):
    r = git("ls-tree", "-r", "-z", "--name-only", base, directory, check=True)
    return [os.fsdecode(path) for path in r.stdout.split(b"\0") if path]


def check_available(tree):
    git("merge-base", "--is-ancestor", AVAILABLE_PIN, "HEAD", check=True)
    committed = git("diff", "--no-ext-diff", "--no-textconv", "--name-status", "--exit-code",
                    AVAILABLE_PIN, "HEAD", "--", *FROZEN_DIRS, text=True)
    if committed.returncode == 1:
        print(f"FAIL: available source: protected paths changed in HEAD relative to {AVAILABLE_PIN}")
        print(committed.stdout, end="")
        return 1
    committed.check_returncode()

    expected = {name for directory in FROZEN_DIRS for name in upstream_tree(AVAILABLE_PIN, directory)}
    actual = {p.relative_to(tree).as_posix() for directory in FROZEN_DIRS for p in (tree / directory).rglob("*")
              if not p.is_dir() or p.is_symlink()}
    problems = [f"{name} is an unexpected protected path" for name in sorted(actual - expected)]
    for name in sorted(expected):
        p = tree / name
        if p.is_symlink() or not p.is_file() or p.read_bytes() != git("show", f"{AVAILABLE_PIN}:{name}", check=True).stdout:
            problems.append(f"{name} differs from {AVAILABLE_PIN}")
    if problems:
        for detail in problems:
            print(f"FAIL: available source: {detail}")
        return 1
    print(f"PASS: available Hortator HAL/loader source matches {AVAILABLE_PIN}")
    return 0


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


def check_historical(tree, tag, base):
    print(f"check_untouched: historical baseline {tag} ({base})")
    bad = []
    files = FROZEN_FILES + [f for d in FROZEN_DIRS for f in upstream_tree(base, d)]
    for f in files:
        p = tree / f
        if not p.exists() or p.read_bytes().decode(errors="replace") != upstream(base, f):
            bad.append(f"{f}: differs from {base}")
    up = upstream(base, "firmware/src/storage.c").splitlines()
    cur = (tree / "firmware/src/storage.c").read_text().splitlines()
    if len(up) != len(cur) or any(a != b and not (MAGIC.match(a) and MAGIC.match(b)) for a, b in zip(up, cur)):
        bad.append("firmware/src/storage.c: differs beyond the ST_MAGIC line")
    if strip_main(upstream(base, "firmware/src/main.c")) != strip_main((tree / "firmware/src/main.c").read_text()):
        bad.append("firmware/src/main.c: differs outside felucca_init() and the boot titles")
    if upstream(base, "firmware/src/core.h").splitlines()[-5:] != (tree / "firmware/src/core.h").read_text().splitlines()[-5:]:
        bad.append("firmware/src/core.h: the boot-guard tail (last 5 lines) changed")
    for b in bad:
        print("FROZEN", b)
    print("check_untouched: " + ("FAILED" if bad else "ok"))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", type=Path, default=ROOT)
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--available-reference", action="store_true")
    mode.add_argument("--historical-ref", action="store_true")
    args = ap.parse_args()
    try:
        if args.available_reference:
            return check_available(args.tree)
        tag, base = historical_reference()
        if args.historical_ref:
            print(base)
            return 0
        return check_historical(args.tree, tag, base)
    except HistoricalUnavailable as exc:
        print(f"SKIPPED: historical: {exc}", file=sys.stderr)
        return 3
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        scope = "available source" if args.available_reference else "historical"
        print(f"FAIL: {scope}: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
