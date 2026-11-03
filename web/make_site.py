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
  firmware/felucca-VER-ble.fwsc  with a second package (the Bluetooth build, FELUCCA_BLE=1): the installer offers
                              both, the plain one first (2026-10-09)
  webapp/installer/index.html index_pkg.html with fm1pkg.js, fm1ota.js, sim/counter.js and the metadata inlined
  webapp/editor/index.html    editor.html (Hortator's editor) in one file: its modules (editor/*.mjs but the tests)
                              and device_data.json (for ?mock) inlined, the typeface embedded, sim/counter.js's
                              visit count; nothing else is fetched, so a saved copy works offline
  manual/index.html           the manual (docs/manual/*.md, built by tools/build_manual.py; search is in the page)
  src/                        not touched

  web/make_site.py build/felucca-X.Y.fwsc X.Y OUT_DIR [build/felucca-ble-X.Y.fwsc]

The package identity (FM-1_9xx) is read from the package; the device reports it
after the install.
"""
import base64
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
LIB = ("fm1pkg.js", "fm1ota.js", "sim/counter.js")   # inlined into the installer (sim/counter.js: the install count)
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


EDITOR_TAG = '<script type="module" src="editor/app.mjs"></script>'
EDITOR_FONT = re.compile(r'src: url\("\.\./\.\./terminus\.woff"\) format\("woff"\), url\("\.\./build/sim/terminus\.woff"\) format\("woff"\);')
# The inlined page's loader: each module becomes a blob: URL with its "./x.mjs" imports (static and dynamic) pointed at
# the others' URLs; app.mjs is imported last. linkModules (between the link: marks) is also run by web/test_web.mjs
# over the built page, with data: URLs, to test the inlined modules themselves.
EDITOR_LOADER = """// The editor's modules (editor/*.mjs) and device_data.json, inlined by web/make_site.py (the JSON above): the page
// needs nothing but itself, so a saved copy works offline.
// link:begin
function linkModules(mods, urlOf) {
  const urls = {}, busy = new Set(), spec = /(["'])\\.\\/([\\w-]+\\.mjs)\\1/g;
  const visit = (name) => {
    if (urls[name]) return urls[name];
    if (!(name in mods)) throw new Error(`no module ${name}`);
    if (busy.has(name)) throw new Error(`an import cycle at ${name}`);
    busy.add(name);
    const code = mods[name].replace(spec, (m, q, dep) => q + visit(dep) + q);
    busy.delete(name);
    return (urls[name] = urlOf(code, name));
  };
  Object.keys(mods).forEach(visit);
  return urls;
}
// link:end
try {
  const src = JSON.parse(document.getElementById("editor-src").textContent);
  globalThis.HORTATOR_DEVICE_DATA = src.data;
  const urls = linkModules(src.modules, (code) => URL.createObjectURL(new Blob([code], { type: "text/javascript" })));
  await import(urls["app.mjs"]);
} catch (e) {
  const el = document.getElementById("msg");
  el.hidden = false;
  el.textContent = `The editor could not start in this browser: ${e.message}`;
}"""


def editor_font():
    """the landing page's typeface (build/sim/terminus.woff, or made from the BDF by tests/sim_font.mjs), or None"""
    built = HERE.parent / "build" / "sim" / "terminus.woff"
    if built.exists():
        return built.read_bytes()
    with tempfile.TemporaryDirectory() as d:
        try:
            subprocess.run(["node", str(HERE.parent / "tests" / "sim_font.mjs"), str(HERE.parent / "assets" / "fonts" / "ter-u16n.bdf"),
                            str(Path(d) / "t.woff")], check=True, capture_output=True)
            return (Path(d) / "t.woff").read_bytes()
        except (OSError, subprocess.CalledProcessError):
            return None


def editor_html():
    """editor.html in one file: its modules and device_data.json inlined (EDITOR_LOADER), the typeface embedded, and
    its visits counted (sim/counter.js inlined before </body>; count.js added only on the published site)"""
    html = (HERE / "editor.html").read_text(encoding="utf-8")
    if html.count("</body>") != 1 or html.count(EDITOR_TAG) != 1 or len(EDITOR_FONT.findall(html)) != 1:
        raise SystemExit("editor.html must contain </body>, its app.mjs script and its @font-face src once; update make_site.py")
    mods = {f.name: f.read_text(encoding="utf-8") for f in sorted((HERE / "editor").glob("*.mjs")) if not f.name.startswith("test_")}
    data = json.loads((HERE / "editor" / "device_data.json").read_text(encoding="utf-8"))
    payload = json.dumps({"modules": mods, "data": data}, separators=(",", ":")).replace("<", "\\u003c")  # (no </script>)
    html = html.replace(EDITOR_TAG, f'<script type="application/json" id="editor-src">{payload}</script>\n'
                                     f'<script type="module">\n{EDITOR_LOADER}\n</script>')
    font = editor_font()
    face = f'src: url("data:font/woff;base64,{base64.b64encode(font).decode()}") format("woff");' if font else "src: local(\"Terminus\");"
    html = EDITOR_FONT.sub(lambda _: face, html)
    counter = strip_module((HERE / "sim" / "counter.js").read_text(encoding="utf-8"))
    return html.replace("</body>", f'<script type="module">\n{counter}\nloadPageCounter();\n</script>\n</body>')


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def checked(pkg):
    """the package's bytes and identity, or a stop: only fm1pkg_make.py packages of a Felucca identity"""
    raw = Path(pkg).read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_9\d\d", product):
        raise SystemExit(f"{pkg}: identity {product!r} is not a Felucca package (FM-1_9xx)")
    if b"FELUCCA-LOADER-1" not in raw:              # Felucca loader marker: never publish a package with vendor files
        raise SystemExit(f"{pkg}: no Felucca loader in it; the site ships only fm1pkg_make.py packages "
                         "(a package patched from an official one carries vendor files)")
    return raw, product


def main(pkg, version, out, ble_pkg=None):
    pkg, out = Path(pkg), Path(out)
    raw, product = checked(pkg)
    if ble_pkg is not None and checked(ble_pkg)[1] != product:
        raise SystemExit(f"{ble_pkg}: identity {checked(ble_pkg)[1]} is not {product}'s")
    html = (HERE / "index_pkg.html").read_text(encoding="utf-8")
    lib = "\n".join(strip_module((HERE / f).read_text(encoding="utf-8")) for f in LIB)
    name = f"felucca-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    info = {"version": version, "product": product, "pkg": "../../firmware/" + name}
    ble_name = name.replace(".fwsc", "-ble.fwsc")
    if ble_pkg is not None:                         # the installer's choice: the plain version first, the default
        info["variants"] = [{"id": "std", "version": version, "pkg": info["pkg"]},
                            {"id": "ble", "version": version + "+ble", "pkg": "../../firmware/" + ble_name}]
    meta = json.dumps(info)
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once; update make_site.py")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)
    inst, ed, fw = out / "webapp" / "installer", out / "webapp" / "editor", out / "firmware"
    for d in (inst, ed, fw):
        d.mkdir(parents=True, exist_ok=True)
    for old in fw.glob("felucca-*.fwsc"):          # the current package(s) only
        old.unlink()
    (inst / "index.html").write_text(html, encoding="utf-8")
    shutil.copy(pkg, fw / name)
    if ble_pkg is not None:
        shutil.copy(ble_pkg, fw / ble_name)
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
    (ed / "index.html").write_text(editor_html(), encoding="utf-8")   # one file: modules, data and typeface inlined
    shutil.rmtree(ed / "editor", ignore_errors=True)  # (what an older build put beside it)
    for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt"):
        (ed / f).unlink(missing_ok=True)
    sys.path.insert(0, str(HERE.parent / "tools"))
    import build_manual
    (out / "manual").mkdir(exist_ok=True)
    build_manual.build(out / "manual" / "index.html")
    landing = sim_page(out)
    if not landing:
        (out / "index.html").write_text(
            '<!doctype html><meta charset="utf-8"><title>Hortator</title>'
            '<meta http-equiv="refresh" content="0; url=webapp/installer/">'
            '<a href="webapp/installer/">Hortator installer</a>\n', encoding="utf-8")
    print(f"site: {out}: {landing or 'index.html (redirect)'}, webapp/installer ({len(html)} B), webapp/editor, "
          f"firmware/{name} ({len(raw)} B, {product})" + (f" + {ble_name}" if ble_pkg is not None else ""))


SIM = HERE.parent / "build" / "sim"
SIM_PUBLISHED = (".html", ".js", ".css", ".wasm", ".gz", ".json",   # not reel.bin / reel.hash / package.fwsc / logs
                 ".woff", ".txt")                                    # the typeface (terminus.woff) and its licence


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
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    main(*sys.argv[1:5])
