#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""web/make_site.py: one release, two versions (2026-10-09). With a second package (the Bluetooth build) the site
carries both and the installer's metadata lists them; without one it is as before (one package, no choice).
Needs a packaged build: build/felucca-UNTESTED.fwsc (DRUM_PACKAGE=1 ./build.sh)."""
import json
import re
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "web"))
import make_site  # noqa: E402

fails = 0


def check(c, what):
    global fails
    print(("ok    " if c else "FAIL  ") + what)
    fails += not c


def meta(out):
    html = (out / "webapp" / "installer" / "index.html").read_text(encoding="utf-8")
    return json.loads(re.search(r"const meta = (\{.*?\});\n", html).group(1))


pkg = ROOT / "build" / "felucca-UNTESTED.fwsc"
if not pkg.exists():
    sys.exit("make_site_test: no build/felucca-UNTESTED.fwsc (DRUM_PACKAGE=1 ./build.sh)")
with tempfile.TemporaryDirectory() as d:
    d = Path(d)
    ble = d / "felucca-ble-UNTESTED.fwsc"
    shutil.copy(pkg, ble)
    out = d / "site"
    make_site.main(pkg, "drum-9.9.9+abc1234", out, ble)
    fw = sorted(f.name for f in (out / "firmware").glob("felucca-*.fwsc"))
    check(fw == ["felucca-drum-9.9.9-abc1234-ble.fwsc", "felucca-drum-9.9.9-abc1234.fwsc"], f"both packages: {fw}")
    m = meta(out)
    v = m.get("variants", [])
    check([x["id"] for x in v] == ["std", "ble"], "the installer lists both versions, the plain one first")
    check(m["pkg"] == v[0]["pkg"] == "../../firmware/felucca-drum-9.9.9-abc1234.fwsc" and
          m["version"] == v[0]["version"] == "drum-9.9.9+abc1234", "the plain version is the default")
    check(v[1]["pkg"] == "../../firmware/felucca-drum-9.9.9-abc1234-ble.fwsc" and
          v[1]["version"] == "drum-9.9.9+abc1234+ble", "the Bluetooth version: its package and its label")
    make_site.main(pkg, "drum-9.9.9+abc1234", out)
    fw = sorted(f.name for f in (out / "firmware").glob("felucca-*.fwsc"))
    check(fw == ["felucca-drum-9.9.9-abc1234.fwsc"], "one package again without the second (old ones removed)")
    check("variants" not in meta(out), "no choice with one package")
    bad = d / "bad.fwsc"
    bad.write_bytes(pkg.read_bytes().replace(b"FELUCCA-LOADER-1", b"XXXXXXXX-LOADER-1"))
    try:
        make_site.main(pkg, "drum-9.9.9+abc1234", d / "site2", bad)
        check(False, "a second package without the Felucca loader is refused")
    except SystemExit:
        check(True, "a second package without the Felucca loader is refused")
print("make_site: " + (f"{fails} FAILED" if fails else "all checks passed"))
sys.exit(fails != 0)
