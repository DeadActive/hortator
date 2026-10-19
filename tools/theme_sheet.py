#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 DEADACTIVE
"""The theme review page (spec 2026-10-19-md-ui §10): build/ui_shots/themes/index.html, one column per theme, the
screens ui_test's test_theme_sheet wrote (PNG, 2x), for reviewing by eye.
  tools/theme_sheet.py [DIR]   (default build/ui_shots/themes)
"""
import html
import sys
from pathlib import Path

THEMES = [("md-red", "MD RED"), ("red-on-black", "RED ON BLACK"), ("white-on-blue", "WHITE ON BLUE"),
          ("black-on-green", "BLACK ON GREEN")]


def main(root):
    root = Path(root)
    pages = sorted({p.stem for t, _ in THEMES for p in (root / t).glob("*.png")})
    rows = []
    for page in pages:
        cells = "".join(f'<td><img src="{t}/{page}.png" alt="{html.escape(page)} {n}"></td>' for t, n in THEMES)
        rows.append(f"<tr><th>{html.escape(page)}</th>{cells}</tr>")
    head = "".join(f"<th>{n}</th>" for _, n in THEMES)
    (root / "index.html").write_text(f"""<!doctype html>
<meta charset="utf-8"><title>Hortator screens by theme</title>
<style>
body {{ background:#1d1d1d; color:#ddd; font:14px system-ui; margin:16px; }}
table {{ border-collapse:collapse; }} th, td {{ padding:6px; vertical-align:top; }}
th {{ text-align:left; font-weight:600; }} img {{ width:480px; height:480px; image-rendering:pixelated; display:block; }}
</style>
<h1>Hortator screens by theme</h1>
<table><tr><th></th>{head}</tr>
{chr(10).join(rows)}
</table>
""")
    print(f"themes: {root / 'index.html'} ({len(pages)} screens x {len(THEMES)} themes)")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "build/ui_shots/themes")
