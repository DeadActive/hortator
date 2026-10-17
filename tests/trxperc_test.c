/* XTTRX, XCTRX (dm_xttrx.c), CLTRX (dm_cltrx.c), RSTRX (dm_rstrx.c): each fixed-point voice against its float twin
 * (the md-re prototypes trx-xt.c / trx-xc.c, trx-cl.c, trx-rs.c, copied below, fed the firmware's rounded values),
 * the measured laws, the levels, the extremes. Each test prints one line; any failure exits 1.
 *   trxperc_test [OUTDIR]   OUTDIR: also writes A/B WAVs (float, the prototype's units) */
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
#define NK 8
static int32_t wl[SECS(8)];
static double ref[SECS(8)];

static double tau_d32(uint32_t d) { return -1 / (FS * log(1 - d / 4294967296.0)); }   /* a Q32 step's tau, s */
static double hz(uint32_t inc) { return inc * (FS / 4294967296.0); }

/* track 0 = model mi with knobs k (-99: the default), one hit at vel, frames rendered into wl; the voice's state
 * right after the trigger in *st */
static dm_state_t st;
static uint32_t st_ph;
static void hit(uint32_t mi, const int16_t *k, uint32_t vel, uint32_t frames)
{
    uint32_t i;
    host_init();
    drum_set_model(&trk[0], mi);
    for (i = 0; i < NK; i++)
        if (k[i] != -99)
            trk[0].p[P_E0 + i] = k[i];
    drum_play(&trk[0], vel, trk[0].nt);   /* (drum_hit plays note 0) */
    st = trk[0].v[0].ms;
    st_ph = trk[0].v[0].ph[0];
    if (frames)
        render_track(&trk[0], wl, frames);
}

/* ---- the twins (the prototypes' algorithms in float; the voice's own rounded values as their parameters) */
static void ref_xt(uint32_t mi, double *y, uint32_t n)
{
    const xt_mach_t *m = &XT_M[mi == DM_XCTRX];
    const xttrx_t *x = &st.xt;
    double f0 = hz(x->inc0), ra = hz(x->inc_r), rt = tau_d32(x->d_r), ka = 1 - x->d_e / 4294967296.0;
    double amp = m->amp / 65536.0, e = x->e / 2147483648.0, cl = x->cl / 16384.0, th = x->th / 65536.0;
    double sl = x->sl / 4294967296.0, tb = m->nb - m->frac / 256.0, kfl = XT_FLOOR / 2147483648.0;
    double cls = m->cls / 65536.0, hs = m->hs1 / 65536.0, hs2 = m->hs2 / 65536.0;
    /* the click's breakpoints (samples) as dm_xttrx.c's table has them */
    static const double CK[2][5] = {{2.98116, 0.1544, 33.98346, 0.0687, 52.15266},
                                    {1.9404, 0.2143, 30.33198, -0.0227, 31.89312}};
    const double *c = CK[mi == DM_XCTRX];
    double cta = c[0], ap = c[1], t1 = c[2], a1 = c[3], t2 = c[4];
    double ph = 0, hp = 0, px = 0, hp2 = 0, px2 = 0, cz = 0, tin = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double t = i, r = t >= t2 ? 0 : t < cta ? -ap * t / cta : t < t1 ? -ap + (ap - a1) * (t - cta) / (t1 - cta) :
                                    -a1 * (t2 - t) / (t2 - t1), yy;
        cz += cls * (r - cz);
        yy = cz * cl;
        if (i >= tb) {
            double tt = (i - tb) / FS, fr, b;
            if (i - 1 < tb)
                ph = 2 * M_PI * (f0 + ra) * tt;
            fr = f0 + ra * exp(-tt / rt);
            b = amp * e * sin(ph);
            ph += 2 * M_PI * fr / FS;
            e = e * ka - kfl;
            if (e < 0)
                e = 0;
            if (fabs(b) > th) {
                b = (b > 0 ? 1 : -1) * (th - sl * tin);
                tin += 1;
            } else
                tin = 0;
            yy += b;
        }
        hp = hs * (hp + yy - px);
        px = yy;
        hp2 = hs2 * (hp2 + hp - px2);
        px2 = hp;
        y[i] = hp2;
    }
}

static const double CLG[5] = {0, 32, 64, 96, 127};
static const double LX0[5] = {-23.1, -24.9, -24.1, -22.3, -22.8}, LX1[5] = {-15.9, -21.4, -20.2, -15.6, -14.7};
static const double LY0[5][5] = {{-20.4, -20.4, -19.7, -18.3, -16.9}, {-26.9, -20.8, -14.7, -14.5, -13.7},
                                 {-44.3, -15.4, -8.9, -11.2, -11.6},  {-60.1, -34.1, -14.3, -11.2, -8.0},
                                 {-71.6, -47.6, -30.0, -15.1, -15.1}};
static const double LY1[5][5] = {{-27.0, -27.0, -26.8, -25.5, -24.2}, {-26.9, -22.3, -17.6, -18.3, -17.2},
                                 {-47.7, -12.5, -8.9, -14.5, -16.0},  {-67.0, -39.8, -15.1, -13.9, -12.6},
                                 {-80.7, -56.6, -38.0, -20.9, -20.9}};
static double clt1(const double *t, double x)
{
    int k = x >= 96 ? 3 : (int)(x / 32);
    return t[k] + (t[k + 1] - t[k]) * (x - CLG[k]) / (CLG[k + 1] - CLG[k]);
}
static double clt2(const double t[5][5], double p, double q)
{
    int i = p >= 96 ? 3 : (int)(p / 32), j = q >= 96 ? 3 : (int)(q / 32);
    double fp = (p - CLG[i]) / (CLG[i + 1] - CLG[i]), fq = (q - CLG[j]) / (CLG[j + 1] - CLG[j]);
    double a = t[i][j] + (t[i][j + 1] - t[i][j]) * fq, b = t[i + 1][j] + (t[i + 1][j + 1] - t[i + 1][j]) * fq;
    return a + (b - a) * fp;
}
static double cl_knobf(double f) { return fmin(127, fmax(0, (f - 942.7) / 17.223)); }
/* the prototype's levels (linear, its units) of X and Y at these pitches (Hz) and ENH */
static void cl_levels(double fx, double fy, double E, double *ax, double *ay)
{
    double P = cl_knobf(fx), T = cl_knobf(fy), w = pow(E / 127, 0.75);
    double lx = clt1(LX0, P) + w * (clt1(LX1, P) - clt1(LX0, P));
    double ly = lx + clt2(LY0, P, T) + w * (clt2(LY1, P, T) - clt2(LY0, P, T));
    *ax = pow(10, (lx + 4) / 20);
    *ay = pow(10, (ly + 4) / 20);
}
static void ref_cl(const int16_t *k, double *y, uint32_t n)
{
    const cltrx_t *c = &st.cl;
    double fx = hz(trk[0].v[0].inc[1]), fy = hz(trk[0].v[0].inc[0]), ax, ay, tau = tau_d32(c->d_e);
    double ca = c->ce / (131072.0 * 256), ct = -1 / (FS * log(CL_K_CLIC / 65536.0));
    uint32_t i, sx = c->sx;
    cl_levels(fx, fy, k[3], &ax, &ay);
    for (i = 0; i < n; i++) {
        double t = i / (double)FS, env = exp(-t / tau);
        double v = ay * sin(2 * M_PI * fy * t) * (i < 32 ? i / 32.0 : 1);
        if (i >= sx) {
            uint32_t kk = i - sx;
            v += ax * sin(2 * M_PI * fx * kk / FS) * (kk < 32 ? kk / 32.0 : 1);
        }
        v *= env;
        if (ca > 0)
            v += ca * cos(2 * M_PI * CL_CLIC_HZ * t) * exp(-t / ct);
        y[i] = v;
    }
}

static const double RSA[17] = {0.0273, 0.0292, 0.0309, 0.0325, 0.0339, 0.0352, 0.0364, 0.0375, 0.0385, 0.0394, 0.0402,
                               0.0410, 0.0417, 0.0424, 0.0430, 0.0435, 0.0440};
static const double DX[10] = {0, 8, 16, 32, 48, 64, 80, 96, 112, 127};
static const double TJ[3][10] = {{0, 11.96, 15.00, 19.80, 22.69, 28.12, 30.76, 33.29, 22.69, 28.59},
                                 {0, 13.24, 15.27, 18.40, 22.59, 26.62, 29.27, 32.93, 35.96, 38.01},
                                 {0, 14.04, 15.08, 20.76, 22.34, 30.45, 28.97, 31.83, 36.07, 37.84}};
static const double KK[3][10] = {{1, 1.225, 1.391, 1.851, 2.216, 2.590, 2.813, 2.862, 3.183, 2.332},
                                 {1, 1.124, 1.214, 1.449, 1.635, 1.871, 2.104, 2.267, 2.404, 2.508},
                                 {1, 1.086, 1.160, 1.220, 1.417, 1.396, 1.754, 1.942, 2.074, 2.204}};
static const double DD[3][10] = {{0, 0, -0.99, -3.26, -1.88, 5.74, 18.03, 34.39, 40.64, 60.44},
                                 {0, -0.06, -0.82, -3.00, -3.94, -4.58, -3.13, 2.74, 12.10, 22.03},
                                 {0, -0.01, -0.92, -1.29, -3.32, -0.99, -5.86, -5.23, -0.82, 4.48}};
static const double FG[3][10] = {{1, 1, 0.996, 0.985, 0.974, 0.952, 0.927, 0.875, 0.864, 0.820},
                                 {1, 0.997, 0.997, 0.992, 0.982, 0.969, 0.951, 0.930, 0.906, 0.881},
                                 {1, 0.998, 0.998, 0.993, 0.986, 0.977, 0.964, 0.950, 0.932, 0.914}};
static double rlerp(const double *t, double x)
{
    double i = x / 8;
    int k = i >= 15 ? 15 : (int)i;
    double f = k == 15 ? (x - 120) / 7 : i - k;
    return t[k] + (t[k + 1] - t[k]) * f;
}
static double rt2(const double t[3][10], double p, double x)
{
    int j = 0, i = p < 64 ? 0 : 1;
    double fx, fp = p < 64 ? p / 64 : (p - 64) / 63, a, b;
    while (j < 8 && x > DX[j + 1])
        j++;
    fx = (x - DX[j]) / (DX[j + 1] - DX[j]);
    a = t[i][j] + (t[i][j + 1] - t[i][j]) * fx;
    b = t[i + 1][j] + (t[i + 1][j + 1] - t[i + 1][j]) * fx;
    return a + (b - a) * fp;
}
static double wtj, wkk, wd0;
static double warp(double th) { return th < wtj ? wkk * th : wd0 + (th - wtj) * (M_PI - wd0) / (M_PI - wtj); }
static double wsin(double ph) { return ph < M_PI ? sin(warp(ph)) : -sin(warp(ph - M_PI)); }
static double rs_click[RS_NCLK];
static void rs_click_make(void)                      /* the prototype's click recipe (v2) */
{
    double cf = 474, cr = 0.0255, chold = 0.007, ctau = 0.0042, cgk = 0.871, clp = 1287, chp = 473, ce = 0, cg;
    int dl = (int)(FS / cf + 0.5);
    double cb[RS_NCLK + 100] = {0};
    uint32_t r = 0x2545F491u, i;
    double ahp = exp(-2 * M_PI * chp / FS), alp = 1 - exp(-2 * M_PI * clp / FS), hx = 0, hy = 0, l1 = 0, l2 = 0;
    for (i = 0; i < RS_NCLK; i++) {
        double t = i / (double)FS, w, c;
        r ^= r << 13;
        r ^= r >> 17;
        r ^= r << 5;
        w = (int32_t)r / 2147483648.0;
        cb[i + dl] = w + cgk * cb[i];
        c = cb[i + dl];
        hy = ahp * (hy + c - hx);
        hx = c;
        l1 += alp * (hy - l1);
        l2 += alp * (l1 - l2);
        rs_click[i] = l2 * (t < chold ? 1 : exp(-(t - chold) / ctau));
        ce += rs_click[i] * rs_click[i];
    }
    cg = cr / sqrt(ce / RS_NCLK);
    for (i = 0; i < RS_NCLK; i++)
        rs_click[i] *= cg;
}
static double rs_p(void) { return fmin(127, fmax(0, hz(trk[0].v[0].inc[0]) / (FS / 256) * 128 - 128)); }
static void ref_rs(const int16_t *k, double *y, uint32_t n)
{
    double f0 = hz(trk[0].v[0].inc[0]), P = rs_p(), X = k[2], tau = tau_d32(st.rs.d_e), amp = rlerp(RSA, P), g = 1;
    double ph = 0;
    uint32_t i;
    wtj = rt2(TJ, P, X) * M_PI / 180;
    wkk = rt2(KK, P, X);
    wd0 = wtj - rt2(DD, P, X) * M_PI / 180;
    if (X > 0) {
        double c = 0, s = 0;
        for (i = 0; i < 2048; i++) {
            double q = 2 * M_PI * i / 2048, v = wsin(q);
            c += v * cos(q);
            s += v * sin(q);
        }
        g = rt2(FG, P, X) / (hypot(c, s) * 2 / 2048);
    }
    for (i = 0; i < n; i++) {
        double t = i / (double)FS, env = exp(-t / tau), body = X > 0 ? g * wsin(ph) : sin(ph);
        ph += 2 * M_PI * f0 / FS;
        if (ph >= 2 * M_PI)
            ph -= 2 * M_PI;
        y[i] = (amp * body + (i < RS_NCLK ? rs_click[i] : 0)) * env;
    }
}

/* the voice against its twin over [0, n): the least-squares gain and the residual (dB re the twin) */
static double vs_ref(uint32_t mi, const int16_t *k, uint32_t n, double *gain)
{
    double xy = 0, yy = 0, e = 0, r = 1e-30;
    uint32_t i;
    hit(mi, k, 127, n);
    if (mi == DM_CLTRX)
        ref_cl(k, ref, n);
    else if (mi == DM_RSTRX)
        ref_rs(k, ref, n);
    else
        ref_xt(mi, ref, n);
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

/* every setting of a model against its twin: residual < lim (< clim with DIST on, knob ck: XT's hard clip and RS's
 * warp both jump; an edge a sample early or late on the last bits is a large error alone, worst when the pitch is
 * near a whole number of samples and the edges keep landing alike, e.g. RSTRX at 172.4 Hz, a 256-sample period:
 * -28 dB), one gain within 1 % */
static void twin_set(uint32_t mi, const int16_t (*K)[NK], uint32_t nk, double secs, int ck, double lim, double clim)
{
    double worst = -999, worst_c = -999, g, g0 = 0, gw = 1;
    uint32_t i;
    char what[256];
    for (i = 0; i < nk; i++) {
        double e = vs_ref(mi, K[i], SECS(secs), &g);
        int clip = ck >= 0 && K[i][ck] > 0;
        if (!i)
            g0 = g;
        if (fabs(g / g0 - 1) > fabs(gw - 1))
            gw = g / g0;
        if (clip ? e > worst_c : e > worst)
            *(clip ? &worst_c : &worst) = e;
        if (e > (clip ? clim : lim))
            printf("     knobs %d %d %d %d %d %d %d: residual %.1f dB\n", K[i][0], K[i][1], K[i][2], K[i][3], K[i][4],
                   K[i][5], K[i][6], e);
    }
    printf("     %s vs its float twin (%u settings, %.1f s): worst residual %.1f dB%s", N_MODEL[mi], nk, secs, worst,
           ck >= 0 ? "" : "\n");
    if (ck >= 0)
        printf(" (with DIST %.1f dB)\n", worst_c);
    printf("     (gain %.0f / unit, spread %.2f %%)\n", g0, 100 * (gw - 1));
    snprintf(what, sizeof what, "%s: every setting matches its float twin (residual < %.0f dB%s, one gain within 1 %%)",
             N_MODEL[mi], lim, ck >= 0 ? ", DIST's edges < -20 dB" : "");
    check(what, worst < lim && worst_c < clim && fabs(gw - 1) < 0.01);
}

static void test_xt_twin(void)
{
    static const int16_t K[][NK] = {{0, 64, 0, 64, 0, 0, 0},      {-24, 64, 0, 64, 0, 0, 0},  {24, 64, 0, 64, 0, 0, 0},
                                    {-12, 64, 0, 64, 0, 0, 0},    {0, 0, 0, 64, 0, 0, 0},     {0, 32, 0, 64, 0, 0, 0},
                                    {0, 96, 0, 64, 0, 0, 0},      {0, 127, 0, 64, 0, 0, 0},   {0, 64, 64, 64, 0, 0, 0},
                                    {0, 64, 127, 0, 0, 0, 0},     {0, 64, 127, 127, 0, 0, 0}, {-12, 64, 127, 64, 0, 0, 0},
                                    {0, 64, 0, 64, 64, 0, 0},     {0, 64, 0, 64, 127, 0, 0},  {0, 32, 0, 64, 127, 0, 0},
                                    {0, 64, 0, 64, 0, 32, 0},     {0, 64, 0, 64, 0, 64, 0},   {0, 64, 0, 64, 0, 127, 0},
                                    {0, 64, 0, 64, 0, 64, 64},    {0, 64, 0, 64, 0, 64, 127}, {0, 64, 0, 64, 0, 127, 127},
                                    {-12, 64, 0, 64, 0, 127, 0},  {12, 64, 0, 64, 0, 64, 0},
                                    {-24, 127, 127, 127, 127, 127, 127}};
    twin_set(DM_XTTRX, K, sizeof K / sizeof K[0], 0.6, 5, -30, -20);
    twin_set(DM_XCTRX, K, sizeof K / sizeof K[0], 0.6, 5, -30, -20);
}

static void test_cl_twin(void)
{
    static const int16_t K[][NK] = {{0, 64, 0, 0, 64, 0},   {-24, 64, 0, 0, 64, 0}, {24, 64, 0, 0, 64, 0},
                                    {-7, 64, 0, 0, 64, 0},  {5, 64, 0, 0, 64, 0},   {0, 64, 0, 0, 0, 0},
                                    {0, 64, 0, 0, 32, 0},   {0, 64, 0, 0, 96, 0},   {0, 64, 0, 0, 127, 0},
                                    {0, 0, 0, 0, 64, 0},    {0, 127, 0, 0, 64, 0},  {0, 64, 16, 0, 64, 0},
                                    {0, 64, 64, 0, 64, 0},  {0, 64, 127, 0, 64, 0}, {0, 64, 0, 64, 64, 0},
                                    {0, 64, 0, 127, 64, 0}, {0, 64, 0, 0, 64, 127}, {12, 96, 40, 100, 20, 64},
                                    {-12, 30, 90, 30, 110, 127}};
    twin_set(DM_CLTRX, K, sizeof K / sizeof K[0], 0.3, -1, -30, 0);
}

static void test_rs_twin(void)
{
    static const int16_t K[][NK] = {{0, 64, 0},   {-24, 64, 0}, {24, 64, 0},  {-7, 64, 0},  {5, 64, 0},
                                    {0, 0, 0},    {0, 32, 0},   {0, 96, 0},   {0, 127, 0},  {0, 64, 8},
                                    {0, 64, 32},  {0, 64, 64},  {0, 64, 96},  {0, 64, 127}, {-7, 64, 64},
                                    {-7, 64, 127}, {5, 64, 127}, {-3, 64, 100}, {2, 64, 50}, {-24, 127, 127}};
    twin_set(DM_RSTRX, K, sizeof K / sizeof K[0], 0.3, 2, -30, -20);
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

/* the pitch heard: TUNE 0 at the measured neutral (+-0.5 %), +-12 an octave */
static void pitch_law(uint32_t mi, int16_t *k, double want, double a, double b, const char *what)
{
    double f, fu, fd;
    char s[200];
    k[0] = 0;
    hit(mi, k, 127, SECS(b));
    f = zc_freq(wl, SECS(a), SECS(b));
    k[0] = 12;
    hit(mi, k, 127, SECS(b));
    fu = zc_freq(wl, SECS(a), SECS(b));
    k[0] = -12;
    hit(mi, k, 127, SECS(b));
    fd = zc_freq(wl, SECS(a), SECS(b));
    printf("     %s TUNE -12 / 0 / +12: %.2f / %.2f / %.2f Hz\n", N_MODEL[mi], fd, f, fu);
    snprintf(s, sizeof s, "%s: TUNE 0 is %s (+-0.5 %%), +-12 an octave", N_MODEL[mi], what);
    check(s, fabs(f / want - 1) < 0.005 && fabs(fu / f / 2 - 1) < 0.005 && fabs(f / fd / 2 - 1) < 0.005);
}

/* XT / XC: the pitch, DEC (the factor over the whole knob, 0.85 s cap; and heard), RDEC, DAMP, DIST, DTYP */
static void test_xt_laws(void)
{
    static const double TAU[2] = {0.1103, 0.0547}, RAT[2] = {1.375, 1.263}, F64[2] = {144.70, 172.91};
    static const double TH0[2] = {0.3404, 0.3266}, THS[2] = {0.002065, 0.001908}, SL[2] = {51.6, 57.3},
                        SLP[2] = {2.31, 2.10};
    uint32_t mc;
    for (mc = 0; mc < 2u; mc++) {
        uint32_t mi = mc ? DM_XCTRX : DM_XTTRX;
        int16_t k[NK] = {0, 127, 0, 64, 0, 0, 0, 0};
        double wd = 0, wr = 0, wth = 0, wsl = 0, wdm = 0, heard[3];
        int d;
        char s[240];
        pitch_law(mi, k, F64[mc], 0.2, 0.8, mc ? "172.9 Hz (PTCH 64)" : "144.7 Hz (PTCH 64)");
        for (d = 0; d <= 127; d++) {
            int16_t kk[NK] = {0, (int16_t)d, 0, (int16_t)d, (int16_t)d, (int16_t)d, (int16_t)d, 0};
            double ld = fmin(0.85, TAU[mc] * pow(RAT[mc], (d - 64) / 8.0)), lr = 1 / (42.0 - 0.3175 * d);
            hit(mi, kk, 127, 0);
            wd = fmax(wd, fabs(tau_d32(st.xt.d_e) / ld - 1));
            wr = fmax(wr, fabs(tau_d32(st.xt.d_r) / lr - 1));
            wdm = fmax(wdm, fabs(st.xt.e / 2147483648.0 - (1 - d / 254.0)) + fabs(st.xt.cl / 16384.0 - (1 + d / 127.0)));
            wth = fmax(wth, fabs(st.xt.th / 65536.0 - (TH0[mc] - THS[mc] * d)) / (TH0[mc] - THS[mc] * d));
            if (d >= 8)
                wsl = fmax(wsl, fabs(st.xt.sl / 4294967296.0 * FS / (SL[mc] * pow(d / 127.0, SLP[mc])) - 1));
        }
        printf("     %s laws over 0..127: DEC %.2f %%, RDEC %.2f %%, DIST's ceiling %.2f %%, DTYP's sag %.2f %% "
               "(8..127), DAMP %.4f\n", N_MODEL[mi], 100 * wd, 100 * wr, 100 * wth, 100 * wsl, wdm);
        snprintf(s, sizeof s, "%s: DEC (tau %.1f ms x %.3f ^ ((d - 64) / 8), to 0.85 s), RDEC, DIST, DTYP, DAMP follow "
                 "the measured laws (1 %%, DAMP 0.001)", N_MODEL[mi], 1000 * TAU[mc], RAT[mc]);
        check(s, wd < 0.01 && wr < 0.01 && wth < 0.01 && wsl < 0.01 && wdm < 0.001);
        {                                               /* DEC heard: 32 / 64 / 96 */
            static const int D[3] = {32, 64, 96};
            uint32_t j;
            for (j = 0; j < 3u; j++) {
                int16_t kk[NK] = {0, (int16_t)D[j], 0, 64, 0, 0, 0, 0};
                double want = TAU[mc] * pow(RAT[mc], (D[j] - 64) / 8.0), a = 0.02, b = 0.02 + want;
                hit(mi, kk, 127, SECS(b + 0.03));
                heard[j] = want / log(rms(wl, SECS(a), SECS(a + 0.02)) / rms(wl, SECS(b), SECS(b + 0.02))) / want;
            }
            printf("     %s DEC 32 / 64 / 96 heard: tau %.2f / %.2f / %.2f x the law\n", N_MODEL[mi], heard[0],
                   heard[1], heard[2]);
            snprintf(s, sizeof s, "%s: DEC heard follows the law within 5 %%", N_MODEL[mi]);
            check(s, fabs(heard[0] - 1) < 0.05 && fabs(heard[1] - 1) < 0.05 && fabs(heard[2] - 1) < 0.05);
        }
    }
}

/* CL: the pitches (942.7 Hz + 17.223 Hz x the knob; TUNE moves both by semitones), DEC, DUAL, the levels */
static void test_cl_laws(void)
{
    int16_t k[NK] = {0, 127, 0, 0, 0, 0, 0, 0};
    double wf = 0, wd = 0, wl_ = 0;
    int d, s;
    pitch_law(DM_CLTRX, k, 2045.0, 0.005, 0.2, "2045 Hz (PTCH 64)");
    for (d = 0; d <= 127; d++) {
        int16_t kk[NK] = {0, (int16_t)d, (int16_t)d, 0, (int16_t)d, 0, 0, 0};
        double want = 0.0548 * exp(0.0293 * (d - 64));
        hit(DM_CLTRX, kk, 127, 0);
        wd = fmax(wd, fabs(tau_d32(st.cl.d_e) / want - 1));
        wf = fmax(wf, fabs(hz(trk[0].v[0].inc[0]) / (942.7 + 17.223 * d) - 1));
        wf = fmax(wf, fabs(st.cl.sx - 32.0 - floor(0.000094 * d * FS + 0.5)));
    }
    for (s = -24; s <= 24; s += 6)
        for (d = 0; d <= 127; d += 9) {
            int16_t kk[NK] = {(int16_t)s, 64, 0, (int16_t)((d * 5) % 128), (int16_t)d, 0, 0, 0};
            double ax, ay, fx, fy;
            hit(DM_CLTRX, kk, 127, 0);
            fx = hz(trk[0].v[0].inc[1]);
            fy = hz(trk[0].v[0].inc[0]);
            wf = fmax(wf, fabs(fy / fx - (942.7 + 17.223 * d) / (942.7 + 17.223 * 64)));
            cl_levels(fx, fy, kk[3], &ax, &ay);
            wl_ = fmax(wl_, fabs(20 * log10(st.cl.ax / 131072.0 / ax)));
            if (ay > 1e-3)
                wl_ = fmax(wl_, fabs(20 * log10(st.cl.ay / 131072.0 / ay)));
        }
    printf("     CLTRX laws: pitches %.4f (the ratio, DUAL's delay in samples), DEC %.2f %%, levels %.2f dB off\n", wf,
           100 * wd, wl_);
    check("CLTRX: TUNE2 942.7 + 17.223 Hz x v, TUNE moving both; DEC tau 54.8 ms x e ^ (0.0293 (v - 64)) (1 %); DUAL "
          "0.094 ms x v; the level tables (0.1 dB)", wf < 0.001 && wd < 0.01 && wl_ < 0.1);
}

/* RS: the pitch, DEC, the level vs pitch, the click as its recipe, DIST's harmonics as measured */
static void test_rs_laws(void)
{
    int16_t k[NK] = {0, 127, 0, 0, 0, 0, 0, 0};
    double wd = 0, wa = 0;
    uint32_t i, cmax = 0;
    int d;
    pitch_law(DM_RSTRX, k, 258.40, 0.02, 0.5, "258.4 Hz (PTCH 64)");
    for (d = 0; d <= 127; d++) {
        int16_t kk[NK] = {(int16_t)(d % 49 - 24), (int16_t)d, 0, 0, 0, 0, 0, 0};
        hit(DM_RSTRX, kk, 127, 0);
        wd = fmax(wd, fabs(tau_d32(st.rs.d_e) / (0.0134 * exp(0.0292 * d)) - 1));
        wa = fmax(wa, fabs(st.rs.amp / 262144.0 / rlerp(RSA, rs_p()) - 1));
    }
    for (i = 0; i < RS_NCLK; i++)
        cmax = (uint32_t)fmax(cmax, fabs(RS_CLICK[i] - rs_click[i] * 262144));
    printf("     RSTRX laws: DEC %.2f %%, the level vs pitch %.2f %%, the click table vs its recipe %u LSB\n", 100 * wd,
           100 * wa, cmax);
    check("RSTRX: DEC tau 13.4 ms x e ^ (0.0292 v) (1 %), the level vs pitch (1 %), the click as its recipe (1 LSB)",
          wd < 0.01 && wa < 0.01 && cmax <= 1u);
    {                                                   /* DIST 127: h3 re h1 (the machine: -8 / -15.5 / -20 dB at
                                                         * PTCH 0 / 64 / 127), h1 re DIST 0 (-1.7 / -1.1 / -0.8 dB) */
        static const int16_t T[3] = {-7, 0, 5};
        static const double H3[3] = {-8, -15.5, -20}, H1[3] = {-1.7, -1.1, -0.8};
        double h3[3], h1[3];
        uint32_t j, ok = 1;
        for (j = 0; j < 3u; j++) {
            int16_t kk[NK] = {T[j], 127, 0, 0, 0, 0, 0, 0};
            double f, a0;
            hit(DM_RSTRX, kk, 127, SECS(0.12));
            f = hz(trk[0].v[0].inc[0]);
            a0 = goertzel(wl + SECS(0.02), SECS(0.1), f);
            kk[2] = 127;
            hit(DM_RSTRX, kk, 127, SECS(0.12));
            h1[j] = 20 * log10(goertzel(wl + SECS(0.02), SECS(0.1), f) / a0);
            h3[j] = 20 * log10(goertzel(wl + SECS(0.02), SECS(0.1), 3 * f) / goertzel(wl + SECS(0.02), SECS(0.1), f));
            ok &= fabs(h3[j] - H3[j]) < 2 && fabs(h1[j] - H1[j]) < 0.5;
        }
        printf("     RSTRX DIST 127 at TUNE -7 / 0 / 5: h3 %.1f / %.1f / %.1f dB (the machine -8 / -15.5 / -20), h1 "
               "%.1f / %.1f / %.1f dB\n", h3[0], h3[1], h3[2], h1[0], h1[1], h1[2]);
        check("RSTRX: DIST's 3rd harmonic as measured (+-2 dB, stronger low), its fundamental's drop (+-0.5 dB)", ok);
    }
}

/* the levels at the defaults within +-3 dB of the comparable models' */
static void test_levels(void)
{
    static const uint32_t M[4] = {DM_XTTRX, DM_XCTRX, DM_CLTRX, DM_RSTRX}, R[4] = {DM_TOM, DM_CONGA, DM_CLAVE, DM_RIM};
    static const int16_t DEF[NK] = {-99, -99, -99, -99, -99, -99, -99, -99};
    uint32_t j, ok = 1;
    for (j = 0; j < 4u; j++) {
        double a, b;
        hit(M[j], DEF, 127, SECS(0.3));
        a = rms(wl, 0, SECS(0.3));
        hit(R[j], DEF, 127, SECS(0.3));
        b = rms(wl, 0, SECS(0.3));
        printf("     %s vs %s at the defaults: %+.1f dB\n", N_MODEL[M[j]], N_MODEL[R[j]], 20 * log10(a / b));
        ok &= fabs(20 * log10(a / b)) <= 3;
    }
    check("XTTRX / XCTRX / CLTRX / RSTRX: the defaults within +-3 dB of TOM / CONGA / CLAVE / RIM", ok);
}

/* the extremes: every knob at 0 / 127 (TUNE -24 / +24) and a step's note +-24, velocity 1 and 127: bounded, ends */
static void test_extremes(void)
{
    static const uint32_t M[4] = {DM_XTTRX, DM_XCTRX, DM_CLTRX, DM_RSTRX}, NKN[4] = {7, 7, 6, 3};
    uint32_t j;
    for (j = 0; j < 4u; j++) {
        uint32_t c, i, ok = 1, ended = 1, nk = NKN[j];
        int32_t pk = 0;
        char s[200];
        for (c = 0; c < 2u << nk; c++) {
            host_init();
            drum_set_model(&trk[0], M[j]);
            for (i = 0; i < nk; i++)
                trk[0].p[P_E0 + i] = (int16_t)((c >> i & 1u) ? (i ? 127 : 24) : (i ? 0 : -24));
            trk[0].nt = (c >> nk & 1u) ? 24 : -24;
            drum_play(&trk[0], (c & 3u) == 3u ? 1 : 127, trk[0].nt);   /* (drum_hit plays note 0) */
            render_track(&trk[0], wl, SECS(6));
            for (i = 0; i < SECS(6); i++) {
                ok &= abs(wl[i]) <= 4 * VOICE_FS;
                pk = abs(wl[i]) > pk ? abs(wl[i]) : pk;
            }
            ended &= !trk[0].v[0].active;
        }
        printf("     %s extremes: peak %.2f x VOICE_FS\n", N_MODEL[M[j]], pk / (double)VOICE_FS);
        snprintf(s, sizeof s, "%s: every knob at its ends, notes +-24, velocity 1 / 127: bounded, the voice ends within "
                 "6 s", N_MODEL[M[j]]);
        check(s, ok && ended);
    }
}

/* re-hits 5 ms apart and a switch to the model while another rings: bounded */
static void test_rehits(void)
{
    static const uint32_t M[4] = {DM_XTTRX, DM_XCTRX, DM_CLTRX, DM_RSTRX};
    static const int16_t K[NK] = {0, 127, 127, 127, 127, 127, 127, 0};
    uint32_t i, j, ok = 1;
    for (j = 0; j < 4u; j++) {
        hit(M[j], K, 127, SECS(0.005));
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
        drum_set_model(&trk[0], M[j]);
        drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
        render_track(&trk[0], wl, SECS(0.3));
        for (i = 0; i < SECS(0.3); i++)
            ok &= abs(wl[i]) <= 4 * VOICE_FS;
    }
    check("XTTRX / XCTRX / CLTRX / RSTRX: re-hits 5 ms apart and a model switch while ringing: bounded", ok);
}

/* the A/B set: the md-re renders' names (machine knobs; PTCH 64 = TUNE 0), the firmware in the prototype's units */
static void write_set(const char *dir, uint32_t mi, const char *pre, const char *const *names, const int16_t (*K)[NK],
                      uint32_t nw)
{
    double g;
    uint32_t w, i, n = SECS(1.5);
    vs_ref(mi, K[0], SECS(0.3), &g);
    for (w = 0; w < nw; w++) {
        char path[512];
        FILE *f;
        uint32_t v;
        snprintf(path, sizeof path, "%s/%s%s.wav", dir, pre, names[w]);
        f = fopen(path, "wb");
        if (!f)
            continue;
        hit(mi, K[w], 127, n);
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
    printf("     wrote %u WAVs (%s*) to %s\n", nw, pre, dir);
}
static void write_wavs(const char *dir)
{
    static const char *const XN[] = {"neutral", "dec_096", "ramp_127", "damp_127", "dist_127", "dtyp_d127_127"};
    static const int16_t XK[][NK] = {{0, 64, 0, 64, 0, 0, 0}, {0, 96, 0, 64, 0, 0, 0}, {0, 64, 127, 64, 0, 0, 0},
                                     {0, 64, 0, 64, 127, 0, 0}, {0, 64, 0, 64, 0, 127, 0}, {0, 64, 0, 64, 0, 127, 127}};
    static const char *const CN[] = {"neutral", "tune_000", "dual_064", "enh_127", "clic_127"};
    static const int16_t CK[][NK] = {{0, 64, 0, 0, 64, 0}, {0, 64, 0, 0, 0, 0}, {0, 64, 64, 0, 64, 0},
                                     {0, 64, 0, 127, 64, 0}, {0, 64, 0, 0, 64, 127}};
    static const char *const RN[] = {"neutral", "dec_127", "dist_064", "dist_127"};
    static const int16_t RK[][NK] = {{0, 64, 0}, {0, 127, 0}, {0, 64, 64}, {0, 64, 127}};
    write_set(dir, DM_XTTRX, "xt_", XN, XK, 6);
    write_set(dir, DM_XCTRX, "xc_", XN, XK, 6);
    write_set(dir, DM_CLTRX, "cl_", CN, CK, 5);
    write_set(dir, DM_RSTRX, "rs_", RN, RK, 4);
}

/* review fix (2026-10-18): TUNE +24 with a step's note +24 (pitch +48 semitones) and the heaviest knobs: no signed
 * overflow (this test runs under UBSan), bounded, ends within 6 s; every MODEL_ of this file at TUNE +-24, notes +-24 */
static void test_note48(uint32_t model, const int16_t *k, const char *what)
{
    uint32_t i, s, ok = 1;
    for (s = 0; s < 4u; s++) {
        host_init();
        drum_set_model(&trk[0], model);
        for (i = 0; i < 8u; i++)
            trk[0].p[P_E0 + i] = k[i];
        trk[0].p[P_E0] = (s & 1u) ? 24 : -24;
        trk[0].nt = (s & 2u) ? 24 : -24;
        drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
        render_track(&trk[0], wl, SECS(6));
        for (i = 0; i < SECS(6); i++)
            ok &= abs(wl[i]) <= 4 * VOICE_FS;
        ok &= !trk[0].v[0].active;
    }
    check(what, ok);
}

int main(int argc, char **argv)
{
    rs_click_make();
    test_xt_twin();
    test_cl_twin();
    test_rs_twin();
    test_xt_laws();
    test_cl_laws();
    test_rs_laws();
    test_levels();
    test_extremes();
    test_rehits();
    if (argc > 1)
        write_wavs(argv[1]);
        {
        static const int16_t K[8] = {10, 127, 127, 127, 127, 127, 0, 0};
        test_note48(DM_CLTRX, K, "CLTRX: TUNE2 127 with TUNE / notes at +-24 (above Nyquist): no overflow, bounded, ends");
        {                                            /* every TUNE with notes +-24 (pitches past Nyquist): UBSan */
            int32_t tu, n, ok = 1;
            uint32_t i;
            for (tu = -24; tu <= 24; tu++)
                for (n = -24; n <= 24; n += 48) {
                    host_init();
                    drum_set_model(&trk[0], DM_CLTRX);
                    for (i = 0; i < 8u; i++)
                        trk[0].p[P_E0 + i] = K[i];
                    trk[0].p[P_E0] = (int16_t)tu;
                    drum_play(&trk[0], 127, n);
                    render_track(&trk[0], wl, SECS(0.3));
                    for (i = 0; i < SECS(0.3); i++)
                        ok &= abs(wl[i]) <= 4 * VOICE_FS;
                }
            check("CLTRX: every TUNE with notes +-24, TUNE2 127: no overflow, bounded", ok);
        }
    }
    printf(fails ? "trxperc_test: %d FAILED\n" : "trxperc_test: all passed\n", fails);
    return fails != 0;
}
