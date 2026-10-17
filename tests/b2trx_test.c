/* B2TRX (dm_b2trx.c): the fixed-point voice against its float reference (the md-re prototype trx-b2.c, copied
 * below), the measured laws (pitch, DECAY, HOLD, RAMP, TICK, DIRT, DIST), the level, the extremes. Each test prints one
 * line; any failure exits 1.
 *   b2trx_test [OUTDIR]   OUTDIR: also writes an A/B set as float WAVs in the prototype's units (md-re renders' names) */
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
static int32_t wl[SECS(6)];
static double ref[SECS(1.2)];
static int32_t seed;                                    /* the hit's noise seed (the core's, per hit) */

static double tau_d32(uint32_t d) { return -1 / (FS * log(1 - d / 4294967296.0)); }   /* a Q32 fall's tau, s */
static uint32_t dec_samples(int d) { return ktrx_tau(3175u, (d - 64) * 19277 / 1000, 132u); }

static double interp(const double (*t)[2], int n, double x)
{
    int i;
    if (x <= t[0][0])
        return t[0][1];
    for (i = 1; i < n; i++)
        if (x <= t[i][0])
            return t[i - 1][1] + (t[i][1] - t[i - 1][1]) * (x - t[i - 1][0]) / (t[i][0] - t[i - 1][0]);
    return t[n - 1][1];
}
static double sat(double x)                             /* the output saturation (md-re proto/trx-b2.c) */
{
    static const double T[][2] = {{0, 0}, {0.285, 0.285}, {0.304, 0.2996}, {0.336, 0.3089}, {0.371, 0.3105},
                                  {0.41, 0.3121}, {0.50, 0.3151}, {0.61, 0.3181}, {0.75, 0.3208}, {0.83, 0.3219},
                                  {1e9, 0.322}};
    double a = fabs(x), y = interp(T, 11, a);
    return x < 0 ? -y : y;
}

/* the prototype (md-re proto/trx-b2.c, start phase 0) in float: TUNE in semitones from PTCH 64 (Hortator's pitch
 * table), DECAY's tau from its Q32 step (capped as every voice), HOLD as the firmware caps it (the voice's 6 s), the
 * firmware's noise (the same xorshift from the hit's seed) */
static void ref_render(const int16_t *p, double *y, uint32_t n)
{
    double D = p[1], R = p[2], H = p[3], T = p[4], N = p[5], DI = p[6], DS = p[7];
    double f0 = inc_tune(HZ(50.32), p[0]) * (FS / 4294967296.0);
    double ra = R > 0 ? 420.0 * pow(R / 127.0, 0.9) : 0, rtau = 0.035 + 0.008 * R / 127.0;
    double tau = tau_d32(0xFFFFFFFFu / dec_samples(p[1])), hold = 0;
    double amp = 0.170, atk = 0.0013, tk = -0.0092 * T, tr = 0.905;
    static const double NT[][2] = {{0, 0}, {32, 0.26}, {64, 0.53}, {96, 0.84}, {127, 1.0}};
    double nlev = interp(NT, 5, N) * 0.16, ntau = 0.0091;
    int bits = (int)lround((104 - DI) / 13.0), shaped = DI >= 40 || DS > 0;
    double qs, g = DS > 0 ? fmin(pow(2.0, DS / 12), 100) : 1;
    double lpp = exp(-2 * M_PI * 18.8 / FS), hk = shaped ? 1.87 : 1.0, ph = 0, lp = 0, z1 = 0, z2 = 0;
    int32_t rng = seed;
    uint32_t i;
    (void)D;
    if (H > 0) {
        double hmax = (B2TRX_LIFE - dec_samples(p[1]) * 97u / 10u) / (double)FS;
        hold = fmin(fmax(0, 0.190 * pow(1.647, (H - 64) / 8) - 0.014), hmax);
    }
    if (bits < 0)
        bits = 0;
    qs = DI > 0 && bits <= 8 ? pow(2.0, -bits) : 0;
    for (i = 0; i < n; i++) {
        double t = i / (double)FS, s = sin(2 * M_PI * ph), env, v, hpo;
        ph += (f0 + ra * exp(-t / rtau)) / FS;
        if (ph >= 1)
            ph -= 1;
        if (qs > 0)
            s = qs * floor(s / qs + 0.5);               /* DIRT */
        if (DS > 0) {
            s *= g;
            s = s > 1 ? 1 : s < -1 ? -1 : s;            /* DIST */
        }
        env = t < hold ? 1 : exp(-(t - hold) / tau);
        if (t < atk)
            env *= t / atk;
        v = amp * s * env;
        if (tk != 0)
            v += tk * pow(tr, i);                       /* TICK */
        if (nlev > 0)
            v += nlev * ((int32_t)noise32(&rng) >> 16) / 32768.0 * exp(-t / ntau);   /* NOISE */
        v = sat(v);
        lp = lpp * lp + (1 - lpp) * v;                  /* the high-pass: y - k LP(y) */
        hpo = v - hk * lp;
        y[i] = 0.75 * hpo + 0.21 * z1 + 0.04 * z2;      /* the output smoothing */
        z2 = z1;
        z1 = hpo;
    }
}

/* track 0 = B2TRX with knobs k (-99: the default), one hit at vel, frames rendered into wl */
static void b2_hit(const int16_t *k, uint32_t vel, uint32_t frames)
{
    uint32_t i;
    host_init();
    drum_set_model(&trk[0], DM_B2TRX);
    for (i = 0; i < 8u; i++)
        if (k[i] != -99)
            trk[0].p[P_E0 + i] = k[i];
    drum_play(&trk[0], vel, trk[0].nt);   /* (drum_hit plays note 0) */
    seed = trk[0].v[0].rng;
    render_track(&trk[0], wl, frames);
}

/* the firmware against the reference over [0, n): the least-squares gain and the residual (dB re the reference) */
static double vs_ref(const int16_t *k, uint32_t n, double *gain)
{
    double xy = 0, yy = 0, e = 0, r = 1e-30;
    uint32_t i;
    b2_hit(k, 127, n);
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

static void test_b2trx_vs_ref(void)
{
    /* TUNE DECAY RAMP HOLD TICK NOISE DIRT DIST */
    static const int16_t K[][8] = {
        {0, 64, 0, 0, 0, 0, 0, 0},      {-24, 64, 0, 0, 0, 0, 0, 0},   {24, 64, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0, 0},       {0, 32, 0, 0, 0, 0, 0, 0},     {0, 96, 0, 0, 0, 0, 0, 0},
        {0, 64, 64, 0, 0, 0, 0, 0},     {0, 64, 127, 0, 0, 0, 0, 0},   {-12, 64, 32, 0, 0, 0, 0, 0},
        {0, 64, 0, 64, 0, 0, 0, 0},     {0, 48, 0, 96, 0, 0, 0, 0},    {0, 64, 0, 0, 32, 0, 0, 0},
        {0, 64, 0, 0, 127, 0, 0, 0},    {0, 64, 0, 0, 0, 64, 0, 0},    {0, 64, 0, 0, 0, 127, 0, 0},
        {0, 64, 0, 0, 0, 0, 64, 0},     {0, 64, 0, 0, 0, 0, 96, 0},    {0, 64, 0, 0, 0, 0, 127, 0},
        {0, 64, 0, 0, 0, 0, 0, 16},     {0, 64, 0, 0, 0, 0, 0, 48},    {0, 64, 0, 0, 0, 0, 0, 127},
        {0, 80, 0, 0, 0, 0, 40, 32},    {7, 72, 100, 40, 80, 80, 88, 0}};
    double worst = -999, g, g0 = 0, gw = 1;
    uint32_t i;
    for (i = 0; i < sizeof K / sizeof K[0]; i++) {
        double e = vs_ref(K[i], SECS(0.3), &g);
        if (!i)
            g0 = g;
        if (fabs(g / g0 - 1) > fabs(gw - 1))
            gw = g / g0;
        if (e > worst)
            worst = e;
        if (e > -30)
            printf("     knobs %d %d %d %d %d %d %d %d: residual %.1f dB\n", K[i][0], K[i][1], K[i][2], K[i][3], K[i][4],
                   K[i][5], K[i][6], K[i][7], e);
    }
    printf("     vs the float reference (%u settings, 0.3 s): worst residual %.1f dB, gain %.0f / unit (spread %.2f %%)\n",
           (unsigned)(sizeof K / sizeof K[0]), worst, g0, 100 * (gw - 1));
    check("B2TRX: every setting matches the float reference (residual < -30 dB, one gain within 1 %)",
          worst < -30 && fabs(gw - 1) < 0.01);
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

static double goertzel(const int32_t *x, uint32_t n, double f)
{
    double c = 2 * cos(2 * M_PI * f / FS), s0, s1 = 0, s2 = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        s0 = x[i] * (0.5 - 0.5 * cos(2 * M_PI * i / n)) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}

/* the measured laws: TUNE 0 = 50.3 Hz (PTCH 64), +-12 an octave; DECAY tau = 72 ms x 1.745 ^ ((d - 64) / 8); HOLD
 * H = 190 ms x 1.647 ^ ((h - 64) / 8) - 14 ms at full level; RAMP's added pitch in Hz whatever the note */
static void test_b2trx_laws(void)
{
    static const int16_t T0[8] = {0, 100, 0, 0, 0, 0, 0, 0}, TU[8] = {12, 100, 0, 0, 0, 0, 0, 0},
                         TD[8] = {-12, 100, 0, 0, 0, 0, 0, 0};
    static const int D[3] = {56, 64, 80};
    double f, fu, fd, tau[3], wd = 0, hl[2], hw[2];
    uint32_t i;
    int d;
    b2_hit(T0, 127, SECS(1.2));
    f = zc_freq(wl, SECS(0.1), SECS(1.2));
    b2_hit(TU, 127, SECS(1.2));
    fu = zc_freq(wl, SECS(0.1), SECS(1.2));
    b2_hit(TD, 127, SECS(1.2));
    fd = zc_freq(wl, SECS(0.1), SECS(1.2));
    printf("     TUNE -12 / 0 / +12: %.2f / %.2f / %.2f Hz\n", fd, f, fu);
    check("B2TRX: TUNE 0 is 50.3 Hz (+-0.5 %), +-12 an octave", fabs(f / 50.3 - 1) < 0.005 &&
          fabs(fu / f / 2 - 1) < 0.005 && fabs(f / fd / 2 - 1) < 0.005);
    for (i = 0; i < 3u; i++) {                          /* (RMS over whole periods of the 50 Hz sine) */
        int16_t k[8] = {12, 0, 0, 0, 0, 0, 0, 0};
        double want = 0.072 * pow(1.745, (D[i] - 64) / 8.0), p = 1 / 100.6;
        uint32_t w = (uint32_t)(4 * p * FS);
        k[1] = (int16_t)D[i];
        b2_hit(k, 127, SECS(1.2));
        tau[i] = want / log(rms(wl, SECS(0.03), SECS(0.03) + w) / rms(wl, SECS(0.03 + want), SECS(0.03 + want) + w));
        tau[i] /= want;
    }
    for (d = 0; d <= 127; d++)
        wd = fmax(wd, fabs(dec_samples(d) / fmin(0.58 * FS, fmax(132, 0.072 * FS * pow(1.745, (d - 64) / 8.0))) - 1));
    printf("     DECAY 56 / 64 / 80 heard: tau %.2f / %.2f / %.2f x the law; its tau vs the law, 0..127 (3 ms .. 0.58 s): "
           "worst %.2f %%\n", tau[0], tau[1], tau[2], 100 * wd);
    check("B2TRX: DECAY follows tau = 72 ms x 1.745 ^ ((d - 64) / 8) (heard within 5 %, the tau within 1 %)",
          fabs(tau[0] - 1) < 0.05 && fabs(tau[1] - 1) < 0.05 && fabs(tau[2] - 1) < 0.05 && wd < 0.01);
    for (i = 0; i < 2u; i++) {                          /* HOLD 64 / 88: full level until H, then DECAY's fall */
        int16_t k[8] = {12, 40, 0, 0, 0, 0, 0, 0};
        uint32_t j, w = (uint32_t)(FS / 100.6 * 2), last = 0;
        double full;
        k[3] = (int16_t)(i ? 88 : 64);
        hw[i] = 0.190 * pow(1.647, (k[3] - 64) / 8.0) - 0.014;
        b2_hit(k, 127, SECS(1.2));
        full = rms(wl, SECS(0.02), SECS(0.02) + w);
        for (j = SECS(0.02); j + w < SECS(1.2); j += 32u)
            if (rms(wl, j, j + w) > full * 0.7)         /* (-3 dB: tau 13.5 ms falls by it in ~4.7 ms) */
                last = j + w / 2;
        hl[i] = last / (double)FS - 0.0047;
    }
    printf("     HOLD 64 / 88: full level for %.3f / %.3f s (law %.3f / %.3f)\n", hl[0], hl[1], hw[0], hw[1]);
    check("B2TRX: HOLD as measured (within 10 ms)", fabs(hl[0] - hw[0]) < 0.01 && fabs(hl[1] - hw[1]) < 0.01);
}

/* RAMP 127: the added pitch in Hz, the same on every note (the pitch 10 ms after the hit, 408 Hz x e ^ (-10 / ~43)) */
static void test_b2trx_ramp(void)
{
    double fa, fb, want = 420 * exp(-0.01 / 0.043);
    int16_t a[8] = {-12, 100, 127, 0, 0, 0, 0, 0}, b[8] = {12, 100, 127, 0, 0, 0, 0, 0};
    b2_hit(a, 127, SECS(0.03));
    fa = zc_freq(wl, SECS(0.007), SECS(0.013)) - 25.16;
    b2_hit(b, 127, SECS(0.03));
    fb = zc_freq(wl, SECS(0.007), SECS(0.013)) - 100.64;
    printf("     RAMP 127, 7-13 ms: +%.0f Hz at TUNE -12, +%.0f Hz at +12 (the law ~%.0f)\n", fa, fb, want);
    check("B2TRX: RAMP adds the same Hz on any note (within 8 %, near the law)", fabs(fa / fb - 1) < 0.08 &&
          fabs(fa / want - 1) < 0.15);
}

/* TICK 127: the pulse pinned at the saturation (-0.322: the peak re the held sine's 0.170); DIRT 127: 3 levels; DIST 127
 * a square (3rd harmonic ~ -9.5 dB) */
static void test_b2trx_shapers(void)
{
    static const int16_t T[8] = {0, 64, 0, 0, 127, 0, 0, 0}, Q[8] = {12, 100, 0, 127, 0, 0, 127, 0},
                         Q0[8] = {12, 100, 0, 127, 0, 0, 0, 0}, S[8] = {12, 100, 0, 127, 0, 0, 0, 127};
    double pk = 0, sq, h3;
    uint32_t i;
    b2_hit(Q0, 127, SECS(0.3));
    sq = 0;
    for (i = SECS(0.1); i < SECS(0.3); i++)
        sq = fmax(sq, abs(wl[i]));
    b2_hit(T, 127, SECS(0.01));
    for (i = 0; i < SECS(0.01); i++)
        pk = fmin(pk, wl[i]);
    printf("     TICK 127: the pulse %.3f x the held sine (-0.322 / 0.170 = %.3f)\n", pk / sq, -0.322 / 0.170);
    b2_hit(Q, 127, SECS(0.3));
    {
        uint32_t nlv = 0;                               /* a 3-level stair (-1, 0, 1) has 4 edges per cycle */
        for (i = SECS(0.1) + 2; i < SECS(0.1) + (uint32_t)(FS / 100.64); i++)   /* (an edge spreads over 3 samples) */
            if (abs(wl[i] - wl[i - 1]) > sq / 8 && abs(wl[i - 1] - wl[i - 2]) <= sq / 8)
                nlv++;
        printf("     DIRT 127: %u steps per cycle (3 levels: 4)\n", nlv);
        check("B2TRX: TICK pinned by the saturation (-0.322 / 0.170 x the sine, within 8 %); DIRT 127 a 3-level stair",
              fabs(pk / sq * 0.170 / 0.322 + 1) < 0.08 && nlv == 4u);
    }
    b2_hit(S, 127, SECS(0.3));
    h3 = 20 * log10(goertzel(wl + SECS(0.1), SECS(0.2), 3 * 100.64) / goertzel(wl + SECS(0.1), SECS(0.2), 100.64));
    printf("     DIST 127: 3rd harmonic %.1f dB\n", h3);
    check("B2TRX: DIST 127 a square (3rd harmonic -9.5 +-1.5 dB)", fabs(h3 + 9.5) < 1.5);
}

/* the level at the defaults within +-3 dB of K909's */
static void test_b2trx_level(void)
{
    static const int16_t DEF[8] = {-99, -99, -99, -99, -99, -99, -99, -99};
    double a, b;
    b2_hit(DEF, 127, SECS(0.3));
    a = rms(wl, 0, SECS(0.3));
    host_init();
    drum_set_model(&trk[0], DM_K909);
    drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
    render_track(&trk[0], wl, SECS(0.3));
    b = rms(wl, 0, SECS(0.3));
    printf("     B2TRX vs K909 at the defaults: %+.1f dB\n", 20 * log10(a / b));
    check("B2TRX: the level at the defaults within +-3 dB of K909's", fabs(20 * log10(a / b)) <= 3);
}

/* the extremes: every knob at 0 / 127 (TUNE -24 / +24) and a step's note +-24, velocity 1 and 127: bounded, ends */
static void test_b2trx_extremes(void)
{
    uint32_t c, i, ok = 1, ended = 1;
    int32_t peak = 0;
    for (c = 0; c < 512u; c++) {
        int16_t k[8];
        for (i = 0; i < 8u; i++)
            k[i] = (int16_t)((c >> i & 1u) ? (i ? 127 : 24) : (i ? 0 : -24));
        host_init();
        drum_set_model(&trk[0], DM_B2TRX);
        for (i = 0; i < 8u; i++)
            trk[0].p[P_E0 + i] = k[i];
        trk[0].nt = (c >> 8 & 1u) ? 24 : -24;
        drum_play(&trk[0], (c & 3u) == 3u ? 1 : 127, trk[0].nt);   /* (drum_hit plays note 0) */
        render_track(&trk[0], wl, SECS(6));
        for (i = 0; i < SECS(6); i++) {
            ok &= abs(wl[i]) <= 4 * VOICE_FS;
            peak = abs(wl[i]) > peak ? abs(wl[i]) : peak;
        }
        ended &= !trk[0].v[0].active;
    }
    printf("     extremes: peak %.2f x VOICE_FS\n", peak / (double)VOICE_FS);
    check("B2TRX: every knob at its ends, notes +-24, velocity 1 / 127: bounded, the voice ends within 6 s",
          ok && ended);
}

/* re-hits 5 ms apart and a switch to B2TRX while another model rings: bounded */
static void test_b2trx_rehits(void)
{
    static const int16_t K[8] = {24, 127, 127, 127, 127, 127, 127, 127};
    uint32_t i, ok = 1;
    b2_hit(K, 127, SECS(0.005));
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
    drum_set_model(&trk[0], DM_B2TRX);
    drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
    render_track(&trk[0], wl, SECS(0.3));
    for (i = 0; i < SECS(0.3); i++)
        ok &= abs(wl[i]) <= 4 * VOICE_FS;
    check("B2TRX: re-hits 5 ms apart and a model switch while ringing: bounded", ok);
}

/* the A/B set: the md-re renders' names and knobs (PTCH 64 = TUNE 0, PTCH 127 = 106.4 Hz ~ TUNE +13) */
static void write_wavs(const char *dir)
{
    static const struct { const char *name; int16_t k[8]; } W[] = {
        {"ptch_064", {0, 64, 0, 0, 0, 0, 0, 0}},  {"ptch_127", {13, 64, 0, 0, 0, 0, 0, 0}},
        {"dec_096", {0, 96, 0, 0, 0, 0, 0, 0}},   {"ramp_064", {0, 64, 64, 0, 0, 0, 0, 0}},
        {"ramp_127", {0, 64, 127, 0, 0, 0, 0, 0}}, {"hold_064", {0, 64, 0, 64, 0, 0, 0, 0}},
        {"tick_127", {0, 64, 0, 0, 127, 0, 0, 0}}, {"nois_127", {0, 64, 0, 0, 0, 127, 0, 0}},
        {"dirt_096", {0, 64, 0, 0, 0, 0, 96, 0}}, {"dirt_127", {0, 64, 0, 0, 0, 0, 127, 0}},
        {"dist_016", {0, 64, 0, 0, 0, 0, 0, 16}}, {"dist_127", {0, 64, 0, 0, 0, 0, 0, 127}}};
    static const int16_t K0[8] = {0, 64, 0, 0, 0, 0, 0, 0};
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
        b2_hit(W[w].k, 127, n);
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
    test_b2trx_vs_ref();
    test_b2trx_laws();
    test_b2trx_ramp();
    test_b2trx_shapers();
    test_b2trx_level();
    test_b2trx_extremes();
    test_b2trx_rehits();
    if (argc > 1)
        write_wavs(argv[1]);
    printf(fails ? "b2trx_test: %d FAILED\n" : "b2trx_test: all passed\n", fails);
    return fails != 0;
}
