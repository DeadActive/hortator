/* SDEFM, CPEFM, RSEFM (dm_efmn.c): the fixed-point voices against their float references (the md-re prototypes
 * efm-sd.c, efm-cp.c, efm-rs.c, copied below), the measured laws, the levels, the extremes. Each test prints one line;
 * any failure exits 1.
 *   efmn_test [OUTDIR]   OUTDIR: also writes an A/B set as float WAVs in the prototypes' units (md-re renders' names) */
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

static double efmn_tau_d32(uint32_t d) { return -1 / (FS * log(1 - d / 4294967296.0)); }   /* a Q32 fall's tau, s */
static double efmn_k32(uint32_t d) { return 1 - d / 4294967296.0; }                      /* its factor per sample */
static double efmn_wrap(double x) { return x - floor(x); }                                /* cycles */

/* the track filter (RBJ high-pass 21.3 Hz, Q 0.68) with the firmware's coefficients */
typedef struct { double x1, x2, y1, y2; } efmn_fhp;
static double efmn_fhp_run(efmn_fhp *h, double x)
{
    double y = (EFMN_HP_B0 * (x - 2 * h->x1 + h->x2) - (double)EFMN_HP_A1 * h->y1 - (double)EFMN_HP_A2 * h->y2) /
               1073741824.0;
    h->x2 = h->x1;
    h->x1 = x;
    h->y2 = h->y1;
    h->y1 = y;
    return y;
}

/* SD and CP's noise-FM carrier in float (md-re proto/efm-sd.c, efm-cp.c): the firmware's increments (TUNE in
 * semitones: Hortator's pitch table), the deviation's tau from its Q32 step, the firmware's FM noise (the same xorshift
 * from the same seed, 16 bits) */
typedef struct { double pc, pm, fc, fm, dev, ei, ki, nz; int32_t fz; uint32_t hc, hold; } efmn_ffm;
static void efmn_ffm_set(efmn_ffm *f, const efmn_fm_t *q)
{
    f->pc = q->pc / 4294967296.0;
    f->pm = q->pm / 4294967296.0;
    f->fc = q->inc / 4294967296.0;
    f->fm = q->incm / 4294967296.0;
    f->dev = q->dev * 4.0 / 4294967296.0;
    f->ei = q->ei ? 1 : 0;
    f->ki = efmn_k32(q->di);
    f->fz = EFMN_SEED;
    f->hc = 0;
    f->hold = q->hold;
    f->nz = 0;
}
static double efmn_ffm_tick(efmn_ffm *f)
{
    double s = sin(2 * M_PI * f->pc);
    if (!f->hc) {
        f->nz = ((int32_t)(noise32(&f->fz) >> 16) - 32768) / 32768.0;
        f->hc = f->hold;
    }
    f->hc--;
    f->pc = efmn_wrap(f->pc + f->fc + f->dev * f->ei * (0.35 * cos(2 * M_PI * f->pm) + 0.85 * f->nz));
    f->pm = efmn_wrap(f->pm + f->fm);
    f->ei *= f->ki;
    return s;
}

/* track 0 = model mi with knobs k (-99: the default), one hit at vel playing note nt, frames rendered into x; the
 * voice's state after its trigger in *st (the float references read the firmware's increments and steps there) */
static dvoice_t efmn_st;
static void efmn_hit(uint32_t mi, const int16_t *k, uint32_t vel, int nt, int32_t *x, uint32_t frames)
{
    uint32_t i;
    host_init();
    drum_set_model(&trk[0], mi);
    for (i = 0; i < 8u; i++)
        if (k[i] != -99)
            trk[0].p[P_E0 + i] = k[i];
    drum_play(&trk[0], vel, nt);
    efmn_st = trk[0].v[0];
    render_track(&trk[0], x, frames);
}

/* SDEFM (md-re proto/efm-sd.c): TUNE DECAY NOIS NDEC MOD MFRQ MDEC HPF; the NOIS layer from the voice's own noise */
static void ref_sd(const int16_t *p, double *y, uint32_t n)
{
    const efmn_t *q = &efmn_st.ms.en;
    efmn_ffm f;
    efmn_fhp h = {0};
    double ea = q->ea / 2147483648.0, ka = efmn_k32(q->da), en = q->en / 2147483648.0, kn = efmn_k32(q->dn);
    double cl = SDEFM_CL / 1073741824.0, an = SDEFM_AN / 1073741824.0, an2 = SDEFM_AN2 / 1073741824.0;
    double nlp = 0, nhp = 0, nxp = 0, ah = 1 - p[7] / 256.0, hh = 0, hx = 0;
    int32_t rn = efmn_st.rng;
    uint32_t i;
    efmn_ffm_set(&f, &q->fm);
    for (i = 0; i < n; i++) {
        double v = ea * efmn_ffm_tick(&f);
        if (en > 0) {
            double w = en * (((int32_t)noise32(&rn) >> 16) / 32768.0);
            nlp += (w - nlp) * cl;
            nhp = an * nhp + an2 * (nlp - nxp);
            nxp = nlp;
            v += nhp;
            en *= kn;
        }
        if (p[7] > 0) {
            hh = ah * hh + (1 + ah) / 2 * (v - hx);
            hx = v;
            v = hh;
        }
        y[i] = efmn_fhp_run(&h, v);
        ea *= ka;
    }
}

/* CPEFM (md-re proto/efm-cp.c): TUNE DECAY CLPS CDEC MOD MFRQ MDEC HPF; the clap period as the firmware's (its
 * table read in log2) */
static void ref_cp(const int16_t *p, double *y, uint32_t n)
{
    const efmn_t *q = &efmn_st.ms.en;
    efmn_ffm f;
    efmn_fhp h = {0};
    double A = CPEFM_A / 2147483648.0, ea = A, ec = A, ka = efmn_k32(q->da), kc = efmn_k32(q->dc);
    double per = q->per / 65536.0, tc = 1, ah = 1 - p[7] / 256.0, hh = 0, hx = 0;
    int nb = p[2] / 8, k = 0;
    uint32_t i;
    efmn_ffm_set(&f, &q->fm);
    for (i = 0; i < n; i++) {
        double env, v;
        if (k < nb) {
            env = ec;
            ec *= kc;
            if ((tc += 1) >= per) {
                tc -= per;
                k++;
                ec = A;
            }
        } else {
            env = ea;
            ea *= ka;
        }
        v = env * efmn_ffm_tick(&f);
        if (p[7] > 0) {
            hh = ah * hh + (1 + ah) / 2 * (v - hx);
            hx = v;
            v = hh;
        }
        y[i] = efmn_fhp_run(&h, v);
    }
}

/* RSEFM (md-re proto/efm-rs.c): TUNE DECAY MOD HPF SNAR SPTC SDEC SMOD; the firmware's increments and deviations
 * (its Kr / Ks tables), each voice's own fixed noise (two xorshifts: the prototype draws both from one) */
static void ref_rs(const int16_t *p, double *y, uint32_t n)
{
    const efmrs_t *q = &efmn_st.ms.er;
    efmn_fhp h = {0};
    double pr = q->pc[0] / 4294967296.0, ps = q->pc[1] / 4294967296.0, fr = q->inc[0] / 4294967296.0;
    double fs = q->inc[1] / 4294967296.0, dr = q->dev[0] * 4.0 / 4294967296.0, ds = q->dev[1] * 4.0 / 4294967296.0;
    double er = q->e[0] / 2147483648.0, es = q->e[1] / 2147483648.0, kr = efmn_k32(q->d[0]), ks = efmn_k32(q->d[1]);
    double lr = RSEFM_LR / 1073741824.0, ls = RSEFM_LS / 1073741824.0, nr = 0, ns = 0, hg = 0, hs = 0;
    int32_t zr = EFMN_SEED, zs = EFMN_SEED2;
    uint32_t i;
    if (p[3] > 0) {
        int H = p[3], j = H >= 112 ? 7 : H / 16;
        int fc = RSEFM_HF[j] + (RSEFM_HF[j + 1] - RSEFM_HF[j]) * (j == 7 ? H - 112 : H % 16) / (j == 7 ? 15 : 16);
        hg = tan(M_PI * fc / FS);
    }
    for (i = 0; i < n; i++) {
        double v = (er * sin(2 * M_PI * pr) + es * sin(2 * M_PI * ps)) * (i == 0 ? 0.75 : 1);
        nr += lr * (((int32_t)(noise32(&zr) >> 1) - 1073741824) / 1073741824.0 - nr);
        if (es > 0)
            ns += ls * (((int32_t)(noise32(&zs) >> 1) - 1073741824) / 1073741824.0 - ns);
        pr = efmn_wrap(pr + fr + dr * (nr + 0.03));
        ps = efmn_wrap(ps + fs + ds * ns);
        er *= kr;
        es *= ks;
        if (hg > 0) {
            double u = (v - hs) * hg / (1 + hg), lp = u + hs;
            hs = lp + u;
            v -= lp;
        }
        y[i] = efmn_fhp_run(&h, v);
    }
}

typedef void (*efmn_ref_fn)(const int16_t *p, double *y, uint32_t n);
/* the firmware against the reference over [0, n): the least-squares gain and the residual (dB re the reference) */
static double efmn_vs_ref(uint32_t mi, efmn_ref_fn rf, const int16_t *k, uint32_t n, double *gain)
{
    double xy = 0, yy = 0, e = 0, r = 1e-30;
    uint32_t i;
    efmn_hit(mi, k, 127, 0, wl, n);
    rf(k, ref, n);
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

static void efmn_ref_set(const char *name, uint32_t mi, efmn_ref_fn rf, const int16_t (*K)[8], uint32_t nk)
{
    double worst = -999, g, g0 = 0, gw = 1;
    uint32_t i;
    char what[160];
    for (i = 0; i < nk; i++) {
        double e = efmn_vs_ref(mi, rf, K[i], SECS(0.3), &g);
        if (!i)
            g0 = g;
        if (fabs(g / g0 - 1) > fabs(gw - 1))
            gw = g / g0;
        if (e > worst)
            worst = e;
        if (e > -36)
            printf("     knobs %d %d %d %d %d %d %d %d: residual %.1f dB\n", K[i][0], K[i][1], K[i][2], K[i][3], K[i][4],
                   K[i][5], K[i][6], K[i][7], e);
    }
    printf("     %s vs the float reference (%u settings, 0.3 s): worst residual %.1f dB, gain %.0f / unit (spread "
           "%.2f %%)\n", name, (unsigned)nk, worst, g0, 100 * (gw - 1));
    snprintf(what, sizeof what, "%s: every setting matches the float reference (residual < -30 dB, one gain within 1 %%)",
             name);
    check(what, worst < -30 && fabs(gw - 1) < 0.01);
}

static void test_efmn_vs_ref(void)
{
    /* TUNE DECAY NOIS NDEC MOD MFRQ MDEC HPF */
    static const int16_t KS[][8] = {
        {0, 64, 64, 64, 64, 64, 64, 0},   {0, 64, 0, 64, 0, 64, 64, 0},    {0, 64, 0, 64, 64, 64, 64, 0},
        {-24, 64, 64, 64, 64, 64, 64, 0}, {24, 64, 64, 64, 64, 64, 64, 0}, {0, 0, 64, 64, 64, 64, 64, 0},
        {0, 127, 64, 64, 64, 64, 64, 0},  {0, 64, 127, 127, 64, 64, 64, 0}, {0, 64, 127, 0, 0, 64, 64, 0},
        {0, 64, 0, 64, 127, 64, 64, 0},   {0, 64, 0, 64, 16, 64, 64, 0},   {0, 64, 0, 64, 64, 0, 64, 0},
        {0, 64, 0, 64, 64, 127, 64, 0},   {0, 64, 0, 64, 64, 64, 0, 0},    {0, 64, 0, 64, 64, 64, 127, 0},
        {0, 64, 64, 64, 64, 64, 64, 16},  {0, 64, 64, 64, 64, 64, 64, 64}, {0, 64, 64, 64, 64, 64, 64, 127},
        {7, 100, 30, 90, 100, 20, 100, 40}, {-12, 20, 127, 20, 127, 127, 0, 1}};
    /* TUNE DECAY CLPS CDEC MOD MFRQ MDEC HPF */
    static const int16_t KC[][8] = {
        {0, 64, 64, 64, 64, 64, 64, 0},   {0, 64, 64, 64, 0, 64, 64, 0},   {0, 64, 0, 64, 64, 64, 64, 0},
        {0, 64, 7, 64, 0, 64, 64, 0},     {0, 64, 127, 0, 0, 64, 64, 0},   {0, 64, 127, 127, 0, 64, 64, 0},
        {0, 64, 64, 32, 64, 64, 64, 0},   {0, 64, 64, 120, 64, 64, 64, 0}, {-24, 64, 64, 64, 64, 64, 64, 0},
        {24, 64, 64, 64, 64, 64, 64, 0},  {0, 0, 64, 64, 64, 64, 64, 0},   {0, 127, 64, 64, 64, 64, 64, 0},
        {0, 64, 64, 64, 127, 64, 64, 0},  {0, 64, 64, 64, 64, 0, 64, 0},   {0, 64, 64, 64, 64, 127, 127, 0},
        {0, 64, 64, 64, 64, 64, 0, 0},    {0, 64, 64, 64, 64, 64, 64, 32}, {0, 64, 64, 64, 64, 64, 64, 127},
        {5, 90, 100, 80, 30, 16, 90, 20}};
    /* TUNE DECAY MOD HPF SNAR SPTC SDEC SMOD */
    static const int16_t KR[][8] = {
        {0, 64, 64, 0, 64, 64, 64, 64},   {0, 64, 0, 0, 0, 64, 64, 64},    {0, 96, 64, 0, 0, 64, 64, 64},
        {0, 0, 0, 0, 127, 64, 96, 0},     {0, 0, 0, 0, 127, 0, 96, 64},    {0, 0, 0, 0, 127, 127, 96, 127},
        {-24, 64, 64, 0, 64, 64, 64, 64}, {24, 64, 64, 0, 64, 64, 64, 64}, {0, 0, 64, 0, 64, 64, 64, 64},
        {0, 127, 127, 0, 64, 64, 64, 64}, {0, 64, 64, 16, 64, 64, 64, 64}, {0, 64, 64, 64, 64, 64, 64, 64},
        {0, 64, 64, 127, 64, 64, 64, 64}, {0, 64, 64, 0, 127, 64, 127, 64}, {0, 64, 64, 0, 64, 64, 0, 0},
        {-9, 30, 100, 40, 90, 20, 110, 8}};
    efmn_ref_set("SDEFM", DM_SDEFM, ref_sd, KS, sizeof KS / sizeof KS[0]);
    efmn_ref_set("CPEFM", DM_CPEFM, ref_cp, KC, sizeof KC / sizeof KC[0]);
    efmn_ref_set("RSEFM", DM_RSEFM, ref_rs, KR, sizeof KR / sizeof KR[0]);
}

static double efmn_zc(const int32_t *x, uint32_t a, uint32_t b)   /* rising zero crossings / time */
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

static double efmn_rms(const int32_t *x, uint32_t a, uint32_t b)
{
    double s = 0;
    uint32_t i;
    for (i = a; i < b; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (b - a));
}

/* the pitch laws (MOD 0: a pure sine): SD 25.64 Hz x 2 ^ ((72 + 40) / 32) = 290.1 Hz, CP 2 ^ ((72 + 88) / 32) = 820.5
 * Hz, RS's rim 243.93 x 4 = 975.7 Hz at TUNE 0; +-12 an octave; RS's second voice 121.91 Hz x 2 ^ (SPTC / 51.1) */
static void test_efmn_pitch(void)
{
    static const struct { uint32_t m; double f; int16_t k[8]; } P[3] = {
        {DM_SDEFM, 290.0835, {0, 127, 0, 64, 0, 64, 64, 0}},
        {DM_CPEFM, 820.48, {0, 127, 0, 64, 0, 64, 64, 0}},
        {DM_RSEFM, 975.72, {0, 127, 0, 0, 0, 64, 64, 0}}};
    static const int SP[3] = {0, 64, 127};
    double worst = 0, f[3];
    uint32_t i, j;
    for (i = 0; i < 3u; i++)
        for (j = 0; j < 3u; j++) {
            int nt = (int)j * 12 - 12;
            efmn_hit(P[i].m, P[i].k, 127, nt, wl, SECS(0.4));
            f[j] = efmn_zc(wl, SECS(0.05), SECS(0.4));
            worst = fmax(worst, fabs(f[j] / (P[i].f * pow(2, nt / 12.0)) - 1));
        }
    printf("     the pitches at notes -12 / 0 / +12, worst %.3f %% off the laws\n", 100 * worst);
    for (j = 0; j < 3u; j++) {                          /* RS's second voice alone: the rim gone (DECAY 0) */
        int16_t k[8] = {0, 0, 0, 0, 127, 0, 127, 0};
        k[5] = (int16_t)SP[j];
        efmn_hit(DM_RSEFM, k, 127, 0, wl, SECS(0.5));
        f[j] = efmn_zc(wl, SECS(0.1), SECS(0.5));
    }
    printf("     RSEFM's second voice at SPTC 0 / 64 / 127 (SMOD 0): %.2f / %.2f / %.2f Hz\n", f[0], f[1], f[2]);
    check("EFM noise voices: the pitch laws (SD 290.1, CP 820.5, RS 975.7 Hz at TUNE 0, +-12 an octave, within 0.3 %; "
          "RS's second voice 121.9 x 2 ^ (SPTC / 51.1), within 1 %: SMOD 0 still modulates)",
          worst < 0.003 && fabs(f[0] / 121.91 - 1) < 0.01 && fabs(f[1] / (121.91 * pow(2, 64 / 51.1)) - 1) < 0.01 &&
          fabs(f[2] / (121.91 * pow(2, 127 / 51.1)) - 1) < 0.01);
}

/* the family's DEC law, tau = 64.6 ms x 2 ^ ((d - 64) / 13.5) (heard on SD's body at 32 / 64 / 96; every model's
 * steps within 0.5 % of it); MDEC tau_i = 112 ms x 2 ^ ((m - 64) / 11.5) and NDEC tau_n = 25.4 ms x 2 ^ ((n - 64) /
 * 18.9) (their steps); the claps' tau = 0.393 x the period */
static void test_efmn_decays(void)
{
    static const int D[3] = {32, 64, 96};
    double tau[3], wd = 0, wi = 0, wn = 0, wc = 0;
    uint32_t i;
    int d;
    for (i = 0; i < 3u; i++) {
        int16_t k[8] = {0, 0, 0, 64, 0, 64, 64, 0};
        double want = 0.0646 * pow(2, (D[i] - 64) / 13.5);
        k[1] = (int16_t)D[i];
        efmn_hit(DM_SDEFM, k, 127, 0, wl, SECS(1.2));
        tau[i] = (want * 2) / log(efmn_rms(wl, SECS(0.01), SECS(0.01) + SECS(0.02)) /
                                  efmn_rms(wl, SECS(0.01 + want * 2), SECS(0.01 + want * 2) + SECS(0.02))) / want;
    }
    for (d = 0; d <= 127; d++) {
        wd = fmax(wd, fabs(efmn_tau_d32(EFMN_DEC(d)) / (0.0646 * pow(2, (d - 64) / 13.5)) - 1));
        wi = fmax(wi, fabs(efmn_tau_d32(efmn_law(EFMN_I0, d - 64, 16696)) / (0.112 * pow(2, (d - 64) / 11.5)) - 1));
        wn = fmax(wn, fabs(efmn_tau_d32(efmn_law(286756u, d - 64, 10159)) / (0.0254 * pow(2, (d - 64) / 18.9)) - 1));
        wc = fmax(wc, fabs(efmn_tau_d32(efmn_step((uint32_t)(((uint64_t)cpefm_period(d) * 25756u) >> 24))) /
                           (0.393 * cpefm_period(d) / 65536.0 / FS) - 1));
    }
    printf("     DEC 32 / 64 / 96 heard: tau %.3f / %.3f / %.3f x the law; the steps vs the laws, 0..127: DEC %.2f %%, "
           "MDEC %.2f %%, NDEC %.2f %%, the claps %.2f %%\n", tau[0], tau[1], tau[2], 100 * wd, 100 * wi, 100 * wn,
           100 * wc);
    check("EFM noise voices: DEC, MDEC, NDEC and the claps' decay follow the measured laws (heard within 3 %, the steps "
          "within 0.5 %)", fabs(tau[0] - 1) < 0.03 && fabs(tau[1] - 1) < 0.03 && fabs(tau[2] - 1) < 0.03 && wd < 0.005 &&
          wi < 0.005 && wn < 0.005 && wc < 0.005);
}

/* CPEFM's claps: floor(CLPS / 8) restarts, one period apart (the measured table: 45 71 114 182 290 464 741 1185 1809
 * samples at CDEC 0, 16 .. 127); MOD 0 at +48 semitones (13 kHz: 3.4 samples a cycle, so a 4-sample peak is the
 * envelope) */
static void test_cpefm_claps(void)
{
    static const int CL[4] = {0, 16, 64, 127}, CD[3] = {0, 64, 127}, PT[3] = {45, 290, 1809};
    uint32_t i, j, ok = 1;
    for (i = 0; i < 4u; i++)
        for (j = 0; j < 3u; j++) {
            int16_t k[8] = {24, 64, 0, 0, 0, 64, 64, 0};
            uint32_t s, n = 0, first = 0, last = 0;
            k[2] = (int16_t)CL[i];
            k[3] = (int16_t)CD[j];
            efmn_hit(DM_CPEFM, k, 127, 24, wl, SECS(1));
            for (s = 8; s + 4 < SECS(1); s++) {         /* a restart: the next 4 samples' peak 4 x the last 4's */
                int32_t pa = 0, pb = 0;
                uint32_t b;
                for (b = 0; b < 4u; b++) {
                    pa = abs(wl[s - 4 + b]) > pa ? abs(wl[s - 4 + b]) : pa;
                    pb = abs(wl[s + b]) > pb ? abs(wl[s + b]) : pb;
                }
                if (pb > 4 * pa && pb > VOICE_FS / 64) {
                    if (!n)
                        first = s;
                    last = s;
                    n++;
                    s += 8;
                }
            }
            if (n != (uint32_t)CL[i] / 8u ||
                (n > 1 && fabs((last - first) / (double)(n - 1) / PT[j] - 1) > (n > 2 ? 0.01 : 0.08))) {
                printf("     CLPS %d CDEC %d: %u restarts, %.1f samples apart\n", CL[i], CD[j], n,
                       n > 1 ? (last - first) / (double)(n - 1) : 0.0);
                ok = 0;
            }
        }
    check("CPEFM: floor(CLPS / 8) claps, one measured period apart (CDEC 0 / 64 / 127: 45 / 290 / 1809 samples)", ok);
}

/* SDEFM's NOIS: linear (+6 dB from 32 to 64), random per hit; the FM noise the same on every hit (NOIS 0: two hits
 * identical); RSEFM / CPEFM: identical hits */
static void test_efmn_noise(void)
{
    static const int16_t N0[8] = {0, 64, 0, 64, 0, 64, 64, 0}, N32[8] = {0, 64, 32, 64, 0, 64, 64, 0},
                         N64[8] = {0, 64, 64, 64, 0, 64, 64, 0}, F[8] = {0, 64, 0, 64, 64, 64, 64, 0},
                         DC[8] = {0, 64, 64, 64, 64, 64, 64, 0}, DR[8] = {0, 64, 64, 0, 64, 64, 64, 64};
    static int32_t body[SECS(0.1)];
    static const uint32_t M[3] = {DM_SDEFM, DM_CPEFM, DM_RSEFM};
    double a, a0, b;
    uint32_t i, m, n = SECS(0.1), same = 1, differ = 0;
    efmn_hit(DM_SDEFM, N0, 127, 0, body, n);
    efmn_hit(DM_SDEFM, N32, 127, 0, wl, n);
    for (i = 0; i < n; i++)
        wl[i] -= body[i];
    a0 = efmn_rms(wl, 0, n);
    efmn_hit(DM_SDEFM, N64, 127, 0, w2, n);
    for (i = 0; i < n; i++)
        w2[i] -= body[i];
    b = efmn_rms(w2, 0, n);
    for (m = 0; m < 3u; m++) {                          /* two hits of one track: the second as a fresh one's */
        int16_t k[8];                                   /* (SD: NOIS 0; CP, RS: the defaults) */
        memcpy(k, m == 0u ? F : m == 1u ? DC : DR, sizeof k);
        host_init();
        drum_set_model(&trk[0], M[m]);
        for (i = 0; i < 8u; i++)
            trk[0].p[P_E0 + i] = k[i];
        drum_play(&trk[0], 127, 0);
        render_track(&trk[0], wl, SECS(0.5));
        drum_play(&trk[0], 127, 0);
        render_track(&trk[0], wl, n);
        efmn_hit(M[m], k, 127, 0, body, n);
        for (i = SECS(0.002); i < n; i++)               /* (the cut voice's declick aside) */
            same &= abs(wl[i] - body[i]) <= 1;
    }
    efmn_hit(DM_SDEFM, N64, 127, 0, wl, n);             /* SD's NOIS: another hit, other noise */
    host_init();
    drum_set_model(&trk[0], DM_SDEFM);
    drum_play(&trk[0], 127, 0);
    drum_play(&trk[0], 127, 0);
    render_track(&trk[0], body, n);
    for (i = 0; i < n; i++)
        differ |= abs(wl[i] - body[i]) > 4;
    {                                                   /* NOIS 127's rms over 5 ms re the law's (0.0562 at the hit) */
        static const int16_t N127[8] = {0, 64, 127, 64, 0, 64, 64, 0};
        double g, tn = efmn_tau_d32(efmn_law(286756u, 0, 10159)), T = 0.005;
        efmn_vs_ref(DM_SDEFM, ref_sd, N0, SECS(0.3), &g);   /* (units per prototype unit) */
        efmn_hit(DM_SDEFM, N0, 127, 0, body, n);
        efmn_hit(DM_SDEFM, N127, 127, 0, wl, n);
        for (i = 0; i < n; i++)
            wl[i] -= body[i];
        a = 20 * log10(efmn_rms(wl, 0, SECS(T)) / g / (0.0562 * sqrt((1 - exp(-2 * T / tn)) * tn / (2 * T))));
    }
    printf("     SDEFM NOIS 64 vs 32: %+.2f dB; NOIS 127 over the first 5 ms %+.2f dB re the law\n", 20 * log10(b / a0),
           a);
    check("EFM noise voices: SDEFM's NOIS linear (+6 +-0.3 dB from 32 to 64), 0.0562 x NOIS / 127 at the hit (+-1 dB), "
          "random per hit; the FM noise the same on every hit (SD with NOIS 0, CP, RS)",
          fabs(20 * log10(b / a0) - 6.02) < 0.3 && fabs(a) < 1 && same && differ);
}

/* the HPF knobs: SD / CP a one-pole pole 1 - HPF / 256, unity at Nyquist; RS a bilinear one-pole at the measured
 * corners (932 Hz at 32, 1991 at 64): the pure body's level against the filters' responses */
static void test_efmn_hpf(void)
{
    static const struct { uint32_t m; int16_t k0[8]; int hi; double f; } H[2] = {
        {DM_SDEFM, {0, 127, 0, 64, 0, 64, 64, 0}, 7, 290.0835}, {DM_RSEFM, {0, 127, 0, 0, 0, 64, 64, 0}, 3, 975.72}};
    static const int HV[2] = {32, 64};
    double worst = 0;
    uint32_t i, j;
    for (i = 0; i < 2u; i++) {
        double l0;
        efmn_hit(H[i].m, H[i].k0, 127, 0, wl, SECS(0.3));
        l0 = efmn_rms(wl, SECS(0.1), SECS(0.3));
        for (j = 0; j < 2u; j++) {
            int16_t k[8];
            double w = 2 * M_PI * H[i].f / FS, want, got;
            memcpy(k, H[i].k0, sizeof k);
            k[H[i].hi] = (int16_t)HV[j];
            efmn_hit(H[i].m, k, 127, 0, wl, SECS(0.3));
            got = efmn_rms(wl, SECS(0.1), SECS(0.3)) / l0;
            if (i == 0) {                               /* (1 + a) / 2 (1 - z^-1) / (1 - a z^-1) */
                double a = 1 - HV[j] / 256.0;
                want = (1 + a) / 2 * sqrt(2 - 2 * cos(w)) / sqrt(1 - 2 * a * cos(w) + a * a);
            } else {                                    /* tan(w / 2) / sqrt(tan^2 + g^2) */
                double g = tan(M_PI * (HV[j] == 32 ? 932 : 1991) / FS), t = tan(w / 2);
                want = t / sqrt(t * t + g * g);
            }
            worst = fmax(worst, fabs(20 * log10(got / want)));
        }
    }
    printf("     HPF 32 / 64 on the pure body: worst %.3f dB off the filters' responses\n", worst);
    check("EFM noise voices: HPF (SD / CP pole 1 - HPF / 256; RS the measured corners) within 0.1 dB", worst < 0.1);
}

/* RSEFM's noise FM: the rim's mean frequency rises with MOD (the +0.03 bias: 0.60 Hz per step at 976 Hz) */
static void test_rsefm_mod(void)
{
    static const int16_t M0[8] = {0, 127, 0, 0, 0, 64, 64, 0}, M64[8] = {0, 127, 64, 0, 0, 64, 64, 0};
    double f0, f1;
    efmn_hit(DM_RSEFM, M0, 127, 0, wl, SECS(0.6));
    f0 = efmn_zc(wl, SECS(0.02), SECS(0.6));
    efmn_hit(DM_RSEFM, M64, 127, 0, wl, SECS(0.6));
    f1 = efmn_zc(wl, SECS(0.02), SECS(0.6));
    printf("     RSEFM MOD 64: the rim's mean frequency %+.1f Hz (the law +38.6)\n", f1 - f0);
    check("RSEFM: MOD raises the rim's mean frequency 0.60 Hz per step (+38.6 +-4 Hz at 64)", fabs(f1 - f0 - 38.6) < 4);
}

/* the levels at the defaults within +-3 dB of S909's, C909's, RIM's (RMS over 0.3 s) */
static void test_efmn_level(void)
{
    static const int16_t DEF[8] = {-99, -99, -99, -99, -99, -99, -99, -99};
    static const struct { uint32_t m, r; } L[3] = {{DM_SDEFM, DM_S909}, {DM_CPEFM, DM_C909}, {DM_RSEFM, DM_RIM}};
    double d[3];
    uint32_t i;
    for (i = 0; i < 3u; i++) {
        efmn_hit(L[i].m, DEF, 127, 0, wl, SECS(0.3));
        d[i] = efmn_rms(wl, 0, SECS(0.3));
        efmn_hit(L[i].r, DEF, 127, 0, wl, SECS(0.3));
        d[i] = 20 * log10(d[i] / efmn_rms(wl, 0, SECS(0.3)));
    }
    printf("     at the defaults: SDEFM vs S909 %+.1f dB, CPEFM vs C909 %+.1f dB, RSEFM vs RIM %+.1f dB\n", d[0], d[1],
           d[2]);
    check("EFM noise voices: the defaults' levels within +-3 dB of S909's, C909's, RIM's", fabs(d[0]) <= 3 &&
          fabs(d[1]) <= 3 && fabs(d[2]) <= 3);
}

/* the extremes: every knob at 0 / 127 (TUNE -24 / +24) and a step's note +-24, velocity 1 and 127: bounded, the voice
 * ends within 6 s; and TUNE -24..24 at notes +-24 with every knob at 127 (pitch to +48 semitones, deviation past
 * Nyquist): bounded (UBSan: no signed overflow) */
static void test_efmn_extremes(void)
{
    static const uint32_t M[3] = {DM_SDEFM, DM_CPEFM, DM_RSEFM};
    uint32_t m, c, i, ok = 1, ended = 1;
    int32_t peak = 0;
    for (m = 0; m < 3u; m++) {
        for (c = 0; c < 512u; c++) {                    /* (bits 0..7 the knobs, bit 8 the note) */
            int16_t k[8] = {0};
            uint32_t f;
            for (i = 0; i < 8u; i++)
                k[i] = (int16_t)((c >> i & 1u) ? (i ? 127 : 24) : (i ? 0 : -24));
            efmn_hit(M[m], k, c % 3u ? 127 : 1, (c >> 8 & 1u) ? 24 : -24, wl, CTL);
            for (f = CTL; f < SECS(6) && trk[0].v[0].active; f += SECS(0.1)) {   /* (until it ends) */
                render_track(&trk[0], wl, SECS(0.1));
                for (i = 0; i < SECS(0.1); i++) {
                    ok &= abs(wl[i]) <= 4 * VOICE_FS;
                    peak = abs(wl[i]) > peak ? abs(wl[i]) : peak;
                }
            }
            ended &= !trk[0].v[0].active;
        }
        for (c = 0; c < 2u * 49u; c++) {
            int16_t k[8] = {0, 127, 127, 127, 127, 127, 127, 127};
            k[0] = (int16_t)((int)(c % 49u) - 24);
            efmn_hit(M[m], k, 127, c < 49u ? 24 : -24, wl, SECS(0.3));
            for (i = 0; i < SECS(0.3); i++) {
                ok &= abs(wl[i]) <= 4 * VOICE_FS;
                peak = abs(wl[i]) > peak ? abs(wl[i]) : peak;
            }
        }
    }
    printf("     extremes: peak %.2f x VOICE_FS\n", peak / (double)VOICE_FS);
    check("EFM noise voices: every knob at its ends, notes +-24, velocity 1 / 127, TUNE -24..24 at notes +-24: "
          "bounded, the voice ends within 6 s", ok && ended);
}

/* the longest voice (DECAY 127: tau 1.6 s, the law) is cut by the lifetime's fall past 5.5 s: ended by 6 s, no step */
static void test_efmn_life(void)
{
    static const uint32_t M[3] = {DM_SDEFM, DM_CPEFM, DM_RSEFM};
    static const int16_t K[3][8] = {{0, 127, 0, 64, 0, 64, 127, 0}, {0, 127, 0, 64, 0, 64, 127, 0},
                                    {0, 127, 0, 0, 127, 64, 127, 0}};
    uint32_t m, i, ok = 1;
    double before = 0, l55 = 0;
    for (m = 0; m < 3u; m++) {
        int32_t step = 0;
        efmn_hit(M[m], K[m], 127, 0, wl, SECS(6));
        for (i = SECS(5.4); i < SECS(6); i++)
            step = abs(wl[i] - wl[i - 1]) > step ? abs(wl[i] - wl[i - 1]) : step;
        before = efmn_rms(wl, SECS(5.3), SECS(5.5));
        l55 = 20 * log10(efmn_rms(wl, SECS(5.3), SECS(5.5)) / efmn_rms(wl, SECS(0.1), SECS(0.3)));
        ok &= !trk[0].v[0].active && step < 2 * before + 4 && efmn_rms(wl, SECS(5.95), SECS(6)) < 1;
    }
    printf("     DECAY 127: %.1f dB at 5.4 s re 0.2 s (tau 1.6 s), then the lifetime's fall\n", l55);
    check("EFM noise voices: DECAY 127 keeps its 1.6 s law to 5.5 s, then fades out smoothly, ended by 6 s", ok);
}

/* re-hits 5 ms apart and a switch to each model while another rings: bounded */
static void test_efmn_rehits(void)
{
    static const uint32_t M[3] = {DM_SDEFM, DM_CPEFM, DM_RSEFM};
    static const int16_t K[8] = {24, 127, 127, 127, 127, 127, 127, 127};
    uint32_t m, i, ok = 1;
    for (m = 0; m < 3u; m++) {
        efmn_hit(M[m], K, 127, 24, wl, SECS(0.005));
        for (i = 0; i < 4u; i++) {
            drum_play(&trk[0], 127, 24);
            render_track(&trk[0], wl, SECS(0.005));
        }
        render_track(&trk[0], wl, SECS(0.3));
        for (i = 0; i < SECS(0.3); i++)
            ok &= abs(wl[i]) <= 4 * VOICE_FS;
        host_init();
        drum_set_model(&trk[0], DM_CYMB);
        drum_play(&trk[0], 127, 0);
        render_track(&trk[0], wl, SECS(0.05));
        drum_set_model(&trk[0], M[m]);
        drum_play(&trk[0], 127, 0);
        render_track(&trk[0], wl, SECS(0.3));
        for (i = 0; i < SECS(0.3); i++)
            ok &= abs(wl[i]) <= 4 * VOICE_FS;
    }
    check("EFM noise voices: re-hits 5 ms apart and a model switch while ringing: bounded", ok);
}

/* the A/B set: the md-re renders' names and knobs (the machine's PTCH 64 = TUNE 0), in the prototypes' units */
static void efmn_wav(const char *dir, const char *name, uint32_t mi, const int16_t *k, double g)
{
    char path[512];
    FILE *f;
    uint32_t v, i, n = SECS(1.0);
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    f = fopen(path, "wb");
    if (!f)
        return;
    efmn_hit(mi, k, 127, 0, wl, n);
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
    static const struct { uint32_t m; efmn_ref_fn rf; const char *pre, *kn[8]; int16_t k0[8]; } M[3] = {
        {DM_SDEFM, ref_sd, "sdefm", {0, "dec", "nois", "ndec", "mod", "mfrq", "mdec", "hpf"}, {0, 64, 64, 64, 64, 64, 64, 0}},
        {DM_CPEFM, ref_cp, "cpefm", {0, "dec", "clps", "cdec", "mod", "mfrq", "mdec", "hpf"}, {0, 64, 64, 64, 64, 64, 64, 0}},
        {DM_RSEFM, ref_rs, "rsefm", {0, "dec", "mod", "hpf", "snar", "sptc", "sdec", "smod"}, {0, 64, 64, 0, 64, 64, 64, 64}}};
    static const int V[4] = {0, 32, 96, 127};
    uint32_t m, j, w = 0;
    for (m = 0; m < 3u; m++) {
        double g;
        char name[64];
        efmn_vs_ref(M[m].m, M[m].rf, M[m].k0, SECS(0.3), &g);   /* the firmware's units per prototype unit */
        snprintf(name, sizeof name, "%s_neutral", M[m].pre);
        efmn_wav(dir, name, M[m].m, M[m].k0, g);
        w++;
        for (j = 1; j < 8u; j++) {
            uint32_t l;
            for (l = 0; l < 4u; l++) {
                int16_t k[8];
                memcpy(k, M[m].k0, sizeof k);
                k[j] = (int16_t)V[l];
                snprintf(name, sizeof name, "%s_%s_%03d", M[m].pre, M[m].kn[j], V[l]);
                efmn_wav(dir, name, M[m].m, k, g);
                w++;
            }
        }
    }
    printf("     wrote %u WAVs to %s\n", w, dir);
}

int main(int argc, char **argv)
{
    test_efmn_vs_ref();
    test_efmn_pitch();
    test_efmn_decays();
    test_cpefm_claps();
    test_efmn_noise();
    test_efmn_hpf();
    test_rsefm_mod();
    test_efmn_level();
    test_efmn_extremes();
    test_efmn_life();
    test_efmn_rehits();
    if (argc > 1)
        write_wavs(argv[1]);
    printf(fails ? "efmn_test: %d FAILED\n" : "efmn_test: all passed\n", fails);
    return fails != 0;
}
