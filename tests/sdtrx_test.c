/* SDTRX (dm_sdtrx.c): the fixed-point voice against its float reference (the md-re prototype trx-sd.c, copied below),
 * the measured laws (osc A's pitch, DECAY, osc B's comb, BUMP, SNAP, CLIP), the level, the extremes. Each test prints
 * one line; any failure exits 1.
 *   sdtrx_test [OUTDIR]   OUTDIR: also writes an A/B set as float WAVs in the prototype's units (md-re renders' names) */
#include "drum_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}
#define SECS(s) ((uint32_t)((s) * FS) / CTL * CTL)
static int32_t wl[SECS(6)], w2[SECS(1)];
static double ref[SECS(1)];

static double tau_d32(uint32_t d) { return -1 / (FS * log(1 - d / 4294967296.0)); }   /* a Q32 fall's tau, s */
static double tau_dec(int d) { return tau_d32(0xFFFFFFFFu / ktrx_tau(2624u, (d - 64) * 6492 / 1000, 100u)); }

/* the prototype's CLIP drive scale (md-re proto/trx-sd.c clipk) */
static double clipk(double c)
{
    static const double CK[17] = {0, 0, 0, 0, 0, 0, 0.5, 1.0, 1.27, 1.66, 4.11, 7.48, 11.64, 15.66, 30.56, 104.0, 370.0};
    double x = c / 8, f;
    int i = x >= 15 ? 15 : (int)x;
    f = i == 15 ? (c - 120) / 7 : x - i;
    if (CK[i] <= 0)
        return CK[i + 1] * f;
    return exp(log(CK[i]) + (log(CK[i + 1]) - log(CK[i])) * f);
}

typedef struct { double a1, a2, a3, k, ic1, ic2; } fsvf;   /* Simper's SVF in float, the firmware's coefficients */
static void fsvf_set(fsvf *s, const dsvf_t *c)
{
    s->a1 = c->a1 / 8192.0;
    s->a2 = c->a2 / 8192.0;
    s->a3 = c->a3 / 8192.0;
    s->k = c->k / 4096.0;
    s->ic1 = s->ic2 = 0;
}
static double fsvf_run(fsvf *s, double v0, double *bp, double *hp)
{
    double v3 = v0 - s->ic2, v1 = s->a1 * s->ic1 + s->a2 * v3, v2 = s->ic2 + s->a2 * s->ic1 + s->a3 * v3;
    s->ic1 = 2 * v1 - s->ic1;
    s->ic2 = 2 * v2 - s->ic2;
    *bp = v1;
    *hp = v0 - s->k * v1 - v2;
    return v2;
}

/* the prototype (md-re proto/trx-sd.c) in float, TUNE in semitones (Hortator's pitch table: both sines and BUMP move
 * by the ratio; the machine's PTCH and TUNE at their neutral 64 and 0), osc B's F1 / F2 as the firmware stores them,
 * DECAY's and the noise's taus from their Q32 steps, SNAP's filters with the firmware's coefficients, and the
 * firmware's noise (the same xorshift from the same seed) */
static void ref_render(const int16_t *p, double *y, uint32_t n)
{
    int s = p[0], D = p[1], S = p[2], T = p[3], BU = p[4], BE = p[5], C = p[6];
    double ratio = pow2_q16(s * 16) / 65536.0;
    double fa = inc_tune(HZ(341.8), s) / 4294967296.0;      /* cycles per sample */
    double f1 = (uint32_t)(inc_tune((uint32_t)((SDTRX_F1[BE] * SDTRX_INC4) >> 16), s) << 5) / 4294967296.0;
    double f2 = inc_tune((uint32_t)((SDTRX_F2[BE] * SDTRX_INC4) >> 16), s) / 4294967296.0;
    double tau = tau_dec(D), ga = 1 - T / 254.0, gb = T / 254.0;
    double bk = 42.0 * pow(BU / 127.0, 2.2) * ratio, bt = 0.0051 * pow(2, BE / 120.0);
    double ck = C > 40 ? clipk(C) : 0;
    double nl = 0.394 * S / 127, ntau = tau_d32(SDTRX_D_N);
    double xl = S > 64 ? 0.2675 * pow((S - 64) / 63.0, 1.59) : 0, xtau = tau_d32(SDTRX_D_X);
    double hpk = SDTRX_HPK / 1073741824.0, hx = 0, hy = 0, pa = 0.75, pb0 = 0.75, pb = 0.75, bp, hp;
    int32_t rng = SDTRX_SEED;
    fsvf h, l, b;
    uint32_t i;
    fsvf_set(&h, &SDTRX_HP);
    fsvf_set(&l, &SDTRX_LP);
    fsvf_set(&b, &SDTRX_BP);
    for (i = 0; i < n; i++) {
        double t = i / (double)FS, osc, v;
        uint32_t blk = i / 32, j = i % 32;
        if (j == 0 && blk > 1)
            pb0 += f1;                                  /* osc B: the block's start phase moves at F1 */
        if (blk >= 1)
            pb = pb0 + f2 * j;                          /* .. and runs at F2 inside it */
        osc = ga * sin(2 * M_PI * pa) + gb * sin(2 * M_PI * pb);
        if (blk >= 1)                                   /* the first block is frozen */
            pa += fa + bk * (t / bt) * exp(1 - t / bt) / FS;
        if (ck > 0) {
            double d = ck * exp(-t / (0.0595 / 2.1));
            if (d > 1e-4)
                osc = tanh(d * osc) / tanh(d);
        }
        v = 0.2074 * osc * exp(-t / tau);
        if (S > 0) {
            double w = ((int32_t)noise32(&rng) >> 16) / 32768.0, lo;
            fsvf_run(&h, w, &bp, &hp);
            lo = fsvf_run(&l, hp, &bp, &hp);
            v += nl * lo * exp(-t / ntau);
            if (xl > 0) {
                fsvf_run(&b, w, &bp, &hp);
                v += xl * b.k * bp * exp(-t / xtau);
            }
        }
        hy = hpk * (hy + v - hx);
        hx = v;
        y[i] = hy;
    }
}

/* track 0 = SDTRX with knobs k (-99: the default), one hit at vel, frames rendered into x */
static void sd_hit(const int16_t *k, uint32_t vel, int32_t *x, uint32_t frames)
{
    uint32_t i;
    host_init();
    drum_set_model(&trk[0], DM_SDTRX);
    for (i = 0; i < 8u; i++)
        if (k[i] != -99)
            trk[0].p[P_E0 + i] = k[i];
    drum_play(&trk[0], vel, trk[0].nt);   /* (drum_hit plays note 0) */
    render_track(&trk[0], x, frames);
}

/* the firmware against the reference over [0, n): the least-squares gain and the residual (dB re the reference) */
static double vs_ref(const int16_t *k, uint32_t n, double *gain)
{
    double xy = 0, yy = 0, e = 0, r = 1e-30;
    uint32_t i;
    sd_hit(k, 127, wl, n);
    ref_render(k, ref, n);
    for (i = 0; i < n; i++) {
        xy += wl[i] * ref[i];
        yy += ref[i] * ref[i];
    }
    *gain = xy / yy;
    for (i = 0; i < n; i++) {
        double d = wl[i] - *gain * ref[i];
        e += d * d;
        r += *gain * *gain * ref[i] * ref[i];
    }
    return 10 * log10(e / r + 1e-30);
}

static void test_sdtrx_vs_ref(void)
{
    /* TUNE DECAY SNAP TONE BUMP BENV CLIP */
    static const int16_t K[][8] = {
        {0, 64, 64, 64, 0, 64, 0, 0},    {0, 64, 0, 64, 0, 64, 0, 0},     {-24, 64, 0, 0, 0, 64, 0, 0},
        {24, 64, 0, 127, 0, 64, 0, 0},   {0, 0, 0, 64, 0, 64, 0, 0},      {0, 127, 0, 64, 0, 64, 0, 0},
        {0, 64, 0, 64, 127, 0, 0, 0},    {0, 64, 0, 0, 127, 127, 0, 0},   {12, 64, 0, 64, 64, 64, 0, 0},
        {0, 64, 0, 127, 0, 0, 0, 0},     {0, 64, 0, 127, 0, 40, 0, 0},    {0, 64, 0, 127, 0, 48, 0, 0},
        {0, 64, 0, 127, 0, 127, 0, 0},   {7, 64, 0, 127, 0, 120, 0, 0},   {0, 64, 32, 64, 0, 64, 0, 0},
        {0, 64, 96, 64, 0, 64, 0, 0},    {0, 64, 127, 64, 0, 64, 0, 0},   {0, 64, 127, 0, 0, 64, 0, 0},
        {0, 64, 0, 64, 0, 64, 64, 0},    {0, 64, 0, 64, 0, 64, 96, 0},    {0, 64, 64, 64, 0, 64, 127, 0},
        {0, 32, 0, 0, 0, 64, 127, 0},    {-12, 100, 127, 100, 127, 20, 112, 0}, {0, 64, 0, 127, 0, 0, 44, 0}};
    double worst = -999, worst_clip = -999, g, g0 = 0, gw = 1;
    uint32_t i;
    for (i = 0; i < sizeof K / sizeof K[0]; i++) {
        double e = vs_ref(K[i], SECS(0.3), &g);
        if (!i)
            g0 = g;
        if (fabs(g / g0 - 1) > fabs(gw - 1))
            gw = g / g0;
        if (K[i][6] > 64) {
            if (e > worst_clip)
                worst_clip = e;
        } else if (e > worst)
            worst = e;
        if (e > -30)
            printf("     knobs %d %d %d %d %d %d %d: residual %.1f dB\n", K[i][0], K[i][1], K[i][2], K[i][3], K[i][4],
                   K[i][5], K[i][6], e);
    }
    printf("     vs the float reference (%u settings, 0.3 s): worst residual %.1f dB (with CLIP %.1f dB), gain %.0f / "
           "unit (spread %.2f %%)\n", (unsigned)(sizeof K / sizeof K[0]), worst, worst_clip, g0, 100 * (gw - 1));
    check("SDTRX: every setting matches the float reference (residual < -30 dB, CLIP's too, one gain within 1 %)",
          worst < -30 && worst_clip < -30 && fabs(gw - 1) < 0.01);
}

static double zc_freq(const int32_t *x, uint32_t a, uint32_t b)   /* rising zero crossings / time */
{
    uint32_t i, n = 0, f = 0, l = 0;
    for (i = a + 1; i < b; i++)
        if (x[i - 1] < 0 && x[i] >= 0) {
            if (!n)
                f = i;
            l = i;
            n++;
        }
    return n > 1 ? (n - 1) * (double)FS / (l - f) : 0;
}

static double rms(const int32_t *x, uint32_t a, uint32_t b)
{
    double s = 0;
    uint32_t i;
    for (i = a; i < b; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (b - a));
}

static double goertzel_c(const int32_t *x, uint32_t n, double f, double *ph)   /* Hann-windowed; magnitude, phase */
{
    double re = 0, im = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double w = x[i] * (0.5 - 0.5 * cos(2 * M_PI * i / n));
        re += w * cos(2 * M_PI * f * i / FS);
        im -= w * sin(2 * M_PI * f * i / FS);
    }
    if (ph)
        *ph = atan2(im, re);
    return sqrt(re * re + im * im) / n;
}
static double goertzel(const int32_t *x, uint32_t n, double f) { return goertzel_c(x, n, f, 0); }

/* the measured laws: osc A 341.8 Hz at TUNE 0 (+-12 an octave); DECAY tau = 59.5 ms x 1.455 ^ ((d - 64) / 16) */
static void test_sdtrx_laws(void)
{
    static const int16_t T0[8] = {0, 127, 0, 0, 0, 64, 0, 0}, TU[8] = {12, 127, 0, 0, 0, 64, 0, 0},
                         TD[8] = {-12, 127, 0, 0, 0, 64, 0, 0};
    static const int D[3] = {32, 64, 96};
    double f, fu, fd, tau[3], wd = 0;
    uint32_t i;
    int d;
    sd_hit(T0, 127, wl, SECS(0.4));
    f = zc_freq(wl, SECS(0.05), SECS(0.4));
    sd_hit(TU, 127, wl, SECS(0.4));
    fu = zc_freq(wl, SECS(0.05), SECS(0.4));
    sd_hit(TD, 127, wl, SECS(0.4));
    fd = zc_freq(wl, SECS(0.05), SECS(0.4));
    printf("     TUNE -12 / 0 / +12: %.2f / %.2f / %.2f Hz\n", fd, f, fu);
    check("SDTRX: TUNE 0 is 341.8 Hz (+-0.3 %), +-12 an octave", fabs(f / 341.8 - 1) < 0.003 &&
          fabs(fu / f / 2 - 1) < 0.003 && fabs(f / fd / 2 - 1) < 0.003);
    for (i = 0; i < 3u; i++) {
        int16_t k[8] = {0, 0, 0, 0, 0, 64, 0, 0};
        double want = 0.0595 * pow(1.455, (D[i] - 64) / 16.0);
        k[1] = (int16_t)D[i];
        sd_hit(k, 127, wl, SECS(1.2));
        tau[i] = (want * 2) / log(rms(wl, SECS(0.01), SECS(0.01) + SECS(0.02)) /
                                  rms(wl, SECS(0.01 + want * 2), SECS(0.01 + want * 2) + SECS(0.02)));
        tau[i] /= want;
    }
    for (d = 0; d <= 127; d++)
        wd = fmax(wd, fabs(tau_dec(d) / (0.0595 * pow(1.455, (d - 64) / 16.0)) - 1));
    printf("     DECAY 32 / 64 / 96 heard: tau %.2f / %.2f / %.2f x the law; its factor vs the law, 0..127: worst "
           "%.2f %%\n", tau[0], tau[1], tau[2], 100 * wd);
    check("SDTRX: DECAY follows tau = 59.5 ms x 1.455 ^ ((d - 64) / 16) (heard within 5 %, the factor within 1 %)",
          fabs(tau[0] - 1) < 0.05 && fabs(tau[1] - 1) < 0.05 && fabs(tau[2] - 1) < 0.05 && wd < 0.01);
}

/* osc B at BENV 64: its line at F1 (1257 Hz) as loud as osc A's at TONE 127 (both 0.5), the next comb line (F1 +
 * 1378 Hz, far from F2 = 1309 Hz) > 20 dB under; the first block frozen at the trough (both sines start at -1) */
static void test_sdtrx_oscb(void)
{
    static const int16_t K[8] = {0, 127, 0, 127, 0, 64, 0, 0}, K0[8] = {0, 127, 0, 0, 0, 64, 0, 0};
    double a, b, c, z;
    uint32_t i, frozen = 1;
    sd_hit(K, 127, wl, SECS(0.25));
    a = goertzel(wl + SECS(0.02), SECS(0.2), 341.8);
    b = goertzel(wl + SECS(0.02), SECS(0.2), 1257.0);
    c = goertzel(wl + SECS(0.02), SECS(0.2), 1257.0 + 1378.125);
    sd_hit(K0, 127, wl, SECS(0.25));
    z = goertzel(wl + SECS(0.02), SECS(0.2), 1257.0);
    printf("     TONE 127, BENV 64: osc B's line at F1 %+.1f dB re A's, the next comb line %+.1f dB; TONE 0: %+.1f dB\n",
           20 * log10(b / a), 20 * log10(c / b), 20 * log10(z / a));
    for (i = 1; i < 32u; i++)                           /* the first block: a level step only (the high-pass' fall) */
        frozen &= wl[i] < 0 && wl[i] >= wl[i - 1] && wl[i] - wl[i - 1] < -wl[0] / 100;
    check("SDTRX: osc B's comb (F1's line at A's level +-2 dB, the next < -20 dB, none at TONE 0); the first block "
          "frozen at the trough", fabs(20 * log10(b / a)) < 2 && c < b * 0.1 && z < a * 0.01 && frozen && wl[0] < 0);
}

/* BUMP: osc A gains K e tb cycles (the alpha function's integral; 0.58 at BUMP 127, BENV 0) */
static void test_sdtrx_bump(void)
{
    static const int16_t B0[8] = {0, 127, 0, 0, 0, 0, 0, 0}, B1[8] = {0, 127, 0, 0, 127, 0, 0, 0},
                         B2[8] = {0, 127, 0, 0, 127, 127, 0, 0};
    double p0, p1, p2, d1, d2, w1 = 42 * exp(1) * 0.0051, w2 = 42 * exp(1) * 0.0051 * pow(2, 127 / 120.0);
    sd_hit(B0, 127, wl, SECS(0.4));
    goertzel_c(wl + SECS(0.2), SECS(0.1), 341.8, &p0);
    sd_hit(B1, 127, wl, SECS(0.4));
    goertzel_c(wl + SECS(0.2), SECS(0.1), 341.8, &p1);
    sd_hit(B2, 127, wl, SECS(0.4));
    goertzel_c(wl + SECS(0.2), SECS(0.1), 341.8, &p2);
    d1 = fmod((p1 - p0) / (2 * M_PI) + 4, 1);
    d2 = fmod((p2 - p0) / (2 * M_PI) + 4, 1);
    printf("     BUMP 127: osc A ahead by %.3f cycles at BENV 0 (law %.3f), %.3f at BENV 127 (law %.3f)\n", d1,
           fmod(w1, 1), d2, fmod(w2, 1));
    check("SDTRX: BUMP's alpha rise follows 42 Hz x (t / tb) e ^ (1 - t / tb), tb = 5.1 ms x 2 ^ (BENV / 120) "
          "(the phase gained within 0.02 cycles)", fabs(d1 - fmod(w1, 1)) < 0.02 && fabs(d2 - fmod(w2, 1)) < 0.02);
}

/* SNAP: the same noise on every hit; linear in the knob (+6 dB from 32 to 64); gone (with the body) by DECAY's end */
static void test_sdtrx_snap(void)
{
    static const int16_t S0[8] = {0, 64, 0, 64, 0, 64, 0, 0}, S32[8] = {0, 64, 32, 64, 0, 64, 0, 0},
                         S64[8] = {0, 64, 64, 64, 0, 64, 0, 0};
    double a, b;
    uint32_t i, same = 1, n = SECS(0.1);
    static int32_t body[SECS(0.1)];
    sd_hit(S0, 127, body, n);
    sd_hit(S32, 127, wl, n);
    for (i = 0; i < n; i++)
        wl[i] -= body[i];
    a = rms(wl, 0, n);
    sd_hit(S64, 127, w2, n);
    for (i = 0; i < n; i++)
        w2[i] -= body[i];
    b = rms(w2, 0, n);
    host_init();                                         /* a second hit of the same track: the same noise */
    drum_set_model(&trk[0], DM_SDTRX);
    drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
    render_track(&trk[0], wl, SECS(0.5));
    drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
    render_track(&trk[0], wl, n);
    sd_hit(S64, 127, body, n);
    for (i = SECS(0.002); i < n; i++)                   /* (the cut voice's declick aside) */
        same &= abs(wl[i] - body[i]) <= 1;
    printf("     SNAP 64 vs 32: the noise %+.2f dB\n", 20 * log10(b / a));
    check("SDTRX: SNAP linear (+6 +-0.3 dB from 32 to 64); the same noise on every hit", fabs(20 * log10(b / a) - 6.02) <
          0.3 && same);
}

/* CLIP 127 (TONE 0): osc A a square early (3rd harmonic ~ -9.5 dB, the fundamental +2.1 dB), clean by 250 ms */
static void test_sdtrx_clip(void)
{
    static const int16_t C0[8] = {0, 96, 0, 0, 0, 64, 0, 0}, C1[8] = {0, 96, 0, 0, 0, 64, 127, 0};
    double f0, f1, h3, h3l;
    sd_hit(C0, 127, wl, SECS(0.4));
    f0 = goertzel(wl + SECS(0.01), SECS(0.05), 341.8);
    sd_hit(C1, 127, wl, SECS(0.4));
    f1 = goertzel(wl + SECS(0.01), SECS(0.05), 341.8);
    h3 = 20 * log10(goertzel(wl + SECS(0.01), SECS(0.05), 3 * 341.8) / f1);
    h3l = 20 * log10(goertzel(wl + SECS(0.25), SECS(0.1), 3 * 341.8) / goertzel(wl + SECS(0.25), SECS(0.1), 341.8));
    printf("     CLIP 127: 10-60 ms the fundamental %+.1f dB, 3rd %.1f dB; 250-350 ms 3rd %.1f dB\n",
           20 * log10(f1 / f0), h3, h3l);
    check("SDTRX: CLIP 127 a square early (fundamental +2.1 +-0.5 dB, 3rd -9.5 +-1.5 dB), clean late (3rd < -40 dB)",
          fabs(20 * log10(f1 / f0) - 2.1) < 0.5 && fabs(h3 + 9.5) < 1.5 && h3l < -40);
}

/* the level at the defaults within +-3 dB of S909's */
static void test_sdtrx_level(void)
{
    static const int16_t DEF[8] = {-99, -99, -99, -99, -99, -99, -99, -99};
    double a, b;
    sd_hit(DEF, 127, wl, SECS(0.3));
    a = rms(wl, 0, SECS(0.3));
    host_init();
    drum_set_model(&trk[0], DM_S909);
    drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
    render_track(&trk[0], wl, SECS(0.3));
    b = rms(wl, 0, SECS(0.3));
    printf("     SDTRX vs S909 at the defaults: %+.1f dB\n", 20 * log10(a / b));
    check("SDTRX: the level at the defaults within +-3 dB of S909's", fabs(20 * log10(a / b)) <= 3);
}

/* the extremes: every knob at 0 / 127 (TUNE -24 / +24) and a step's note +-24, velocity 1 and 127: bounded, ends */
static void test_sdtrx_extremes(void)
{
    uint32_t c, i, ok = 1, ended = 1;
    int32_t peak = 0;
    for (c = 0; c < 256u; c++) {
        int16_t k[8] = {0};
        for (i = 0; i < 7u; i++)
            k[i] = (int16_t)((c >> i & 1u) ? (i ? 127 : 24) : (i ? 0 : -24));
        host_init();
        drum_set_model(&trk[0], DM_SDTRX);
        for (i = 0; i < 8u; i++)
            trk[0].p[P_E0 + i] = k[i];
        trk[0].nt = (c >> 7 & 1u) ? 24 : -24;
        drum_play(&trk[0], (c & 3u) == 3u ? 1 : 127, trk[0].nt);   /* (drum_hit plays note 0) */
        render_track(&trk[0], wl, SECS(6));
        for (i = 0; i < SECS(6); i++) {
            ok &= abs(wl[i]) <= 4 * VOICE_FS;
            peak = abs(wl[i]) > peak ? abs(wl[i]) : peak;
        }
        ended &= !trk[0].v[0].active;
    }
    printf("     extremes: peak %.2f x VOICE_FS\n", peak / (double)VOICE_FS);
    check("SDTRX: every knob at its ends, notes +-24, velocity 1 / 127: bounded, the voice ends within 6 s", ok && ended);
}

/* re-hits 5 ms apart and a switch to SDTRX while another model rings: bounded */
static void test_sdtrx_rehits(void)
{
    static const int16_t K[8] = {24, 127, 127, 127, 127, 127, 127, 0};
    uint32_t i, ok = 1;
    sd_hit(K, 127, wl, SECS(0.005));
    for (i = 0; i < 4u; i++) {
        drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
        render_track(&trk[0], wl, SECS(0.005));
    }
    render_track(&trk[0], wl, SECS(0.3));
    for (i = 0; i < SECS(0.3); i++)
        ok &= abs(wl[i]) <= 4 * VOICE_FS;
    host_init();
    drum_set_model(&trk[0], DM_CYMB);
    drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
    render_track(&trk[0], wl, SECS(0.05));
    drum_set_model(&trk[0], DM_SDTRX);
    drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
    render_track(&trk[0], wl, SECS(0.3));
    for (i = 0; i < SECS(0.3); i++)
        ok &= abs(wl[i]) <= 4 * VOICE_FS;
    check("SDTRX: re-hits 5 ms apart and a model switch while ringing: bounded", ok);
}

/* the A/B set: the md-re renders' names and knobs (the machine's PTCH 64, TUNE 0 = Hortator's TUNE 0) */
static void write_wavs(const char *dir)
{
    static const struct { const char *name; int16_t k[8]; } W[] = {
        {"ptch_064", {0, 64, 64, 64, 0, 64, 0, 0}},  {"dec_032", {0, 32, 64, 64, 0, 64, 0, 0}},
        {"dec_127", {0, 127, 64, 64, 0, 64, 0, 0}},  {"bump_127", {0, 64, 64, 64, 127, 64, 0, 0}},
        {"benv_000", {0, 64, 64, 64, 0, 0, 0, 0}},   {"benv_048", {0, 64, 64, 64, 0, 48, 0, 0}},
        {"snap_000", {0, 64, 0, 64, 0, 64, 0, 0}},   {"snap_127", {0, 64, 127, 64, 0, 64, 0, 0}},
        {"tone_000", {0, 64, 64, 0, 0, 64, 0, 0}},   {"tone_127", {0, 64, 64, 127, 0, 64, 0, 0}},
        {"clip_096", {0, 64, 64, 64, 0, 64, 96, 0}}, {"clip_127", {0, 64, 64, 64, 0, 64, 127, 0}}};
    static const int16_t K0[8] = {0, 64, 0, 64, 0, 64, 0, 0};
    double g;
    uint32_t w, i, n = SECS(1.0);
    vs_ref(K0, SECS(0.3), &g);                       /* the firmware's units per prototype unit */
    for (w = 0; w < sizeof W / sizeof W[0]; w++) {
        char path[512];
        FILE *f;
        uint32_t v;
        snprintf(path, sizeof path, "%s/%s.wav", dir, W[w].name);
        f = fopen(path, "wb");
        if (!f)
            continue;
        sd_hit(W[w].k, 127, wl, n);
        fwrite("RIFF", 1, 4, f); v = 36 + n * 8; fwrite(&v, 4, 1, f);
        fwrite("WAVEfmt \x10\0\0\0\x03\0\x02\0", 1, 16, f);
        v = FS; fwrite(&v, 4, 1, f); v = FS * 8; fwrite(&v, 4, 1, f);
        fwrite("\x08\0\x20\0data", 1, 8, f); v = n * 8; fwrite(&v, 4, 1, f);
        for (i = 0; i < n; i++) {
            float s = (float)(wl[i] / g);
            fwrite(&s, 4, 1, f);
            fwrite(&s, 4, 1, f);
        }
        fclose(f);
    }
    printf("     wrote %u WAVs to %s\n", (unsigned)(sizeof W / sizeof W[0]), dir);
}

int main(int argc, char **argv)
{
    test_sdtrx_vs_ref();
    test_sdtrx_laws();
    test_sdtrx_oscb();
    test_sdtrx_bump();
    test_sdtrx_snap();
    test_sdtrx_clip();
    test_sdtrx_level();
    test_sdtrx_extremes();
    test_sdtrx_rehits();
    if (argc > 1)
        write_wavs(argv[1]);
    printf(fails ? "sdtrx_test: %d FAILED\n" : "sdtrx_test: all passed\n", fails);
    return fails != 0;
}
