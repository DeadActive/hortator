/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Small-canvas renderer (no full framebuffer). Draw text/lines into
 * an off-screen strip, then blit it in one DMA transfer. Pixels are stored
 * byte-swapped (the panel takes RGB565 big-endian). */
typedef struct {               /* proportional, see tools/gen_font.py */
    uint8_t h;
    uint8_t pad;               /* bitmap starts this many pixels left of the pen */
    uint8_t first, last;
    const uint8_t *adv;        /* advance per glyph */
    const uint8_t *bw;         /* bitmap width per glyph (starts FONT_PAD left of the pen) */
    const uint16_t *off;       /* byte offset of each glyph */
    const uint8_t *data;
} felucca_font_t;
typedef struct {               /* 1-bit Machinedrum-style font (assets/fonts/md_font.txt, tools/gen_font.py) */
    uint8_t h, first, last;
    const uint8_t *w;          /* glyph width in px (0: no glyph) */
    const uint8_t *rows;       /* rows[i * h + r]: bit (w - 1 - c) = column c */
} md_font_t;
#include "felucca_font.h"
#include "felucca_logo.h"         /* the HortatoR logo (tools/gen_logo.py) */
#include "felucca_boot.h"         /* the start screen (tools/gen_boot.py) */

#define CV_MAX (240u * 124u)      /* the graph strip is 240 x 124 */
static uint16_t cv_px[CV_MAX] __attribute__((section(".pool")));
static uint32_t cv_w, cv_h;
static int32_t cv_oy;            /* y offset for graph drawing */
static int32_t cv_sy;            /* the screen row of canvas row 0 (cv_begin_at): dither lines up across strips */

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define C_BLACK 0x0000u
#define C_WHITE 0xFFFFu              /* accent only: what is being touched / where we are */
/* The screen is five steps of one colour, darkest to brightest, plus white.
 * Palettes are picked in the HOME-hold menu (COLOR). */
typedef struct {
    const char *name;
    uint16_t c[5];
} palette_t;
static const palette_t PALETTES[] = {
    {"GREEN", {RGB(0, 40, 12), RGB(0, 84, 30), RGB(16, 140, 54), RGB(56, 200, 92), RGB(120, 255, 146)}},
    {"AMBER", {RGB(60, 26, 0), RGB(110, 50, 0), RGB(170, 82, 0), RGB(225, 120, 8), RGB(255, 166, 40)}},
    {"CYAN", {RGB(0, 30, 50), RGB(0, 62, 96), RGB(16, 112, 160), RGB(56, 172, 222), RGB(140, 222, 255)}},
    {"RED", {RGB(52, 8, 8), RGB(100, 18, 14), RGB(170, 36, 26), RGB(226, 64, 48), RGB(255, 112, 92)}},
    {"MONO", {RGB(40, 40, 40), RGB(80, 80, 80), RGB(130, 130, 130), RGB(186, 186, 186), RGB(226, 226, 226)}},
};
#define NPALETTES (sizeof(PALETTES) / sizeof(PALETTES[0]))
static uint16_t pal[5];
#define C_LINE pal[0]                /* 1 rules, separators */
#define C_DIM pal[1]                 /* 2 inactive, empty steps, units */
#define C_GRAY pal[2]                /* 3 labels */
#define C_AMB pal[3]                 /* 4 secondary text */
#define C_HI pal[4]                  /* 5 values, curves */

static void palette_set(uint32_t i)
{
    uint32_t k;
    for (k = 0; k < 5u; k++)
        pal[k] = PALETTES[i % NPALETTES].c[k];
}

/* Machinedrum-style themes (spec 2026-10-19-md-ui §2): two colours, BG and INK. Stored in settings.palette bits 0..7
 * as THEME_TAG | index; the old palette ids 0..4 (GREEN AMBER CYAN RED MONO) map on load. */
typedef struct {
    const char *name;
    uint16_t bg, ink;
} theme_t;
static const theme_t THEMES[] = {
    {"MD RED", RGB(0xe0, 0x47, 0x2b), RGB(0x38, 0x10, 0x0a)},
    {"RED ON BLACK", RGB(0x17, 0x06, 0x05), RGB(0xf0, 0x53, 0x2f)},
    {"WHITE ON BLUE", RGB(0x24, 0x45, 0xc8), RGB(0xea, 0xf1, 0xff)},
    {"BLACK ON GREEN", RGB(0xa3, 0xbf, 0x3c), RGB(0x1c, 0x2a, 0x08)},
};
#define NTHEMES (sizeof(THEMES) / sizeof(THEMES[0]))
#define THEME_TAG 0x10u
static uint16_t C_BG, C_INK;

static uint32_t theme_index(uint32_t stored)
{
    static const uint8_t OLD[5] = {3, 0, 2, 0, 1};   /* GREEN AMBER CYAN RED MONO */
    stored &= 0xFFu;
    if (stored < 5u)
        return OLD[stored];
    if ((stored & ~0xFu) == THEME_TAG && (stored & 0xFu) < NTHEMES)
        return stored & 0xFu;
    return 0;
}

static uint16_t mix565(uint16_t a, uint16_t b, uint32_t pct)   /* a .. b, pct of b */
{
    uint32_t r = ((a >> 11) * (100u - pct) + (b >> 11) * pct) / 100u;
    uint32_t g = (((a >> 5) & 63u) * (100u - pct) + ((b >> 5) & 63u) * pct) / 100u;
    uint32_t l = ((a & 31u) * (100u - pct) + (b & 31u) * pct) / 100u;
    return (uint16_t)(r << 11 | g << 5 | l);
}

/* the theme of a stored COLOR id; also the old five-step pal[] as a BG..INK ramp for the screens not yet redrawn */
static void theme_set(uint32_t stored)
{
    const theme_t *t = &THEMES[theme_index(stored)];
    uint32_t k;
    C_BG = t->bg;
    C_INK = t->ink;
    for (k = 0; k < 5u; k++)
        pal[k] = mix565(t->bg, t->ink, 20u * (k + 1u));
}

static inline uint16_t swap16(uint32_t c) { return (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8)); }

static void cv_begin(uint32_t w, uint32_t h, uint16_t bg)
{
    uint32_t i, n;
    if (w * h > CV_MAX)
        h = CV_MAX / w;
    lcd_sync();                     /* the last blit may still read cv_px */
    cv_w = w;
    cv_h = h;
    cv_sy = 0;
    n = w * h;
    for (i = 0; i < n; i++)
        cv_px[i] = swap16(bg);
}

static void cv_blit(uint32_t x, uint32_t y) { lcd_blit(x, y, cv_w, cv_h, cv_px); }

/* canvas rows r0 .. cv_h-1 only, to screen row y + r0 */
static void cv_blit_from(uint32_t x, uint32_t y, uint32_t r0)
{
    if (r0 < cv_h)
        lcd_blit(x, y + r0, cv_w, cv_h - r0, cv_px + r0 * cv_w);
}

static inline void cv_pset(int32_t x, int32_t y, uint16_t c)
{
    y += cv_oy;
    if ((uint32_t)x < cv_w && (uint32_t)y < cv_h)
        cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16(c);
}

static void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t x1 = x + w, y1 = y + h + cv_oy, i;
    uint16_t sc = swap16(c);
    y += cv_oy;                             /* clipped once, then filled row by row */
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 > (int32_t)cv_w)
        x1 = (int32_t)cv_w;
    if (y1 > (int32_t)cv_h)
        y1 = (int32_t)cv_h;
    for (; y < y1; y++)
        for (i = x; i < x1; i++)
            cv_px[(uint32_t)y * cv_w + (uint32_t)i] = sc;
}

static void cv_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, guard = 2000;
    while (guard--) {                       /* bounded: a line is never longer than 480 px */
        int32_t e2 = 2 * err;               /* both tests use the same error value */
        cv_pset(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* a dotted line: every other pixel along cv_line's path, from the first */
static void cv_line_dot(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, guard = 2000, k = 0;
    while (guard--) {
        int32_t e2 = 2 * err;
        if (!(k++ & 1))
            cv_pset(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* glyph index of a character: lower case folds to upper case when the font
 * has none, anything missing (and C1 controls) draws as '?' */
static uint32_t glyph(const felucca_font_t *f, uint32_t ch)
{
    if (ch >= 'a' && ch <= 'z' && f->last < 'a')
        ch -= 32u;
    if (ch < f->first || ch > f->last || (ch >= 127u && ch < 160u))
        ch = '?';
    return ch - f->first;
}

/* text, alpha-blended onto black with colour c; returns the end x */
static int32_t cv_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    uint16_t ramp[16];
    uint32_t r = c >> 11, g = (c >> 5) & 63u, b = c & 31u, a;
    for (a = 0; a < 16u; a++)
        ramp[a] = (uint16_t)(((r * a / 15u) << 11) | ((g * a / 15u) << 5) | (b * a / 15u));
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), gx, gy, w, bpr;
        const uint8_t *gd;
        w = f->bw[gi];
        bpr = (w + 1u) / 2u;
        gd = f->data + f->off[gi];
        for (gy = 0; gy < f->h; gy++)
            for (gx = 0; gx < w; gx++) {
                uint32_t v = gd[gy * bpr + gx / 2u];
                v = (gx & 1u) ? (v & 15u) : (v >> 4);
                if (v)
                    cv_pset(x - f->pad + (int32_t)gx, y + (int32_t)gy, ramp[v]);
            }
        x += f->adv[gi];
    }
    return x;
}

/* the HortatoR logo (LOGO_W x LOGO_H), alpha-blended onto black with colour c, top left at x, y */
static void cv_logo(int32_t x, int32_t y, uint16_t c)
{
    uint16_t ramp[16];
    uint32_t r = c >> 11, g = (c >> 5) & 63u, b = c & 31u, a, gx, gy;
    for (a = 0; a < 16u; a++)
        ramp[a] = (uint16_t)(((r * a / 15u) << 11) | ((g * a / 15u) << 5) | (b * a / 15u));
    for (gy = 0; gy < LOGO_H; gy++)
        for (gx = 0; gx < LOGO_W; gx++) {
            uint32_t v = LOGO_DATA[gy * ((LOGO_W + 1u) / 2u) + gx / 2u];
            v = (gx & 1u) ? (v & 15u) : (v >> 4);
            if (v)
                cv_pset(x + (int32_t)gx, y + (int32_t)gy, c == C_INK ? (v > 7u ? C_INK : C_BG) : ramp[v]);
        }
}

/* start screen rows y0 .. y0 + cv_h - 1 onto the canvas (cv_begin(240, rows) first): level 0 black, 1..5 the
 * palette's steps, 6 white */
static void cv_boot(uint32_t y0)
{
    uint16_t lut[16] = {0};                 /* 7..15 never occur (gen_boot.py checks); black if they did */
    uint32_t k, i, n = cv_w * cv_h;
    const uint8_t *src = BOOT_DATA + y0 * (BOOT_W / 2u);
    for (k = 0; k < 5u; k++)
        lut[k + 1u] = swap16(pal[k]);
    lut[6] = swap16(C_WHITE);
    for (i = 0; i < n; i += 2u) {
        uint32_t v = src[i / 2u];
        cv_px[i] = lut[v >> 4];
        cv_px[i + 1u] = lut[v & 15u];
    }
}

static int32_t text_w(const felucca_font_t *f, const char *s)
{
    int32_t w = 0;
    for (; *s; s++)
        w += f->adv[glyph(f, (uint8_t)*s)];
    return w;
}

/* MD glyph index of a character: lower case folds to caps in a font with caps, Ü / ü draw as U; a glyph the font
 * lacks draws as its '?' (or nothing when it has none). 0: nothing to draw. */
static int md_glyph(const md_font_t *f, uint32_t ch, uint32_t *gi)
{
    if (ch >= 'a' && ch <= 'z' && f->first <= 'A' && f->last >= 'Z')
        ch -= 32u;
    else if (ch == 0xDCu || ch == 0xFCu)
        ch = 'U';
    if (ch < f->first || ch > f->last || !f->w[ch - f->first]) {
        if ('?' < f->first || '?' > f->last || !f->w['?' - f->first])
            return 0;
        ch = '?';
    }
    *gi = ch - f->first;
    return 1;
}

/* 1-bit text: font pixels as sc x sc blocks of colour c (the background is left as it is); returns the pen x after
 * the string (each glyph advances its width + 1, times sc) */
static int32_t md_text(int32_t x, int32_t y, const md_font_t *f, uint32_t sc, const char *s, uint16_t c)
{
    for (; *s; s++) {
        uint32_t gi, w, r, k;
        if (!md_glyph(f, (uint8_t)*s, &gi))
            continue;
        w = f->w[gi];
        for (r = 0; r < f->h; r++) {
            uint32_t bits = f->rows[gi * f->h + r];
            for (k = 0; k < w; k++)
                if (bits >> (w - 1u - k) & 1u)
                    cv_rect(x + (int32_t)(k * sc), y + (int32_t)(r * sc), (int32_t)sc, (int32_t)sc, c);
        }
        x += (int32_t)((w + 1u) * sc);
    }
    return x;
}

/* the width md_text draws, without the gap after the last glyph */
static int32_t md_text_w(const md_font_t *f, uint32_t sc, const char *s)
{
    int32_t w = 0;
    for (; *s; s++) {
        uint32_t gi;
        if (md_glyph(f, (uint8_t)*s, &gi))
            w += (int32_t)((f->w[gi] + 1u) * sc);
    }
    return w ? w - (int32_t)sc : 0;
}

/* ---- Machinedrum-style primitives (spec 2026-10-19-md-ui §4); all 1 px, two colours */

/* a canvas that will be blitted at screen row sy (so cv_dither's pattern is continuous across strips) */
static void cv_begin_at(uint32_t w, uint32_t h, uint32_t sy, uint16_t bg)
{
    cv_begin(w, h, bg);
    cv_sy = (int32_t)sy;
}

static void cv_frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t ink)
{
    cv_rect(x, y, w, 1, ink);
    cv_rect(x, y + h - 1, w, 1, ink);
    cv_rect(x, y, 1, h, ink);
    cv_rect(x + w - 1, y, 1, h, ink);
}

static void cv_hdot(int32_t x0, int32_t x1, int32_t y, uint16_t ink)   /* 1 on, 1 off from x0 */
{
    for (; x0 <= x1; x0 += 2)
        cv_pset(x0, y, ink);
}

static void cv_vdot(int32_t x, int32_t y0, int32_t y1, uint16_t ink)
{
    for (; y0 <= y1; y0 += 2)
        cv_pset(x, y0, ink);
}

/* the MD's grey: ink where screen x + y is even */
static void cv_dither(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t ink)
{
    int32_t i, j;
    for (j = y; j < y + h; j++)
        for (i = x + ((x + j + cv_oy + cv_sy) & 1); i < x + w; i += 2)
            cv_pset(i, j, ink);
}

static void cv_ring(int32_t cx, int32_t cy, int32_t r, uint16_t ink)   /* midpoint circle */
{
    int32_t x = r, y = 0, e = 1 - r;
    while (x >= y) {
        cv_pset(cx + x, cy + y, ink);
        cv_pset(cx + y, cy + x, ink);
        cv_pset(cx - y, cy + x, ink);
        cv_pset(cx - x, cy + y, ink);
        cv_pset(cx - x, cy - y, ink);
        cv_pset(cx - y, cy - x, ink);
        cv_pset(cx + y, cy - x, ink);
        cv_pset(cx + x, cy - y, ink);
        y++;
        if (e < 0)
            e += 2 * y + 1;
        else {
            x--;
            e += 2 * (y - x) + 1;
        }
    }
}

/* sin of a whole degree, Q10 */
static int32_t isin(int32_t deg)
{
    static const uint16_t Q[91] = {
        0, 18, 36, 54, 71, 89, 107, 125, 143, 160, 178, 195, 213, 230, 248, 265, 282, 299, 316, 333, 350, 367, 384,
        400, 416, 433, 449, 465, 481, 496, 512, 527, 543, 558, 573, 587, 602, 616, 630, 644, 658, 672, 685, 698, 711,
        724, 737, 749, 761, 773, 784, 796, 807, 818, 828, 839, 849, 859, 868, 878, 887, 896, 904, 912, 920, 928, 935,
        943, 949, 956, 962, 968, 974, 979, 984, 989, 994, 998, 1002, 1005, 1008, 1011, 1014, 1016, 1018, 1020, 1022,
        1023, 1023, 1024, 1024};
    deg %= 360;
    if (deg < 0)
        deg += 360;
    if (deg <= 90)
        return Q[deg];
    if (deg <= 180)
        return Q[180 - deg];
    if (deg <= 270)
        return -(int32_t)Q[deg - 180];
    return -(int32_t)Q[360 - deg];
}

/* the point at deg, half-pixel radius d2 / 2, from cx, cy (y up), rounded half away from zero */
static void polar(int32_t cx, int32_t cy, int32_t deg, int32_t d2, int32_t *x, int32_t *y)
{
    int32_t qx = isin(deg + 90) * d2, qy = isin(deg) * d2;
    *x = cx + (qx >= 0 ? qx + 1024 : qx - 1024) / 2048;
    *y = cy - (qy >= 0 ? qy + 1024 : qy - 1024) / 2048;
}

/* the MD knob: ring, 12 o'clock tick, end stops at 7:30 / 4:30, the value (0..1000) as a 3x3 blob on the 270 degree
 * sweep inside the ring; lfo (0..1000, < 0 none): the live modulated value as a hollow 3x3 dot outside it */
static void cv_knob(int32_t cx, int32_t cy, int32_t r, int32_t v, int32_t lfo, uint16_t ink)
{
    int32_t x, y;
    cv_ring(cx, cy, r, ink);
    cv_pset(cx, cy - r - 2, ink);
    polar(cx, cy, 225, 2 * (r + 2), &x, &y);
    cv_rect(x - 1, y, 2, 1, ink);
    polar(cx, cy, -45, 2 * (r + 2), &x, &y);
    cv_rect(x - 1, y, 2, 1, ink);
    v = v < 0 ? 0 : v > 1000 ? 1000 : v;
    polar(cx, cy, 225 - v * 270 / 1000, 2 * r - 7, &x, &y);
    cv_rect(x - 1, y - 1, 3, 3, ink);
    if (lfo >= 0) {
        lfo = lfo > 1000 ? 1000 : lfo;
        polar(cx, cy, 225 - lfo * 270 / 1000, 2 * (r + 3), &x, &y);
        cv_frame(x - 1, y - 1, 3, 3, ink);
    }
}

/* inverted title tab: ink box 13 px tall, the text in bg; returns its width */
static int32_t cv_tab(int32_t x, int32_t y, const char *s, uint16_t ink, uint16_t bg)
{
    int32_t w = md_text_w(&MD_F3, 2, s) + 6;
    cv_rect(x, y, w, 13, ink);
    md_text(x + 3, y + 2, &MD_F3, 2, s, bg);
    return w;
}

/* MD window: bg inside, 1 px top / left edge, 2 px right / bottom edge (the drop shadow, to x + w, y + h), notched
 * corners; a title (if any) as a tab centred on the top edge */
static void cv_popup(int32_t x, int32_t y, int32_t w, int32_t h, const char *title, uint16_t ink, uint16_t bg)
{
    cv_rect(x, y, w, h, bg);
    cv_rect(x, y, w, 1, ink);
    cv_rect(x, y, 1, h, ink);
    cv_rect(x + w - 2, y, 2, h + 1, ink);
    cv_rect(x, y + h - 1, w, 2, ink);
    cv_rect(x, y, 1, 1, bg);
    cv_rect(x + w - 1, y, 1, 1, bg);
    cv_rect(x, y + h, 1, 1, bg);
    if (title && title[0]) {
        int32_t tw = md_text_w(&MD_F3, 2, title) + 12, tx = x + w / 2 - tw / 2;
        cv_rect(tx, y - 6, tw, 15, ink);
        md_text(tx + 6, y - 3, &MD_F3, 2, title, bg);
    }
}

/* a key name in a 1 px box (14 px tall), the action 4 px after it; returns the end x */
static int32_t cv_keycap(int32_t x, int32_t y, const char *key, const char *act, uint16_t ink)
{
    int32_t w = md_text_w(&MD_F3, 2, key) + 6;
    cv_frame(x, y, w, 14, ink);
    md_text(x + 3, y + 2, &MD_F3, 2, key, ink);
    return act && act[0] ? md_text(x + w + 4, y + 2, &MD_F3, 2, act, ink) : x + w;
}

/* a button 15 px tall: outline + ink text (off), ink box + bg text (on) */
static void cv_button(int32_t x, int32_t y, int32_t w, const char *s, int on, uint16_t ink, uint16_t bg)
{
    int32_t tx = x + (w - md_text_w(&MD_F3, 2, s)) / 2;
    if (on)
        cv_rect(x, y, w, 15, ink);
    else
        cv_frame(x, y, w, 15, ink);
    md_text(tx, y + 3, &MD_F3, 2, s, on ? bg : ink);
}

/* the first n of 16 trigs: 10x10 boxes on a 14 px pitch, +2 px per beat; a trig (bit i of on16) solid when accented
 * (acc16), else outline + dither; no trig an outline; the playhead (step 0..15) an outline 2 px out; a 2x2 beat dot
 * under steps 1, 5, 9, 13 */
static void cv_trigs(int32_t x0, int32_t y, uint32_t n, uint32_t on16, uint32_t acc16, uint32_t step, uint16_t ink)
{
    int32_t i;
    for (i = 0; i < (int32_t)n && i < 16; i++) {
        int32_t bx = x0 + i * 14 + (i / 4) * 2;
        if (on16 >> i & acc16 >> i & 1u)
            cv_rect(bx, y, 10, 10, ink);
        else {
            cv_frame(bx, y, 10, 10, ink);
            if (on16 >> i & 1u)
                cv_dither(bx + 1, y + 1, 8, 8, ink);
        }
        if ((uint32_t)i == step)
            cv_frame(bx - 2, y - 2, 14, 14, ink);
        if (i % 4 == 0)
            cv_rect(bx + 4, y + 14, 2, 2, ink);
    }
}

/* one-shot: text in a box, cleared to black, blitted */
static void draw_text_box(uint32_t x, uint32_t y, uint32_t w, const felucca_font_t *f, const char *s,
                          uint16_t c, int align)
{
    int32_t tw = text_w(f, s), tx = 0;
    cv_begin(w, f->h, C_BLACK);
    if (align == 1)
        tx = ((int32_t)w - tw) / 2;
    else if (align == 2)
        tx = (int32_t)w - tw;
    cv_text(tx, 0, f, s, c);
    cv_blit(x, y);
    lcd_sync();                     /* one-shots (boot, crash, UBOOT, update) finish here */
}
