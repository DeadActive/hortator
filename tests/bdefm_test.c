/* BDEFM, XTEFM (dm_bdefm.c): the fixed-point voice against its float twin (the md-re prototypes efm-bd.c / efm-xt.c,
 * copied below, fed the firmware's rounded values), the measured laws, the levels, the extremes. The chaotic cases
 * (BDEFM's MFB from 48, XTEFM's FM stand-in) cannot follow a trajectory: there the spectra and levels are compared.
 * Each test prints one line; any failure exits 1.
 *   bdefm_test [OUTDIR]   OUTDIR: also writes the A/B set as float WAVs in the prototypes' units (md-re renders' names) */
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

static double bdefm_tau_d32(uint32_t d) { return -1 / (FS * log(1 - d / 4294967296.0)); }   /* a Q32 step's tau, s */

/* track 0 = model mi with knobs k (-99: the old default), one hit at vel, frames rendered into wl; the voice's state right
 * after the trigger in st / st_ph */
static bdefm_t st;
static uint32_t st_ph[2];
static void bdefm_hit(uint32_t mi, const int16_t *k, uint32_t vel, uint32_t frames)
{
    uint32_t i;
    host_init();
    old_set_model(&trk[0], mi);
    for (i = 0; i < 8u; i++)
        if (k[i] != -99)
            trk[0].p[P_E0 + i] = k[i];
    drum_play(&trk[0], vel, trk[0].nt);   /* (drum_hit plays note 0) */
    st = trk[0].v[0].ms.bdefm;
    st_ph[0] = trk[0].v[0].ph[0];
    st_ph[1] = trk[0].v[0].ph[1];
    if (frames)
        render_track(&trk[0], wl, frames);
}

/* the prototypes (md-re proto/efm-bd.c, efm-xt.c) in float, with the voice's own rounded values: the pitch from its
 * increment (Hortator's pitch table: TUNE's accuracy is test_bdefm_laws'), the decays from their Q32 steps, the
 * deviation, the feedback and the start phases as the trigger set them. The laws themselves: test_bdefm_laws. */
static void bdefm_twin(double *y, uint32_t n)
{
    double f0 = st.inc0 * (FS / 4294967296.0), R = (st.rk + 16) / 16.0, Q = st.mf / 16.0, I0 = st.i0 / 134217728.0;
    double B = M_PI * st.fb / 64, kr = 1 - st.d_r / 4294967296.0, ki = 1 - st.d_i / 4294967296.0;
    double ka = 1 - st.d_a / 4294967296.0;
    double pc = st_ph[0] * (2 * M_PI / 4294967296.0), pm = (int32_t)st_ph[1] * (2 * M_PI / 4294967296.0), m = 0;
    double ah = exp(-2 * M_PI * 121 / FS), hy = 0, hx = 0;
    double w = 2 * M_PI * 21.3 / FS, al = sin(w) / (2 * 0.68), a0 = 1 + al;
    double b0 = (1 + cos(w)) / 2 / a0, b1 = -(1 + cos(w)) / a0, a1 = -2 * cos(w) / a0, a2 = (1 - al) / a0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0, er = R - 1, ei = I0, ea = 1;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double fc = f0 * (1 + er), yy, o;
        if (I0 > 0)
            m = cos(pm + B * m);
        yy = ea * sin(pc);
        pc += 2 * M_PI * fc * (1 + ei * m) / FS;
        pm += 2 * M_PI * fc * Q / FS;
        if (pc > 1e4) pc = fmod(pc, 2 * M_PI);
        if (pm > 1e4) pm = fmod(pm, 2 * M_PI);
        er *= kr; ei *= ki; ea *= ka;
        if (st.xt) {                                    /* the voice's 121 Hz high-pass */
            hy = ah * (hy + yy - hx);
            hx = yy;
            yy = hy;
        }
        o = b0 * yy + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2;   /* the track's 21.3 Hz high-pass */
        x2 = x1; x1 = yy; y2 = y1; y1 = o;
        y[i] = o;
    }
}

/* the firmware against the twin over [0, n): the least-squares gain and the residual (dB re the twin) */
static double bdefm_vs_twin(uint32_t mi, const int16_t *k, uint32_t n, double *gain)
{
    double xy = 0, yy = 0, e = 0, r = 1e-30;
    uint32_t i;
    bdefm_hit(mi, k, 127, n);
    bdefm_twin(ref, n);
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

static void test_bdefm_vs_twin(uint32_t mi, const char *name, const int16_t (*K)[8], uint32_t nk, double *g0out)
{
    double worst = -999, g, g0 = 0, gw = 1;
    uint32_t i;
    char what[200];
    for (i = 0; i < nk; i++) {
        double e = bdefm_vs_twin(mi, K[i], SECS(0.3), &g);
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
    printf("     %s vs the float twin (%u settings, 0.3 s): worst residual %.1f dB, gain %.0f / unit (spread %.2f %%)\n",
           name, nk, worst, g0, 100 * (gw - 1));
    snprintf(what, sizeof what, "%s: every non-chaotic setting matches the float twin (residual < -30 dB, one gain "
             "within 1 %%)", name);
    check(what, worst < -30 && fabs(gw - 1) < 0.01);
    *g0out = g0;
}

/* ---- spectra: third-octave bands (Hann frames of 4096, hop 2048), 63 Hz .. 16 kHz */
#define BDEFM_NFFT 4096
static void bdefm_fft(double *re, double *im, uint32_t n)
{
    uint32_t i, j, len;
    for (i = 1, j = 0; i < n; i++) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        double a = -2 * M_PI / len;
        for (i = 0; i < n; i += len)
            for (j = 0; j < len / 2; j++) {
                double wr = cos(a * j), wi = sin(a * j);
                double xr = re[i + j + len / 2] * wr - im[i + j + len / 2] * wi;
                double xi = re[i + j + len / 2] * wi + im[i + j + len / 2] * wr;
                re[i + j + len / 2] = re[i + j] - xr;
                im[i + j + len / 2] = im[i + j] - xi;
                re[i + j] += xr;
                im[i + j] += xi;
            }
    }
}
#define BDEFM_NB 25
static void bdefm_bands(const double *x, uint32_t n, double *db)
{
    static double re[BDEFM_NFFT], im[BDEFM_NFFT];
    double e[BDEFM_NB] = {0};
    uint32_t s, i, b;
    for (s = 0; s + BDEFM_NFFT <= n; s += BDEFM_NFFT / 2) {
        for (i = 0; i < BDEFM_NFFT; i++) {
            re[i] = x[s + i] * (0.5 - 0.5 * cos(2 * M_PI * i / BDEFM_NFFT));
            im[i] = 0;
        }
        bdefm_fft(re, im, BDEFM_NFFT);
        for (i = 1; i < BDEFM_NFFT / 2; i++) {
            double f = i * (double)FS / BDEFM_NFFT;
            int k = (int)floor(3 * log2(f / 63.0) + 0.5);
            if (k >= 0 && k < BDEFM_NB)
                e[k] += re[i] * re[i] + im[i] * im[i];
        }
    }
    for (b = 0; b < BDEFM_NB; b++)
        db[b] = 10 * log10(e[b] + 1e-20);
}
/* the spectral distance of the firmware (wl, gain g per twin unit) to the twin (ref) over n: the mean |dB| over the
 * bands within 50 dB of the loudest, and the level difference (RMS, dB) */
static double bdefm_specdist(uint32_t n, double g, double *lev)
{
    static double a[SECS(1)];
    double da[BDEFM_NB], db[BDEFM_NB], top = -999, sum = 0, ea = 0, eb = 0;
    uint32_t i, b, nb = 0;
    for (i = 0; i < n; i++) {
        a[i] = wl[i] / g;
        ea += a[i] * a[i];
        eb += ref[i] * ref[i];
    }
    bdefm_bands(a, n, da);
    bdefm_bands(ref, n, db);
    for (b = 0; b < BDEFM_NB; b++)
        top = fmax(top, db[b]);
    for (b = 0; b < BDEFM_NB; b++)
        if (db[b] > top - 50) {
            sum += fabs(da[b] - db[b]);
            nb++;
        }
    *lev = 10 * log10(ea / eb);
    return sum / nb;
}

/* the chaotic cases: the firmware's third-octave spectrum and level vs the twin's (the same statistics) */
static void test_bdefm_chaos(uint32_t mi, const char *name, const int16_t (*K)[8], uint32_t nk, double g0)
{
    double worst = 0, wlev = 0, d, lev, g;
    uint32_t i;
    char what[200];
    for (i = 0; i < nk; i++) {
        bdefm_vs_twin(mi, K[i], SECS(0.5), &g);
        d = bdefm_specdist(SECS(0.5), g0, &lev);
        printf("     %s %d %d %d %d %d %d %d %d: third-octave distance %.2f dB, level %+.2f dB\n", name, K[i][0], K[i][1],
               K[i][2], K[i][3], K[i][4], K[i][5], K[i][6], K[i][7], d, lev);
        worst = fmax(worst, d);
        wlev = fmax(wlev, fabs(lev));
    }
    snprintf(what, sizeof what, "%s: the chaotic settings' spectra as the twin's (third-octave mean < 2 dB, level "
             "within 1.5 dB)", name);
    check(what, worst < 2 && wlev < 1.5);
}

static double bdefm_zc(const int32_t *x, uint32_t a, uint32_t b)   /* rising zero crossings / time */
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
static double bdefm_rms(const int32_t *x, uint32_t a, uint32_t b)
{
    double s = 0;
    uint32_t i;
    for (i = a; i < b; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (b - a));
}
static double bdefm_goertzel(const int32_t *x, uint32_t n, double f)
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

/* the measured laws: the pitch (TUNE 0 = PTCH 64, +-12 an octave), DEC / RDEC / MDEC (their factors over the knob),
 * RAMP's start pitch, MOD / MFRQ as true FM (Bessel sidebands), MFB nothing at MOD 0, CLIC the start phase */
static void test_bdefm_laws(void)
{
    static const double F0[2] = {102.56, 86.133};
    static const char *const NM[2] = {"BDEFM", "XTEFM"};
    uint32_t mc, i;
    for (mc = 0; mc < 2u; mc++) {
        uint32_t mi = mc ? DM_XTEFM : DM_BDEFM;
        int16_t k[8] = {0, 110, 16, 64, 0, 64, 64, 0};
        double f[3], wd = 0, wr = 0, wi = 0, tau[3];
        int d;
        char what[200];
        for (i = 0; i < 3u; i++) {
            k[0] = (int16_t)(12 * ((int)i - 1));
            bdefm_hit(mi, k, 127, SECS(0.8));
            f[i] = bdefm_zc(wl, SECS(0.2), SECS(0.8));
        }
        printf("     %s TUNE -12 / 0 / +12: %.2f / %.2f / %.2f Hz\n", NM[mc], f[0], f[1], f[2]);
        snprintf(what, sizeof what, "%s: TUNE 0 is %.2f Hz (the machine's PTCH 64, +-0.5 %%), +-12 an octave", NM[mc],
                 F0[mc]);
        check(what, fabs(f[1] / F0[mc] - 1) < 0.005 && fabs(f[2] / f[1] / 2 - 1) < 0.005 && fabs(f[1] / f[0] / 2 - 1) < 0.005);
        for (d = 0; d <= 127; d++) {
            double la = fmin(0.58, 0.0646 * pow(2, (d - 64) / 13.5)), lr = 0.0818 * pow(2, (d - 64) / 12.67);
            double li = 0.112 * pow(2, (d - 64) / 11.5);
            int16_t kk[8] = {0, 64, 16, 64, 64, 64, 64, 0};
            kk[1] = kk[3] = kk[6] = (int16_t)d;
            bdefm_hit(mi, kk, 127, 0);
            wd = fmax(wd, fabs(bdefm_tau_d32(st.d_a) / la - 1));
            wr = fmax(wr, fabs(bdefm_tau_d32(st.d_r) / lr - 1));
            wi = fmax(wi, fabs(bdefm_tau_d32(st.d_i) / li - 1));
        }
        printf("     %s DEC / RDEC / MDEC factors vs the laws, 0..127: worst %.2f %% / %.2f %% / %.2f %%\n", NM[mc],
               100 * wd, 100 * wr, 100 * wi);
        snprintf(what, sizeof what, "%s: DEC (to its 0.58 s end), RDEC, MDEC follow the measured laws within 1 %% over "
                 "the knob", NM[mc]);
        check(what, wd < 0.01 && wr < 0.01 && wi < 0.01);
        for (i = 0; i < 3u; i++) {                      /* DEC heard */
            static const int D[3] = {40, 64, 96};
            int16_t kk[8] = {12, 64, 16, 64, 0, 64, 64, 0};
            double want = 0.0646 * pow(2, (D[i] - 64) / 13.5), a, b;
            kk[1] = (int16_t)D[i];
            bdefm_hit(mi, kk, 127, SECS(1.5));
            a = bdefm_rms(wl, SECS(0.02), SECS(0.02) + SECS(0.02));
            b = bdefm_rms(wl, SECS(0.02 + want * 2), SECS(0.02 + want * 2) + SECS(0.02));
            tau[i] = want * 2 / log(a / b) / want;
        }
        printf("     %s DEC 40 / 64 / 96 heard: tau %.3f / %.3f / %.3f x the law\n", NM[mc], tau[0], tau[1], tau[2]);
        snprintf(what, sizeof what, "%s: DEC heard follows tau = 64.6 ms x 2 ^ ((d - 64) / 13.5) within 5 %%", NM[mc]);
        check(what, fabs(tau[0] - 1) < 0.05 && fabs(tau[1] - 1) < 0.05 && fabs(tau[2] - 1) < 0.05);
        {                                               /* RAMP: the start pitch f0 x RAMP / 16, back to f0 */
            int16_t kk[8] = {12, 127, 64, 127, 0, 64, 64, 0};
            double a, b, wa, wb;
            bdefm_hit(mi, kk, 127, SECS(1.5));
            a = bdefm_zc(wl, 0, SECS(0.02));
            wa = 2 * F0[mc] * (1 + 3 * exp(-0.01 / bdefm_tau_d32(st.d_r)));
            kk[2] = 32;
            kk[3] = 0;                                  /* RAMP 32 over RDEC 0 (2.5 ms): f0 from ~25 ms on */
            bdefm_hit(mi, kk, 127, SECS(0.3));
            b = bdefm_zc(wl, SECS(0.05), SECS(0.3));
            wb = 2 * F0[mc];
            printf("     %s RAMP 64 / RDEC 127, 0-20 ms: %.1f Hz (law %.1f); RAMP 32 / RDEC 0 after 50 ms: %.2f Hz "
                   "(f0 %.2f)\n", NM[mc], a, wa, b, wb);
            snprintf(what, sizeof what, "%s: RAMP starts at f0 x RAMP / 16 (within 2 %%) and returns to f0 (0.5 %%)",
                     NM[mc]);
            check(what, fabs(a / wa - 1) < 0.02 && fabs(b / wb - 1) < 0.005);
        }
        {                                               /* MOD 0: MFB (CLIC on XTEFM aside) changes nothing */
            int16_t kk[8] = {0, 64, 0, 64, 0, 40, 64, 0};
            static int32_t a[SECS(0.3)];
            int same;
            bdefm_hit(mi, kk, 127, SECS(0.3));
            memcpy(a, wl, sizeof a);
            kk[5] = 127;
            kk[6] = 0;
            if (!mc)
                kk[7] = 127;
            bdefm_hit(mi, kk, 127, SECS(0.3));
            same = !memcmp(a, wl, sizeof a);
            snprintf(what, sizeof what, "%s: MOD 0 is a pure carrier (MFRQ, MDEC%s change nothing)", NM[mc],
                     mc ? "" : ", MFB");
            check(what, same);
        }
    }
    {                                                   /* BDEFM: true FM, the PM index MOD / MFRQ: Bessel sidebands */
        int16_t kk[8] = {0, 127, 16, 64, 32, 40, 127, 0};
        double c, s1, s2, w1 = 20 * log10(0.36884 / 0.84629), w2 = 20 * log10(0.07582 / 0.84629);
        bdefm_hit(DM_BDEFM, kk, 127, SECS(0.25));
        c = bdefm_goertzel(wl + SECS(0.05), SECS(0.2), 102.56);
        s1 = 20 * log10(bdefm_goertzel(wl + SECS(0.05), SECS(0.2), 102.56 * 3.5) / c);
        s2 = 20 * log10(bdefm_goertzel(wl + SECS(0.05), SECS(0.2), 102.56 * 6) / c);
        printf("     BDEFM MOD 32 MFRQ 40 (index 0.8): fc + fm %.1f dB (J1/J0 %.1f), fc + 2 fm %.1f dB (J2/J0 %.1f)\n", s1,
               w1, s2, w2);
        check("BDEFM: MOD / MFRQ are true FM (sidebands as Bessel's J1 / J2 at index MOD / MFRQ, within 1 dB)",
              fabs(s1 - w1) < 1 && fabs(s2 - w2) < 1);
    }
    {                                                   /* XTEFM: CLIC 127 starts at the trough, 0 at zero */
        int16_t kk[8] = {12, 64, 16, 64, 0, 64, 64, 127};
        double pk, z;
        bdefm_hit(DM_XTEFM, kk, 127, SECS(0.05));
        z = wl[0];
        for (i = 0; i < SECS(0.05); i++)
            z = fmin(z, wl[i]);
        pk = wl[0] / z;
        printf("     XTEFM CLIC 127: the first sample %.3f x the lowest\n", pk);
        kk[7] = 0;
        bdefm_hit(DM_XTEFM, kk, 127, SECS(0.05));
        printf("     XTEFM CLIC 0: the first sample %d (the lowest at 127: %.0f)\n", wl[0], z);
        check("XTEFM: CLIC 127 starts at its trough (the first sample the lowest, within 5 %), CLIC 0 at zero",
              pk > 0.95 && abs(wl[0]) < 64);
    }
}

/* the levels at the defaults: BDEFM vs K909, XTEFM vs TOM, within +-3 dB */
static void test_bdefm_level(void)
{
    static const int16_t DEF[8] = {-99, -99, -99, -99, -99, -99, -99, -99};
    static const uint32_t MI[2] = {DM_BDEFM, DM_XTEFM}, RF[2] = {DM_K909, DM_TOM};
    uint32_t j;
    int ok = 1;
    for (j = 0; j < 2u; j++) {
        double a, b;
        bdefm_hit(MI[j], DEF, 127, SECS(0.3));
        a = bdefm_rms(wl, 0, SECS(0.3));
        host_init();
        old_set_model(&trk[0], RF[j]);
        drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
        render_track(&trk[0], wl, SECS(0.3));
        b = bdefm_rms(wl, 0, SECS(0.3));
        printf("     %s vs %s at the defaults: %+.2f dB\n", N_MODEL[MI[j]], N_MODEL[RF[j]], 20 * log10(a / b));
        ok &= fabs(20 * log10(a / b)) <= 3;
    }
    check("BDEFM, XTEFM: the levels at the defaults within +-3 dB of K909's / TOM's", ok);
}

/* the extremes: every knob at 0 / 127 (TUNE -24 / +24) and a step's note +-24, velocity 1 and 127: bounded, ends;
 * then TUNE -24..24 at notes +-24 with the deepest FM (up to +48 semitones: past Nyquist) */
static void test_bdefm_extremes(void)
{
    static const uint32_t MI[2] = {DM_BDEFM, DM_XTEFM};
    uint32_t j, c, i, ok = 1, ended = 1;
    int tu, nt;
    for (j = 0; j < 2u; j++)
        for (c = 0; c < 512u; c++) {
            int16_t k[8];
            for (i = 0; i < 8u; i++)
                k[i] = (int16_t)((c >> i & 1u) ? (i ? 127 : 24) : (i ? 0 : -24));
            host_init();
            old_set_model(&trk[0], MI[j]);
            for (i = 0; i < 8u; i++)
                trk[0].p[P_E0 + i] = k[i];
            trk[0].nt = (c >> 8 & 1u) ? 24 : -24;
            drum_play(&trk[0], (c & 3u) == 3u ? 1 : 127, trk[0].nt);   /* (drum_hit plays note 0) */
            render_track(&trk[0], wl, SECS(6));
            for (i = 0; i < SECS(6); i++)
                ok &= abs(wl[i]) <= 4 * VOICE_FS;
            ended &= !trk[0].v[0].active;
        }
    for (j = 0; j < 2u; j++)
        for (tu = -24; tu <= 24; tu += 4)
            for (nt = -24; nt <= 24; nt += 48) {
                static const int16_t K[8] = {0, 30, 127, 127, 127, 127, 127, 127};
                host_init();
                old_set_model(&trk[0], MI[j]);
                for (i = 0; i < 8u; i++)
                    trk[0].p[P_E0 + i] = K[i];
                trk[0].p[P_E0] = (int16_t)tu;
                drum_play(&trk[0], 127, nt);
                render_track(&trk[0], wl, SECS(1));
                for (i = 0; i < SECS(1); i++)
                    ok &= abs(wl[i]) <= 4 * VOICE_FS;
            }
    check("BDEFM, XTEFM: every knob at its ends, notes +-24, velocity 1 / 127, TUNE -24..24 at notes +-24 with the "
          "deepest FM: bounded, the voice ends within 6 s", ok && ended);
}

/* re-hits 5 ms apart and a switch to the model while another one rings: bounded */
static void test_bdefm_rehits(void)
{
    static const int16_t K[8] = {0, 127, 127, 127, 127, 127, 127, 127};
    static const uint32_t MI[2] = {DM_BDEFM, DM_XTEFM};
    uint32_t j, i, ok = 1;
    for (j = 0; j < 2u; j++) {
        bdefm_hit(MI[j], K, 127, SECS(0.005));
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
        old_set_model(&trk[0], MI[j]);
        drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
        render_track(&trk[0], wl, SECS(0.3));
        for (i = 0; i < SECS(0.3); i++)
            ok &= abs(wl[i]) <= 4 * VOICE_FS;
    }
    check("BDEFM, XTEFM: re-hits 5 ms apart and a model switch while ringing: bounded", ok);
}

/* the A/B set: the md-re renders' names and knobs (BDEFM: PTCH 0 / 64 / 127 = TUNE -24 / 0 / +24; XTEFM: PTCH 0 /
 * 127 = TUNE -12 / +12); the neutral patch 64 64 0 64 64 64 64 0 = the defaults */
static void bdefm_write_wavs(const char *dir, double gb, double gx)
{
    static const struct { const char *name; uint32_t xt; int16_t k[8]; } W[] = {
        {"efm-bd_neutral", 0, {0, 64, 0, 64, 64, 64, 64, 0}},   {"efm-bd_ptch_000", 0, {-24, 64, 0, 64, 64, 64, 64, 0}},
        {"efm-bd_ptch_127", 0, {24, 64, 0, 64, 64, 64, 64, 0}}, {"efm-bd_ramp_016", 0, {0, 64, 16, 64, 64, 64, 64, 0}},
        {"efm-bd_ramp_127", 0, {0, 64, 127, 64, 64, 64, 64, 0}}, {"efm-bd_dec_096", 0, {0, 96, 0, 64, 64, 64, 64, 0}},
        {"efm-bd_mod_000", 0, {0, 64, 0, 64, 0, 64, 64, 0}},    {"efm-bd_mod_127", 0, {0, 64, 0, 64, 127, 64, 64, 0}},
        {"efm-bd_mfrq_016", 0, {0, 64, 0, 64, 64, 16, 64, 0}},  {"efm-bd_mfb_032", 0, {0, 64, 0, 64, 64, 64, 64, 32}},
        {"efm-bd_mfb_096", 0, {0, 64, 0, 64, 64, 64, 64, 96}},  {"efm-xt_neutral", 1, {0, 64, 0, 64, 64, 64, 64, 0}},
        {"efm-xt_mod_000", 1, {0, 64, 0, 64, 0, 64, 64, 0}},    {"efm-xt_ptch_000", 1, {-12, 64, 0, 64, 64, 64, 64, 0}},
        {"efm-xt_ptch_127", 1, {12, 64, 0, 64, 64, 64, 64, 0}}, {"efm-xt_ramp_064", 1, {0, 64, 64, 64, 64, 64, 64, 0}},
        {"efm-xt_clic_127", 1, {0, 64, 0, 64, 64, 64, 64, 127}}, {"efm-xt_mod_127", 1, {0, 64, 0, 64, 127, 64, 64, 0}},
        {"efm-xt_dec_096", 1, {0, 96, 0, 64, 64, 64, 64, 0}}};
    uint32_t w, i, n = SECS(2.0);
    for (w = 0; w < sizeof W / sizeof W[0]; w++) {
        char path[512];
        FILE *f;
        uint32_t v;
        double g = W[w].xt ? gx / 0.166 : gb / 0.203;    /* (the prototypes' levels) */
        snprintf(path, sizeof path, "%s/%s.wav", dir, W[w].name);
        f = fopen(path, "wb");
        if (!f)
            continue;
        bdefm_hit(W[w].xt ? DM_XTEFM : DM_BDEFM, W[w].k, 127, n);
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
    /* TUNE DECAY RAMP RDEC MOD MFRQ MDEC MFB / CLIC: MOD 0, the clean FM (MFB up to 16, or 32 with a fast modulator:
     * past B = 1 rad the feedback map amplifies the last bits, at a slow one it bifurcates: the chaotic list), each
     * knob's ends (the sub-audio RAMP 0 starts audible: at DEC 0 or RDEC 127 XTEFM is ~-50 dB, its output's LSBs) */
    static const int16_t KB[][8] = {
        {0, 64, 0, 64, 0, 64, 64, 0},     {0, 64, 16, 64, 0, 64, 64, 0},   {-24, 64, 0, 64, 0, 64, 64, 0},
        {24, 64, 0, 64, 0, 64, 64, 0},    {0, 0, 0, 64, 0, 64, 64, 0},     {0, 110, 0, 64, 0, 64, 64, 0},
        {0, 64, 127, 64, 0, 64, 64, 0},   {0, 64, 127, 0, 0, 64, 64, 0},   {0, 64, 0, 127, 0, 64, 64, 0},
        {0, 64, 0, 64, 64, 64, 64, 0},    {0, 64, 0, 64, 127, 64, 64, 0},  {0, 64, 0, 64, 32, 64, 64, 0},
        {0, 64, 0, 64, 64, 0, 64, 0},     {0, 64, 0, 64, 64, 16, 64, 0},   {0, 64, 0, 64, 64, 127, 64, 0},
        {0, 64, 0, 64, 64, 64, 0, 0},     {0, 64, 0, 64, 64, 64, 127, 0},  {0, 64, 0, 64, 64, 64, 64, 16},
        {0, 64, 0, 64, 64, 64, 64, 32},   {0, 96, 32, 48, 32, 40, 96, 16}, {-12, 80, 64, 32, 96, 24, 48, 12},
        {12, 64, 0, 64, 127, 127, 32, 0}};
    static const int16_t KX[][8] = {
        {0, 64, 0, 64, 0, 64, 64, 0},     {0, 64, 16, 64, 0, 64, 64, 0},   {-12, 64, 0, 64, 0, 64, 64, 0},
        {12, 64, 0, 64, 0, 64, 64, 0},    {-24, 96, 16, 64, 0, 64, 64, 0}, {24, 64, 0, 64, 0, 64, 64, 0},
        {0, 0, 64, 64, 0, 64, 64, 0},     {0, 110, 16, 64, 0, 64, 64, 0},  {0, 64, 127, 64, 0, 64, 64, 0},
        {0, 64, 64, 0, 0, 64, 64, 0},     {0, 64, 127, 127, 0, 64, 64, 0},   {0, 64, 0, 64, 0, 64, 64, 32},
        {0, 64, 0, 64, 0, 64, 64, 64},    {0, 64, 0, 64, 0, 64, 64, 127},  {0, 127, 16, 64, 0, 40, 127, 127},
        {0, 64, 0, 64, 64, 64, 0, 0}};    /* (MDEC 0: the FM is over in ~1 ms; the stand-in's start is still chaos) */
    static const int16_t CB[][8] = {
        {0, 64, 0, 64, 64, 64, 64, 64}, {0, 64, 0, 64, 64, 64, 64, 96}, {0, 64, 0, 64, 64, 64, 64, 127},
        {0, 96, 16, 64, 127, 40, 96, 80}, {-12, 80, 64, 32, 96, 24, 48, 24}};
    static const int16_t CX[][8] = {
        {0, 64, 0, 64, 64, 64, 64, 0},  {0, 64, 0, 64, 16, 64, 64, 0},  {0, 64, 0, 64, 127, 64, 64, 0},
        {0, 127, 16, 64, 16, 40, 127, 0}, {0, 64, 0, 64, 64, 127, 64, 0}, {12, 64, 64, 64, 32, 40, 127, 0}};
    double gb, gx;
    test_bdefm_vs_twin(DM_BDEFM, "BDEFM", KB, sizeof KB / sizeof KB[0], &gb);
    test_bdefm_vs_twin(DM_XTEFM, "XTEFM", KX, sizeof KX / sizeof KX[0] - 1, &gx);
    test_bdefm_chaos(DM_BDEFM, "BDEFM", CB, sizeof CB / sizeof CB[0], gb);
    test_bdefm_chaos(DM_XTEFM, "XTEFM", CX, sizeof CX / sizeof CX[0], gx);
    test_bdefm_laws();
    test_bdefm_level();
    test_bdefm_extremes();
    test_bdefm_rehits();
    if (argc > 1)
        bdefm_write_wavs(argv[1], gb, gx);
    printf(fails ? "bdefm_test: %d FAILED\n" : "bdefm_test: all passed\n", fails);
    return fails != 0;
}
