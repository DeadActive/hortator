/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE
 * The turn model and the #23 / #63 / #126 cases after upstream Felucca's tests/input_test.c (1.1.5, 276f72a):
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the knobs as the drum firmware reads them (firmware/src/enc.c), through the real scan: hal/fm1_input.h's
 * fm1_input_tick runs on host GPIO registers every 100 us (a frame = 11 ticks, ~1.1 ms), led_dim_tick right after it
 * as in main.c's fm1_timer5_irq (it runs enc_frame once a frame), and the UI takes the steps with enc_take. Encoder 0's
 * contacts toggle on a time line (a turn of n detents, its 4 transitions per detent spread evenly or bunched into a
 * part of the click, each edge bouncing or not); the row line reads them while the scan has their column latched.
 * Checked:
 *   - one slow click = one step, + = clockwise; slow single clicks with pauses: exactly one each; a slow click with
 *     bouncing contacts never steps twice;
 *   - #23: a knob held mid-click, then turned: one step per click; slow turns with mid-click pauses keep counting;
 *   - #63: a still knob, one contact chattering or both lost together: no step;
 *   - #126: 12-detent turns at speed over every phase of the scan: up to 300 detents/s even, 150/s bunched in half a
 *     click, 40/s bunched in a quarter (a flick that snaps), every detent; slow bouncy turns; a reversal at speed;
 *   - enc_take reads and clears: steps taken during a turn add up to the turn.
 * -DENC_TEST_HAL measures the path before 0.23.3 instead (the HAL's own decoder, fm1_enc_take): it fails #126.
 * Prints a table: detents counted of 12 per speed and bunching. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#define fm1_ticks fm1_ticks_hw                     /* the HAL's TIMER4 read, unused: host_now instead */
#include "../firmware/hal/fm1_time.h"
#include "../firmware/hal/fm1_gpio.h"
#undef fm1_ticks
static uint32_t host_now;
static uint32_t fm1_ticks(void) { return host_now += 2400u; }
static volatile uint32_t host_gpio[8][8];          /* port, register / 4 */
#undef FM1_PR
#define FM1_PR(p, r) host_gpio[(p)][(r) >> 2]
#define FM1_INPUT_NOW() host_now
#include "../firmware/hal/fm1_input.h"
#ifdef ENC_TEST_HAL                                /* the path before 0.23.3: the HAL's decoder */
static void enc_frame(void) {}
static int32_t enc_take(uint32_t e) { int32_t s = fm1_in.enc_steps[e]; fm1_in.enc_steps[e] = 0; return s; }
#define ENC_RESET() ((void)0)
#else
#include "../firmware/src/enc.c"
#define ENC_RESET() (memset(&enc_st, 0, sizeof enc_st), memset(enc_seen, 0, sizeof enc_seen))
#endif
#include "../firmware/src/led_dim.c"

static int fails;
static int check(const char *what, int ok)
{
    printf("%-100s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
    return ok;
}
static uint32_t rng = 0x12345u;
static uint32_t rnd(uint32_t n)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng % n;
}

/* encoder 0's contacts A (0) and B (1): toggle times (us), 1 = closed */
#define ENE 8192
static struct { uint32_t t[ENE], n, i, s; } enl[2];
static void en_add(uint32_t l, uint32_t t) { if (enl[l].n < ENE && (!enl[l].n || t > enl[l].t[enl[l].n - 1u])) enl[l].t[enl[l].n++] = t; }
/* a turn of n detents from t0 (dir +1 clockwise: B A B A from the detent state 0, -1: A B A B), per us a detent, its 4
 * transitions bunched over w (1 = even) of each, each edge bouncing (runs of 30..bmax us) for up to bspan us (at most
 * half the way to the next edge); returns its end */
static uint32_t en_turn(uint32_t t0, int32_t n, int32_t dir, double per, double w, uint32_t bspan, uint32_t bmax)
{
    int32_t i, j;
    for (i = 0; i < n; i++)
        for (j = 0; j < 4; j++) {
            double f = 0.5 - w / 2 + (j + 0.5) * w / 4, nf = j < 3 ? 0.5 - w / 2 + (j + 1.5) * w / 4 : 1.0 + 0.5 - w / 2 + 0.5 * w / 4;
            uint32_t t = t0 + (uint32_t)(per * (i + f)), gap = (uint32_t)(per * (nf - f)), l = (uint32_t)((j & 1) ^ (dir > 0)), end;
            end = t + (bspan < gap / 2u ? bspan : gap / 2u);
            en_add(l, t);
            while (bspan && bmax) {                /* bounce: back and forth in pairs, the new level last */
                uint32_t a = t + 30u + rnd(bmax), b = a + 30u + rnd(bmax);
                if (b >= end)
                    break;
                en_add(l, a);
                en_add(l, b);
                t = b;
            }
        }
    return t0 + (uint32_t)(per * n);
}
static uint32_t en_at(uint32_t l, uint32_t us)     /* (us only rises until the next reset) */
{
    while (enl[l].i < enl[l].n && enl[l].t[enl[l].i] <= us) {
        enl[l].i++;
        enl[l].s ^= 1u;
    }
    return enl[l].s;
}

static uint32_t sim_us;
static int32_t (*force)(uint32_t col);             /* the contacts' state (A << 1 | B) instead of the time line, or 0 */
static int32_t forced;
static int32_t force_fn(uint32_t col) { (void)col; return forced; }
/* one TIMER5 tick as main.c runs it: the row lines show what the column the scan latched one tick ago reads (the
 * contacts of encoder 0: A at FM1_ENC[0][0..1], B at [2..3]; closed = low), then fm1_input_tick, led_dim_tick */
static void tick(void)
{
    const uint8_t *m = FM1_ENC[0];
    uint32_t p = fm1__tick_col, a = 0, b = 0, closed = 0;
    if (force) {
        a = ((uint32_t)force(p) >> 1) & 1u;
        b = (uint32_t)force(p) & 1u;
    } else {
        a = en_at(0, sim_us);
        b = en_at(1, sim_us);
    }
    if (p == m[0] && a)
        closed |= 1u << m[1];
    if (p == m[2] && b)
        closed |= 1u << m[3];
    /* rows: bit 0 PA0, 1..4 PA5..PA8, 5 PB7 (pull-ups: high = open) */
    host_gpio[FM1_PA][FM1_IN >> 2] = 0xFFFFFFFFu & ~((closed & 1u) | (closed & 0x1Eu) << 4);
    host_gpio[FM1_PB][FM1_IN >> 2] = 0xFFFFFFFFu & ~((closed & 0x20u) << 2);
    fm1_input_tick();
    led_dim_tick();
    sim_us += 100u;
}
static void reset(void)
{
    uint32_t i;
    memset((void *)host_gpio, 0, sizeof host_gpio);
    memset((void *)&fm1_in, 0, sizeof fm1_in);
    memset((void *)&fm1_in_stat, 0, sizeof fm1_in_stat);
    for (i = 0; i < FM1_NENC; i++)
        fm1_in.enc_prev[i] = fm1_in.enc_last[i] = 0xFF;
    fm1__tick_col = 0;
    led_dim_on = 0;
    ENC_RESET();
    memset(enl, 0, sizeof enl);
    sim_us = 0;
    force = 0;
}
static void run_to(uint32_t us)
{
    while (sim_us < us)
        tick();
}
/* the steps a turn made, the scan run 20 ms past its end; t0 its start */
static int32_t turn(uint32_t t0, int32_t n, int32_t dir, double rate, double w, uint32_t bspan, uint32_t bmax)
{
    uint32_t end = en_turn(t0, n, dir, 1e6 / rate, w, bspan, bmax);
    run_to(end + 20000u);
    return enc_take(0);
}
static void hold(int32_t st, uint32_t frames)      /* the contacts forced to st for that many frames */
{
    force = force_fn;
    forced = st;
    run_to(sim_us + frames * FM1_NCOL * 100u);
    force = 0;
}

int main(void)
{
    uint32_t r, ph, d, bad, n, worst;
    int32_t s;
    char what[128];
#ifdef ENC_TEST_HAL
    printf("enc_test: the HAL's decoder (the path before 0.23.3)\n");
#else
    printf("enc_test: firmware/src/enc.c\n");
#endif

    {   /* the table: detents counted of 12, 40 scan phases x 2 directions */
        static const double RATE[] = {10, 20, 30, 40, 60, 80, 110, 150, 200, 300}, W[] = {1.0, 0.5, 0.25};
        uint32_t wi;
        printf("det/s   even (w 1)     bunched 0.5    bunched 0.25   0.25 bouncing\n");
        for (r = 0; r < sizeof RATE / sizeof *RATE; r++) {
            printf("%-6.0f", RATE[r]);
            for (wi = 0; wi < 4u; wi++) {
                double sum = 0;
                for (bad = 0, n = 0, ph = 0; ph < 40u; ph++)
                    for (d = 0; d < 2u; d++) {
                        int32_t dir = d ? -1 : 1;
                        reset();
                        s = turn(5000u + ph * 53u, 12, dir, RATE[r], W[wi < 3u ? wi : 2u], wi == 3u ? 300u : 0u, 80u);
                        sum += s * dir;
                        n++;
                        bad += s != 12 * dir;
                    }
                printf("  %5.2f %3u%% off", sum / n, bad * 100u / n);
            }
            printf("\n");
        }
    }

    reset();                                       /* one slow click, clockwise: +1 */
    hold(0, 50);
    s = turn(sim_us, 1, 1, 3.0, 1.0, 0u, 0u);
    check("one slow click (3 detents/s) clockwise: one step, +1", s == 1);
#ifndef ENC_TEST_HAL
    check("the hook decodes once a scan frame (enc.c frames = the HAL's)", enc_st.frames == fm1_in.frames &&
          enc_st.frames == sim_us / (FM1_NCOL * 100u));
#endif
    for (bad = 0, ph = 0; ph < 20u; ph++) {        /* slow single clicks, either way, pauses between: one step each */
        int32_t dir = ph & 1u ? -1 : 1;
        reset();
        hold(0, 50);
        for (n = 0; n < 6u; n++) {
            s = turn(sim_us + 50000u + ph * 37u, 1, dir, 2.0 + ph % 5u, ph & 2u ? 1.0 : 0.4, 0u, 0u);
            bad += s != dir;
        }
    }
    check("slow single clicks (2..6 detents/s, 50 ms apart): exactly one step each", !bad);
    for (bad = 0, ph = 0; ph < 40u; ph++) {        /* a slow click, its contacts bouncing up to 3 ms: one step */
        int32_t dir = ph & 1u ? -1 : 1;
        reset();
        hold(0, 50);
        s = turn(sim_us + ph * 29u, 1, dir, 2.0 + ph % 4u, ph & 2u ? 1.0 : 0.3, 3000u, 400u);
        bad += s != dir;
    }
    check("a slow click with contacts bouncing up to 3 ms: one step, never two", !bad);

    {   /* #23 (upstream 1.0): one detent (rest) state learned only */
        static const int32_t SEQ[] = {1, 3, 2, 0};   /* one detent from the rest 0 (A<<1|B) */
        uint32_t c, i;
        reset();
        hold(0, FM1_REST_FRAMES + 100u);           /* at rest: the detent */
        hold(1, FM1_REST_FRAMES + 100u);           /* held mid-click for over the rest time: relearned as THE detent */
        hold(3, 4u);
        hold(2, 4u);
        hold(0, 4u);                               /* that click ends */
        for (c = 0; c < 4u; c++)
            for (i = 0; i < 4u; i++)
                hold(SEQ[i], 4u);
        s = enc_take(0);
        check("#23: a knob held mid-click, then turned: one step per click, not two (4)", s == 4 || s == -4);
        reset();
        hold(0, FM1_REST_FRAMES + 100u);
        for (c = 0; c < 4u; c++)
            for (i = 0; i < 4u; i++)
                hold(SEQ[i], i < 3u ? 200u : 4u);  /* ~0.2 s pauses on the mid states */
        s = enc_take(0);
        check("#23: short mid-click pauses: the knob keeps counting (4)", s == 4 || s == -4);
    }
    {   /* #63: a knob left alone on its detent, one contact chattering (runs of 1..5 frames), or both lost together */
        uint32_t rest, mode, f, g, moved = 0;
        for (mode = 0; mode < 3u; mode++)
            for (rest = 0; rest < 4u; rest++) {
                reset();
                for (f = 0, g = 0; f < 40000u; f++) {
                    uint32_t st = rest;
                    if (g) {
                        st ^= mode == 0u ? 2u : mode == 1u ? 1u : 3u;
                        g--;
                    } else if (f >= 100u && rnd(300) == 0u) {
                        g = 1u + rnd(5);
                    }
                    hold((int32_t)st, 1u);
                    moved |= enc_take(0) != 0;
                }
            }
        check("#63 a still knob: one contact chattering or both lost together never steps", !moved);
    }
    {   /* #126 turns at speed through the scan: every detent counted, 12-detent turns over every phase of the scan */
        static const double EVEN[] = {50, 100, 150, 200, 250, 300}, BUNCH[] = {50, 100, 150}, SNAP[] = {20, 30, 40};
        for (bad = 0, n = 0, worst = 0, r = 0; r < 12u; r++)
            for (ph = 0; ph < 40u; ph++)
                for (d = 0; d < 2u; d++) {
                    int32_t dir = d ? -1 : 1;
                    double rate = r < 6u ? EVEN[r] : r < 9u ? BUNCH[r - 6u] : SNAP[r - 9u];
                    reset();
                    s = turn(5000u + ph * 53u, 12, dir, rate, r < 6u ? 1.0 : r < 9u ? 0.5 : 0.25, 0u, 0u);
                    n++;
                    if (s != 12 * dir) {
                        uint32_t off = (uint32_t)(s * dir < 12 ? 12 - s * dir : s * dir - 12);
                        bad++;
                        worst = off > worst ? off : worst;
                    }
                }
        snprintf(what, sizeof what, "#126 12-detent turns to 300/s even, 150/s bunched in 1/2, 40/s in 1/4: every detent "
                 "(%u of %u off, worst %u)", (unsigned)bad, (unsigned)n, (unsigned)worst);
        check(what, !bad);
    }
    {
        static const double SLOW[] = {3, 8, 20, 40};
        for (bad = 0, r = 0; r < 4u; r++)
            for (ph = 0; ph < 20u; ph++)
                for (d = 0; d < 2u; d++) {
                    int32_t dir = d ? -1 : 1;
                    reset();
                    bad += turn(5000u + ph * 71u, 6, dir, SLOW[r], ph & 1u ? 1.0 : 0.4, 1500u, 400u) != 6 * dir;
                }
        snprintf(what, sizeof what, "#126 slow turns (3..40 detents/s), contacts bouncing 1.5 ms: one step a click (%u off)",
                 (unsigned)bad);
        check(what, !bad);
    }
    for (bad = 0, ph = 0; ph < 40u; ph++) {        /* 8 right, then 6 left at once */
        uint32_t t;
        int32_t s1, s2;
        reset();
        t = en_turn(5000u + ph * 37u, 8, 1, 1e6 / 120.0, 0.7, 200u, 60u);
        run_to(t + 2000u);
        s1 = enc_take(0);
        t = en_turn(t + 2000u, 6, -1, 1e6 / 120.0, 0.7, 200u, 60u);
        run_to(t + 20000u);
        s2 = enc_take(0);
        bad += s1 != 8 || s2 != -6;
    }
    check("#126 120 detents/s, bouncing 0.2 ms: 8 right then 6 left at once -> +8, then -6", !bad);
    for (bad = 0, ph = 0; ph < 20u; ph++) {        /* taken while turning (the UI reads every pass): the sum is the turn */
        uint32_t end;
        int32_t sum = 0;
        reset();
        end = en_turn(5000u + ph * 41u, 24, ph & 1u ? -1 : 1, 1e6 / 80.0, 0.5, 200u, 60u);
        while (sim_us < end + 20000u) {
            run_to(sim_us + 700u + rnd(30000u));
            sum += enc_take(0);
        }
        bad += sum != (ph & 1u ? -24 : 24) || enc_take(0) != 0;
    }
    check("taken during a turn (every 0.7..31 ms): the steps add up to the turn, then nothing", !bad);

    printf(fails ? "enc_test: %d FAILED\n" : "enc_test: all passed\n", fails);
    return fails != 0;
}
