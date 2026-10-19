/* CBEFM, HHEFM, CYEFM (dm_efmm.c): the fixed-point voices against float twins of the md-re prototypes (proto/efm-cb.c,
 * efm-hh.c, efm-cy.c, copied below; the machine's track high-pass left out, as in the voices) given the voice's own
 * constants and start (its increments, its decay steps, the hat's random phases from its seed), the measured laws, the
 * levels, the extremes, the choke. Each test prints one line; any failure exits 1.
 *   efmmetal_test [OUTDIR]   OUTDIR: also writes the voices' renders of the md-re sweeps (2 s, the twins' units;
 *   the renders' names under cb_, hh_, cy_; PTCH 64 = TUNE 0)
 *   efmmetal_test -k MODEL K0..K7 SECS   one setting against its twin (WIN=1: per 10 ms) */
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
static dvoice_t v0;                                     /* the voice right after its trigger */

/* track 0 = model mi with knobs k (-99: the old default), one hit at vel; frames rendered into wl */
static void efmm_hit(uint32_t mi, const int16_t *k, uint32_t vel, uint32_t frames)
{
    uint32_t i;
    host_init();
    old_set_model(&trk[0], mi);
    for (i = 0; i < 8u; i++)
        if (k[i] != -99)
            trk[0].p[P_E0 + i] = k[i];
    drum_play(&trk[0], vel, trk[0].nt);   /* (drum_hit plays note 0) */
    v0 = trk[0].v[0];
    render_track(&trk[0], wl, frames);
}

/* ---- the prototypes in float, with the voice's constants */
#define PH(x) ((x) * (2 * M_PI / 4294967296.0))       /* 2^32 phase -> radians */
#define STEP(d) (1 - (d) / 4294967296.0)                /* a Q32 decay step -> its factor */

/* efm-cb: TUNE DECAY SNAP FB MOD MFRQ MDEC */
static void efmm_ref_cb(double *y, uint32_t n)
{
    const cbefm_t *c = &v0.ms.cbe;
    double g[2] = {1, CBEFM_G2 / 32768.0}, B = c->bq * 2 * M_PI / 131072.0, s = c->sn / 1073741824.0;
    double pc[2], pm[2], yk[2] = {0, 0}, ea = 1, es = 1, ei = c->ei / 268435456.0;
    double ka = STEP(c->da), ks = STEP(c->ds), ki = STEP(c->di);
    uint32_t i;
    int k;
    for (k = 0; k < 2; k++)
        pc[k] = PH(c->pc[k]), pm[k] = PH(c->pm[k]);
    for (i = 0; i < n; i++) {
        double o = 0;
        for (k = 0; k < 2; k++) {
            double m = cos(pm[k]);
            yk[k] = sin(pc[k] + B * yk[k]);
            o += g[k] * yk[k];
            pc[k] = fmod(pc[k] + PH(c->inc[k]) * (1 + ei * m), 2 * M_PI);
            pm[k] = fmod(pm[k] + PH(c->minc[k]), 2 * M_PI);
        }
        y[i] = o * ea * (1 - s + s * es);
        ea *= ka, es *= ks, ei *= ki;
    }
}

/* efm-hh: TUNE DECAY TREM TFRQ MOD MFRQ MDEC FB; its random phases: the voice's draws */
static void efmm_ref_hh(double *y, uint32_t n)
{
    const hhefm_t *c = &v0.ms.hhe;
    double wc = PH(c->inc), wm = PH(c->minc), I = c->ei / 134217728.0, beta = c->bq * 2 * M_PI / 131072.0;
    double a = HHEFM_A1 / 1073741824.0, a2 = 1 - HHEFM_A2 / 1073741824.0, dep = c->dep / 1073741824.0;
    double ka = STEP(c->da), ki = STEP(c->di), wt = PH(c->tinc), pc = 0, pm = PH(c->pm), h = 0, up = 0, l = 0, env = 1;
    int32_t rng = v0.rng;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double m = cos(pm), u, g;
        if (i % 32 == 0)
            pc = PH(noise32(&rng));
        u = sin(pc);
        pc = fmod(pc + wc * (1 + I * m) + beta * u, 2 * M_PI);
        pm = fmod(pm + wm, 2 * M_PI);
        h = a * (h + u - up), up = u;
        l = (1 - a2) * h + a2 * l;
        g = 1 - dep * (1 - cos(wt * i)) / 2;
        y[i] = l * env * g;
        I *= ki, env *= ka;
    }
}

/* efm-cy: TUNE DECAY FB HPF MOD MFRQ MDEC */
static void efmm_ref_cy(double *y, uint32_t n)
{
    const cyefm_t *c = &v0.ms.cye;
    double pc[4], pm[4], yp[4] = {0}, I = c->ih / 4096.0 * 2 * M_PI / 131072.0, B = c->bq * 2 * M_PI / 131072.0;
    double ka = STEP(c->da), ki = STEP(c->di), gk = c->gk / 1073741824.0, s1 = 0, s2 = 0, env = 1;
    uint32_t i;
    int k;
    for (k = 0; k < 4; k++)
        pc[k] = PH(c->pc[k]), pm[k] = PH(c->pm[k]);
    for (i = 0; i < n; i++) {
        double o = 0;
        for (k = 0; k < 4; k++) {
            double s = sin(pc[k] + I * cos(pm[k]) + B * yp[k]);
            yp[k] = s, o += s;
            pc[k] = fmod(pc[k] + PH(c->wc[k]), 2 * M_PI);
            pm[k] = fmod(pm[k] + PH(c->wm[k]), 2 * M_PI);
        }
        o *= env * (i < 16 ? (i + 1.0) / 17 : 1);
        I *= ki, env *= ka;
        if (gk > 0) {
            double v = (o - s1) * gk, lp = v + s1;
            s1 = lp + v, o -= lp;
            v = (o - s2) * gk, lp = v + s2;
            s2 = lp + v, o -= lp;
        }
        y[i] = o;
    }
}

/* the voice against its twin over [0, n): the least-squares gain and the residual (dB re the twin) */
static double efmm_resid(uint32_t n, double *gain)
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
static double efmm_vs_twin(uint32_t mi, const int16_t *k, uint32_t n, double *gain)
{
    efmm_hit(mi, k, 127, n);
    if (mi == DM_CBEFM)
        efmm_ref_cb(ref, n);
    else if (mi == DM_HHEFM)
        efmm_ref_hh(ref, n);
    else
        efmm_ref_cy(ref, n);
    return efmm_resid(n, gain);
}

static void efmm_twin_set(uint32_t mi, const int16_t (*K)[8], uint32_t nk, double secs)
{
    double worst = -999, g, g0 = 0, gw = 1;
    uint32_t i;
    char b[200];
    for (i = 0; i < nk; i++) {
        double e = efmm_vs_twin(mi, K[i], SECS(secs), &g);
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
    printf("     %s vs its float twin (%u settings, %.1f s): worst residual %.1f dB, gain %.0f / unit (spread %.2f %%)\n",
           N_MODEL[mi], nk, secs, worst, g0, 100 * (gw - 1));
    snprintf(b, sizeof b, "%s: every setting matches its float twin (residual < -30 dB, one gain within 1 %%)", N_MODEL[mi]);
    check(b, worst < -30 && fabs(gw - 1) < 0.01);
}

static void efmm_test_twins(void)
{
    static const int16_t B[][8] = {
        {0, 64, 0, 0, 64, 64, 64},  {0, 0, 0, 0, 64, 64, 64},   {0, 32, 0, 0, 64, 64, 64},  {0, 96, 0, 0, 64, 64, 64},
        {0, 127, 0, 0, 64, 64, 64}, {0, 64, 64, 0, 64, 64, 64}, {0, 64, 127, 0, 64, 64, 64}, {0, 0, 127, 0, 0, 64, 127},
        {0, 64, 0, 16, 64, 64, 64}, {0, 64, 0, 32, 64, 64, 64}, {0, 64, 0, 48, 64, 64, 64}, {0, 96, 0, 32, 0, 64, 127},
        {0, 64, 0, 0, 0, 64, 64},   {0, 64, 0, 0, 32, 64, 64},  {0, 64, 0, 0, 127, 64, 64}, {0, 64, 0, 0, 64, 0, 64},
        {0, 64, 0, 0, 64, 16, 64},  {0, 64, 0, 0, 64, 32, 64},  {0, 64, 0, 0, 64, 127, 64}, {0, 64, 0, 0, 64, 64, 0},
        {0, 64, 0, 0, 64, 64, 32},  {0, 64, 0, 0, 64, 64, 127}, {-12, 64, 0, 0, 64, 64, 64}, {12, 80, 40, 24, 100, 24, 90},
        {24, 64, 0, 0, 127, 127, 127}, {0, 64, 0, 64, 64, 64, 64}, {0, 64, 0, 96, 64, 64, 64}};
    static const int16_t H[][8] = {
        {0, 64, 0, 64, 64, 64, 64, 0},   {0, 0, 0, 64, 64, 64, 64, 0},    {0, 32, 0, 64, 64, 64, 64, 0},
        {0, 127, 0, 64, 64, 64, 64, 0},  {0, 64, 64, 64, 64, 64, 64, 0},  {0, 64, 127, 64, 64, 64, 64, 0},
        {0, 64, 127, 0, 64, 64, 64, 0},  {0, 64, 127, 127, 64, 64, 64, 0}, {0, 64, 0, 64, 0, 64, 64, 0},
        {0, 64, 0, 64, 32, 64, 64, 0},   {0, 64, 0, 64, 127, 64, 64, 0},  {0, 64, 0, 64, 64, 0, 64, 0},
        {0, 64, 0, 64, 64, 32, 64, 0},   {0, 64, 0, 64, 64, 127, 64, 0},  {0, 64, 0, 64, 64, 64, 0, 0},
        {0, 64, 0, 64, 64, 64, 127, 0},  {0, 64, 0, 64, 64, 64, 64, 1},   {0, 64, 0, 64, 64, 64, 64, 8},
        {0, 64, 0, 64, 64, 64, 64, 16},  {0, 64, 0, 64, 64, 64, 64, 32},  {-12, 64, 0, 64, 64, 64, 64, 0},
        {12, 90, 80, 100, 30, 20, 100, 4}, {0, 64, 0, 64, 64, 64, 64, 48}, {0, 64, 0, 64, 64, 64, 64, 64}};
    static const int16_t C[][8] = {
        {0, 64, 0, 0, 64, 64, 64},  {0, 0, 0, 0, 64, 64, 64},   {0, 32, 0, 0, 64, 64, 64},  {0, 127, 0, 0, 64, 64, 64},
        {0, 64, 16, 0, 64, 64, 64}, {0, 64, 32, 0, 64, 64, 64}, {0, 64, 0, 8, 64, 64, 64},  {0, 64, 0, 32, 64, 64, 64},
        {0, 64, 0, 64, 64, 64, 64}, {0, 64, 0, 127, 64, 64, 64}, {0, 64, 0, 0, 0, 64, 64},  {0, 64, 0, 0, 16, 64, 64},
        {0, 64, 0, 0, 127, 64, 64}, {0, 64, 0, 0, 64, 0, 64},   {0, 64, 0, 0, 64, 32, 64},  {0, 64, 0, 0, 64, 127, 64},
        {0, 64, 0, 0, 64, 64, 0},   {0, 64, 0, 0, 64, 64, 127}, {-12, 64, 0, 0, 64, 64, 64}, {12, 100, 24, 40, 90, 20, 100},
        {0, 64, 64, 0, 64, 64, 64}, {0, 64, 96, 0, 64, 64, 64}};
    /* the chaotic feedback (CB and CY FB 127, HH FB 80 and up): its character, not its trajectory (the machine's
     * neither): the level within 3 dB of the twin's */
    static const struct { uint32_t mi; int16_t k[8]; } X[] = {
        {DM_CBEFM, {0, 64, 0, 127, 64, 64, 64}}, {DM_CBEFM, {0, 96, 0, 127, 0, 64, 127}},
        {DM_HHEFM, {0, 64, 0, 64, 64, 64, 64, 80}}, {DM_HHEFM, {0, 64, 0, 64, 64, 64, 64, 127}},
        {DM_CYEFM, {0, 64, 127, 0, 64, 64, 64}}, {DM_CYEFM, {0, 64, 127, 0, 0, 64, 64}}};
    uint32_t i, j, ok = 1;
    efmm_twin_set(DM_CBEFM, B, sizeof B / sizeof B[0], 0.5);
    efmm_twin_set(DM_HHEFM, H, sizeof H / sizeof H[0], 0.5);
    efmm_twin_set(DM_CYEFM, C, sizeof C / sizeof C[0], 0.5);
    for (i = 0; i < sizeof X / sizeof X[0]; i++) {
        double g, a = 0, b = 0, gn = X[i].mi == DM_CBEFM ? 7457 : X[i].mi == DM_HHEFM ? 14148 : 996;   /* (the gains
                                                         * above) */
        efmm_vs_twin(X[i].mi, X[i].k, SECS(0.5), &g);
        for (j = 0; j < SECS(0.5); j++)
            a += (double)wl[j] * wl[j], b += gn * gn * ref[j] * ref[j];
        printf("     %s knobs %d %d %d %d %d %d %d %d (chaotic): level %+.1f dB re the twin\n", N_MODEL[X[i].mi], X[i].k[0],
               X[i].k[1], X[i].k[2], X[i].k[3], X[i].k[4], X[i].k[5], X[i].k[6], X[i].k[7], 10 * log10(a / b));
        ok &= fabs(10 * log10(a / b)) < 3;
    }
    check("the chaotic feedback (CB, CY FB 127; HH FB 80, 127): bounded, the level within 3 dB of the twin's", ok);
}

static double efmm_rms(const int32_t *x, uint32_t a, uint32_t b)
{
    double s = 0;
    uint32_t i;
    for (i = a; i < b; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (b - a));
}
static double efmm_goertzel(const int32_t *x, uint32_t n, double f)
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
/* the RMS over the hit: from its start to its last sample above -40 dB re its peak */
static double efmm_hit_rms(uint32_t n)
{
    int32_t pk = 0;
    uint32_t i, last = 0;
    for (i = 0; i < n; i++)
        pk = abs(wl[i]) > pk ? abs(wl[i]) : pk;
    for (i = 0; i < n; i++)
        if (abs(wl[i]) > pk / 100)
            last = i;
    return efmm_rms(wl, 0, last + 1);
}

/* the levels at the defaults (RMS over the hit) within +-3 dB of the existing model of the same instrument */
static void efmm_test_levels(void)
{
    static const uint32_t L[3][2] = {{DM_CBEFM, DM_COWB}, {DM_HHEFM, DM_HATC}, {DM_CYEFM, DM_CYMB}};
    static const int16_t DEF[8] = {-99, -99, -99, -99, -99, -99, -99, -99};
    uint32_t i, ok = 1;
    for (i = 0; i < 3u; i++) {
        double a, b;
        int32_t pk = 0;
        uint32_t j;
        efmm_hit(L[i][0], DEF, 127, SECS(3));
        a = efmm_hit_rms(SECS(3));
        for (j = 0; j < SECS(3); j++)
            pk = abs(wl[j]) > pk ? abs(wl[j]) : pk;
        efmm_hit(L[i][1], DEF, 127, SECS(3));
        b = efmm_hit_rms(SECS(3));
        printf("     %s vs %s at the defaults (RMS over the hit, to -40 dB): %+.1f dB (peak %d)\n", N_MODEL[L[i][0]],
               N_MODEL[L[i][1]], 20 * log10(a / b), pk);
        ok &= fabs(20 * log10(a / b)) <= 3;
    }
    check("levels: CBEFM ~ COWB, HHEFM ~ HATC, CYEFM ~ CYMB at the defaults (+-3 dB)", ok);
}

/* a decay's time constant from the RMS of two windows (w s) at t0 and t0 + dt (s) */
static double efmm_heard_tau(double t0, double dt, double w)
{
    return dt / log(efmm_rms(wl, SECS(t0), SECS(t0) + SECS(w)) / efmm_rms(wl, SECS(t0 + dt), SECS(t0 + dt) + SECS(w)));
}
/* a Q32 step's time constant (s) */
static double efmm_tc(uint32_t d) { return -1 / (FS * log(STEP(d))); }
static int efmm_near(double a, double b, double tol) { return fabs(a / b - 1) < tol; }

/* the measured laws: the constants each trigger computes against the prototypes' formulas, and what is heard */
static void efmm_test_laws(void)
{
    static const int16_t DEF[8] = {-99, -99, -99, -99, -99, -99, -99, -99};
    uint32_t ok = 1, ok2 = 1;
    int d;
    double a, b, c, w[3];
    for (d = 0; d <= 127; d++) {                    /* DECAY (all three), MDEC (CB, HH; CY its own), FB, MOD */
        int16_t k[8] = {0, (int16_t)d, 0, 0, (int16_t)d, 64, (int16_t)d, 0};
        double ta = 0.0646 * pow(2, (d - 64) / 13.5), ti = 0.112 * pow(2, (d - 64) / 11.5);
        efmm_hit(DM_CBEFM, k, 127, CTL);
        ok &= efmm_near(efmm_tc(v0.ms.cbe.da), ta, 0.005) && efmm_near(efmm_tc(v0.ms.cbe.di), ti, 0.005);
        ok &= fabs(v0.ms.cbe.ei / 268435456.0 - d / 32.65) < 1e-6;
        efmm_hit(DM_HHEFM, k, 127, CTL);
        ok &= efmm_near(efmm_tc(v0.ms.hhe.da), ta, 0.005) && efmm_near(efmm_tc(v0.ms.hhe.di), ti, 0.005);
        ok &= fabs(v0.ms.hhe.ei / 134217728.0 - d / 16.0) < 1e-6;
        efmm_hit(DM_CYEFM, k, 127, CTL);
        ok &= efmm_near(efmm_tc(v0.ms.cye.da), ta, 0.005) &&
              efmm_near(efmm_tc(v0.ms.cye.di), 0.275 * pow(2, d / 24.0), 0.005);
        ok &= efmm_near(v0.ms.cye.ih / 4096.0 * 2 * M_PI / 131072.0, (d + 7) / 13.5, 1e-4);
        k[3] = (int16_t)d, k[7] = (int16_t)d, k[2] = (int16_t)d;   /* FB: CB's k[3], HH's k[7], CY's k[2] */
        efmm_hit(DM_CBEFM, k, 127, CTL);
        ok &= fabs(v0.ms.cbe.bq * 2 * M_PI / 131072.0 + 0.0180 * d) < 2e-4;
        ok &= fabs(v0.ms.cbe.sn / 1073741824.0 - d / 157.0) < 1e-6;
        efmm_hit(DM_HHEFM, k, 127, CTL);
        ok &= fabs(v0.ms.hhe.bq * 2 * M_PI / 131072.0 + 0.049 * d) < 2e-4;
        ok &= fabs(v0.ms.hhe.dep / 1073741824.0 - d / 127.0) < 1e-6;
        efmm_hit(DM_CYEFM, k, 127, CTL);
        ok &= fabs(v0.ms.cye.bq * 2 * M_PI / 131072.0 - d / 64.0) < 2e-4;
        k[3] = (int16_t)d;                              /* HH TFRQ, CY HPF */
        efmm_hit(DM_HHEFM, k, 127, CTL);
        ok &= efmm_near(4294967296.0 / v0.ms.hhe.tinc / FS, 0.130 * pow(2, -(d - 64.0) / 21.4), 0.005);
        k[5] = (int16_t)d;                              /* MFRQ */
        efmm_hit(DM_CBEFM, k, 127, CTL);
        ok &= efmm_near((double)v0.ms.cbe.minc[0] / v0.ms.cbe.inc[0], (d < 1 ? 1 : d) / 16.0, 1e-6) ||
              (double)v0.ms.cbe.inc[0] * (d < 1 ? 1 : d) / 16 >= 4294967296.0;
        efmm_hit(DM_CYEFM, k, 127, CTL);
        ok &= efmm_near((double)v0.ms.cye.wm[3] / v0.ms.cye.wc[3], 1.00055 * (d / 16.0 + 0.5), 1e-6) ||
              v0.ms.cye.wc[3] * 1.00055 * (d / 16.0 + 0.5) >= 4294967296.0;
    }
    check("laws: DECAY 64.6 ms x 2 ^ ((v - 64) / 13.5), MDEC (CB, HH) 112 ms x 2 ^ ((v - 64) / 11.5), CY MDEC 275 ms x "
          "2 ^ (v / 24), MOD /32.65 /16 (v + 7) / 13.5, FB -0.018 -0.049 1 / 64, SNAP / 157, TREM / 127, TFRQ, MFRQ "
          "(each within 0.5 %)", ok);
    /* the pitches: CB's tones 409.7 and x 1.4805, HH 577.0, CY 580.05 x the ratios; TUNE +12 an octave */
    {
        static const int16_t CB0[8] = {0, 127, 0, 0, 0, 64, 64}, CB12[8] = {12, 127, 0, 0, 0, 64, 64};
        static const int16_t CY0[8] = {0, 127, 0, 0, 0, 64, 0}, CY12[8] = {12, 127, 0, 0, 0, 64, 0};
        static const double R[4] = {1, 1.4103, 1.8005, 2.6997};
        double l, off;
        uint32_t k;
        efmm_hit(DM_CBEFM, CB0, 127, SECS(0.5));
        a = efmm_goertzel(wl, SECS(0.5), 409.729), b = efmm_goertzel(wl, SECS(0.5), 409.729 * 1.48047);
        off = efmm_goertzel(wl, SECS(0.5), 409.729 * 1.2);
        ok2 &= a > 30 * off && fabs(20 * log10(b / a) - 0.17) < 0.5;
        efmm_hit(DM_CBEFM, CB12, 127, SECS(0.5));
        c = efmm_goertzel(wl, SECS(0.5), 2 * 409.729);
        ok2 &= c > 30 * efmm_goertzel(wl, SECS(0.5), 2 * 409.729 * 1.2) && efmm_near(c, a, 0.05);
        printf("     CB at MOD 0: 409.7 Hz %.0f, x 1.4805 %+.2f dB re it (1.02: +0.17), TUNE +12 an octave up\n", a,
               20 * log10(b / a));
        efmm_hit(DM_CYEFM, CY0, 127, SECS(0.5));
        for (k = 0; k < 4u; k++) {
            l = efmm_goertzel(wl, SECS(0.5), 580.05 * R[k]);
            off = efmm_goertzel(wl, SECS(0.5), 580.05 * R[k] * 1.03);
            ok2 &= l > 10 * off;
        }
        a = efmm_goertzel(wl, SECS(0.5), 580.05 * 2.6997);
        efmm_hit(DM_CYEFM, CY12, 127, SECS(0.5));
        ok2 &= efmm_goertzel(wl, SECS(0.5), 2 * 580.05 * 2.6997) > 10 * efmm_goertzel(wl, SECS(0.5), 2 * 580.05 * 2.6997 * 1.03);
        efmm_hit(DM_HHEFM, DEF, 127, CTL);
        ok2 &= efmm_near((double)v0.ms.hhe.inc * FS / 4294967296.0, 243.0 * pow(2, 64 / 51.3), 1e-5);
        check("pitches: CB 409.7 Hz and x 1.4805 (+0.17 dB), CY's four lines at 580.05 x 1 / 1.4103 / 1.8005 / 2.6997, HH "
              "577.0 Hz; TUNE +12 an octave up", ok2);
    }
    /* heard: DECAY's tau (CB, HH, CY at MOD 0), SNAP 127's drop (-14.4 dB after its fast part), TREM 127's notch at
     * T / 2, CY's HPF 64 on its 580 Hz line (the 2-pole Q 0.5 response, -17.9 dB) */
    {
        static const int16_t CBD[8] = {0, 96, 0, 0, 0, 64, 64}, CBS[8] = {0, 96, 127, 0, 0, 64, 64};
        static const int16_t HHD[8] = {0, 96, 0, 64, 0, 64, 64, 0}, HHT[8] = {0, 127, 127, 64, 64, 64, 64, 0};
        static const int16_t CYD[8] = {0, 96, 0, 0, 0, 64, 0}, CYH[8] = {0, 96, 0, 64, 0, 64, 0};
        double want = 0.0646 * pow(2, 32 / 13.5), snap, notch, top, hp, hp0;
        efmm_hit(DM_CBEFM, CBD, 127, SECS(1.0));
        w[0] = efmm_heard_tau(0.05, want, 0.02) / want;
        a = efmm_rms(wl, SECS(0.15), SECS(0.17));
        efmm_hit(DM_CBEFM, CBS, 127, SECS(1.0));
        snap = 20 * log10(efmm_rms(wl, SECS(0.15), SECS(0.17)) / a);
        efmm_hit(DM_HHEFM, HHD, 127, SECS(1.0));
        w[1] = efmm_heard_tau(0.05, want, 0.15) / want;   /* (150 ms: the random blocks) */
        efmm_hit(DM_CYEFM, CYD, 127, SECS(1.0));
        w[2] = efmm_heard_tau(0.05, want, 0.05) / want;
        efmm_hit(DM_HHEFM, HHT, 127, SECS(0.3));
        top = efmm_rms(wl, SECS(0.0), SECS(0.01));
        notch = efmm_rms(wl, SECS(0.060), SECS(0.070));   /* T / 2 = 65 ms */
        efmm_hit(DM_CYEFM, CYD, 127, SECS(0.5));
        hp0 = efmm_goertzel(wl, SECS(0.5), 580.05);
        efmm_hit(DM_CYEFM, CYH, 127, SECS(0.5));
        hp = 20 * log10(efmm_goertzel(wl, SECS(0.5), 580.05) / hp0);
        printf("     DECAY 96 heard: tau CB %.3f HH %.3f CY %.3f x the law; SNAP 127 %+.1f dB; TREM 127 notch %.1f dB; "
               "CY HPF 64 at 580 Hz %+.1f dB\n", w[0], w[1], w[2], snap, 20 * log10(notch / top), hp);
        check("heard: DECAY's tau (+-5 %), SNAP 127 -14.4 dB (+-1), TREM 127 a notch below -25 dB at T / 2, CY HPF 64 "
              "-17.9 dB at 580 Hz (+-1)", efmm_near(w[0], 1, 0.05) && efmm_near(w[1], 1, 0.05) && efmm_near(w[2], 1, 0.05) &&
              fabs(snap + 14.4) < 1 && 20 * log10(notch / top) < -25 && fabs(hp + 17.9) < 1);
    }
}

/* every knob at its ends (TUNE -24 / +24), a step's note +-24, velocity 1 / 127: bounded, the voice ends in 6 s */
static void efmm_test_extremes(void)
{
    static const uint32_t M[3] = {DM_CBEFM, DM_HHEFM, DM_CYEFM}, NK[3] = {7, 8, 7};
    uint32_t j, c, i, ok = 1, ended = 1;
    for (j = 0; j < 3u; j++)
        for (c = 0; c < (2u << NK[j]); c++) {
            host_init();
            old_set_model(&trk[0], M[j]);
            for (i = 0; i < NK[j]; i++)
                trk[0].p[P_E0 + i] = (int16_t)((c >> i & 1u) ? (i ? 127 : 24) : (i ? 0 : -24));
            trk[0].nt = (c >> NK[j] & 1u) ? 24 : -24;
            drum_play(&trk[0], (c & 3u) == 3u ? 1 : 127, trk[0].nt);   /* (drum_hit plays note 0) */
            render_track(&trk[0], wl, SECS(6));
            for (i = 0; i < SECS(6); i++)
                ok &= abs(wl[i]) <= 4 * VOICE_FS;
            if (trk[0].v[0].active || trk[0].v[1].active) {
                printf("     %s: knobs %u, note %+d: still sounding at 6 s\n", N_MODEL[M[j]], c, trk[0].nt);
                ended = 0;
            }
        }
    check("every knob at its ends, notes +-24, velocity 1 / 127: bounded, the voice ends within 6 s (the three models)",
          ok && ended);
}

/* TUNE +-24 with notes +-24 (pitch +-48 semitones) and the heaviest knobs: no signed overflow (UBSan), bounded, ends */
static void efmm_test_note48(uint32_t model, const int16_t *k, const char *what)
{
    uint32_t i, s, ok = 1;
    for (s = 0; s < 4u; s++) {
        host_init();
        old_set_model(&trk[0], model);
        for (i = 0; i < 8u; i++)
            trk[0].p[P_E0 + i] = k[i];
        trk[0].p[P_E0] = (s & 1u) ? 24 : -24;
        trk[0].nt = (s & 2u) ? 24 : -24;
        drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
        render_track(&trk[0], wl, SECS(6));
        for (i = 0; i < SECS(6); i++)
            ok &= abs(wl[i]) <= 4 * VOICE_FS;
        ok &= !trk[0].v[0].active && !trk[0].v[1].active;
    }
    check(what, ok);
}

/* re-hits 5 ms apart at every knob 127 and a switch to each model while another rings: bounded */
static void efmm_test_rehits(void)
{
    static const uint32_t M[3] = {DM_CBEFM, DM_HHEFM, DM_CYEFM};
    static const int16_t K[8] = {0, 127, 127, 127, 127, 127, 127, 127};
    uint32_t j, i, ok = 1;
    for (j = 0; j < 3u; j++) {
        efmm_hit(M[j], K, 127, SECS(0.005));
        for (i = 0; i < 4u; i++) {
            drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
            render_track(&trk[0], wl, SECS(0.005));
        }
        render_track(&trk[0], wl, SECS(0.3));
        for (i = 0; i < SECS(0.3); i++)
            ok &= abs(wl[i]) <= 4 * VOICE_FS;
        host_init();
        old_set_model(&trk[0], DM_CYMB);
        drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
        render_track(&trk[0], wl, SECS(0.05));
        old_set_model(&trk[0], M[j]);
        drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
        render_track(&trk[0], wl, SECS(0.3));
        for (i = 0; i < SECS(0.3); i++)
            ok &= abs(wl[i]) <= 4 * VOICE_FS;
    }
    check("re-hits 5 ms apart and a model switch while ringing: bounded (the three models)", ok);
}

/* HHEFM in choke group 1 (set: no voice has a default group): a ringing open hat on another group-1 track is cut by
 * it, and it by them */
static void efmm_test_choke(void)
{
    uint32_t ok;
    host_init();
    old_set_model(&trk[1], DM_HATO);
    old_set_model(&trk[2], DM_HHEFM);
    old_set_model(&trk[3], DM_CYEFM);
    trk[1].p[P_CHOKE] = trk[2].p[P_CHOKE] = 1;
    drum_hit(&trk[1], 127);
    render_track(&trk[1], wl, SECS(0.05));
    drum_hit(&trk[2], 127);
    ok = trk[2].p[P_CHOKE] == 1 && !trk[1].v[0].active && trk[3].p[P_CHOKE] == 0 && DMODELS[DM_CBEFM].choke == 0 &&
         DMODELS[DM_CYEFM].voices == 2 && DMODELS[DM_HHEFM].voices == 1;
    render_track(&trk[2], wl, SECS(0.01));
    drum_hit(&trk[1], 127);
    ok &= !trk[2].v[0].active;
    check("HHEFM in choke group 1 (no default group; CYEFM 2 voices): it cuts a ringing HATO and HATO cuts it", ok);
}

/* the md-re sweeps as the voice renders them (the twin's units: the emulator renders' scale) */
static void efmm_write_wav(const char *path, uint32_t n, double g)
{
    FILE *f = fopen(path, "wb");
    uint32_t v, i;
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
static void efmm_write_wavs(const char *dir)
{
    static const struct { uint32_t mi; const char *pre, *knob; int idx; double unit; } S[] = {
        {DM_CBEFM, "cb", "dec", 1, 0.115}, {DM_CBEFM, "cb", "snap", 2, 0.115}, {DM_CBEFM, "cb", "fb", 3, 0.115},
        {DM_CBEFM, "cb", "mod", 4, 0.115}, {DM_CBEFM, "cb", "mfrq", 5, 0.115}, {DM_CBEFM, "cb", "mdec", 6, 0.115},
        {DM_HHEFM, "hh", "dec", 1, 0.91},  {DM_HHEFM, "hh", "trem", 2, 0.91},  {DM_HHEFM, "hh", "tfrq", 3, 0.91},
        {DM_HHEFM, "hh", "mod", 4, 0.91},  {DM_HHEFM, "hh", "mfrq", 5, 0.91},  {DM_HHEFM, "hh", "mdec", 6, 0.91},
        {DM_HHEFM, "hh", "fb", 7, 0.91},   {DM_CYEFM, "cy", "dec", 1, 0.0484}, {DM_CYEFM, "cy", "fb", 2, 0.0484},
        {DM_CYEFM, "cy", "hpf", 3, 0.0484}, {DM_CYEFM, "cy", "mod", 4, 0.0484}, {DM_CYEFM, "cy", "mfrq", 5, 0.0484},
        {DM_CYEFM, "cy", "mdec", 6, 0.0484}};
    static const int16_t N[3][8] = {{0, 64, 0, 0, 64, 64, 64, 0}, {0, 64, 0, 64, 64, 64, 64, 0}, {0, 64, 0, 0, 64, 64, 64, 0}};
    uint32_t s, w = 0;
    int v;
    for (s = 0; s < sizeof S / sizeof S[0]; s++)
        for (v = 0; v <= 128; v += 32) {
            int16_t k[8];
            char path[512];
            double g;
            uint32_t j, m = S[s].mi == DM_CBEFM ? 0 : S[s].mi == DM_HHEFM ? 1 : 2;
            for (j = 0; j < 8u; j++)
                k[j] = N[m][j];
            k[S[s].idx] = (int16_t)(v > 127 ? 127 : v);
            efmm_vs_twin(S[s].mi, k, SECS(0.3), &g);   /* the firmware's units per twin unit */
            efmm_hit(S[s].mi, k, 127, SECS(2.0));
            snprintf(path, sizeof path, "%s/%s_%s_%03d.wav", dir, S[s].pre, S[s].knob, k[S[s].idx]);
            efmm_write_wav(path, SECS(2.0), g / S[s].unit);
            w++;
        }
    printf("     wrote %u WAVs to %s\n", w, dir);
}

int main(int argc, char **argv)
{
    if (argc > 10 && !strcmp(argv[1], "-k")) {         /* (debug: -k MODEL k0..k7 SECS: one setting vs its twin) */
        int16_t k[8];
        double g, e;
        uint32_t i, nn = SECS(atof(argv[11]));
        for (i = 0; i < 8u; i++)
            k[i] = (int16_t)atoi(argv[3 + i]);
        e = efmm_vs_twin((uint32_t)atoi(argv[2]), k, nn, &g);
        printf("residual %.1f dB, gain %.0f\n", e, g);
        if (getenv("WIN")) {
            uint32_t a, w = SECS(0.01);
            for (a = 0; a + w <= nn; a += w) {
                double ee = 0, rr = 1e-30;
                for (i = a; i < a + w; i++) {
                    ee += (wl[i] - g * ref[i]) * (wl[i] - g * ref[i]);
                    rr += g * g * ref[i] * ref[i];
                }
                printf("  %3u ms: %6.1f dB (level %.1f dB)\n", a * 1000 / (uint32_t)FS, 10 * log10(ee / rr + 1e-30),
                       10 * log10(rr / w));
            }
        }
        return 0;
    }
    efmm_test_twins();
    efmm_test_levels();
    efmm_test_laws();
    efmm_test_extremes();
    efmm_test_rehits();
    efmm_test_choke();
    {
        static const int16_t KB[8] = {24, 127, 127, 127, 127, 127, 127, 0}, KH[8] = {24, 127, 127, 127, 127, 127, 127, 127};
        efmm_test_note48(DM_CBEFM, KB, "CBEFM: TUNE +-24 with notes +-24, every knob 127: no overflow, bounded, ends");
        efmm_test_note48(DM_HHEFM, KH, "HHEFM: TUNE +-24 with notes +-24, every knob 127: no overflow, bounded, ends");
        efmm_test_note48(DM_CYEFM, KB, "CYEFM: TUNE +-24 with notes +-24, every knob 127: no overflow, bounded, ends");
    }
    if (argc > 1)
        efmm_write_wavs(argv[1]);
    printf(fails ? "efmmetal_test: %d FAILED\n" : "efmmetal_test: all passed\n", fails);
    return fails != 0;
}
