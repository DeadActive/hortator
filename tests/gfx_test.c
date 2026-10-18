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

int main(void)
{
    ui_host_init();
    test_md_text();
    printf(fails ? "gfx_test: %d FAILED\n" : "gfx_test: all passed\n", fails);
    return fails ? 1 : 0;
}
