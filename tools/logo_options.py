#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 DEADACTIVE
"""Four abstract 1-bit logo marks for BOOT / ABOUT (spec 2026-10-19-md-ui §9a), each drawn on a 240x240 MD RED
screen with HORTATOR in the MD 3x5 font at 2x under it, for the user to pick by eye.
  tools/logo_options.py OUTDIR  ->  OUTDIR/logo_a.png .. logo_d.png (the marks alone, 1-bit),
                                    OUTDIR/screen_a.png .. (2x previews), OUTDIR/sheet.png
"""
import math
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
BG, INK = (0xe0, 0x47, 0x2b), (0x38, 0x10, 0x0a)


def font():
    glyphs, cur = {}, None
    for line in (ROOT / "assets/fonts/md_font.txt").read_text().splitlines():
        if not line.strip() or line.startswith("# "):
            continue
        p = line.split()
        if len(p) == 2 and p[0] in ("F3", "BIG", "ICON"):
            cur = glyphs.setdefault((p[0], " " if p[1] == "space" else p[1]), []) if p[0] == "F3" else None
            continue
        if cur is not None:
            cur.append(line)
    return glyphs


F = font()


def text(px, s, x, y, sc=2):
    for ch in s:
        g = F[("F3", ch)]
        for r, row in enumerate(g):
            for c, v in enumerate(row):
                if v == "#":
                    for j in range(sc):
                        for i in range(sc):
                            px.add((x + c * sc + i, y + r * sc + j))
        x += (len(g[0]) + 1) * sc
    return x


def text_w(s, sc=2):
    return sum((len(F[("F3", ch)][0]) + 1) * sc for ch in s) - sc


def ring(px, cx, cy, r, w=1):
    for y in range(cy - r - 1, cy + r + 2):
        for x in range(cx - r - 1, cx + r + 2):
            d = math.hypot(x - cx, y - cy)
            if r - w < d <= r:
                px.add((x, y))


def disc_dither(px, cx, cy, r):
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            if math.hypot(x - cx, y - cy) <= r and (x + y) % 2 == 0:
                px.add((x, y))


def rect(px, x, y, w, h):
    for j in range(h):
        for i in range(w):
            px.add((x + i, y + j))


def frame(px, x, y, w, h):
    for i in range(w):
        px.add((x + i, y))
        px.add((x + i, y + h - 1))
    for j in range(h):
        px.add((x, y + j))
        px.add((x + w - 1, y + j))


def dither(px, x, y, w, h):
    for j in range(h):
        for i in range(w):
            if (x + i + y + j) % 2 == 0:
                px.add((x + i, y + j))


def mark_a():
    """the drum: a two-ring head, a dithered skin, a stick striking it (the hortator's beat)"""
    px = set()
    ring(px, 60, 60, 52, 3)
    ring(px, 60, 60, 40, 1)
    disc_dither(px, 60, 60, 38)
    rect(px, 52, 52, 17, 17)                         # the hit: a solid square at the centre
    for k in range(70):                               # the stick from top right into the hit, 4 px thick
        x, y = 112 - k, 4 + k * 0.8
        rect(px, int(x), int(y), 4, 4)
    return px, 120, 120


def mark_b():
    """the trig stack: a 4 x 4 pattern of MD trig boxes whose solid ones draw an H"""
    px = set()
    H = ["#..#", "#..#", "####", "#..#"]
    for r in range(4):
        for c in range(4):
            x, y = c * 30, r * 30
            if H[r][c] == "#":
                rect(px, x, y, 24, 24)
            elif (r + c) % 2:
                frame(px, x, y, 24, 24)
                dither(px, x + 2, y + 2, 20, 20)
            else:
                frame(px, x, y, 24, 24)
    return px, 114, 114


def mark_c():
    """the oars: twelve bars rising and falling in time (a galley's stroke), the stroke on the 1 solid, the rest
    dithered"""
    px = set()
    heights = [56, 30, 44, 20, 56, 30, 44, 20, 56, 30, 44, 20]
    for i, h in enumerate(heights):
        x = i * 13
        if i % 4 == 0:
            rect(px, x, 64 - h, 9, h * 2)
        else:
            frame(px, x, 64 - h, 9, h * 2)
            dither(px, x + 1, 64 - h + 1, 7, h * 2 - 2)
    return px, 152, 128


def mark_d():
    """the pulse: a square wave beating four times, a 3 px stroke, the downbeat's pulse filled"""
    px = set()
    x, y, hi, lo = 0, 70, 10, 70
    for beat in range(4):
        w_hi, w_lo = (14, 22) if beat else (22, 14)
        rect(px, x, hi, 3, lo - hi + 3)               # up
        rect(px, x, hi, w_hi + 3, 3)                  # the top
        if beat == 0:
            rect(px, x, hi, w_hi + 3, lo - hi + 3)
        x += w_hi
        rect(px, x, hi, 3, lo - hi + 3)               # down
        rect(px, x, lo, w_lo + 3, 3)                  # the bottom
        x += w_lo
    for k in range(4):
        rect(px, 4 + k * 36, lo + 14, 4, 4)           # the beat dots
    return px, x + 3, 92


def screen(px, w, h, name):
    img = Image.new("RGB", (240, 240), BG)
    ox, oy = (240 - w) // 2, max(14, (200 - h) // 2 - 6)
    for (x, y) in px:
        if 0 <= ox + x < 240 and 0 <= oy + y < 240:
            img.putpixel((ox + x, oy + y), INK)
    t = set()
    text(t, "HORTATOR", (240 - text_w("HORTATOR")) // 2, oy + h + 14)
    for (x, y) in t:
        img.putpixel((x, y), INK)
    for i in range(240):                               # the 1 px frame
        for p in ((i, 0), (i, 239), (0, i), (239, i)):
            img.putpixel(p, INK)
    return img


def main(out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    shots = []
    for key, fn in zip("abcd", (mark_a, mark_b, mark_c, mark_d)):
        px, w, h = fn()
        mark = Image.new("1", (w, h), 0)
        for (x, y) in px:
            if 0 <= x < w and 0 <= y < h:
                mark.putpixel((x, y), 1)
        mark.save(out / f"logo_{key}.png")
        sc = screen(px, w, h, key)
        sc.resize((480, 480), Image.NEAREST).save(out / f"screen_{key}.png")
        shots.append(sc)
        print(f"logo_{key}: {w} x {h}, {len(px)} px")
    sheet = Image.new("RGB", (4 * 484, 484), (40, 40, 40))
    for i, sc in enumerate(shots):
        sheet.paste(sc.resize((480, 480), Image.NEAREST), (i * 484 + 2, 2))
    sheet.save(out / "sheet.png")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "build/logo_options")
