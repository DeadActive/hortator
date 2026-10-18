/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 DEADACTIVE */
/* The Machinedrum-style drawing layer (gfx.c md_* / cv_*): fonts and primitives, checked on the canvas pixels. */
#include "ui_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

#define BG 0x0000u
#define INK 0xFFFFu
static uint16_t px(int32_t x, int32_t y) { return swap16(cv_px[(uint32_t)y * cv_w + (uint32_t)x]); }

/* the w x 5 pattern (rows '#'/'.') at x, y drawn at scale sc: every font pixel an sc x sc block */
static int pattern_at(int32_t x, int32_t y, uint32_t sc, const char *const rows[5])
{
    int32_t r, c, i, j, w = (int32_t)strlen(rows[0]);
    for (r = 0; r < 5; r++)
        for (c = 0; c < w; c++)
            for (j = 0; j < (int32_t)sc; j++)
                for (i = 0; i < (int32_t)sc; i++)
                    if (px(x + c * (int32_t)sc + i, y + r * (int32_t)sc + j) != (rows[r][c] == '#' ? INK : BG))
                        return 0;
    return 1;
}

static void test_md_text(void)
{
    static const char *const A[5] = {"###", "#.#", "###", "#.#", "#.#"};
    static const char *const Q[5] = {"###", "..#", ".##", "...", ".#."};
    int32_t end;
    uint16_t keep[16 * 16];
    cv_begin(16, 16, BG);
    end = md_text(0, 0, &MD_F3, 2, "A", INK);
    check("md_text: 'A' at 2x is the MD 3x5 A, each font pixel a 2x2 block", pattern_at(0, 0, 2, A));
    check("md_text: the pen after 'A' at 2x is 8", end == 8);
    check("md_text: only ink pixels are written (the A's holes keep the background)", px(2, 2) == BG && px(3, 7) == BG);
    check("md_text_w: ':' is 1 px wide (2 at 2x)", md_text_w(&MD_F3, 2, ":") == 2);
    cv_begin(16, 16, BG);
    check("md_text: ':' at 2x advances 4", md_text(0, 0, &MD_F3, 2, ":", INK) == 4);
    cv_begin(16, 16, BG);
    md_text(0, 0, &MD_F3, 2, "A", INK);
    memcpy(keep, cv_px, sizeof keep);
    cv_begin(16, 16, BG);
    md_text(0, 0, &MD_F3, 2, "a", INK);
    check("md_text: lower case draws as caps", !memcmp(keep, cv_px, sizeof keep));
    cv_begin(16, 16, BG);
    md_text(0, 0, &MD_F3, 2, "\x01", INK);
    check("md_text: a glyph the font lacks draws as '?'", pattern_at(0, 0, 2, Q));
    check("md_text_w: BIG '1' is 3 wide (6 at 2x)", md_text_w(&MD_BIG, 2, "1") == 6);
    check("md_text_w: '12' at 2x = 3 + 1 + 3 font px = 14 (no trailing gap)", md_text_w(&MD_F3, 2, "12") == 14);
}

static uint32_t ink_in(int32_t x, int32_t y, int32_t w, int32_t h)   /* INK pixels in a canvas rectangle */
{
    uint32_t n = 0;
    int32_t i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            n += px(i, j) == INK;
    return n;
}

static void test_primitives(void)
{
    int32_t i, k;
    uint32_t ok;
    cv_begin_at(4, 2, 7, BG);
    cv_dither(0, 0, 4, 2, INK);
    check("dither: parity follows the screen row (canvas row 0 = screen row 7: (0,0) clear, (1,0) ink)",
          px(0, 0) == BG && px(1, 0) == INK && px(0, 1) == INK && px(1, 1) == BG);
    cv_begin_at(4, 2, 8, BG);
    cv_dither(0, 0, 4, 2, INK);
    check("dither: canvas row 0 = screen row 8: (0,0) ink", px(0, 0) == INK && px(1, 0) == BG);
    cv_begin(8, 4, BG);
    cv_hdot(0, 6, 0, INK);
    cv_vdot(7, 0, 3, INK);
    check("hdot / vdot: 1 on, 1 off, from the first coordinate",
          px(0, 0) == INK && px(1, 0) == BG && px(2, 0) == INK && px(6, 0) == INK && px(7, 0) == INK && px(7, 1) == BG &&
          px(7, 2) == INK);
    cv_begin(40, 40, BG);
    cv_knob(20, 20, 8, 0, -1, INK);
    check("knob: the value blob at the minimum sits at 7:30 inside the ring (centre 17,23)",
          ink_in(16, 22, 3, 3) == 9 && px(23, 23) == BG);
    check("knob: 12 o'clock tick 2 px above the ring", px(20, 10) == INK && px(20, 11) == BG && px(20, 12) == INK);
    check("knob: no LFO dot without an LFO (only the tick in its box)", ink_in(19, 8, 3, 3) == 1);
    cv_begin(40, 40, BG);
    cv_knob(20, 20, 8, 1000, 500, INK);
    check("knob: the value blob at the maximum sits at 4:30 (centre 23,23)", ink_in(22, 22, 3, 3) == 9 && px(17, 23) == BG);
    check("knob: the LFO value as a hollow 3x3 dot outside the ring (12 o'clock: 8 ink, the centre clear)",
          ink_in(19, 8, 3, 3) == 8 && px(20, 9) == BG);
    cv_begin(80, 60, BG);
    cv_popup(10, 20, 50, 30, "", INK, BG);
    check("popup: 1 px top / left edge, 2 px right / bottom edge, notched corner",
          px(10, 25) == INK && px(11, 25) == BG && px(58, 25) == INK && px(59, 25) == INK && px(57, 25) == BG &&
          px(30, 49) == INK && px(30, 50) == INK && px(30, 48) == BG && px(10, 20) == BG);
    cv_begin(80, 30, BG);
    check("tab: width = text + 6, 13 px tall ink box", cv_tab(0, 0, "AB", INK, BG) == 20 && px(0, 0) == INK &&
          px(19, 12) == INK && px(20, 0) == BG && px(3, 2) == BG);
    cv_begin(80, 30, BG);
    k = cv_keycap(0, 0, "OCT", "OK", INK);
    check("keycap: a 1 px box round the key, the action 4 px after it",
          px(0, 0) == INK && px(1, 1) == BG && px(27, 13) == INK && px(28, 13) == BG && k == 48 && ink_in(32, 2, 14, 10) > 0);
    cv_begin(80, 30, BG);
    cv_button(0, 0, 30, "ON", 1, INK, BG);
    cv_button(40, 0, 30, "ON", 0, INK, BG);
    check("button: on = ink box with clear text, off = outline with ink text",
          px(1, 1) == INK && ink_in(0, 0, 30, 15) > 300 && px(41, 1) == BG && ink_in(40, 0, 30, 15) < 200);
    ok = 1;
    for (k = 0; k < 16; k++) {
        cv_begin(240, 20, BG);
        cv_trigs(0, 2, 0xFFFFu, (uint32_t)k, INK);
        for (i = 0; i < 15; i++) {
            int32_t bx = i * 14 + (i / 4) * 2, nx = (i + 1) * 14 + ((i + 1) / 4) * 2, x, clear = 0;
            for (x = bx + 10; x < nx; x++)
                clear += px(x, 7) == BG;
            ok &= clear >= 2;
        }
    }
    check("trigs: every gap between neighbours keeps >= 2 clear columns, playhead included", ok);
    cv_begin(240, 20, BG);
    cv_trigs(0, 2, 1u | 1u << 4, 99, INK);
    check("trigs: box 4 starts at x 58 (+2 per beat); trig = solid 10x10, empty = outline",
          ink_in(58, 2, 10, 10) == 100 && ink_in(0, 2, 10, 10) == 100 && ink_in(14, 2, 10, 10) == 36);
    check("trigs: beat dots under steps 1 and 5", ink_in(4, 16, 2, 2) == 4 && ink_in(62, 16, 2, 2) == 4 &&
          ink_in(18, 16, 2, 2) == 0);
}

int main(void)
{
    ui_host_init();
    test_md_text();
    test_primitives();
    printf(fails ? "gfx_test: %d FAILED\n" : "gfx_test: all passed\n", fails);
    return fails ? 1 : 0;
}
