#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""The drum firmware's version: MAJOR.MINOR.PATCH in the file VERSION.txt (docs/VERSIONING.md).

  python3 tools/version.py                      print the version
  python3 tools/version.py bump patch|minor|major
                                                raise it, date CHANGELOG.md's "Unreleased" section with it
  python3 tools/version.py check build          the built app and build/site carry it
  python3 tools/version.py --selftest
"""
import datetime
import json
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FORM = re.compile(r"(\d+)\.(\d+)\.(\d+)")
UNRELEASED = "## Unreleased"


def read(root=ROOT):
    v = (root / "VERSION.txt").read_text().strip()
    if not FORM.fullmatch(v):
        raise SystemExit(f"VERSION.txt: {v!r} is not MAJOR.MINOR.PATCH")
    return v


def firmware_string(v):
    """FELUCCA_VERSION: the ABOUT page and the console"""
    return f"DRUM-{v}"


def bumped(v, part):
    major, minor, patch = map(int, FORM.fullmatch(v).groups())
    if part == "major":
        return f"{major + 1}.0.0"
    if part == "minor":
        return f"{major}.{minor + 1}.0"
    if part == "patch":
        return f"{major}.{minor}.{patch + 1}"
    raise SystemExit(f"bump {part}: use patch, minor or major")


def bump(part, root=ROOT, today=None):
    old = read(root)
    new = bumped(old, part)
    log = root / "CHANGELOG.md"
    text = log.read_text()
    if UNRELEASED not in text:
        raise SystemExit(f"CHANGELOG.md has no '{UNRELEASED}' section to release")
    head, body = text.split(UNRELEASED, 1)
    if not body.split("\n## ", 1)[0].strip():
        raise SystemExit(f"CHANGELOG.md: '{UNRELEASED}' is empty; say what changed first")
    day = (today or datetime.date.today()).isoformat()
    log.write_text(f"{head}{UNRELEASED}\n\n## {new} - {day}{body}")
    (root / "VERSION.txt").write_text(new + "\n")
    return old, new


def check(build, root=ROOT):
    """errors: the version the build should carry, against what build/ holds"""
    v, errors = read(root), []
    app = build / "felucca.bin"
    if firmware_string(v).encode() + b"\0" not in app.read_bytes():
        errors.append(f"{app}: no '{firmware_string(v)}' string (ABOUT page, console)")
    page = build / "site" / "webapp" / "installer" / "index.html"
    if page.exists():
        m = re.search(r'"version": ("[^"]*")', page.read_text(encoding="utf-8"))
        label = json.loads(m[1]) if m else None
        if not (label and re.fullmatch(rf"drum-{re.escape(v)}\+[0-9a-f]+(-dirty)?", label)):
            errors.append(f"{page}: installer label {label!r}, expected drum-{v}+<commit>")
    else:
        errors.append(f"{page}: missing (DRUM_PACKAGE=1 ./build.sh writes it)")
    return errors


def selftest():
    fails = []
    def expect(what, ok):
        print(("  ok   " if ok else "  FAIL ") + what)
        if not ok:
            fails.append(what)
    expect("patch 0.5.0 -> 0.5.1", bumped("0.5.0", "patch") == "0.5.1")
    expect("minor 0.5.3 -> 0.6.0", bumped("0.5.3", "minor") == "0.6.0")
    expect("major 0.9.2 -> 1.0.0", bumped("0.9.2", "major") == "1.0.0")
    expect("minor 0.9.0 -> 0.10.0 (no carry)", bumped("0.9.0", "minor") == "0.10.0")
    with tempfile.TemporaryDirectory() as d:
        r = Path(d)
        (r / "VERSION.txt").write_text("0.5.0\n")
        (r / "CHANGELOG.md").write_text("# Changelog\n\n## Unreleased\n\n- TRS MIDI\n\n## 0.5.0 - 2026-10-06\n\n- first\n")
        old, new = bump("minor", r, datetime.date(2026, 10, 7))
        expect("bump writes VERSION.txt", (old, new, read(r)) == ("0.5.0", "0.6.0", "0.6.0"))
        expect("bump dates the Unreleased notes, opens a new empty section",
               (r / "CHANGELOG.md").read_text() == "# Changelog\n\n## Unreleased\n\n## 0.6.0 - 2026-10-07\n\n"
               "- TRS MIDI\n\n## 0.5.0 - 2026-10-06\n\n- first\n")
        try:
            bump("patch", r)
            expect("an empty Unreleased section is refused", False)
        except SystemExit:
            expect("an empty Unreleased section is refused", read(r) == "0.6.0")
        (r / "VERSION.txt").write_text("0.6\n")
        try:
            read(r)
            expect("VERSION.txt 0.6 is refused", False)
        except SystemExit:
            expect("VERSION.txt 0.6 is refused", True)
        (r / "VERSION.txt").write_text("0.5.0\n")
        b = r / "build"
        page = b / "site" / "webapp" / "installer" / "index.html"
        page.parent.mkdir(parents=True)
        def label(x):
            page.write_text("<script>const META = " + json.dumps({"version": x, "product": "FM-1_900"}) + ";</script>")
        (b / "felucca.bin").write_bytes(b"\x00xDRUM-0.5.0\x00y")
        label("drum-0.5.0+57f7e31")
        expect("a build carrying 0.5.0 passes", check(b, r) == [])
        (b / "felucca.bin").write_bytes(b"\x00xDRUM-0.5.01\x00y")
        expect("a build carrying another version fails", len(check(b, r)) == 1)
        (b / "felucca.bin").write_bytes(b"\x00xDRUM-0.5.0\x00y")
        label("drum-0.5.0+57f7e31-dirty")
        expect("a -dirty label passes", check(b, r) == [])
        label("drum-57f7e31")
        expect("an installer label without the version fails", len(check(b, r)) == 1)
    return 1 if fails else 0


def main(argv):
    if argv[:1] == ["--selftest"]:
        return selftest()
    if argv[:1] == ["bump"] and len(argv) == 2:
        old, new = bump(argv[1])
        print(f"{old} -> {new}: commit VERSION.txt + CHANGELOG.md, then tag it: git tag drum-v{new}")
        return 0
    if argv[:1] == ["check"] and len(argv) == 2:
        errors = check(Path(argv[1]))
        for e in errors:
            print("  FAIL", e)
        if not errors:
            print(f"  ok   build carries {firmware_string(read())} (installer drum-{read()}+<commit>)")
        return 1 if errors else 0
    if not argv:
        print(read())
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
