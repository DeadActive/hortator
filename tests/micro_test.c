/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* Microtiming and record quantize (spec docs/superpowers/specs/2026-10-31-microtiming-quantize-copytools-design.md
 * sections 1 and 2): a step's micro (-50 .. +50) moves its hit by micro / 100 of a step (a late hit inside its own
 * step; an early one in the step before, the sequencer looking one step ahead), its ratchets, PROB and locks with it;
 * live recording places a hit by QUANT (1/16, 1/8, 1/32, OFF: the hit's own offset as the micro). */
#include "drum_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static void play(void) { transport_req = 1; }
#define CEILB(x) (((x) + CTL - 1u) / CTL)            /* a hit due at sample x plays in the first block at or after it */

/* the steps' starts from PLAY as the sequencer counts them (an independent model): division div (ids 0..5) at 120 BPM,
 * swing sw (the track's + the global), each step's period carrying its remainder, the even counts long */
static uint32_t S0[400];
static void starts(uint32_t div, int32_t sw)
{
    static const uint32_t DEN[6] = {1, 2, 4, 8, 3, 6};
    uint32_t beat = FS * 60u / 120u, den = DEN[div], rem = 0, c, at = 0;
    int32_t s = sw * (int32_t)(beat / den) / 250;
    for (c = 0; c < 400u; c++) {
        uint32_t per = (beat + rem) / den;
        S0[c] = at;
        rem = (beat + rem) % den;
        at += per + (uint32_t)((c & 1u) ? -s : s);
    }
}
static uint32_t L0(uint32_t c) { return S0[c + 1u] - S0[c]; }
/* where the hit of the step played as count c with micro m is due: late, m / 100 of its own step in; early, (100 + m)
 * / 100 into the step before (-50: halfway between their starts); step 0 at PLAY: at once */
static uint32_t due(uint32_t c, int32_t m)
{
    if (m >= 0)
        return S0[c] + L0(c) * (uint32_t)m / 100u;
    if (!c)
        return 0;
    return S0[c - 1u] + L0(c - 1u) * (uint32_t)(100 + m) / 100u;
}

/* the blocks at which track ti was hit while the mix runs for frames samples */
static uint32_t hits_at(uint32_t ti, uint32_t frames, uint32_t *at, uint32_t max)
{
    uint32_t f, n = 0, a = hit_age(&trk[ti]);
    for (f = 0; f < frames; f += CTL) {
        render_mix(0, 0, CTL);
        if (hit_age(&trk[ti]) != a) {
            a = hit_age(&trk[ti]);
            if (n < max)
                at[n] = f / CTL;
            n++;
        }
    }
    return n;
}

static void fresh(void)
{
    host_init();
    song.g[G_BPM] = 120;
}

/* + and - micro at 1/16, 1/8 and 16T, with and without swing, LEN 16 and 5: step 1 late, step 3 early, step 0 early
 * (at PLAY: at once; round the loop: in the last step) */
static void micro_timing(void)
{
    static const uint8_t DIV[3] = {2, 1, 5};
    static const uint8_t LEN[2] = {16, 5};
    uint32_t d, w, l, mi, ok = 1, runs = 0;
    for (d = 0; d < 3u; d++)
        for (w = 0; w < 2u; w++)
            for (l = 0; l < 2u; l++)
                for (mi = 0; mi < 3u; mi++) {
                    static const int8_t M[3] = {25, 50, 7};
                    uint32_t at[64], want[64], n, nw = 0, c, len = LEN[l], k;
                    int32_t m = M[mi];
                    fresh();
                    song.g[G_SWING] = (int16_t)(w ? 40 : 0);
                    trk[0].p[P_SDIV] = DIV[d];
                    trk[0].p[P_SLEN] = (int16_t)len;
                    starts(DIV[d], w ? 40 : 0);
                    trk[0].step[0].on = trk[0].step[1].on = trk[0].step[3].on = 1;
                    trk[0].step[0].micro = -30;
                    trk[0].step[1].micro = (int8_t)m;
                    trk[0].step[3].micro = (int8_t)-m;
                    play();
                    for (c = 0; c <= 3u * len; c++) {      /* (the next loop's step 0 comes early: in the run) */
                        uint32_t s = c % len;
                        if ((s == 0u || s == 1u || s == 3u) && due(c, trk[0].step[s].micro) < S0[3u * len] - CTL)
                            want[nw++] = CEILB(due(c, trk[0].step[s].micro));
                    }
                    n = hits_at(0, S0[3u * len] - CTL, at, 64);
                    ok &= n == nw;
                    for (k = 0; k < n && k < nw; k++)
                        if (at[k] != want[k]) {
                            if (runs++ < 6u)
                                printf("     DIV %u swing %u LEN %u micro %d: hit %u at block %u, want %u\n", DIV[d],
                                       w ? 40u : 0u, len, m, k, at[k], want[k]);
                            ok = 0;
                        }
                    if (n != nw && runs++ < 6u)
                        printf("     DIV %u swing %u LEN %u micro %d: %u hits, want %u\n", DIV[d], w ? 40u : 0u, len, m,
                               n, nw);
                }
    check("micro: + late in its own step, - early in the step before (step 0 round the loop too), every DIV / swing / LEN",
          ok);
}

/* all micro 0: the hits exactly where they were (the steps' starts) */
static void micro_zero_unchanged(void)
{
    uint32_t at[64], n, k, ok = 1;
    fresh();
    starts(2, 25);
    song.g[G_SWING] = 25;
    for (k = 0; k < 16u; k++)
        trk[0].step[k].on = 1;
    play();
    n = hits_at(0, S0[32] - CTL, at, 64);
    for (k = 0; k < n && k < 32u; k++)
        ok &= at[k] == CEILB(S0[k]);
    check("micro 0: every hit on its step's start, as before", ok && n == 32u);
}

/* RATCH with micro: the roll spreads from the hit over the rest of its step (late: to its end; early: through the
 * step before's rest and its own) */
static void micro_ratchets(void)
{
    uint32_t at[32], n, k, ok = 1, h, span, want[8];
    fresh();
    starts(2, 0);
    trk[0].step[2].on = 1;
    trk[0].step[2].rat = 1;                          /* 2 hits */
    trk[0].step[2].micro = 50;
    play();
    n = hits_at(0, S0[4] - CTL, at, 32);
    h = S0[2] + L0(2) * 50u / 100u;
    span = S0[3] - h;
    want[0] = CEILB(h);
    want[1] = CEILB(h + span / 2u);
    ok &= n == 2u && at[0] == want[0] && at[1] == want[1];
    check("micro + RATCH 2, +50: the second hit halfway through the step's rest", ok);
    fresh();
    starts(2, 0);
    trk[0].step[2].on = 1;
    trk[0].step[2].rat = 3;                          /* 4 hits */
    trk[0].step[2].micro = -50;
    play();
    n = hits_at(0, S0[4] - CTL, at, 32);
    h = S0[1] + L0(1) * 50u / 100u;
    span = S0[3] - h;                                /* the step before's rest + its own */
    ok = n == 4u;
    for (k = 0; k < 4u && k < n; k++)
        ok &= at[k] == CEILB(h + k * span / 4u);
    check("micro + RATCH 4, -50: four hits evenly from the early hit to the step's end", ok);
}

/* PROB with micro: one decision per hit, at the hit; A/B counts the loop the step is in (an early step 0 is the next
 * loop's); 0 % never plays */
static void micro_prob(void)
{
    uint32_t at[64], n, ok, k;
    fresh();
    starts(2, 0);
    trk[0].p[P_SLEN] = 4;
    trk[0].step[0].on = 1;
    trk[0].step[0].cond = 22;                        /* 1:2: loops 0, 2, 4 .. */
    trk[0].step[0].micro = -25;
    play();
    n = hits_at(0, S0[22], at, 64);                 /* loops 0 .. 5 (loop 6's early step 0: past the run) */
    ok = n == 3u && at[0] == 0u;
    for (k = 1; k < n && k < 3u; k++)
        ok &= at[k] == CEILB(due(8u * k, -25));
    check("micro + PROB 1:2 on an early step 0: plays on loops 0, 2, 4 (the early hit counts its own loop)", ok);
    fresh();
    trk[0].step[1].on = 1;
    trk[0].step[1].cond = 1;                         /* 0 % */
    trk[0].step[1].micro = 40;
    trk[0].step[3].on = 1;
    trk[0].step[3].cond = 1;
    trk[0].step[3].micro = -40;
    play();
    n = hits_at(0, 40u * 5512u, at, 64);
    check("micro + PROB 0 %: neither a late nor an early step ever plays", n == 0u);
}

static void to_sample(uint32_t s)                     /* the mix run until sample s from PLAY (the blocks before it) */
{
    static uint32_t now;
    if (!s)
        now = 0;
    while (now + CTL <= s) {
        render_mix(0, 0, CTL);
        now += CTL;
    }
}

/* locks with micro: applied from the hit, snapped back at the step's own end */
static void micro_locks(void)
{
    int ok;
    uint32_t h;
    fresh();
    starts(2, 0);
    trk[0].p[P_E1] = 64;
    trk[0].step[2].on = 1;
    trk[0].step[2].micro = 50;
    lock_set(0, 2, P_E1, 100);
    play();
    to_sample(0);
    h = S0[2] + L0(2) / 2u;
    to_sample(S0[2] + CTL);                          /* step 2 begun, its hit not yet */
    ok = trk[0].p[P_E1] == 64;
    to_sample(h + 2u * CTL);
    ok &= trk[0].p[P_E1] == 100;
    to_sample(S0[3] + 2u * CTL);
    ok &= trk[0].p[P_E1] == 64;
    check("micro +50 + a lock: the lock from the late hit, back at the step's end", ok);
    fresh();
    trk[0].p[P_E1] = 64;
    trk[0].step[1].on = trk[0].step[2].on = 1;
    trk[0].step[2].micro = -50;
    lock_set(0, 1, P_E1, 20);
    lock_set(0, 2, P_E1, 100);
    play();
    to_sample(0);
    h = S0[1] + L0(1) / 2u;
    to_sample(S0[1] + 2u * CTL);
    ok = trk[0].p[P_E1] == 20;                       /* step 1's own lock */
    to_sample(h + 2u * CTL);
    ok &= trk[0].p[P_E1] == 100;                     /* step 2's, from its early hit */
    to_sample(S0[2] + 2u * CTL);
    ok &= trk[0].p[P_E1] == 100;                     /* (held through its own step) */
    to_sample(S0[3] + 2u * CTL);
    ok &= trk[0].p[P_E1] == 64;
    check("micro -50 + a lock: the lock from the early hit (the step before's let go), held to its own step's end", ok);
}

/* micro on an off step: nothing */
static void micro_off_step_quiet(void)
{
    uint32_t at[8], n;
    fresh();
    trk[0].step[2].micro = 40;                       /* off: nothing */
    trk[0].step[5].micro = -40;
    play();
    n = hits_at(0, 20u * 5512u, at, 8);
    check("micro on an off step: plays nothing", n == 0u);
}

/* ---- record quantize (rec_hit by QUANT) */
static uint32_t rec_now;
static void rec_setup(uint32_t q)
{
    fresh();
    starts(2, 0);
    rec_quant = (uint8_t)q;
    song.rec = 1;
    play();
    render_mix(0, 0, CTL);
    rec_now = CTL;
}
static void rec_to(uint32_t s)                       /* run to sample s (block granular) */
{
    while (rec_now + CTL <= s) {
        render_mix(0, 0, CTL);
        rec_now += CTL;
    }
}
/* a live hit on track 1 (key) at frac / 100 into step c: the step it lands in, its micro, on; the position the sequencer
 * saw (in its step) */
static uint32_t rec_pos;
static int32_t rec_at(uint32_t q, uint32_t c, uint32_t frac, int8_t *micro)
{
    uint32_t i;
    rec_setup(q);
    rec_to(S0[c] + L0(c) * frac / 100u + CTL);
    rec_pos = trk[0].seq_pos;
    input_hit(&trk[0], 100u);
    for (i = 0; i < NSTEP; i++)
        if (trk[0].step[i].on) {
            *micro = trk[0].step[i].micro;
            return (int32_t)i;
        }
    return -1;
}
static void quant_modes(void)
{
    int8_t m = 99;
    int ok;
    int32_t s;
    s = rec_at(QUANT_16, 2, 30, &m);
    ok = s == 2 && m == 0;
    s = rec_at(QUANT_16, 2, 70, &m);
    ok &= s == 3 && m == 0;
    check("QUANT 1/16: the nearest step, micro 0 (as before)", ok);
    s = rec_at(QUANT_8, 2, 30, &m);
    ok = s == 2 && m == 0;
    s = rec_at(QUANT_8, 2, 80, &m);
    ok &= s == 2 && m == 0;
    s = rec_at(QUANT_8, 3, 20, &m);
    ok &= s == 4 && m == 0;
    s = rec_at(QUANT_8, 1, 5, &m);
    ok &= s == 2 && m == 0;
    check("QUANT 1/8: the nearest even step (a two-step grid), micro 0", ok);
    s = rec_at(QUANT_32, 2, 10, &m);
    ok = s == 2 && m == 0;
    s = rec_at(QUANT_32, 2, 30, &m);
    ok &= s == 2 && m == 50;
    s = rec_at(QUANT_32, 2, 62, &m);
    ok &= s == 3 && m == -50;
    s = rec_at(QUANT_32, 2, 90, &m);
    ok &= s == 3 && m == 0;
    check("QUANT 1/32: the nearest half step (micro 0 or +-50)", ok);
    s = rec_at(QUANT_OFF, 2, 30, &m);
    ok = s == 2 && m == (int8_t)((rec_pos * 100u + L0(2) / 2u) / L0(2));
    ok &= m >= 28 && m <= 34;
    s = rec_at(QUANT_OFF, 2, 70, &m);
    ok &= s == 3 && m == -(int8_t)(((L0(2) - rec_pos) * 100u + L0(2) / 2u) / L0(2));
    ok &= m <= -26 && m >= -31;
    check("QUANT OFF: the nearest step, the hit's own offset as its micro", ok);
}

/* a live hit recorded early into the next step (OFF: a - micro) sounds once: the look-ahead does not play it again;
 * the next loop plays it early */
static void quant_off_no_double(void)
{
    uint32_t a, at[8], n;
    int8_t m;
    int32_t s = rec_at(QUANT_OFF, 2, 70, &m);
    a = hit_age(&trk[0]);
    rec_to(S0[3] + 4u * CTL);
    n = hit_age(&trk[0]) != a;
    check("QUANT OFF: the recorded early hit sounds once (live), not again from the look-ahead", s == 3 && m < 0 && !n);
    song.rec = 0;
    n = hits_at(0, S0[16 + 4] - rec_now, at, 8);
    check("QUANT OFF: .. and next time round, early by its micro",
          n == 1u && at[0] + rec_now / CTL == CEILB(due(16u + 3u, m)));
}
/* a live hit recorded into the playing step (OFF) whose late hit is still to come: it replaces it (no double) */
static void quant_off_replaces_due(void)
{
    uint32_t a, live;
    int8_t m;
    rec_setup(QUANT_OFF);
    trk[0].step[2].on = 1;
    trk[0].step[2].micro = 45;
    rec_to(S0[2] + L0(2) / 5u + CTL);
    a = dvage;
    input_hit(&trk[0], 100u);
    m = trk[0].step[2].micro;
    live = dvage - a;
    rec_to(S0[3] + 2u * CTL);
    check("QUANT OFF: a hit recorded into the playing step before its late hit: that hit is the step's (sounds once)",
          live == 1u && dvage - a == 1u && m > 0 && m < 45);
}

int main(void)
{
    micro_zero_unchanged();
    micro_timing();
    micro_ratchets();
    micro_prob();
    micro_locks();
    micro_off_step_quiet();
    quant_modes();
    quant_off_no_double();
    quant_off_replaces_due();
    printf(fails ? "micro_test: %d FAILED\n" : "micro_test: all passed\n", fails);
    return fails ? 1 : 0;
}
