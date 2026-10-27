/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE
 * accel_up: upstream Felucca's knob acceleration as of 1.1.5 .. 1.6.1 (firmware/src/ui_input.c accel, 276f72a):
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the knob acceleration (ui_input.c accel, knob_turn) against upstream Felucca's, through main.c's loop:
 * a frame is a read, ui_draw (no reads, 3 ms on a light page, 30 ms on a heavy one), then reads every 0.1 ms until 15 ms
 * since the frame began; a read takes the clicks since the last one (enc_take). Clicks come evenly at 5 .. 150 / s.
 * Checked (MENU > KNOB ACCEL ON, a 0..127 value):
 *   - slow turns (5, 10 clicks/s): one step a click, every read;
 *   - medium and fast turns (20 .. 150 clicks/s): no more clicks to sweep 0..127 than upstream's, and fewer at
 *     20 .. 60 clicks/s on a light page;
 *   - a single click after a pause, the first click of a reversal: one step;
 *   - KNOB ACCEL OFF: one step a click at any speed;
 *   - a list of names (MODEL, 45 entries; F_ENUM): one entry a click at any speed (upstream: lists never accelerate).
 * Prints the clicks to sweep 0..127: this firmware's, upstream's. */
#include "ui_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%-96s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
    return ok;
}

/* upstream Felucca 1.6.1's accel (verbatim but for its clock and its MENU flag) */
static uint32_t up_ms, up_enc_t[NE];
#define UP_ACC_GAP 40u
#define UP_ACC_RATE 50u
static int32_t accel_up(uint32_t role, int32_t s, int32_t range)
{
    uint32_t now = up_ms & 0xFFFFFFu, st = up_enc_t[role], up = s > 0, pi = st >> 25, a, i, m = 1;
    if (range <= 32 || !s)
        return s;
    a = (uint32_t)(s < 0 ? -s : s);
    i = ((now - st) & 0xFFFFFFu) / a;                   /* ms per detent of this read */
    if (!st || ((st >> 24) & 1u) != up || i >= UP_ACC_GAP)
        i = 127u;                                       /* a new turn, or reversed */
    else if (pi < UP_ACC_GAP) {
        m = UP_ACC_RATE / (i > pi ? i : pi ? pi : 1u);
        a = range > 64 ? 8u : 4u;                       /* (the cap) */
        m = m < 1u ? 1u : m > a ? a : m;
    }
    up_enc_t[role] = now | up << 24 | (i ? i : 1u) << 25;
    return s * (int32_t)m;
}

static uint32_t clock_ms;                          /* the loop's time; host_ticks follows it */
static int32_t accel_fw(uint32_t role, int32_t s, int32_t range)
{
    host_ticks = clock_ms * 1000u * FM1_TICKS_PER_US - 1000u * FM1_TICKS_PER_US;   /* (fm1_ticks adds 1 ms a call) */
    return accel(role, s, range);
}
static int32_t accel_ref(uint32_t role, int32_t s, int32_t range)
{
    up_ms = clock_ms;
    return accel_up(role, s, range);
}
typedef int32_t (*accel_fn)(uint32_t, int32_t, int32_t);

static int slow_bad;                               /* a read that moved other than one step a click */
/* clicks until a 0..127 value reaches 127 from 0 (or 400: never), `rate` clicks/s, ui_draw `draw` ms; dir -1: down from
 * 127 to 0 */
static unsigned sweep(accel_fn f, double rate, uint32_t draw, int dir)
{
    uint32_t us0 = 10u * 1000u * 1000u, frame = us0, taken = 0;   /* (10 s in: a new turn) */
    double per = 1e6 / rate;
    int32_t v = dir > 0 ? 0 : 127;
    memset(ui.enc_t, 0, sizeof ui.enc_t);
    memset(up_enc_t, 0, sizeof up_enc_t);
    for (;;) {
        uint32_t us = frame, end = frame + 15000u;
        int first = 1;
        while (first || us < end) {
            uint32_t clicks = (uint32_t)((us - us0) / per);
            if (clicks > taken) {
                int32_t s = (int32_t)(clicks - taken) * dir, d;
                taken = clicks;
                clock_ms = us / 1000u;
                d = f(EN_K1, s, 127);
                slow_bad |= d != s;
                v = v + d < 0 ? 0 : v + d > 127 ? 127 : v + d;
                if (dir > 0 ? v >= 127 : v <= 0)
                    return taken;
            }
            if (taken >= 400u)
                return 400u;
            us += first ? (draw ? draw * 1000u : 100u) : 100u;
            first = 0;
        }
        frame = us;
    }
}

int main(void)
{
    static const double RATE[] = {5, 10, 20, 30, 40, 60, 100, 150};
    static const uint32_t DRAW[] = {3, 30};
    unsigned r, d, n_fw[2][8], n_up[2][8];
    int ok;
    ui_host_init();
    settings.accel = 1;
    for (d = 0; d < 2u; d++) {
        printf("ui_draw %u ms: clicks to sweep 0..127\nclicks/s ", (unsigned)DRAW[d]);
        for (r = 0; r < 8u; r++)
            printf("%5.0f", RATE[r]);
        printf("\nhortator ");
        for (r = 0; r < 8u; r++)
            printf("%5u", n_fw[d][r] = sweep(accel_fw, RATE[r], DRAW[d], 1));
        printf("\nupstream ");
        for (r = 0; r < 8u; r++)
            printf("%5u", n_up[d][r] = sweep(accel_ref, RATE[r], DRAW[d], 1));
        printf("\n");
    }
    for (ok = 1, d = 0; d < 2u; d++)
        for (r = 0; r < 2u; r++) {
            slow_bad = 0;
            ok &= sweep(accel_fw, RATE[r], DRAW[d], 1) == 127u && !slow_bad;
            slow_bad = 0;
            ok &= sweep(accel_fw, RATE[r], DRAW[d], -1) == 127u && !slow_bad;
        }
    check("slow turns (5, 10 clicks/s), up and down: one step a click, every read", ok);
    for (ok = 1, d = 0; d < 2u; d++)
        for (r = 2; r < 8u; r++)
            ok &= n_fw[d][r] <= n_up[d][r] && sweep(accel_fw, RATE[r], DRAW[d], -1) <= n_up[d][r];
    check("medium and fast turns (20..150 clicks/s): no more clicks to sweep 0..127 than upstream's", ok);
    for (ok = 1, r = 2; r < 6u; r++)
        ok &= n_fw[0][r] * 4u <= n_up[0][r] * 3u;
    check("20..60 clicks/s on a light page: at most 3/4 of upstream's clicks to sweep", ok);
    {   /* a single click after a pause; a reversal */
        int32_t a;
        memset(ui.enc_t, 0, sizeof ui.enc_t);
        clock_ms = 20000u;
        a = accel_fw(EN_K2, 1, 127);
        clock_ms += 300u;
        ok = a == 1 && accel_fw(EN_K2, 1, 127) == 1;
        for (r = 0; r < 20u; r++) {                /* a fast turn up (60 clicks/s) */
            clock_ms += 17u;
            accel_fw(EN_K2, 1, 127);
        }
        clock_ms += 17u;
        ok &= accel_fw(EN_K2, 1, 127) > 1;         /* (accelerated by now) */
        clock_ms += 17u;
        ok &= accel_fw(EN_K2, -1, 127) == -1;      /* reversed: one step */
        check("a single click after a pause: one step; the first click of a reversal: one step", ok);
    }
    settings.accel = 0;
    for (ok = 1, d = 0; d < 2u; d++)
        for (r = 0; r < 8u; r++) {
            slow_bad = 0;
            sweep(accel_fw, RATE[r], DRAW[d], 1);  /* (a read may hold clicks past the top) */
            ok &= !slow_bad;
        }
    check("KNOB ACCEL OFF: one step a click at any speed", ok);
    settings.accel = 1;
    {   /* MODEL, a list of 45 names, turned at 100 clicks/s, one click a read: one entry a click */
        const param_desc_t *md = &TP[P_MODEL];
        int32_t v = param_turn(md, md->min, -100), v0;   /* (the first in the shown order) */
        memset(ui.enc_t, 0, sizeof ui.enc_t);
        clock_ms = 40000u;
        for (r = 0; r < 30u; r++) {
            clock_ms += 10u;
            v = knob_turn(EN_K1, md, v, 1);
        }
        v0 = enum_rank(md, param_turn(md, md->min, -100));
        printf("MODEL after 30 clicks at 100/s: %d entries on (of %d)\n", (int)(enum_rank(md, v) - v0), (int)(md->max - md->min + 1));
        check("a list of names (MODEL, 45): one entry a click at 100 clicks/s", md->max - md->min > 32 && enum_rank(md, v) - v0 == 30);
    }
    printf(fails ? "accel_test: %d FAILED\n" : "accel_test: all passed\n", fails);
    return fails != 0;
}
