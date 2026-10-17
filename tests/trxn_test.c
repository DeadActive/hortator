/* CPTRX, MATRX (dm_trxn.c): the fixed-point voices against their float twins (the md-re prototypes proto/trx-cp.c and
 * proto/trx-ma.c, rewritten below with the firmware's knob mapping, driven by the voice's own noise: the same xorshift
 * from the same seed), the measured laws, the levels, the extremes. Each test prints one line; any failure exits 1.
 *   trxn_test [OUTDIR]   OUTDIR: also writes the A/B set as float WAVs in the prototypes' units (md-re renders' names) */
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
static int32_t wl[SECS(8)];
static double ref[SECS(8)];

/* ---- the prototypes' float parts */
static uint32_t frng;
static double fwhite(void)
{
    frng ^= frng << 13;
    frng ^= frng >> 17;
    frng ^= frng << 5;
    return (int32_t)frng / 2147483648.0;
}
typedef struct { double b0, b1, b2, a1, a2, x1, x2, y1, y2; } fbq;
static void fbq_set(fbq *f, int hp, double fc, double q)          /* RBJ cookbook HP / LP */
{
    double w = 2 * M_PI * fc / FS, c = cos(w), al = sin(w) / (2 * q), a0 = 1 + al;
    f->b0 = (hp ? (1 + c) : (1 - c)) / 2 / a0;
    f->b1 = (hp ? -(1 + c) : (1 - c)) / a0;
    f->b2 = f->b0;
    f->a1 = -2 * c / a0;
    f->a2 = (1 - al) / a0;
    f->x1 = f->x2 = f->y1 = f->y2 = 0;
}
static double fbq_run(fbq *f, double x)
{
    double y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1;
    f->x1 = x;
    f->y2 = f->y1;
    f->y1 = y;
    return y;
}
static double tab(const double *x, const double *y, int n, double v)
{
    int i;
    if (v <= x[0])
        return y[0];
    for (i = 1; i < n; i++)
        if (v <= x[i])
            return y[i - 1] + (y[i] - y[i - 1]) * (v - x[i - 1]) / (x[i] - x[i - 1]);
    return y[n - 1];
}
static const double K5[5] = {0, 32, 64, 96, 127};

/* a 2-pole section's cutoff and Q, read back from the firmware's coefficients */
static void bq_fq(const tnbq_t *c, int hp, double *f, double *q)
{
    double b0 = c->b0 / 1073741824.0, a1 = c->a1 / 1073741824.0, a2 = c->a2 / 1073741824.0, g2, a0, g;
    if (hp) {
        a0 = 1 / b0;
        g2 = 1 + a1 * a0 / 2;
    } else {
        g2 = 2 / (2 - a1 / b0);
        a0 = g2 / b0;
    }
    g = sqrt(g2);
    *f = atan(g) * FS / M_PI;
    *q = g / (a0 - 1 - g2);
    (void)a2;
}

/* track 0 = model mi with knobs k (-99: the default), one hit at vel, frames rendered into wl; the voice's seed */
static uint32_t hit(uint32_t mi, const int16_t *k, uint32_t vel, uint32_t frames, uint32_t seed)
{
    uint32_t i, s;
    host_init();
    drum_set_model(&trk[0], mi);
    for (i = 0; i < 8u; i++)
        if (k[i] != -99)
            trk[0].p[P_E0 + i] = k[i];
    drum_hit(&trk[0], vel);
    if (seed)
        trk[0].v[0].rng = (int32_t)seed;
    s = (uint32_t)trk[0].v[0].rng;
    render_track(&trk[0], wl, frames);
    return s;
}

static double rms(const int32_t *x, uint32_t a, uint32_t b)
{
    double s = 0;
    uint32_t i;
    for (i = a; i < b; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (b - a));
}

/* the firmware against the twin over [0, n): the least-squares gain and the residual (dB re the twin) */
static double resid(uint32_t n, double *gain)
{
    double xy = 0, yy = 0, e = 0, r = 1e-30;
    uint32_t i;
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

/* ================================================================ CPTRX: TUNE DECAY CLPY HARD RATE ROOM RSIZ RTUN */
static cptrx_t cps;                                     /* the voice's state at the hit (its onsets and ends) */

/* proto/trx-cp.c with TUNE for TONE (fh by semitones; the low-pass's and the level's TONE parts up to TONE's ends) and
 * the voice's integer onsets and ends; everything else the measured laws in float */
static void cp_ref(const int16_t *k, uint32_t seed, double *y, uint32_t n)
{
    static const double FL[5] = {4327, 5708, 6157, 6482, 6508}, QL[5] = {0.56, 1.04, 1.66, 3.0, 4.62};
    static const double GH[5] = {-0.6, -0.8, 0, -0.2, 0}, GT[5] = {-0.3, -0.3, 0, 0.3, 1.0};
    static const double TAU[5] = {2.3, 5.6, 10.0, 20.0, 43.0}, RL[5] = {-200, -45, -35.5, -32, -31};
    double s = k[0], to = fmin(127, fmax(0, 64 + s * 50 / 12)), HA = k[3], RO = k[5], RS = k[6], RT = k[7];
    double fh = 203.0 * pow(2.0, 64 / 50.0) * pow(2, s / 12), qh = 0.49 + 0.0105 * HA;
    double fl = tab(K5, FL, 5, HA) * (0.9 + 0.0016 * to), ql = tab(K5, QL, 5, HA);
    double g = pow(10.0, (tab(K5, GH, 5, HA) + tab(K5, GT, 5, to)) / 20.0) * 0.0361;
    double tau = tab(K5, TAU, 5, k[1]) * 0.001 * FS, c = 1.118 + 1.028 * k[2] / 127.0, L = 0.0855;
    double rl = RO > 0 ? pow(10.0, tab(K5, RL, 5, RO) / 20.0) : 0, rtau = 0.019 * pow(2.0, RS / 62.0) * FS;
    double fr = 692.0 * pow(2.0, RT / 50.0);
    fbq h1, h2, l1, r1, r2;
    uint32_t i, j;
    fbq_set(&h1, 1, fh, qh);
    fbq_set(&h2, 1, fh, qh);
    fbq_set(&l1, 0, fl, ql);
    fbq_set(&r1, 0, fr, 1.9);
    fbq_set(&r2, 0, fr, 1.9);
    frng = seed;
    for (i = 0; i < n; i++) {
        double v = 0;
        if (i < cps.cend) {
            double env = 0, x;
            for (j = 0; j < 4u; j++)
                if (i >= cps.on[j])
                    env += (j < 3u ? c : 1.0) * exp(-(double)(i - cps.on[j]) / tau);
            x = fbq_run(&l1, fbq_run(&h2, fbq_run(&h1, fwhite()))) * g * env;
            v = x / pow(1 + pow(fabs(x / L), 6), 1 / 6.0);
        }
        if (i < cps.rend)
            v += fbq_run(&r2, fbq_run(&r1, fwhite())) * 2.8 * rl * exp(-(double)i / rtau);
        y[i] = v;
    }
}

static double cp_vs_ref(const int16_t *k, uint32_t n, double *gain)
{
    uint32_t seed = hit(DM_CPTRX, k, 127, 0, 0);
    cps = trk[0].v[0].ms.cp;
    render_track(&trk[0], wl, n);
    cp_ref(k, seed, ref, n);
    return resid(n, gain);
}

static void test_cp_vs_ref(void)
{
    static const int16_t K[][8] = {{0, 64, 64, 64, 64, 0, 64, 64},     {-15, 64, 64, 64, 64, 0, 64, 64},
                                   {15, 64, 64, 64, 64, 0, 64, 64},    {-24, 64, 64, 64, 64, 0, 64, 64},
                                   {24, 64, 64, 64, 64, 0, 64, 64},    {0, 0, 64, 64, 64, 0, 64, 64},
                                   {0, 127, 64, 64, 64, 0, 64, 64},    {0, 32, 0, 64, 64, 0, 64, 64},
                                   {0, 64, 127, 64, 64, 0, 64, 64},    {0, 64, 64, 0, 64, 0, 64, 64},
                                   {0, 64, 64, 127, 64, 0, 64, 64},    {0, 64, 64, 64, 0, 0, 64, 64},
                                   {0, 64, 64, 64, 127, 0, 64, 64},    {0, 64, 64, 64, 64, 64, 64, 64},
                                   {0, 64, 64, 64, 64, 127, 0, 0},     {0, 64, 64, 64, 64, 127, 127, 127},
                                   {0, 96, 0, 32, 96, 96, 100, 30},    {-12, 127, 127, 127, 0, 127, 127, 127},
                                   {12, 0, 127, 0, 127, 40, 0, 127}};
    double worst = -999, g, g0 = 0, gw = 1;
    uint32_t i;
    for (i = 0; i < sizeof K / sizeof K[0]; i++) {
        double e = cp_vs_ref(K[i], SECS(K[i][5] ? 0.6 : 0.4), &g);
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
    printf("     CPTRX vs the float twin (19 settings, the same noise): worst residual %.1f dB, gain %.0f / unit "
           "(spread %.2f %%)\n", worst, g0, 100 * (gw - 1));
    check("CPTRX: every setting matches the float twin (residual < -30 dB, one gain within 1 %)",
          worst < -30 && fabs(gw - 1) < 0.01);
}

/* the laws on the voice itself: the filters' cutoffs and Qs (from its coefficients), the burst onsets and DECAY's tau
 * (its step), the room's; then heard: the onsets in the output (RATE 127), DECAY's tail */
static void test_cp_laws(void)
{
    static const double FL[5] = {4327, 5708, 6157, 6482, 6508}, QL[5] = {0.56, 1.04, 1.66, 3.0, 4.62};
    static const double TAU[5] = {2.3, 5.6, 10.0, 20.0, 43.0};
    double wf = 0, wq = 0, wt = 0, wr = 0, won = 0;
    int s, h, r;
    for (s = -24; s <= 24; s += 3)
        for (h = 0; h <= 127; h += 9) {
            int16_t k[8] = {0, 64, 64, 0, 64, 127, 64, 64};
            double f, q, to = fmin(127, fmax(0, 64 + s * 50 / 12.0));
            k[0] = (int16_t)s;
            k[3] = (int16_t)h;
            hit(DM_CPTRX, k, 127, 0, 0);
            cps = trk[0].v[0].ms.cp;
            bq_fq(&cps.hp, 1, &f, &q);
            wf = fmax(wf, fabs(f / (203.0 * pow(2.0, 64 / 50.0 + s / 12.0)) - 1));
            wq = fmax(wq, fabs(q / (0.49 + 0.0105 * h) - 1));
            bq_fq(&cps.lp, 0, &f, &q);
            wf = fmax(wf, fabs(f / (tab(K5, FL, 5, h) * (0.9 + 0.0016 * to)) - 1));
            wq = fmax(wq, fabs(q / tab(K5, QL, 5, h) - 1));
        }
    for (r = 0; r <= 127; r++) {
        int16_t k[8] = {0, 64, 64, 64, 64, 127, 64, 64};
        double T, f, q, tau;
        k[1] = k[4] = k[6] = k[7] = (int16_t)r;
        hit(DM_CPTRX, k, 127, 0, 0);
        cps = trk[0].v[0].ms.cp;
        T = (2.27 + 0.468 * r) * 0.001 * FS;
        won = fmax(won, fmax(fabs(cps.on[1] - 0.335 * T), fmax(fabs(cps.on[2] - 0.72 * T), fabs(cps.on[3] - T))));
        tau = -1 / log(1 - cps.d / 4294967296.0) / FS * 1000;   /* the step's tau, ms */
        wt = fmax(wt, fabs((tau + 0.5 / FS * 1000) / tab(K5, TAU, 5, r) - 1));   /* (a step's half sample) */
        tau = -1 / log(1 - cps.rd / 4294967296.0) / FS;
        wr = fmax(wr, fabs((tau + 0.5 / FS) / (0.019 * pow(2.0, r / 62.0)) - 1));
        bq_fq(&cps.rq, 0, &f, &q);
        wr = fmax(wr, fmax(fabs(f / (692.0 * pow(2.0, r / 50.0)) - 1), fabs(q / 1.9 - 1)));
    }
    printf("     worst vs the laws: cutoffs %.3f %%, Qs %.3f %%, onsets %.2f samples, DECAY tau %.3f %%, room %.3f %%\n",
           100 * wf, 100 * wq, won, 100 * wt, 100 * wr);
    check("CPTRX: TUNE (fh 493 Hz x 2^(TUNE / 12)), HARD's Qs and low-pass, RATE's onsets, DECAY's tau, RSIZ and RTUN "
          "follow the measured laws (0.3 %, one sample)", wf < 0.003 && wq < 0.003 && won <= 1 && wt < 0.003 && wr < 0.003);
    {                                                   /* heard: the four bursts of RATE 127 (DECAY 0: short) */
        static const int16_t K[8] = {0, 0, 64, 64, 127, 0, 64, 64};
        double T = (2.27 + 0.468 * 127) * 0.001 * FS, fr[4] = {0, 0.335, 0.72, 1};
        uint32_t j, ok = 1;
        hit(DM_CPTRX, K, 127, SECS(0.1), 0);
        for (j = 0; j < 4u; j++) {                      /* each onset: silence-ish before (DECAY 0), loud after */
            uint32_t o = (uint32_t)(fr[j] * T + 0.5);
            double a = j ? rms(wl, o - 40, o - 1) : 0, b = rms(wl, o + 1, o + 40);
            ok &= b > 4 * a;
        }
        check("CPTRX: RATE 127's four bursts heard at T x {0, 0.335, 0.72, 1} (T = 61.7 ms)", ok);
    }
    {                                                   /* heard: DECAY's tail (after the last burst, CLPY 0, averaged
                                                         * over 8 noises) */
        static const int D[3] = {32, 64, 96};
        double got[3];
        uint32_t i, sd;
        for (i = 0; i < 3u; i++) {
            int16_t k[8] = {0, 0, 0, 64, 64, 0, 64, 64};
            double tau = tab(K5, TAU, 5, D[i]) * 0.001, a = 0, b = 0;
            uint32_t o;
            k[1] = (int16_t)D[i];
            for (sd = 1; sd <= 8u; sd++) {
                hit(DM_CPTRX, k, 127, SECS(0.5), sd * 2654435761u | 1u);
                o = trk[0].v[0].ms.cp.on[3] + (uint32_t)(0.5 * tau * FS);
                a += rms(wl, o, o + (uint32_t)(tau * FS)) * rms(wl, o, o + (uint32_t)(tau * FS));
                b += rms(wl, o + (uint32_t)(2 * tau * FS), o + (uint32_t)(3 * tau * FS)) *
                     rms(wl, o + (uint32_t)(2 * tau * FS), o + (uint32_t)(3 * tau * FS));
            }
            got[i] = 2 * tau / (0.5 * log(a / b)) / tau;
        }
        printf("     DECAY 32 / 64 / 96 heard: tau %.2f / %.2f / %.2f x the law\n", got[0], got[1], got[2]);
        check("CPTRX: DECAY heard follows tau 5.6 / 10 / 20 ms within 10 %",
              fabs(got[0] - 1) < 0.1 && fabs(got[1] - 1) < 0.1 && fabs(got[2] - 1) < 0.1);
    }
}

/* the clip: the clap never passes L (the output's ceiling: L x CP_OUT / 4 through velocity and the voice level); ROOM 0 is
 * off (RSIZ / RTUN change nothing), ROOM 127 rings on after the clap */
static void test_cp_clip_room(void)
{
    static const int16_t LOUD[8] = {15, 127, 127, 127, 32, 0, 64, 64}, R0[8] = {0, 64, 64, 64, 64, 0, 0, 0},
                         R0b[8] = {0, 64, 64, 64, 64, 0, 127, 127}, R1[8] = {0, 64, 64, 64, 64, 127, 127, 64};
    static int32_t a[SECS(0.6)];
    int32_t pk = 0, ceil = mulq15(mulq15(32760 * CP_OUT / 4, vel_gain(&(dvoice_t){.vel = 127})), VOICE_FS) + 2;
    double tail0, tail1;
    uint32_t i;
    hit(DM_CPTRX, LOUD, 127, SECS(0.5), 0);
    for (i = 0; i < SECS(0.5); i++)
        pk = abs(wl[i]) > pk ? abs(wl[i]) : pk;
    hit(DM_CPTRX, R0, 127, SECS(0.6), 0);
    memcpy(a, wl, sizeof a);
    tail0 = rms(wl, SECS(0.2), SECS(0.4));
    hit(DM_CPTRX, R0b, 127, SECS(0.6), 0);
    hit(DM_CPTRX, R1, 127, SECS(0.6), 0);
    tail1 = rms(wl, SECS(0.2), SECS(0.4));
    printf("     the loudest clap's peak %d (ceiling %d); 200-400 ms: ROOM 0 %.1f, ROOM 127 RSIZ 127 %.1f\n", pk, ceil,
           tail0, tail1);
    hit(DM_CPTRX, R0b, 127, SECS(0.6), 0);
    check("CPTRX: the clap never passes the clip's L; ROOM 0 is off (RSIZ, RTUN no change); ROOM 127 rings on",
          pk <= ceil && pk > ceil * 9 / 10 && !memcmp(a, wl, sizeof a) && tail0 == 0 && tail1 > 20);
}

/* ================================================================ MATRX: TUNE DECAY ATT REV DAMP RATL TONE HARD */
static matrx_t mas;

/* proto/trx-ma.c with TUNE (fh by semitones, at most 11 kHz), RTYP at 0, and the voice's envelope corners in samples;
 * everything else the measured laws in float */
static void ma_ref(const int16_t *k, uint32_t seed, double *y, uint32_t n)
{
    static const double H5[5] = {0, 32, 64, 80, 127}, HS[5] = {0.787, 0.92, 1.0, 1.014, 1.054};
    static const double GT[5] = {-3.1, -2.3, 0, 2.8, 6.3}, GH[5] = {1.5, 0.7, 0, -0.9, -1.3};
    static const double D9[9] = {0, 16, 32, 48, 64, 80, 96, 112, 127},
                        PO[9] = {0.30, 0.28, 0.35, 0.42, 0.52, 0.62, 0.68, 0.74, 0.78};
    double TO = k[6], HA = k[7];
    double fh = fmin(3620.0 * pow(2.0, TO / 124.0) * tab(H5, HS, 5, HA) * pow(2, k[0] / 12.0), 0.24921 * FS);
    double q = (0.47 + 0.0068 * HA) * (1.26 - 0.0043 * TO) / 0.985;
    double la = 1 - exp(-2 * M_PI * 16000 / FS), lp = 0, gate = 1;
    double g = 0.0131 * pow(10.0, (tab(K5, GT, 5, TO) + tab(K5, GH, 5, HA)) / 20.0);
    double poff = tab(D9, PO, 9, k[4]), rl = 0.59 * k[5] / 127.0;
    double t1 = mas.t1, t2 = mas.t2, dv = mas.dv, t3 = mas.t3;
    fbq h1, h2;
    uint32_t i;
    fbq_set(&h1, 1, fh, q);
    fbq_set(&h2, 1, fh, q);
    frng = seed;
    for (i = 0; i < n; i++) {
        double t = i, env, x, v;
        if (i >= mas.tend) {
            y[i] = 0;
            continue;
        }
        if (t < t1)
            env = pow(t / t1, 1.3);
        else if (t < t2)
            env = 1;
        else if (t < t2 + dv)
            env = fabs(1 - 2 * (t - t2) / dv);
        else if (t < t3)
            env = 1;
        else if (t < t3 + 0.0007 * FS)
            env = 1 - (1 - 0.004) * (t - t3) / (0.0007 * FS);
        else
            env = 0.004 * exp(-(t - t3 - 0.0007 * FS) / (0.011 * FS));
        if (i % 32 == 0) {
            double u = 0;
            int j;
            for (j = 0; j < 6; j++)
                u += fwhite() * 0.5;
            gate = (fwhite() * 0.5 + 0.5) < poff ? 0 : pow(10.0, 2.3 * u / 0.707 / 20.0);
        }
        x = fbq_run(&h2, fbq_run(&h1, fwhite() * gate));
        lp += la * (x - lp);
        v = lp * g;
        if (rl > 0)
            v += fwhite() * g * 1.47 * rl * pow(fmin(t / FS, 0.3) / 0.19, 0.7);
        y[i] = v * env;
    }
}

static double ma_vs_ref(const int16_t *k, uint32_t n, double *gain)
{
    uint32_t seed = hit(DM_MATRX, k, 127, 0, 0);
    mas = trk[0].v[0].ms.ma;
    render_track(&trk[0], wl, n);
    ma_ref(k, seed, ref, n);
    return resid(n, gain);
}

static void test_ma_vs_ref(void)
{
    static const int16_t K[][8] = {{0, 64, 0, 0, 0, 0, 64, 64},     {0, 64, 64, 0, 0, 0, 64, 64},
                                   {0, 64, 127, 0, 0, 0, 64, 64},   {0, 0, 0, 0, 0, 0, 64, 64},
                                   {0, 127, 0, 0, 0, 0, 64, 64},    {0, 64, 0, 64, 0, 0, 64, 64},
                                   {0, 64, 0, 127, 0, 0, 64, 64},   {0, 64, 0, 0, 64, 0, 64, 64},
                                   {0, 64, 0, 0, 127, 0, 64, 64},   {0, 64, 0, 0, 0, 64, 64, 64},
                                   {0, 127, 0, 0, 0, 127, 64, 64},  {0, 64, 0, 0, 0, 0, 0, 64},
                                   {0, 64, 0, 0, 0, 0, 127, 64},    {0, 64, 0, 0, 0, 0, 64, 0},
                                   {0, 64, 0, 0, 0, 0, 64, 127},    {-12, 64, 0, 0, 0, 0, 64, 64},
                                   {12, 64, 0, 0, 0, 0, 64, 64},    {-24, 100, 30, 90, 50, 100, 20, 110},
                                   {24, 127, 127, 127, 127, 127, 127, 127}};
    double worst = -999, g, g0 = 0, gw = 1;
    uint32_t i;
    for (i = 0; i < sizeof K / sizeof K[0]; i++) {
        double e = ma_vs_ref(K[i], SECS(0.7), &g);
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
    printf("     MATRX vs the float twin (19 settings, the same noise): worst residual %.1f dB, gain %.0f / unit "
           "(spread %.2f %%)\n", worst, g0, 100 * (gw - 1));
    check("MATRX: every setting matches the float twin (residual < -30 dB, one gain within 1 %)",
          worst < -30 && fabs(gw - 1) < 0.01);
}

/* the laws on the voice: the envelope's corners (ATT, DECAY = SUS, REV in 1.451 ms steps; 14 ms more hold), the
 * high-pass (TONE, HARD, TUNE) from its coefficients; heard: the hold's end, DAMP's silent blocks */
static void test_ma_laws(void)
{
    static const double H5[5] = {0, 32, 64, 80, 127}, HS[5] = {0.787, 0.92, 1.0, 1.014, 1.054};
    static const double D9[9] = {0, 16, 32, 48, 64, 80, 96, 112, 127},
                        PO[9] = {0.30, 0.28, 0.35, 0.42, 0.52, 0.62, 0.68, 0.74, 0.78};
    double wt = 0, wf = 0, wq = 0;
    int a, s;
    for (a = 0; a <= 127; a++) {
        int16_t k[8] = {0, 64, 0, 0, 0, 0, 64, 64};
        double ta, ts, tr;
        k[1] = (int16_t)(127 - a);
        k[2] = (int16_t)a;
        k[3] = (int16_t)((a * 5) % 128);
        hit(DM_MATRX, k, 127, 0, 0);
        mas = trk[0].v[0].ms.ma;
        ta = 64.0 * a;                                  /* (1.451 ms: two 32-sample blocks) */
        ts = 0.014 * FS + 64.0 * k[1];
        tr = 64.0 * k[3];
        wt = fmax(wt, fmax(fabs(mas.t1 - ta), fmax(fabs(mas.t2 - ta - ts), fabs(mas.t3 - ta - ts - tr))));
        wt = fmax(wt, fabs(mas.dv - 0.0006 * k[3] * FS));
    }
    for (s = -24; s <= 24; s += 4)
        for (a = 0; a <= 127; a += 9) {
            int16_t k[8] = {0, 64, 0, 0, 0, 0, 64, 64};
            double f, q, want, wq0;
            k[0] = (int16_t)s;
            k[6] = (int16_t)a;
            k[7] = (int16_t)(127 - a);
            hit(DM_MATRX, k, 127, 0, 0);
            mas = trk[0].v[0].ms.ma;
            bq_fq(&mas.hp, 1, &f, &q);
            want = fmin(3620.0 * pow(2.0, a / 124.0) * tab(H5, HS, 5, 127 - a) * pow(2, s / 12.0), 0.24921 * FS);
            wq0 = (0.47 + 0.0068 * (127 - a)) * (1.26 - 0.0043 * a) / 0.985;
            wf = fmax(wf, fabs(f / want - 1));
            wq = fmax(wq, fabs(q / wq0 - 1));
        }
    printf("     worst vs the laws: envelope corners %.2f samples, high-pass cutoff %.3f %%, Q %.3f %%\n", wt, 100 * wf,
           100 * wq);
    check("MATRX: ATT, DECAY (SUS), REV's corners within 1 sample (1.451 ms steps: 64 samples); TONE, HARD, TUNE's high-pass "
          "within 0.5 %", wt <= 1 && wf < 0.005 && wq < 0.005);
    {                                                   /* heard: the hold ends (the cut) at 14 + 1.451 x DECAY ms */
        static const int D[3] = {0, 64, 127};
        double worst = 0;
        uint32_t i, j;
        for (i = 0; i < 3u; i++) {
            int16_t k[8] = {0, 64, 0, 0, 0, 0, 64, 64};
            double want = (0.014 + 0.001451 * D[i]) * FS, lv = 0;
            uint32_t at = 0;
            k[1] = (int16_t)D[i];
            hit(DM_MATRX, k, 127, SECS(0.4), 0);
            for (j = 0; j + 32u <= SECS(0.4); j += 32) {   /* the end of the last 32-sample window above -30 dB re the
                                                         * hold (a grain's gap is a window quieter than that) */
                double r = rms(wl, j, j + 32);
                if (j < SECS(0.01))
                    lv = fmax(lv, r);
                if (r > lv * 0.0316)
                    at = j + 32;
            }
            want += 0.0007 * FS;                        /* (the cut: 0.7 ms to -48 dB) */
            worst = fmax(worst, fabs(at - want) / FS * 1000);
        }
        printf("     the hold's end (and its 0.7 ms cut) heard within %.2f ms of 14 + 1.451 x DECAY ms\n", worst);
        check("MATRX: the hold's end heard at 14 + 1.451 ms x DECAY (within 1 ms: a 32-sample window)", worst < 1);
    }
    {                                                   /* DAMP: the share of silent blocks, 64 hits of DECAY 127 */
        static const int D[3] = {0, 64, 127};
        double share[3];
        uint32_t i, sd, b;
        for (i = 0; i < 3u; i++) {
            int16_t k[8] = {0, 127, 0, 0, 0, 0, 64, 64};
            uint32_t off = 0, all = 0;
            k[4] = (int16_t)D[i];
            for (sd = 1; sd <= 64u; sd++) {
                hit(DM_MATRX, k, 127, 0, sd * 2654435761u | 1u);
                for (b = 0; b < 250u; b++) {
                    dvoice_t *v = &trk[0].v[0];
                    int32_t tmp[32] = {0};
                    matrx_render(&trk[0], v, tmp, 32);
                    off += v->ms.ma.gq == 0;
                    all++;
                }
            }
            share[i] = off / (double)all;
        }
        printf("     DAMP 0 / 64 / 127: silent blocks %.3f / %.3f / %.3f (the law %.2f / %.2f / %.2f)\n", share[0],
               share[1], share[2], tab(D9, PO, 9, 0), tab(D9, PO, 9, 64), tab(D9, PO, 9, 127));
        check("MATRX: DAMP's silent blocks as measured (p_off within 0.02)", fabs(share[0] - 0.30) < 0.02 &&
              fabs(share[1] - 0.52) < 0.02 && fabs(share[2] - 0.78) < 0.02);
    }
}

/* ================================================================ both */
/* the level at the defaults within +-3 dB of the reference model's: RMS over the hit (from the hit to the last sample
 * above -40 dB re its peak), 4 noises */
static double hit_rms(uint32_t n)
{
    int32_t pk = 0;
    uint32_t i, last = 1;
    for (i = 0; i < n; i++)
        pk = abs(wl[i]) > pk ? abs(wl[i]) : pk;
    for (i = 0; i < n; i++)
        if (abs(wl[i]) * 100 > pk)
            last = i + 1;
    return rms(wl, 0, last);
}

static void test_levels(void)
{
    static const int16_t DEF[8] = {-99, -99, -99, -99, -99, -99, -99, -99};
    static const struct { uint32_t m, r; const char *name, *rn; } L[2] = {{DM_CPTRX, DM_C909, "CPTRX", "C909"},
                                                                          {DM_MATRX, DM_HATC, "MATRX", "HATC"}};
    uint32_t i, sd, ok = 1;
    for (i = 0; i < 2u; i++) {
        double a = 0, b = 0, db;
        for (sd = 1; sd <= 4u; sd++) {
            hit(L[i].m, DEF, 127, SECS(1), sd * 2654435761u | 1u);
            a += pow(hit_rms(SECS(1)), 2);
            hit(L[i].r, DEF, 127, SECS(1), sd * 2654435761u | 1u);
            b += pow(hit_rms(SECS(1)), 2);
        }
        db = 10 * log10(a / b);
        printf("     %s vs %s at the defaults (RMS over the hit): %+.1f dB\n", L[i].name, L[i].rn, db);
        ok &= fabs(db) <= 3;
    }
    check("CPTRX / MATRX: the level at the defaults within +-3 dB of C909's / HATC's", ok);
}

/* the extremes: every knob at 0 / 127 (TUNE -24 / +24) and a step's note +-24, velocity 1 and 127: bounded, ends */
static void test_extremes(void)
{
    static const uint32_t M[2] = {DM_CPTRX, DM_MATRX};
    uint32_t mi, c, i, ok = 1, ended = 1;
    for (mi = 0; mi < 2u; mi++)
        for (c = 0; c < 512u; c++) {
            host_init();
            drum_set_model(&trk[0], M[mi]);
            for (i = 0; i < 8u; i++)
                trk[0].p[P_E0 + i] = (int16_t)((c >> i & 1u) ? (i ? 127 : 24) : (i ? 0 : -24));
            trk[0].nt = (c >> 8 & 1u) ? 24 : -24;
            drum_hit(&trk[0], (c & 3u) == 3u ? 1 : 127);
            render_track(&trk[0], wl, SECS(6));
            for (i = 0; i < SECS(6); i++)
                ok &= abs(wl[i]) <= 4 * VOICE_FS;
            ended &= !trk[0].v[0].active;
        }
    check("CPTRX / MATRX: every knob at its ends, notes +-24, velocity 1 / 127: bounded, the voice ends within 6 s",
          ok && ended);
}

/* re-hits 5 ms apart and a switch to the model while another rings: bounded */
static void test_rehits(void)
{
    static const uint32_t M[2] = {DM_CPTRX, DM_MATRX};
    static const int16_t K[8] = {24, 127, 127, 127, 127, 127, 127, 127};
    uint32_t mi, i, ok = 1;
    for (mi = 0; mi < 2u; mi++) {
        hit(M[mi], K, 127, SECS(0.005), 0);
        for (i = 0; i < 4u; i++) {
            drum_hit(&trk[0], 127);
            render_track(&trk[0], wl, SECS(0.005));
        }
        render_track(&trk[0], wl, SECS(0.3));
        for (i = 0; i < SECS(0.3); i++)
            ok &= abs(wl[i]) <= 4 * VOICE_FS;
        host_init();
        drum_set_model(&trk[0], DM_CYMB);
        drum_hit(&trk[0], 127);
        render_track(&trk[0], wl, SECS(0.05));
        drum_set_model(&trk[0], M[mi]);
        drum_hit(&trk[0], 127);
        render_track(&trk[0], wl, SECS(0.3));
        for (i = 0; i < SECS(0.3); i++)
            ok &= abs(wl[i]) <= 4 * VOICE_FS;
    }
    check("CPTRX / MATRX: re-hits 5 ms apart and a model switch while ringing: bounded", ok);
}

/* the A/B set: the md-re renders' names and knobs (the machine's knobs mapped: CP TONE t = TUNE (t - 64) x 12 / 50,
 * MA RTYP left out), in the prototype's units (the twin's gain at the defaults) */
static void wav(const char *dir, const char *pre, const char *name, uint32_t n, double g)
{
    char path[512];
    FILE *f;
    uint32_t v, i;
    snprintf(path, sizeof path, "%s/%s%s.wav", dir, pre, name);
    f = fopen(path, "wb");
    if (!f)
        return;
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

static void write_wavs(const char *dir)
{
    static const struct { const char *name; int16_t k[8]; } C[] = {   /* TUNE DECAY CLPY HARD RATE ROOM RSIZ RTUN */
        {"neutral", {0, 64, 64, 64, 64, 0, 64, 64}},    {"clpy_000", {0, 64, 0, 64, 64, 0, 64, 64}},
        {"clpy_127", {0, 64, 127, 64, 64, 0, 64, 64}},  {"tone_000", {-15, 64, 64, 64, 64, 0, 64, 64}},
        {"tone_127", {15, 64, 64, 64, 64, 0, 64, 64}},  {"hard_000", {0, 64, 64, 0, 64, 0, 64, 64}},
        {"hard_127", {0, 64, 64, 127, 64, 0, 64, 64}},  {"rich_000", {0, 0, 64, 64, 64, 0, 64, 64}},
        {"rich_127", {0, 127, 64, 64, 64, 0, 64, 64}},  {"rate_000", {0, 64, 64, 64, 0, 0, 64, 64}},
        {"rate_127", {0, 64, 64, 64, 127, 0, 64, 64}},  {"room_064", {0, 64, 64, 64, 64, 64, 64, 64}},
        {"room_127", {0, 64, 64, 64, 64, 127, 64, 64}}, {"rm_rsiz127_r0", {0, 64, 64, 64, 64, 127, 127, 64}},
        {"rm_rtun000_r0", {0, 64, 64, 64, 64, 127, 64, 0}},  {"rm_rtun127_r0", {0, 64, 64, 64, 64, 127, 64, 127}}};
    static const struct { const char *name; int16_t k[8]; } M[] = {   /* TUNE DECAY ATT REV DAMP RATL TONE HARD */
        {"neutral", {0, 64, 0, 0, 0, 0, 64, 64}},       {"att_064", {0, 64, 64, 0, 0, 0, 64, 64}},
        {"att_127", {0, 64, 127, 0, 0, 0, 64, 64}},     {"sus_000", {0, 0, 0, 0, 0, 0, 64, 64}},
        {"sus_127", {0, 127, 0, 0, 0, 0, 64, 64}},      {"rev_064", {0, 64, 0, 64, 0, 0, 64, 64}},
        {"rev_127", {0, 64, 0, 127, 0, 0, 64, 64}},     {"damp_064", {0, 64, 0, 0, 64, 0, 64, 64}},
        {"damp_127", {0, 64, 0, 0, 127, 0, 64, 64}},    {"ratl_064", {0, 64, 0, 0, 0, 64, 64, 64}},
        {"ratl_127", {0, 64, 0, 0, 0, 127, 64, 64}},    {"tone_000", {0, 64, 0, 0, 0, 0, 0, 64}},
        {"tone_127", {0, 64, 0, 0, 0, 0, 127, 64}},     {"hard_000", {0, 64, 0, 0, 0, 0, 64, 0}},
        {"hard_127", {0, 64, 0, 0, 0, 0, 64, 127}}};
    double g;
    uint32_t w, n = SECS(1.0);
    cp_vs_ref(C[0].k, SECS(0.3), &g);
    for (w = 0; w < sizeof C / sizeof C[0]; w++) {
        hit(DM_CPTRX, C[w].k, 127, n, 0);
        wav(dir, "cptrx_", C[w].name, n, g);
    }
    ma_vs_ref(M[0].k, SECS(0.3), &g);
    for (w = 0; w < sizeof M / sizeof M[0]; w++) {
        hit(DM_MATRX, M[w].k, 127, n, 0);
        wav(dir, "matrx_", M[w].name, n, g);
    }
    printf("     wrote %u WAVs to %s\n", (unsigned)(sizeof C / sizeof C[0] + sizeof M / sizeof M[0]), dir);
}

int main(int argc, char **argv)
{
    test_cp_vs_ref();
    test_cp_laws();
    test_cp_clip_room();
    test_ma_vs_ref();
    test_ma_laws();
    test_levels();
    test_extremes();
    test_rehits();
    if (argc > 1)
        write_wavs(argv[1]);
    printf(fails ? "trxn_test: %d FAILED\n" : "trxn_test: all passed\n", fails);
    return fails != 0;
}
