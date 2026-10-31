/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The delay bus (fx.c): TIME's triplet and dotted values, MODE PING-PONG (one line at half rate, the echoes left /
 * right), NORMAL kept bit for bit (the old loop as the reference, and the kit's render pinned before the change),
 * switching modes clears the line. */
#include "drum_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static uint32_t fnv(const int32_t *x, uint32_t n)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < n; i++)
        h = (h ^ (uint32_t)x[i]) * 16777619u;
    return h;
}

/* ------------------------------------------------------------------------------------------------ TIME --- */
static void test_times(void)
{
    /* 4T 1/4D 1/8D 1/16D 32T (ids 10..14): 2/3, 3/2, 3/4, 3/8, 1/12 of a quarter, floored */
    static const uint32_t W120[5] = {14700, 33075, 16537, 8268, 1837};
    static const uint32_t W40[5] = {44100, 99225, 49612, 24806, 5512};
    static const uint32_t W240[5] = {7350, 16537, 8268, 4134, 918};
    static const char *const NAME[5] = {"4T", "1/4D", "1/8D", "1/16D", "32T"};
    /* the knob: longest first, the new values between their neighbours */
    static const uint8_t ORDER[15] = {9, 8, 7, 6, 11, 0, 12, 10, 1, 13, 4, 2, 5, 3, 14};
    uint32_t d, ok = 1, k;
    char s[8];
    const char *u;
    host_init();
    for (d = 0; d < 5u; d++) {
        song.g[G_BPM] = 120;
        ok &= div_samples(10u + d) == W120[d];
        song.g[G_BPM] = 40;
        ok &= div_samples(10u + d) == W40[d];
        song.g[G_BPM] = 240;
        ok &= div_samples(10u + d) == W240[d];
    }
    check("TIME: 4T 1/4D 1/8D 1/16D 32T exact at 40, 120, 240 BPM", ok);
    ok = 1;
    for (d = 0; d < 5u; d++) {
        param_format(&GP[G_DTIME], (int32_t)(10u + d), s, &u);
        ok &= !strcmp(s, NAME[d]);
    }
    check("TIME: the new values' names (ids 10..14 appended)", ok);
    check("TIME: 15 values on the delay, the steps' DIV keeps its 10",
          GP[G_DTIME].max == 14 && GP[G_DTIME].min == 0 && TP[P_SDIV].max == 9);
    ok = 1;
    for (k = 0; k + 1u < 15u; k++)
        ok &= param_turn(&GP[G_DTIME], ORDER[k], 1) == ORDER[k + 1u] &&
              param_turn(&GP[G_DTIME], ORDER[k + 1u], -1) == ORDER[k];
    ok &= param_turn(&GP[G_DTIME], 14, 1) == 14 && param_turn(&GP[G_DTIME], 9, -1) == 9;
    check("TIME: the knob steps longest to shortest (4BAR .. 1/2 1/4D 1/4 1/8D 4T 1/8 1/16D 8T 1/16 16T 1/32 32T)", ok);
    ok = 1;
    song.g[G_BPM] = 120;
    for (k = 0; k + 1u < 15u; k++)
        ok &= div_samples(ORDER[k]) > div_samples(ORDER[k + 1u]);
    check("TIME: the knob's order is the order of the lengths", ok);
    ok = 1;
    for (k = 0; k < 10u; k++)
        ok &= param_turn(&TP[P_SDIV], (int32_t)k, 0) == (int32_t)k;
    ok &= param_turn(&TP[P_SDIV], 6, 1) == 0;            /* the steps: 1/2 -> 1/4 as before (no dotted values) */
    check("TIME: the steps' DIV order unchanged", ok);
    song.g[G_BPM] = 40;
    song.g[G_DTIME] = 11;                                /* 1/4D at 40 BPM = 2.25 s: past the line */
    ok = delay_samples() == DLY_LEN - 1u;
    song.g[G_DTIME] = 10;                                /* 4T at 40 BPM = 1 s: fits */
    ok &= delay_samples() == 44100u;
    song.g[G_BPM] = 240;
    song.g[G_DTIME] = 14;                                /* 32T at 240 BPM */
    ok &= delay_samples() == 918u;
    check("TIME: a dotted time past the line is cut to it (1.49 s), the others exact", ok);
}

/* ---------------------------------------------------------------------------------------------- the bus --- */
static int32_t cin[CTL], din[CTL], rin[CTL], wt[CTL];
/* n samples of the delay alone (no chorus, no reverb input: their output is exactly 0) from in; L / R: the delay's
 * return as the master mix gets it (NORMAL: the mono wet on both) */
static void bus_run(const int32_t *in, int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t f, i;
    for (f = 0; f < n; f += CTL) {
        for (i = 0; i < CTL; i++) {
            cin[i] = rin[i] = 0;
            din[i] = in && f + i < n ? in[f + i] : 0;
        }
        fx_buses(cin, din, rin, wt, CTL);
        for (i = 0; i < CTL && f + i < n; i++) {
            int32_t s = dpp.side ? dly_side[i] : 0;
            if (l)
                l[f + i] = wt[i] + s;
            if (r)
                r[f + i] = wt[i];
        }
    }
}

/* the delay as it was (0.26.0's fx_buses, its delay lines only), for NORMAL */
static struct { uint32_t w; int32_t lp; int16_t buf[DLY_LEN]; } ref;
static void ref_run(const int32_t *in, int32_t *out, uint32_t n)
{
    uint32_t f, i, dl;
    for (f = 0; f < n; f += CTL) {
        int32_t fb = song.g[G_DFDBK] * 230, col = 2000 + song.g[G_DCOLOR] * 240, dmix = song.g[G_DMIX] * 258;
        dl = delay_samples();
        for (i = 0; i < CTL; i++) {
            int32_t x = ref.buf[(ref.w - dl) & (DLY_LEN - 1u)], d = f + i < n ? in[f + i] : 0;
            ref.lp += mulq15(x - ref.lp, col);
            ref.buf[ref.w & (DLY_LEN - 1u)] = (int16_t)clamp((d >> 1) + mulq15(ref.lp, fb), -32768, 32767);
            ref.w++;
            if (f + i < n)
                out[f + i] = mulq15(x << 1, dmix);
        }
    }
}

#define NB (3u * 65536u)
static int32_t IN[NB], L[NB], R[NB], RF[NB];
static uint32_t trng = 1;
static int32_t rnd(int32_t a)
{
    trng = trng * 1664525u + 1013904223u;
    return (int32_t)(trng >> 8) % (2 * a + 1) - a;
}

static void test_normal_bit_identical(void)
{
    static const struct { int16_t time, fb, col, mix, bpm; } S[] = {
        {1, 60, 70, 90, 120}, {4, 120, 0, 127, 133}, {9, 100, 127, 64, 40}, {3, 0, 30, 127, 240}, {6, 110, 10, 127, 97},
    };
    uint32_t s, i, ok = 1;
    for (s = 0; s < sizeof S / sizeof S[0]; s++) {
        host_init();
        memset(&ref, 0, sizeof ref);
        song.g[G_DTIME] = S[s].time;
        song.g[G_DFDBK] = S[s].fb;
        song.g[G_DCOLOR] = S[s].col;
        song.g[G_DMIX] = S[s].mix;
        song.g[G_BPM] = S[s].bpm;
        for (i = 0; i < NB; i++)
            IN[i] = i < NB / 3u || (i / 4096u) % 5u == 0 ? rnd(i & 1u ? 300000 : 60000) : 0;
        bus_run(IN, L, R, NB);
        ref_run(IN, RF, NB);
        for (i = 0; i < NB; i++)
            ok &= L[i] == RF[i] && R[i] == RF[i];
    }
    check("NORMAL: the delay's return = the old loop's, sample for sample (5 settings, loud random sends, 4.5 s each)",
          ok);
}

/* the kit with the delay busy (hats and the clap into it, high FEEDBACK), 2 bars; its hash pinned from 0.26.0 */
#define KIT_N (2u * 16u * (FS * 60 / 120 / 4) / CTL * CTL)
#define HASH_DLY_KIT 0xb944c19cu   /* (re-pinned: the snare's ROOM reverb, its loops rounding towards zero) */
static int32_t KL[KIT_N], KR[KIT_N];
static uint32_t kit_hash(int16_t time)
{
    uint32_t i, k;
    host_init();
    for (i = 0; i < 4u; i++)
        for (k = 0; k < 16u; k++)
            trk[i].step[k].on = k % (i + 2u) == 0;
    trk[2].p[P_DLY] = 100;
    trk[3].p[P_DLY] = 127;
    trk[1].p[P_REV] = 60;
    song.g[G_DTIME] = time;
    song.g[G_DFDBK] = 110;
    song.g[G_DCOLOR] = 40;
    transport_req = 1;
    render_mix(KL, KR, KIT_N);
    return fnv(KL, KIT_N) ^ fnv(KR, KIT_N) * 31u;
}

static void test_normal_kit_pinned(void)
{
    uint32_t h = kit_hash(4) ^ kit_hash(1) * 7u;
    if (!HASH_DLY_KIT)
        printf("kit hash %08x\n", h);
    check("NORMAL: the kit through the delay (8T, 1/8), the master's output = 0.26.0's (hash)", h == HASH_DLY_KIT);
}

/* ------------------------------------------------------------------------------------------- PING-PONG --- */
/* a smooth burst (a Hann window, 256 samples, mostly low frequencies: the half rate's filters keep it) at t0 */
#define T0 1000u
static void burst(int32_t *in, uint32_t n, int32_t amp)
{
    uint32_t i;
    memset(in, 0, n * sizeof *in);
    for (i = 0; i < 256u; i++)
        in[T0 + i] = (int32_t)(amp * 0.5 * (1.0 - cos(2.0 * M_PI * i / 256.0)));
}
/* the centre of x's energy in [a, b) and its peak */
static double centre(const int32_t *x, uint32_t a, uint32_t b, double *peak)
{
    double s = 0, st = 0, p = 0;
    uint32_t i;
    for (i = a; i < b; i++) {
        double e = (double)x[i] * x[i];
        s += e;
        st += e * i;
        if (fabs((double)x[i]) > p)
            p = fabs((double)x[i]);
    }
    *peak = p;
    return s > 0 ? st / s : -1.0;
}
static double energy(const int32_t *x, uint32_t a, uint32_t b)
{
    double s = 0;
    uint32_t i;
    for (i = a; i < b; i++)
        s += (double)x[i] * x[i];
    return s;
}

static void pp_setup(int16_t time, int16_t bpm, int16_t fb)
{
    host_init();
    song.g[G_DMODE] = 1;
    song.g[G_DTIME] = time;
    song.g[G_BPM] = bpm;
    song.g[G_DFDBK] = fb;
    song.g[G_DCOLOR] = 127;                              /* the loop's low-pass open */
    song.g[G_DMIX] = 127;
    bus_run(0, 0, 0, 8u * CTL);                          /* (the mode switched in: the line cleared) */
}

/* echoes k = 1.. at T0 + 128 + k d: odd k on the left, even on the right; the other side silent there; each one
 * FEEDBACK x the one before (as NORMAL's); returns how many matched */
static uint32_t pp_echoes(uint32_t d, uint32_t nk, double fbq, double *worst_dt, double *worst_ratio)
{
    uint32_t k, good = 0;
    double prev = 0;
    *worst_dt = 0;
    *worst_ratio = 0;
    for (k = 1; k <= nk; k++) {
        uint32_t c = T0 + 128u + k * d, a = c - 400u, b = c + 400u;
        const int32_t *on = k & 1u ? L : R, *off = k & 1u ? R : L;
        double pk, t = centre(on, a, b, &pk), eo = energy(on, a, b), ef = energy(off, a, b);
        double dt = fabs(t - c), ratio = prev > 0 ? pk / prev / fbq : 1.0;
        if (dt > *worst_dt)
            *worst_dt = dt;
        if (fabs(ratio - 1.0) > fabs(*worst_ratio - 1.0) || k == 2u)
            *worst_ratio = ratio;
        good += dt <= 2.0 && ef < eo * 1e-4 && fabs(ratio - 1.0) < 0.06 && pk > 100.0;
        prev = pk;
    }
    return good;
}

static void test_pingpong_impulse(void)
{
    uint32_t d, g;
    double dt, ratio, pk;
    char what[200];
    pp_setup(0, 120, 100);                               /* 1/4 at 120 BPM: 22050 samples */
    d = delay_samples();
    burst(IN, NB, 20000);
    bus_run(IN, L, R, 6u * 22050u + 4000u);
    g = pp_echoes(d, 5, 100.0 * 230.0 / 32768.0, &dt, &ratio);
    snprintf(what, sizeof what, "PING-PONG: a burst's echoes at d, 2d .. 5d alternate L R L R L, the other side silent, "
                                "each FEEDBACK x the last (5 of 5: %u; |dt| <= %.2f, ratio %.3f)", g, dt, ratio);
    check(what, g == 5u);
    check("PING-PONG: nothing before the first echo, the dry not in the return",
          energy(L, 0, T0 + 128u + d - 400u) == 0 && energy(R, 0, T0 + 128u + 2u * d - 400u) == 0);

    pp_setup(10, 131, 60);                               /* 4T at 131 BPM: an odd delay (13465 samples) */
    d = delay_samples();
    bus_run(IN, L, R, 5u * d + 4000u);
    g = pp_echoes(d, 4, 60.0 * 230.0 / 32768.0, &dt, &ratio);
    snprintf(what, sizeof what, "PING-PONG: an odd TIME (4T at 131 BPM, %u samples): 4 echoes in place (%u; |dt| <= "
                                "%.2f, ratio %.3f)", d, g, dt, ratio);
    check(what, (d & 1u) && g == 4u);

    pp_setup(9, 40, 120);                                /* 4BAR at 40 BPM: cut to the line, 65535 samples */
    d = delay_samples();
    bus_run(IN, L, R, 2u * d + 4000u);
    g = pp_echoes(d, 2, 120.0 * 230.0 / 32768.0, &dt, &ratio);
    (void)centre(L, T0 + 128u + d - 400u, T0 + 128u + d + 400u, &pk);
    snprintf(what, sizeof what, "PING-PONG: the longest TIME is NORMAL's (1.49 s, %u samples): the left at d, the right "
                                "at 2d (%u of 2; |dt| <= %.2f)", d, g, dt);
    check(what, d == DLY_LEN - 1u && g == 2u);
}

/* PING-PONG's level and NORMAL's: the same echoes (the first ones within 0.5 dB) */
static void test_pingpong_level(void)
{
    double p1, p2, n1, n2;
    pp_setup(1, 120, 90);
    burst(IN, NB, 20000);
    bus_run(IN, L, R, 3u * 11025u + 4000u);
    (void)centre(L, T0 + 128u + 11025u - 400u, T0 + 128u + 11025u + 400u, &p1);
    (void)centre(R, T0 + 128u + 22050u - 400u, T0 + 128u + 22050u + 400u, &p2);
    host_init();
    song.g[G_DTIME] = 1;
    song.g[G_DFDBK] = 90;
    song.g[G_DCOLOR] = 127;
    song.g[G_DMIX] = 127;
    bus_run(IN, L, R, 3u * 11025u + 4000u);
    (void)centre(L, T0 + 128u + 11025u - 400u, T0 + 128u + 11025u + 400u, &n1);
    (void)centre(L, T0 + 128u + 22050u - 400u, T0 + 128u + 22050u + 400u, &n2);
    check("PING-PONG: its first two echoes as loud as NORMAL's (within 0.5 dB)",
          fabs(20.0 * log10(p1 / n1)) < 0.5 && fabs(20.0 * log10(p2 / n2)) < 0.5);
}

/* the loudest sends at full FEEDBACK: no overflow (UBSan), the line saturates and stays bounded */
static void test_pingpong_loud(void)
{
    uint32_t i, ok = 1;
    pp_setup(5, 240, 120);
    song.g[G_DCOLOR] = 0;
    for (i = 0; i < NB; i++)
        IN[i] = i < NB / 2u ? rnd(2000000) : 0;
    bus_run(IN, L, R, NB);
    for (i = 0; i < NB; i++)
        ok &= L[i] >= -70000 && L[i] <= 70000 && R[i] >= -70000 && R[i] <= 70000;
    check("PING-PONG: loud sends at full FEEDBACK stay bounded (no overflow)", ok);
}

/* a mode change clears the line: nothing of the old echoes plays after it, the new mode starts from silence */
static void test_switch_clears(void)
{
    uint32_t i, ok = 1, m;
    for (m = 0; m < 2u; m++) {
        double pk;
        host_init();
        song.g[G_DMODE] = (int16_t)m;
        song.g[G_DTIME] = 1;
        song.g[G_DFDBK] = 120;
        bus_run(0, 0, 0, 8u * CTL);
        burst(IN, NB, 25000);
        bus_run(IN, L, R, 30000u);                       /* echoes in the line */
        ok &= energy(L, 0, 30000u) + energy(R, 0, 30000u) > 0;
        song.g[G_DMODE] = (int16_t)!m;                   /* switch, nothing more sent */
        bus_run(0, L, R, 100000u);
        for (i = 0; i < 100000u; i++)
            ok &= L[i] == 0 && R[i] == 0;
        bus_run(IN, L, R, 30000u);                       /* the new mode works */
        (void)centre(L, T0 + 128u + 11025u - 400u, T0 + 128u + 11025u + 400u, &pk);
        ok &= pk > 1000.0;
    }
    check("MODE: a switch (both ways) clears the line: silence after it, then the new mode's echoes", ok);
}

/* PING-PONG through the master: the left has the first echo, the right the second; NORMAL's are equal */
static void test_pingpong_master(void)
{
    static int32_t ML[60000], MR[60000];
    uint32_t m, ok = 1;
    for (m = 0; m < 2u; m++) {
        uint32_t k;
        host_init();
        for (k = 0; k < NTRK; k++)
            trk[k].p[P_REV] = trk[k].p[P_CHOR] = trk[k].p[P_DLY] = 0;
        trk[3].step[0].on = 1;                           /* one hat, 1/4 = 22050 at 120 BPM */
        trk[3].p[P_DLY] = 127;
        trk[3].p[P_SLEN] = 64;
        song.g[G_DMODE] = (int16_t)m;
        song.g[G_DTIME] = 0;
        song.g[G_DFDBK] = 60;
        transport_req = 1;
        render_mix(ML, MR, 60000u);
        if (m) {
            double l1 = energy(ML, 21000u, 30000u), r1 = energy(MR, 21000u, 30000u);
            double l2 = energy(ML, 43000u, 52000u), r2 = energy(MR, 43000u, 52000u);
            ok &= l1 > 50.0 * r1 && r2 > 50.0 * l2 && l1 > 0 && r2 > 0;
        } else {
            for (k = 0; k < 60000u; k++)
                ok &= ML[k] == MR[k];
        }
    }
    check("PING-PONG through the master: the first echo on the left, the second on the right (NORMAL: L = R)", ok);
}

/* MENU > USB AUDIO 10 CH FX: channels 9-10 carry the delay's left and right in PING-PONG */
static void test_pingpong_usb_fx(void)
{
    static int32_t UL[60000], UR[60000];
    uint32_t f, i, k, ok = 1;
    host_init();
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_REV] = trk[k].p[P_CHOR] = trk[k].p[P_DLY] = 0;
    trk[3].step[0].on = 1;
    trk[3].p[P_DLY] = 127;
    trk[3].p[P_SLEN] = 64;
    song.g[G_DMODE] = 1;
    song.g[G_DTIME] = 0;
    song.g[G_DFDBK] = 60;
    usb10 = 1;
    usb_fxpair = 1;
    usb8_g = 32768;
    transport_req = 1;
    for (f = 0; f < 60000u; f += CTL) {
        const int32_t *p;
        mix_block(mixo, CTL);
        p = usb8_main(mixo, CTL);
        for (i = 0; i < CTL && f + i < 60000u; i++) {
            UL[f + i] = p[2u * i];
            UR[f + i] = p[2u * i + 1u];
        }
    }
    usb10 = 0;
    usb_fxpair = 0;
    usb8_g = 0;
    {
        double l1 = energy(UL, 21000u, 30000u), r1 = energy(UR, 21000u, 30000u);
        double l2 = energy(UL, 43000u, 52000u), r2 = energy(UR, 43000u, 52000u);
        ok &= l1 > 50.0 * r1 && r2 > 50.0 * l2 && l1 > 0 && r2 > 0;
    }
    check("PING-PONG on USB 10 CH FX: channel 9 the first echo, channel 10 the second", ok);
}

int main(void)
{
    test_times();
    test_normal_bit_identical();
    test_normal_kit_pinned();
    test_pingpong_impulse();
    test_pingpong_level();
    test_pingpong_loud();
    test_switch_clears();
    test_pingpong_master();
    test_pingpong_usb_fx();
    printf("%s (%d failed)\n", fails ? "delay_test: FAILED" : "delay_test: all passed", fails);
    return fails ? 1 : 0;
}
