#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""Boot screen study: twenty 240 x 240 HortatoR start screens, each built from glitch, pixel sort
and dither.

  boot_variants.py OUTDIR [N ...]        (N: only these screens, 1..20)

Screens 1-10 set the word in UnifrakturMaguntia (assets/fonts/, SIL OFL 1.1) stretched tall and
emboldened. Screens 11-20 use one gothic or horror face each (FONTS: SIL OFL 1.1, fetched from
Google Fonts into build/fonts-cache on first use) and do not care whether the word reads. Every
screen uses the screen's seven levels only: 0 black, 1..5 the COLOR palette's five steps (gfx.c
PALETTES), 6 white, so it recolours with the palette like the rest of the UI. OUTDIR gets vNN.png
(the level 0..6 per pixel, 8-bit grey) and a preview vNN_<palette>.png per palette, through RGB565.
"""
import sys
from pathlib import Path

import numpy as np
from PIL import BdfFontFile, Image, ImageDraw, ImageFilter, ImageFont

W = H = 240
ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / "assets" / "fonts" / "UnifrakturMaguntia-Book.ttf"
TEXT = "HortatoR"
# intensity of each level, used when dithering a 0..1 field down to the seven levels
LEVELS = np.array([0.0, 0.14, 0.30, 0.48, 0.68, 0.86, 1.0])
PALETTES = {   # gfx.c PALETTES
    "green": [(0, 40, 12), (0, 84, 30), (16, 140, 54), (56, 200, 92), (120, 255, 146)],
    "amber": [(60, 26, 0), (110, 50, 0), (170, 82, 0), (225, 120, 8), (255, 166, 40)],
    "cyan": [(0, 30, 50), (0, 62, 96), (16, 112, 160), (56, 172, 222), (140, 222, 255)],
    "red": [(52, 8, 8), (100, 18, 14), (170, 36, 26), (226, 64, 48), (255, 112, 92)],
    "mono": [(40, 40, 40), (80, 80, 80), (130, 130, 130), (186, 186, 186), (226, 226, 226)],
}
YY, XX = np.mgrid[0:H, 0:W]
OUT = None


# ---------------------------------------------------------------- type

# the second set's faces, all SIL OFL 1.1 from github.com/google/fonts, fetched once into
# build/fonts-cache (UnifrakturMaguntia ships in assets/fonts)
GF = "https://raw.githubusercontent.com/google/fonts/main/ofl/"
FONTS = {
    "maguntia": (None, FONT, None),
    "cook": (None, ROOT / "assets" / "fonts" / "UnifrakturCook-Bold.ttf", None),
    "metalmania": (GF + "metalmania/MetalMania-Regular.ttf", "MetalMania-Regular.ttf", None),
    "blakaink": (GF + "blakaink/BlakaInk-Regular.ttf", "BlakaInk-Regular.ttf", None),
    "blakahollow": (GF + "blakahollow/BlakaHollow-Regular.ttf", "BlakaHollow-Regular.ttf", None),
    "texturina": (GF + "texturina/Texturina%5Bopsz,wght%5D.ttf", "Texturina.ttf", {"Weight": 900, "Optical size": 72}),
    "fruktur": (GF + "fruktur/Fruktur-Regular.ttf", "Fruktur-Regular.ttf", None),
    "eater": (GF + "eater/Eater-Regular.ttf", "Eater-Regular.ttf", None),
    "bastarda": (GF + "jacquardabastarda9/JacquardaBastarda9-Regular.ttf", "JacquardaBastarda9-Regular.ttf", None),
    "nosifer": (GF + "nosifer/Nosifer-Regular.ttf", "Nosifer-Regular.ttf", None),
    "pirata": (GF + "pirataone/PirataOne-Regular.ttf", "PirataOne-Regular.ttf", None),
}


def font_file(key):
    url, name, var = FONTS[key]
    if url is None:
        return name, var
    path = ROOT / "build" / "fonts-cache" / name
    if not path.exists():
        import urllib.request
        path.parent.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(url, path)
    return path, var


def logo(w, h, bold=3, font="maguntia", text=TEXT, aa=True):
    """the word as a 0..1 alpha field stretched to exactly w x h (ink box), emboldened by bold px
    at double size"""
    path, var = font_file(font)
    f = ImageFont.truetype(str(path), 200)
    if var:
        f.set_variation_by_axes([var.get(a["name"].decode() if isinstance(a["name"], bytes) else a["name"],
                                         a["default"]) for a in f.get_variation_axes()])
    l, t, r, b = f.getbbox(text)
    im = Image.new("L", (r - l + 8, b - t + 8), 0)
    ImageDraw.Draw(im).text((4 - l, 4 - t), text, font=f, fill=255)
    im = im.crop(im.getbbox()).resize((2 * w, 2 * h), Image.LANCZOS)
    if bold > 1:
        im = im.filter(ImageFilter.MaxFilter(bold))
    return np.asarray(im.resize((w, h), Image.LANCZOS if aa else Image.NEAREST), np.float32) / 255.0


def word(w, h, x, y, bold=3, font="maguntia"):
    """the word placed on an empty screen-sized field"""
    f = np.zeros((H, W), np.float32)
    paste(f, logo(w, h, bold, font), x, y)
    return f


def small_font():
    out = OUT / "_ter16"
    if not out.with_suffix(".pil").exists():
        with open(ROOT / "assets/fonts/ter-u16n.bdf", "rb") as fp:
            BdfFontFile.BdfFontFile(fp).save(str(out))
    return ImageFont.load(str(out.with_suffix(".pil")))


def tag(scr, text, y, level, clear=True):
    """Terminus 16 text (the firmware's FONT_S) centred in one level, on a cleared strip"""
    f = small_font()
    l, t, r, b = f.getbbox(text)
    im = Image.new("L", (W, 16), 0)
    ImageDraw.Draw(im).text(((W - (r - l)) // 2, 0), text, font=f, fill=255)
    m = np.asarray(im) > 127
    if clear:
        xs = np.where(m.any(0))[0]
        scr[y - 2:y + 16, xs[0] - 4:xs[-1] + 5] = 0
    scr[y:y + 16][m] = level


def paste(field, a, x, y):
    h, w = a.shape
    x0, y0, x1, y1 = max(x, 0), max(y, 0), min(x + w, W), min(y + h, H)
    if x1 > x0 and y1 > y0:
        field[y0:y1, x0:x1] = np.maximum(field[y0:y1, x0:x1], a[y0 - y:y1 - y, x0 - x:x1 - x])


def outline(m, r=1):
    d = m.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            d |= np.roll(np.roll(m, dy, 0), dx, 1)
    return d & ~m


# ---------------------------------------------------------------- dither

BAYER4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0
BAYER8 = np.block([[4 * BAYER4 * 16, 4 * BAYER4 * 16 + 2], [4 * BAYER4 * 16 + 3, 4 * BAYER4 * 16 + 1]]) / 64.0


def q_ordered(f, mat, lo=0, hi=6):
    """ordered (Bayer) dither of a 0..1 field across levels lo..hi"""
    lv = LEVELS[lo:hi + 1]
    th = np.tile(mat, (H // mat.shape[0] + 1, W // mat.shape[1] + 1))[:H, :W] + 0.5 / mat.size
    v = np.clip(f, 0, 1) * (lv[-1] - lv[0]) + lv[0]
    i = np.clip(np.searchsorted(lv, v, side="right") - 1, 0, len(lv) - 2)
    frac = (v - lv[i]) / (lv[i + 1] - lv[i])
    return (lo + i + (frac > th)).astype(np.uint8)


def q_diffuse(f, kind="fs", lo=0, hi=6):
    """error diffusion, serpentine (Floyd-Steinberg or Atkinson), across levels lo..hi"""
    lv = LEVELS[lo:hi + 1]
    g = np.clip(f, 0, 1).astype(np.float64) * (lv[-1] - lv[0]) + lv[0]
    out = np.zeros(g.shape, np.uint8)
    taps = ([(0, 1, 7 / 16), (1, -1, 3 / 16), (1, 0, 5 / 16), (1, 1, 1 / 16)] if kind == "fs" else
            [(0, 1, 1 / 8), (0, 2, 1 / 8), (1, -1, 1 / 8), (1, 0, 1 / 8), (1, 1, 1 / 8), (2, 0, 1 / 8)])
    for y in range(H):
        rev = y & 1
        for x in (range(W - 1, -1, -1) if rev else range(W)):
            v = g[y, x]
            k = int(np.abs(lv - v).argmin())
            out[y, x] = lo + k
            e = v - lv[k]
            for dy, dx, wgt in taps:
                xx = x - dx if rev else x + dx
                if y + dy < H and 0 <= xx < W:
                    g[y + dy, xx] += e * wgt
    return out


# ---------------------------------------------------------------- sort / glitch

def sort_runs(f, mask, axis=1, desc=False):
    """pixel sort: inside every run of True in mask (along axis 1 = rows, 0 = columns) sort the
    values"""
    f = f.copy() if axis == 1 else f.T.copy()
    m = mask if axis == 1 else mask.T
    for r in range(f.shape[0]):
        d = np.diff(np.concatenate([[0], m[r].astype(np.int8), [0]]))
        for s, e in zip(np.where(d == 1)[0], np.where(d == -1)[0]):
            seg = np.sort(f[r, s:e])
            f[r, s:e] = seg[::-1] if desc else seg
    return f if axis == 1 else f.T


def smooth_noise(rng, n, cells):
    """1-D value noise 0..1, n samples over cells cells, blocky-smooth like a sort's interval map"""
    k = rng.random(cells + 2)
    x = np.linspace(0, cells, n)
    i = x.astype(int)
    t = x - i
    t = t * t * (3 - 2 * t)
    return k[i] * (1 - t) + k[i + 1] * t


def streaks(f, rng, lo, hi, up=False, cells=24, gain=1.0):
    """the sort drip: from every column's outermost ink edge, a run of lo..hi px. Each run is
    noise sampled along it and sorted, bright at the letter, so it falls off in steps like a
    sorted interval of the image."""
    out = np.zeros_like(f)
    ink = f > 0.5
    lens = lo + (hi - lo) * smooth_noise(rng, W, cells) ** 1.7
    for x in range(W):
        ys = np.where(ink[:, x])[0]
        if not len(ys) or rng.random() < 0.12:
            continue
        n = int(lens[x] * (0.6 + 0.8 * rng.random()))
        if n < 2:
            continue
        run = np.sort(rng.random(n) ** 0.8)[::-1] * np.linspace(1, 0.15, n) * gain
        if up:
            y1 = ys[0]
            y0 = max(0, y1 - n)
            out[y0:y1, x] = run[:y1 - y0][::-1]
        else:
            y0 = ys[-1] + 1
            y1 = min(H, y0 + n)
            out[y0:y1, x] = run[:y1 - y0]
    return out


def tear(f, rng, n, maxshift, hmin=2, hmax=10, region=(0, H)):
    """horizontal slice displacement: n bands of the field shifted sideways"""
    f = f.copy()
    for _ in range(n):
        h = int(rng.integers(hmin, hmax))
        y = int(rng.integers(region[0], max(region[0] + 1, region[1] - h)))
        f[y:y + h] = np.roll(f[y:y + h], int(rng.integers(-maxshift, maxshift + 1)), axis=1)
    return f


def row_smear(f, rng, n, region, minlen=20, maxlen=120, desc=True):
    """sync loss: n rows whose intervals are pixel-sorted sideways"""
    m = np.zeros((H, W), bool)
    for _ in range(n):
        y = int(rng.integers(*region))
        h = int(rng.integers(1, 4))
        x = int(rng.integers(0, W - minlen))
        m[y:y + h, x:x + int(rng.integers(minlen, maxlen))] = True
    return sort_runs(f, m, axis=1, desc=desc)


def over(scr, idx, where=None):
    m = idx > 0 if where is None else where
    scr[m] = idx[m]
    return scr


# ---------------------------------------------------------------- the ten screens

def v01_melt(rng):
    """MELT. The word hangs high; every column below it is a sorted drip, Bayer-dithered down
    into black; three slices of the word slip sideways."""
    f = word(220, 84, 10, 34)
    f = tear(f, rng, 2, 4, 2, 4, (44, 76))
    d = streaks(f, rng, 20, 150, cells=30)
    scr = q_ordered(d * 0.95, BAYER8, 0, 5)
    scr[f > 0.5] = 6
    scr[outline(f > 0.5) & (YY > 70) & (scr < 4)] = 4
    tag(scr, "DRUM MACHINE", 216, 3)
    return scr


def v02_tear(rng):
    """SIGNAL TEAR. VHS slice displacement through the word, a dim ghost one step behind, torn
    rows sorted out sideways into streaks, the ghost Bayer-screened on scanlines."""
    f = word(216, 86, 12, 70)
    g = word(216, 86, 3, 75)
    t = tear(f, rng, 7, 12, 2, 7, (70, 156))
    g = tear(g, rng, 5, 20, 2, 6, (75, 160))
    sm = row_smear(t * (0.6 + 0.4 * rng.random((H, W))), rng, 22, (70, 156), 30, 140)
    scr = q_ordered(g * 0.35, BAYER4, 0, 2)
    scr[YY % 2 == 1] = np.minimum(scr[YY % 2 == 1], 1)
    over(scr, q_ordered(sm * 0.8, BAYER4, 0, 4))
    scr[t > 0.5] = 5
    for _ in range(5):
        y = int(rng.integers(72, 150))
        b = (t[y:y + 2] > 0.5)
        scr[y:y + 2][b] = 6
    tag(scr, "DRUM MACHINE", 172, 3)
    return scr


def v03_halo(rng):
    """ATKINSON HALO. An elliptic glow behind the word, its rows pixel-sorted in bands and
    Atkinson-dithered; the word knocked out in black with a white rim; two tears through it."""
    g = np.clip(1 - np.sqrt(((XX - 120) / 150.0) ** 2 + ((YY - 110) / 70.0) ** 2), 0, 1) ** 1.5
    g = g * (0.8 + 0.2 * np.random.default_rng(3).random((H, W)))
    band = np.zeros((H, W), bool)
    for y0 in (62, 96, 128, 150):
        band[y0:y0 + int(rng.integers(3, 9)), int(rng.integers(0, 60)):int(rng.integers(150, 240))] = True
    g = sort_runs(g, band & (g > 0.1), axis=1, desc=bool(rng.random() < 0.5))
    f = word(214, 88, 13, 66)
    f = tear(f, rng, 2, 6, 2, 4, (90, 140))
    scr = q_diffuse(g * 0.92, "atkinson", 0, 5)
    scr[outline(f > 0.5, 2)] = 6
    scr[f > 0.5] = 0
    tag(scr, "DRUM MACHINE", 192, 4)
    return scr


def v04_sorted_field(rng):
    """SORTED FIELD. A cloudy field pixel-sorted row by row into long ramps; the letters break
    the sort intervals so the word stands as black negative space. Bayer 4, torn rows."""
    lowf = np.asarray(Image.fromarray((rng.random((8, 10)) * 255).astype(np.uint8))
                      .resize((W, H), Image.BICUBIC), np.float32) / 255.0
    fld = 0.75 * lowf + 0.25 * rng.random((H, W))
    f = word(222, 92, 9, 66)
    env = np.clip(1 - np.abs(YY - 112) / 92.0, 0, 1) ** 0.5
    mask = (fld > 0.25 + 0.4 * smooth_noise(rng, H, 40)[:, None]) & (f < 0.3)
    s = sort_runs(fld, mask, axis=1, desc=False) * env
    s = tear(s, rng, 10, 16, 1, 4, (20, 210))
    scr = q_ordered(s * 0.95, BAYER4, 0, 5)
    k = f > 0.5
    scr[k] = 0
    scr[k & ~np.roll(k, 1, 0)] = 6
    tag(scr, "DRUM MACHINE", 216, 5)
    return scr


def v05_reflection(rng):
    """BAYER REFLECTION. The word on a horizon; the mirror image below it sorted into drips,
    Bayer-dithered away and torn row by row like water."""
    f = word(216, 80, 12, 38)
    mir = np.zeros_like(f)
    paste(mir, logo(216, 80)[::-1], 12, 124)
    mir = np.maximum(mir * 0.75, streaks(mir, rng, 6, 70, cells=36, gain=0.7))
    mir = mir * np.clip(1 - (YY - 124) / 116.0, 0, 1) * (YY >= 124)
    mir = tear(mir, rng, 22, 9, 1, 3, (124, 236))
    scr = q_ordered(mir, BAYER4, 0, 4)
    scr[tear(f, rng, 2, 6, 2, 4, (70, 112)) > 0.5] = 6
    scr[121, 6:234] = 2
    tag(scr, "DRUM MACHINE", 12, 3)
    return scr


def v06_datamosh(rng):
    """DATAMOSH. 8 x 8 macroblocks of the word smeared, dropped and stuck, each repeat a step
    darker; stuck blocks Bayer-filled; the dropped blocks drip sorted columns."""
    f = word(216, 90, 12, 66)
    out = f.copy()
    lvl = np.full((H, W), 5, np.uint8)
    stuck = np.zeros((H, W), bool)
    for _ in range(80):
        by, bx = int(rng.integers(8, 20)) * 8, int(rng.integers(0, 30)) * 8
        if f[by:by + 8, bx:bx + 8].max() < 0.5:
            continue
        r = rng.random()
        if r < 0.45:
            d = 1 if rng.random() < 0.5 else -1
            for k in range(1, int(rng.integers(2, 6))):
                x2 = bx + d * 8 * k
                if 0 <= x2 <= W - 8:
                    out[by:by + 8, x2:x2 + 8] = np.maximum(out[by:by + 8, x2:x2 + 8], f[by:by + 8, bx:bx + 8])
                    lvl[by:by + 8, x2:x2 + 8] = max(1, 5 - k)
        elif r < 0.75:
            dy = int(rng.integers(1, 4)) * 8
            if by + dy + 8 <= H:
                out[by + dy:by + dy + 8, bx:bx + 8] = f[by:by + 8, bx:bx + 8]
                lvl[by + dy:by + dy + 8, bx:bx + 8] = 3
        else:
            stuck[by:by + 8, bx:bx + 8] = True
    scr = np.where(out > 0.5, lvl, 0).astype(np.uint8)
    scr[stuck & (scr == 0)] = q_ordered(np.full((H, W), 0.45), BAYER4, 0, 2)[stuck & (scr == 0)]
    d = streaks(out * (lvl < 5), rng, 4, 46, cells=30, gain=0.6)
    over(scr, q_ordered(d, BAYER4, 0, 3), (scr == 0) & (d > 0))
    scr[(f > 0.5) & (scr >= 5)] = 6
    tag(scr, "DRUM MACHINE", 206, 3)
    return scr


def v07_band(rng):
    """INVERTED BAND. A bright band across the screen, its rows pixel-sorted in intervals and
    Bayer 8 dithered; the word cut out of it in black; the band's edges torn."""
    g = np.clip(1 - np.abs(YY - 112) / 62.0, 0, 1) ** 0.45
    g = g * (0.7 + 0.3 * rng.random((H, W)))
    m = (rng.random((H, 1)) < 0.5) & (smooth_noise(rng, W, 14)[None, :] > 0.35)
    g = sort_runs(g, m, axis=1, desc=True)
    g = tear(g, rng, 18, 22, 1, 5, (46, 180))
    f = word(208, 90, 16, 67)
    scr = q_ordered(g, BAYER8, 0, 5)
    scr[f > 0.5] = 0
    tag(scr, "DRUM MACHINE", 202, 5)
    return scr


def v08_static(rng):
    """FROM STATIC. The word rising out of TV static: signal grows toward the middle, sync-lost
    rows sorted into smears, two tears; Floyd-Steinberg."""
    f = word(218, 90, 11, 68)
    noise = rng.random((H, W)).astype(np.float32) * (0.5 + 0.5 * rng.random((H, 1)))
    snr = np.clip(1 - np.abs(YY - 113) / 76.0, 0, 1) ** 0.8
    fld = 0.42 * noise * (1 - 0.8 * snr) + snr * f
    fld = row_smear(fld, rng, 30, (0, H), 30, 200, desc=bool(rng.random() < 0.5))
    fld = tear(fld, rng, 3, 10, 2, 5, (80, 150))
    scr = q_diffuse(fld, "fs", 0, 6)
    tag(scr, "DRUM MACHINE", 202, 5)
    return scr


def v09_echo(rng):
    """ECHO STACK. The word and its delay taps below, each tap smaller, darker, torn harder and
    dithered sparser; the last taps drip sorted columns."""
    scr = np.zeros((H, W), np.uint8)
    taps = [(6, 1.0, 22, 70, None), (5, 0.9, 92, 40, BAYER4), (4, 0.75, 132, 28, BAYER8),
            (3, 0.6, 160, 20, BAYER8), (2, 0.5, 180, 14, BAYER4)]
    for i, (lv, amt, y, h, mat) in reversed(list(enumerate(taps))):
        f = word(210, h, 15 + (6 * i if i % 2 else -4 * i), y, 3 if i == 0 else 1)
        if mat is None:
            over(scr, (f > 0.5).astype(np.uint8) * lv)
            continue
        f = tear(f, rng, i + 1, 3 + 3 * i, 1, 3, (y, y + h))
        if i >= 3:
            f = np.maximum(f, streaks(f, rng, 2, 16, cells=40, gain=0.6))
        q = q_ordered(f * amt, mat, 0, lv)
        over(scr, q, (q > 0) & (scr == 0))
    tag(scr, "DRUM MACHINE", 214, 3)
    return scr


def v10_cathedral(rng):
    """CATHEDRAL. The word stretched tall like a black-metal logo, sorted up into spires and down
    into roots, mirrored left to right; Bayer 8; one tear across the nave."""
    f = word(226, 104, 7, 68)
    up = streaks(f, rng, 4, 66, up=True, cells=44)
    dn = streaks(f, rng, 4, 60, cells=44)
    sp = np.maximum(up, dn)
    sp = np.maximum(sp, sp[:, ::-1] * 0.8)
    sp = tear(sp, rng, 4, 3, 1, 3)
    scr = q_ordered(sp, BAYER8, 0, 5)
    k = tear(f, rng, 1, 5, 4, 6, (110, 130)) > 0.5
    scr[k] = 5
    scr[k & ~np.roll(k, 1, 1)] = 6
    scr[k & ~np.roll(k, 1, 0)] = 6
    tag(scr, "DRUM MACHINE", 4, 3)
    return scr


# ---------------------------------------------------------------- the second ten: legibility optional

def tag_mask(text, x=None):
    f = small_font()
    l, t, r, b = f.getbbox(text)
    im = Image.new("L", (W, 16), 0)
    ImageDraw.Draw(im).text(((W - (r - l)) // 2 if x is None else x, 0), text, font=f, fill=255)
    return np.asarray(im, np.float32) / 255.0


def wrecked_tag(scr, text, y, level, rng, shift=10, sort=True, mirror=False):
    """the label torn into slices and sorted along its rows (it may stop reading; fine)"""
    m = tag_mask(text)
    if mirror:
        m = m[:, ::-1]
    m = tear(np.pad(m, ((0, H - 16), (0, 0))), rng, 5, shift, 1, 4, (0, 16))[:16]
    if sort:
        m = row_smear(np.pad(m * (0.5 + 0.5 * rng.random(m.shape)), ((0, H - 16), (0, 0))), rng, 6, (0, 16),
                      20, 90)[:16]
    sl = scr[y:y + 16]
    sl[m > 0.45] = level
    return scr


def thorns(m, rng, n, maxlen, up=True, down=True):
    """black-metal spikes: from random ink-edge pixels, tapering single-pixel spikes up/down,
    leaning a little"""
    out = m.copy()
    edge_up = m & ~np.roll(m, 1, 0)
    edge_dn = m & ~np.roll(m, -1, 0)
    for e, d, on in ((edge_up, -1, up), (edge_dn, 1, down)):
        if not on:
            continue
        ys, xs = np.where(e)
        if not len(ys):
            continue
        for k in rng.choice(len(ys), min(n, len(ys)), replace=False):
            y, x = ys[k], xs[k]
            ln = int(maxlen * rng.random() ** 1.5) + 3
            lean = rng.uniform(-0.35, 0.35)
            for s in range(ln):
                yy, xx = y + d * s, int(round(x + lean * s))
                if not (0 <= yy < H and 0 <= xx < W):
                    break
                wdt = max(0, int(2 * (1 - s / ln)))
                out[yy, max(0, xx - wdt):xx + wdt + 1] = True
    return out


def v11_thorns(rng):
    """THORNS (Metal Mania). The word grown into a black-metal logo: hundreds of tapering spikes,
    folded into perfect left-right symmetry, sorted drips under a Bayer fog; the label mirrored."""
    f = word(228, 70, 6, 82, 3, "metalmania") > 0.5
    t = thorns(f, rng, 260, 70)
    t = t | t[:, ::-1]
    sym = t.astype(np.float32)
    fog = np.clip(1 - np.abs(YY - 118) / 110.0, 0, 1) * 0.35 * (0.6 + 0.4 * smooth_noise(rng, W, 9)[None, :])
    fog = sort_runs(fog + 0.15 * rng.random((H, W)), rng.random((H, W)) < 0.97, axis=0, desc=True) * 0.6
    scr = q_ordered(fog, BAYER8, 0, 2)
    d = streaks(sym, rng, 10, 90, cells=50, gain=0.8)
    over(scr, q_ordered(d, BAYER4, 0, 4), d > 0.05)
    scr[t] = 5
    scr[f | f[:, ::-1]] = 6
    wrecked_tag(scr, "DRUM MACHINE", 222, 2, rng, mirror=True)
    return scr


def polar(strip, r0, r1, cx=120, cy=120, turns=1.0, phase=0.0):
    """wrap a (h, w) strip around a ring: strip top at r1 (outside), bottom at r0"""
    h, w = strip.shape
    dx, dy = XX - cx + 0.5, YY - cy + 0.5
    r = np.sqrt(dx * dx + dy * dy)
    th = (np.arctan2(dx, -dy) / (2 * np.pi) + 1 + phase) % 1.0
    sx = (th * turns * w).astype(int) % w
    sy = ((r1 - r) / (r1 - r0) * h).astype(int)
    ok = (sy >= 0) & (sy < h)
    out = np.zeros((H, W), np.float32)
    out[ok] = strip[sy[ok], sx[ok]]
    return out


def v12_sigil(rng):
    """SIGIL (UnifrakturCook). The word twice round a ring, its letters sorted outward into a
    corona of rays; a Bayer black sun in the middle; the ring torn."""
    s = logo(300, 46, 3, "cook")
    strip = np.zeros((150, 640), np.float32)
    strip[104:150, 10:310] = s
    strip[104:150, 330:630] = s
    rays = np.zeros_like(strip)
    ink = strip > 0.5
    for x in range(strip.shape[1]):
        ys = np.where(ink[:, x])[0]
        if len(ys) and rng.random() < 0.8:
            n = int(min(ys[0], 10 + 94 * rng.random() ** 1.3))
            rays[ys[0] - n:ys[0], x] = np.sort(rng.random(n)) * np.linspace(0.1, 1, n)
    ph = rng.random()
    ring = polar(strip, 52, 118, phase=ph)
    cor = polar(rays, 52, 118, phase=ph)
    r = np.sqrt((XX - 120) ** 2 + (YY - 120) ** 2)
    sun = np.clip(1 - r / 52.0, 0, 1) ** 0.7 * 0.55
    scr = q_ordered(sun, BAYER8, 0, 3)
    over(scr, q_ordered(cor * 0.9, BAYER4, 0, 4), cor > 0.05)
    k = tear(ring, rng, 6, 8, 1, 4) > 0.5
    scr[k] = 6
    scr[(np.abs(r - 50) < 1.0)] = 5
    scr[(np.abs(r - 119) < 0.7)] = 2
    return scr


def v13_slitscan(rng):
    """SLIT-SCAN (Blaka Ink). The word sampled through moving slits: every column slides on its
    own sine, every row on another; then the whole screen column-sorted; Floyd-Steinberg."""
    a = logo(236, 110, 3, "blakaink")
    big = np.zeros((H, W), np.float32)
    paste(big, a, 2, 65)
    yoff = (26 * np.sin(XX[0] / 13.0) + 14 * np.sin(XX[0] / 4.1 + 1) * (rng.random(W) < 0.3)).astype(int)
    xoff = (12 * np.sin(YY[:, 0] / 9.0 + 2)).astype(int)
    warped = big[(YY - yoff[None, :]) % H, (XX - xoff[:, None]) % W]
    fld = warped * 0.9 + 0.12 * rng.random((H, W))
    m = (fld > 0.3 + 0.5 * smooth_noise(rng, W, 30)[None, :]) | (rng.random((H, W)) < 0.0)
    m = np.maximum.accumulate(m[::-1], axis=0)[::-1] & (rng.random((1, W)) < 0.55)
    fld = sort_runs(fld, m, axis=0, desc=False)
    scr = q_diffuse(fld, "fs", 0, 6)
    wrecked_tag(scr, "DRUM MACHINE", 6, 4, rng, shift=30)
    return scr


def v14_collapse(rng):
    """CRT COLLAPSE (Texturina Black). The picture caving in: the word squashed through five
    frames down to one white scan line that explodes sideways in sorted streaks."""
    scr = np.zeros((H, W), np.uint8)
    frames = [(150, 1), (90, 2), (48, 3), (22, 4), (8, 5)]
    for hgt, lv in frames:
        f = word(232, hgt, 4, 120 - hgt // 2, 3, "texturina")
        f = tear(f, rng, 3, 4 + 30 // lv, 1, 3)
        q = q_ordered(f * (0.3 + 0.14 * lv), BAYER4 if lv % 2 else BAYER8, 0, lv)
        over(scr, q, q > 0)
    line = np.zeros((H, W), np.float32)
    line[117:123] = np.clip(1 - np.abs(XX[117:123] - 120) / 125.0, 0, 1) ** 0.4
    line = row_smear(line + 0.3 * rng.random((H, W)) * (np.abs(YY - 120) < 4), rng, 16, (114, 126), 60, 240)
    over(scr, q_ordered(line, BAYER4, 0, 6), line > 0.4)
    scr[119:121, 40:200] = 6
    scr[118:122, 112:128] = 6
    wrecked_tag(scr, "DRUM MACHINE", 212, 1, rng, shift=4)
    return scr


def v15_tunnel(rng):
    """FEEDBACK TUNNEL (Fruktur). Camera pointed at its own monitor: the word nested inside itself
    nine times, each copy turned, smaller, brighter, Bayer-screened; the outer copies torn."""
    scr = np.zeros((H, W), np.uint8)
    base = Image.fromarray((logo(236, 100, 3, "fruktur") * 255).astype(np.uint8))
    for k in range(9):
        s = 2.4 * 0.72 ** k
        w, h = int(236 * s), int(100 * s)
        if w < 12:
            break
        im = base.resize((w, h), Image.LANCZOS).rotate(k * 7 - 20, Image.BICUBIC, expand=True)
        a = np.asarray(im, np.float32) / 255.0
        f = np.zeros((H, W), np.float32)
        paste(f, a, 120 - a.shape[1] // 2 + int(4 * k), 120 - a.shape[0] // 2 - int(3 * k))
        if k < 3:
            f = tear(f, rng, 8, 18, 2, 8)
        lv = min(5, 1 + k)
        q = q_ordered(f * (0.45 + 0.07 * k), BAYER8 if k % 2 else BAYER4, 0, lv)
        scr[f > 0.35] = 0
        over(scr, q, q > 0)
        if k == 4:                                            # the copy in focus: solid white, black rim
            core = f > 0.5
            scr[outline(core, 1)] = 0
            scr[core] = 6
    return scr


def v16_kick(rng):
    """KICK (Eater). Every row of the word pushed sideways by a kick drum: a sine whose pitch and
    level fall down the screen; the hard-pushed rows sorted into smears; the waveform itself
    drawn down the edge."""
    f = word(220, 150, 10, 46, 3, "eater")
    y = np.arange(H)
    env = np.exp(-np.clip(y - 30, 0, None) / 70.0) * (y > 30)
    phase = np.cumsum(2 * np.pi * (0.02 + 0.10 * np.exp(-np.clip(y - 30, 0, None) / 30.0)))
    wave = env * np.sin(phase)
    off = (60 * wave).astype(int)
    out = np.stack([np.roll(f[r], off[r]) for r in range(H)])
    hard = np.abs(wave) > 0.45
    m = np.zeros((H, W), bool)
    m[hard] = True
    out = sort_runs(out * (0.55 + 0.45 * rng.random((H, W))), m & (rng.random((H, 1)) < 0.7), axis=1,
                    desc=bool(rng.random() < 0.5))
    scr = q_ordered(out, BAYER4, 0, 6)
    scr[(out > 0.5) & ~hard[:, None]] = 6
    for r in range(H):                                      # the waveform, down the right edge
        x = int(226 + 12 * wave[r])
        scr[r, min(x, 238):min(x, 238) + 2] = 4
    wrecked_tag(scr, "DRUM MACHINE", 222, 3, rng)
    return scr


def v17_macro(rng):
    """MACRO (Jacquarda Bastarda 9). The pixel blackletter blown up until its pixels are bricks,
    cropped to the middle of the word, every brick filled with its own Bayer level, then the
    whole thing sorted along the diagonals."""
    a = logo(600, 300, 1, "bastarda", aa=False)
    f = (a[30:270, 170:410] > 0.5).astype(np.float32)
    blk = 6
    lvl = rng.random((H // blk + 1, W // blk + 1))
    fill = np.kron(lvl, np.ones((blk, blk)))[:H, :W]
    fld = f * (0.35 + 0.65 * fill)
    sh = np.stack([np.roll(fld[r], r) for r in range(H)])             # shear: diagonals become columns
    m = (sh > 0.1) & (rng.random((1, W)) < 0.5)
    m = np.maximum.accumulate(m, axis=0) & (rng.random((1, W)) < 0.6)
    sh = sort_runs(sh, m, axis=0, desc=True)
    fld = np.stack([np.roll(sh[r], -r) for r in range(H)])
    scr = q_ordered(fld, BAYER4, 0, 6)
    scr[(f > 0.5) & (fill > 0.85)] = 6
    return scr


def v18_xor(rng):
    """XOR (Blaka Hollow). The outline word XORed with a shifted copy of itself and with
    concentric rings, so it dissolves into moire; a Bayer glow under it; torn."""
    a = word(232, 120, 4, 60, 1, "blakahollow") > 0.5
    b = np.roll(np.roll(a, 7, 1), 5, 0)
    r = np.sqrt((XX - 120) ** 2 + (YY - 132) ** 2)
    rings = ((r // 5) % 2).astype(bool) & (r < 118)
    x = a ^ b ^ (rings & (np.abs(YY - 120) < 64))
    x = tear(x.astype(np.float32), rng, 9, 14, 2, 8) > 0.5
    glow = np.clip(1 - r / 120.0, 0, 1) ** 1.2
    scr = np.where(x, q_ordered(glow, BAYER8, 2, 6), q_ordered(glow * 0.35, BAYER4, 0, 2)).astype(np.uint8)
    scr[a & ~b] = 6
    wrecked_tag(scr, "DRUM MACHINE", 216, 5, rng, shift=20)
    return scr


def v19_waterfall(rng):
    """WATERFALL (Nosifer). The dripping word at the top and the whole screen below it column-
    sorted into a falling sheet, broken by datamosh blocks; Floyd-Steinberg."""
    f = word(232, 64, 4, 18, 3, "nosifer")
    sheet = 0.5 * rng.random((H, W)) * np.clip((YY - 40) / 60.0, 0, 1)
    m = np.maximum.accumulate(f > 0.5, axis=0) & (rng.random((1, W)) < 0.75)
    sheet = sort_runs(sheet, m, axis=0, desc=True)              # each column's noise falls in order
    d = streaks(f, rng, 60, 230, cells=70, gain=1.0)
    fld = np.maximum(np.maximum(f, d), sheet * 0.8)
    for _ in range(26):
        by, bx = int(rng.integers(10, 29)) * 8, int(rng.integers(0, 30)) * 8
        sy, sx = int(rng.integers(2, 10)) * 8, int(rng.integers(0, 30)) * 8
        fld[by:by + 8, bx:bx + 8] = fld[sy:sy + 8, sx:sx + 8] * rng.uniform(0.6, 1.3)
    fld = tear(fld, rng, 12, 24, 1, 6, (90, 240))
    scr = q_diffuse(fld, "fs", 0, 6)
    scr[f > 0.5] = 6
    return scr


def v20_wall(rng):
    """WALL (Pirata One). The word tiled down the screen like fly-posters, every row broken a
    different way: torn, sorted, inverted, dithered, dripping; one row left whole and white."""
    scr = np.zeros((H, W), np.uint8)
    rows = 7
    hgt = H // rows
    a = logo(150, hgt - 4, 3, "pirata")
    keep = 3
    for i in range(rows):
        f = np.zeros((H, W), np.float32)
        x0 = -((i * 53) % 150)
        for k in range(3):
            paste(f, a, x0 + k * 154, i * hgt + 2)
        band = (YY >= i * hgt) & (YY < (i + 1) * hgt)
        mode = (i * 3 + 1) % 5 if i != keep else -1
        if mode == 0:
            q = q_ordered(tear(f, rng, 6, 30, 1, 4, (i * hgt, (i + 1) * hgt)) * 0.7, BAYER4, 0, 4)
        elif mode == 1:
            s = row_smear(f * (0.4 + 0.6 * rng.random((H, W))), rng, 14, (i * hgt, (i + 1) * hgt), 40, 200)
            q = q_ordered(s, BAYER8, 0, 5)
        elif mode == 2:
            q = np.where(f > 0.5, 0, q_ordered(np.full((H, W), 0.55), BAYER8, 0, 3)).astype(np.uint8)
        elif mode == 3:
            q = q_diffuse(f * 0.6, "atkinson", 0, 4)
        elif mode == 4:
            d = streaks(f, rng, 4, hgt, cells=60, gain=0.7) * band
            q = q_ordered(np.maximum(f * 0.5, d), BAYER4, 0, 3)
        else:
            q = (f > 0.5).astype(np.uint8) * 6
        scr[band] = q[band]
    wrecked_tag(scr, "DRUM MACHINE DRUM MACHINE", keep * hgt + hgt - 2, 0, rng, shift=2, sort=False)
    return scr


SCREENS = [v01_melt, v02_tear, v03_halo, v04_sorted_field, v05_reflection,
           v06_datamosh, v07_band, v08_static, v09_echo, v10_cathedral,
           v11_thorns, v12_sigil, v13_slitscan, v14_collapse, v15_tunnel,
           v16_kick, v17_macro, v18_xor, v19_waterfall, v20_wall]


def colourise(scr, pal):
    lut = np.array([(0, 0, 0)] + PALETTES[pal] + [(255, 255, 255)], np.uint16)
    rgb = lut[scr]
    rgb[..., 0] = (rgb[..., 0] >> 3) * 255 // 31          # through RGB565, as the panel shows it
    rgb[..., 1] = (rgb[..., 1] >> 2) * 255 // 63
    rgb[..., 2] = (rgb[..., 2] >> 3) * 255 // 31
    return Image.fromarray(rgb.astype(np.uint8))


def main():
    global OUT
    OUT = Path(sys.argv[1])
    OUT.mkdir(parents=True, exist_ok=True)
    only = {int(a) for a in sys.argv[2:]}
    for i, fn in enumerate(SCREENS, 1):
        if only and i not in only:
            continue
        scr = fn(np.random.default_rng(1000 + i))
        assert scr.shape == (H, W) and scr.max() <= 6
        Image.fromarray(scr).save(OUT / f"v{i:02d}.png")
        for p in PALETTES:
            colourise(scr, p).save(OUT / f"v{i:02d}_{p}.png")
        print(f"v{i:02d} {fn.__name__[4:]}: levels {np.unique(scr).tolist()}")


if __name__ == "__main__":
    main()
