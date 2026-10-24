/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Small-canvas renderer (no full framebuffer). Draw text/lines into
 * an off-screen strip, then blit it in one DMA transfer. Pixels are stored
 * byte-swapped (the panel takes RGB565 big-endian). */
typedef struct {               /* 1-bit Machinedrum-style font (assets/fonts/md_font.txt, tools/gen_font.py) */
    uint8_t h, first, last;
    const uint8_t *w;          /* glyph width in px (0: no glyph) */
    const uint8_t *rows;       /* rows[i * h + r]: bit (w - 1 - c) = column c */
} md_font_t;
#include "felucca_font.h"

#define CV_MAX (240u * 124u)      /* the graph strip is 240 x 124 */
static uint16_t cv_px[CV_MAX] __attribute__((section(".pool")));
static uint32_t cv_w, cv_h;
static int32_t cv_oy;            /* y offset for graph drawing */
static int32_t cv_sy;            /* the screen row of canvas row 0 (cv_begin_at): dither lines up across strips */
static int32_t cv_wx, cv_wy;     /* a window (cv_begin_win): where cv_px's corner lies in the canvas drawn (else 0, 0) */

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
/* Machinedrum-style themes (spec 2026-10-19-md-ui §2): two colours, BG and INK. Stored in settings.palette bits 0..7
 * as THEME_TAG | index (index: bits 0..3, up to 16 themes); the old palette ids 0..4 (GREEN AMBER CYAN RED MONO) map
 * on load. Every colour in both polarities (owner 2026-10-23): the colour as the ground, then INV, the colour as the
 * ink on a neutral ground pushed further out (darker / lighter: the contrast up, as RED INV's black). THEMES[] is in
 * stored order: the index is what settings keep, so a theme never moves (an older Hortator reads 4.. as MD RED);
 * THEME_ORDER is the order MENU > COLOR shows (each colour, then its INV) */
typedef struct {
    const char *name;          /* MENU > COLOR's value */
    uint16_t bg, ink;
} theme_t;
static const theme_t THEMES[] = {
    {"RED", RGB(0xe0, 0x47, 0x2b), RGB(0x38, 0x10, 0x0a)},          /* 0 MD RED (default) */
    {"RED INV", RGB(0x17, 0x06, 0x05), RGB(0xf0, 0x53, 0x2f)},      /* 1 RED ON BLACK */
    {"BLUE", RGB(0x24, 0x45, 0xc8), RGB(0xea, 0xf1, 0xff)},         /* 2 WHITE ON BLUE */
    {"GREEN", RGB(0xa3, 0xbf, 0x3c), RGB(0x1c, 0x2a, 0x08)},        /* 3 BLACK ON GREEN */
    {"BLUE INV", RGB(0xf4, 0xf8, 0xff), RGB(0x24, 0x45, 0xc8)},     /* 4 BLUE ON WHITE */
    {"GREEN INV", RGB(0x0c, 0x12, 0x04), RGB(0xb4, 0xd0, 0x44)},    /* 5 GREEN ON BLACK */
};
#define NTHEMES (sizeof(THEMES) / sizeof(THEMES[0]))
#define THEME_TAG 0x10u
_Static_assert(NTHEMES <= 16u, "the theme index is 4 bits (THEME_TAG | index)");
static const uint8_t THEME_ORDER[NTHEMES] = {0, 1, 2, 4, 3, 5};   /* RED RED INV BLUE BLUE INV GREEN GREEN INV */
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

/* the stored COLOR id one theme on (dir > 0) or back in the shown order, wrapping */
static uint32_t theme_step(uint32_t stored, int32_t dir)
{
    uint32_t i, cur = theme_index(stored);
    for (i = 0; i < NTHEMES && THEME_ORDER[i] != cur; i++)
        ;
    return THEME_TAG | THEME_ORDER[(i + (dir > 0 ? 1u : NTHEMES - 1u)) % NTHEMES];
}

/* the theme of a stored COLOR id */
static void theme_set(uint32_t stored)
{
    const theme_t *t = &THEMES[theme_index(stored)];
    C_BG = t->bg;
    C_INK = t->ink;
}

static inline uint16_t swap16(uint32_t c) { return (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8)); }

static void cv_begin(uint32_t w, uint32_t h, uint16_t bg)
{
    uint32_t i, n;
    uint16_t sb = swap16(bg);
    if (w * h > CV_MAX)
        h = CV_MAX / w;
    lcd_sync();                     /* the last blit may still read cv_px */
    cv_w = w;
    cv_h = h;
    cv_sy = 0;
    cv_wx = cv_wy = 0;
    n = w * h;
    for (i = 0; i < n; i++)
        cv_px[i] = sb;
}

/* a window of a canvas: only its part x, y, w x h is kept (the drawing is the whole canvas's, clipped), the canvas's
 * row 0 at screen row sy (cv_dither's pattern as on the whole); cv_blit puts the part where it lies. A region redrawn
 * in part sends only that part (the LCD transfer is the cost) */
static void cv_begin_win(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t sy, uint16_t bg)
{
    cv_begin(w, h, bg);
    cv_sy = (int32_t)sy;
    cv_wx = (int32_t)x;
    cv_wy = (int32_t)y;
}

static void cv_blit(uint32_t x, uint32_t y) { lcd_blit(x + (uint32_t)cv_wx, y + (uint32_t)cv_wy, cv_w, cv_h, cv_px); }

/* canvas rows r0 .. cv_h-1 only, to screen row y + r0 (a whole canvas) */
static void cv_blit_from(uint32_t x, uint32_t y, uint32_t r0)
{
    if (r0 < cv_h)
        lcd_blit(x, y + r0, cv_w, cv_h - r0, cv_px + r0 * cv_w);
}

static inline void cv_pset(int32_t x, int32_t y, uint16_t c)
{
    x -= cv_wx;
    y += cv_oy - cv_wy;
    if ((uint32_t)x < cv_w && (uint32_t)y < cv_h)
        cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16(c);
}

static void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t x1, y1, i;
    uint16_t sc = swap16(c);
    x -= cv_wx;
    x1 = x + w;
    y += cv_oy - cv_wy;                     /* clipped once, then filled row by row */
    y1 = y + h;
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

/* MD glyph index of a character: lower case folds to caps in a font with caps, Ü / ü draw as U; a glyph the font
 * lacks draws as its '?' (or nothing when it has none). 0: nothing to draw. */
static int md_glyph(const md_font_t *f, uint32_t ch, uint32_t *gi)
{
    if (ch >= 'a' && ch <= 'z' && f->first <= 'A' && f->last >= 'Z')
        ch -= 32u;
    else if (ch == 0xDCu || ch == 0xFCu)
        ch = 'U';
    else if (ch == 0xB0u)                           /* the degree sign: md_font.txt's '`' */
        ch = '`';
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

/* parameter locks (spec 2026-10-24): under each of the first n trigs whose bit is set in mask16, a 10 x 1 px
 * underline at y + 12 (under the box, rows y..y+9, and the playhead's outline, to y+11; over the beat dot, y+14) */
static void cv_lock_marks(int32_t x0, int32_t y, uint32_t n, uint32_t mask16, uint16_t ink)
{
    int32_t i;
    for (i = 0; i < (int32_t)n && i < 16; i++)
        if ((mask16 >> i) & 1u)
            cv_rect(x0 + i * 14 + (i / 4) * 2, y + 12, 10, 1, ink);
}

/* the logo (spec 2026-10-19-md-ui §9a; the user's pick, C3-4 "VU"): 8 bars mirrored round a dotted centre line, every
 * fourth solid, the rest dithered, moving like a level meter. 144 x 120 at ox, oy (half: 72 x 60). Frame f (15 fps):
 * the downbeat bars full every 8th frame, the others a pseudo-random 30..90 % (a new value every 2 frames); a 2 px
 * peak dot over and under each bar holds, then falls 1.5 px a frame (peaks: 8 half-pixel values the caller keeps; 0:
 * none, the dots on the bars) */
static void cv_vu(int32_t ox, int32_t oy, uint32_t f, uint32_t half, int16_t *peaks)
{
    static const uint8_t BASE[8] = {52, 24, 40, 16, 52, 24, 40, 16};
    int32_t s = half ? 2 : 1, i;
    cv_hdot(ox, ox + 143 / s, oy + 60 / s, C_INK);
    for (i = 0; i < 8; i++) {
        uint32_t n = ((f / 2u) * 9u + (uint32_t)i) * 1103515245u + 12345u, r = (n >> 16) & 0x7FFFu;
        int32_t lvl = f % 8u == 0u && i % 4 == 0 ? BASE[i] : BASE[i] * (300 + (int32_t)(600u * r / 32767u)) / 1000;
        int32_t x = ox + i * 18 / s, w = 12 / s, top, bot, ph;
        lvl /= s;
        top = oy + 58 / s - lvl;
        bot = oy + 63 / s;
        if (i % 4 == 0) {
            cv_rect(x, top, w, lvl, C_INK);
            cv_rect(x, bot, w, lvl, C_INK);
        } else {
            cv_dither(x, top, w, lvl, C_INK);
            cv_dither(x, bot, w, lvl, C_INK);
        }
        ph = lvl;
        if (peaks) {                                  /* the peak: held at the level, falling 1.5 px a frame */
            int32_t p2 = peaks[i] - 3;
            peaks[i] = (int16_t)(p2 < lvl * 2 ? lvl * 2 : p2);
            ph = peaks[i] / 2;
        }
        cv_rect(x, oy + 58 / s - ph - 4 / s, w, 2 / s, C_INK);
        cv_rect(x, bot + ph + 2 / s, w, 2 / s, C_INK);
    }
}

/* one-shot text boxes keep the names the frozen main.c uses (crash, UBOOT; tools/check_untouched.py): FONT_S / FONT_L
 * are MD_F3 at 2x / 4x, C_BLACK / C_WHITE the frozen screens' colours (they stay black and white) */
typedef struct {
    uint8_t sc;
} box_font_t;
static const box_font_t FONT_S = {2}, FONT_L = {4};
#define C_BLACK 0x0000u
#define C_WHITE 0xFFFFu

/* one-shot: text in a box w wide, aligned 0 left, 1 centre, 2 right, blitted; ink c: C_INK on the theme's ground (the
 * update / calibration / safe-start screens), anything else on black (the frozen ones); before a theme is set, the
 * default one */
static void draw_text_box(uint32_t x, uint32_t y, uint32_t w, const box_font_t *f, const char *s, uint16_t c, int align)
{
    int32_t tw, tx = 0;
    if (C_BG == C_INK)
        theme_set(THEME_TAG | 0u);
    tw = md_text_w(&MD_F3, f->sc, s);
    cv_begin(w, 5u * f->sc, c == C_INK ? C_BG : C_BLACK);
    if (align == 1)
        tx = ((int32_t)w - tw) / 2;
    else if (align == 2)
        tx = (int32_t)w - tw;
    md_text(tx, 0, &MD_F3, f->sc, s, c);
    cv_blit(x, y);
    lcd_sync();                     /* one-shots (boot, crash, UBOOT, update) finish here */
}
