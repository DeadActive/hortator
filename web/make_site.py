#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Drum machine fork: 2026 DEADACTIVE
"""Make the site (GitHub Pages):

  index.html                  the Hortator landing page with the simulator, when tools/build_sim.sh has built
                              build/sim (docs/SIMULATOR.md; + its scripts, fm1sim.wasm, the reel, version.json,
                              source.tar.gz); a redirect to the installer otherwise
  firmware/felucca-VER.fwsc   the package (+ LICENSE, LICENSING.md, LICENSES/: the package holds
                              JieLi SDK files under Apache-2.0, see LICENSING.md)
  webapp/installer/index.html index_pkg.html with fm1pkg.js, fm1ota.js and the metadata inlined
  webapp/editor/index.html    editor.html (+ fukiai.ttf, FUKIAI-LICENSE.txt)
  src/                        not touched

  web/make_site.py build/felucca-X.Y.fwsc X.Y OUT_DIR

The package identity (FM-1_9xx) is read from the package; the device reports it
after the install.
"""
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_9\d\d", product):
        raise SystemExit(f"{pkg}: identity {product!r} is not a Felucca package (FM-1_9xx)")
    if b"FELUCCA-LOADER-1" not in raw:              # Felucca loader marker: never publish a package with vendor files
        raise SystemExit(f"{pkg}: no Felucca loader in it; the site ships only fm1pkg_make.py packages "
                         "(a package patched from an official one carries vendor files)")
    html = (HERE / "index_pkg.html").read_text(encoding="utf-8")
    lib = strip_module((HERE / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1ota.js").read_text(encoding="utf-8"))
    name = f"felucca-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    meta = json.dumps({"version": version, "product": product, "pkg": "../../firmware/" + name})
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once; update make_site.py")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)
    inst, ed, fw = out / "webapp" / "installer", out / "webapp" / "editor", out / "firmware"
    for d in (inst, ed, fw):
        d.mkdir(parents=True, exist_ok=True)
    for old in fw.glob("felucca-*.fwsc"):          # one package: the current one
        old.unlink()
    (inst / "index.html").write_text(html, encoding="utf-8")
    shutil.copy(pkg, fw / name)
    lic = HERE.parent / "LICENSES"                  # (upstream 1.0) the package holds JieLi SDK files (Apache-2.0):
    (fw / "LICENSES").mkdir(exist_ok=True)          # their licence travels next to it, with the others we ship
    names = sorted(f.name for f in lic.glob("*.txt"))
    for n in names:
        shutil.copy(lic / n, fw / "LICENSES" / n)
    (fw / "LICENSES" / "index.html").write_text(    # Pages lists no folders
        '<!doctype html><meta charset="utf-8"><title>Licences</title><h1>Licence texts</h1><ul>'
        + "".join(f'<li><a href="{n}">{n}</a></li>' for n in names)
        + '</ul><p><a href="../LICENSING.md">LICENSING.md</a> · <a href="../LICENSE">LICENSE (GPL-3.0)</a></p>\n',
        encoding="utf-8")
    for doc in ("LICENSE", "LICENSING.md"):
        shutil.copy(HERE.parent / doc, fw / doc)
    shutil.copy(HERE / "editor.html", ed / "index.html")
    for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt"):
        if (HERE / f).exists():
            shutil.copy(HERE / f, ed / f)
    landing = sim_page(out)
    if not landing:
        (out / "index.html").write_text(
            '<!doctype html><meta charset="utf-8"><title>Hortator</title>'
            '<meta http-equiv="refresh" content="0; url=webapp/installer/">'
            '<a href="webapp/installer/">Hortator installer</a>\n', encoding="utf-8")
    print(f"site: {out}: {landing or 'index.html (redirect)'}, webapp/installer ({len(html)} B), webapp/editor, "
          f"firmware/{name} ({len(raw)} B, {product})")


SIM = HERE.parent / "build" / "sim"
SIM_PUBLISHED = (".html", ".js", ".css", ".wasm", ".gz", ".json")   # not reel.bin / reel.hash / package.fwsc / logs


def sim_page(out):
    """the landing page with the simulator at the site's root, from build/sim (tools/build_sim.sh); '' when not built"""
    if not (SIM / "fm1sim.wasm").exists():
        return ""
    files = [f for f in SIM.iterdir() if f.is_file() and f.suffix in SIM_PUBLISHED]
    for f in files:
        shutil.copy(f, out / f.name)
    try:                                           # the page shows the version it was built from: say if it is not this tree's
        built = json.loads((SIM / "version.json").read_text(encoding="utf-8"))["commit"]
        here = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=HERE, capture_output=True, text=True).stdout.strip()
        if here and not here.startswith(built) and not built.startswith(here):
            print(f"site: warning: build/sim was built from {built}, this tree is {here}: run tools/build_sim.sh first")
    except (OSError, KeyError, ValueError):
        pass
    return f"index.html (landing page + simulator, {len(files)} files)"


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
