/* CBTRX, CHTRX, OHTRX, CYTRX (dm_trxcb.c, dm_trxm.c): the fixed-point voices against float twins of the md-re
 * prototypes (proto/trx-cb.c, trx-ch.c, trx-oh.c, trx-cy.c, copied below) given the same start (the oscillators' phases
 * the hit drew, the same noise), the measured laws, the levels, the extremes, the choke. Each test prints one line;
 * any failure exits 1.
 *   trxmetal_test [OUTDIR]   OUTDIR: also writes the A/B set as float WAVs in the prototypes' units (md-re renders'
 *   names, under cb_, ch_, oh_, cy_)
 *   trxmetal_test -k MODEL K0..K7 SECS   one setting against its twin (WIN=1: per 10 ms; TWINWAV=path: both as WAVs) */
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
static void hit(uint32_t mi, const int16_t *k, uint32_t vel, uint32_t frames)
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

/* ---- the prototypes in float (their laws as written there; TUNE 0) */
typedef struct { double b0, b1, b2, a1, a2, x1, x2, y1, y2; } bq_t;
static void bq_set(bq_t *f, int hp, double fc, double q)          /* RBJ cookbook */
{
    double w0, c, al, a0;
    if (fc > 20000)
        fc = 20000;
    w0 = 2 * M_PI * fc / FS, c = cos(w0), al = sin(w0) / (2 * q), a0 = 1 + al;
    f->b0 = (hp ? (1 + c) / 2 : (1 - c) / 2) / a0;
    f->b1 = (hp ? -(1 + c) : 1 - c) / a0;
    f->b2 = f->b0;
    f->a1 = -2 * c / a0;
    f->a2 = (1 - al) / a0;
    f->x1 = f->x2 = f->y1 = f->y2 = 0;
}
static double bq_run(bq_t *f, double x)
{
    double y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1; f->x1 = x; f->y2 = f->y1; f->y1 = y;
    return y;
}
static int32_t trng;                                    /* the twin's noise: the voice's own sequence */
static double twhite(void) { return ((int32_t)noise32(&trng) >> 16) / 32768.0; }

/* the bank's counters from the voice's phases (TUNE 0: phase = count x the exact increment) */
static void bank_counts(const trxm_t *m, int *ph)
{
    static const int MAP[6] = {0, 1, 2, 3, 4, 6};
    int k;
    for (k = 0; k < 6; k++)
        ph[MAP[k]] = (int)(m->ph[k] / TRXM_INC[k]);
    ph[5] = ph[0] % 57;
}
static const int MP[7] = {114, 102, 86, 80, 58, 57, 52};
static double bank_tick(int *ph, const double *W)
{
    double bank = 0;
    int k;
    for (k = 0; k < 7; k++) {
        int hi = k == 5 ? ph[k] < 15 : ph[k] < MP[k] / 2;
        bank += W[k] * (hi ? 1 : -1);
        if (++ph[k] >= MP[k])
            ph[k] = 0;
    }
    return bank;
}

/* trx-ch / trx-oh: knobs TUNE DECAY GAP HPF LPF MTAL */
static void ref_hat(const int16_t *p, int open, double *y, uint32_t n)
{
    static const double W[7] = {0.57, 1, 1, 1, 1, 0.49, 1};
    double DEC = p[1], GAP = p[2], HPF = p[3], LPF = p[4], MT = p[5], m = MT / 127.0;
    double lev = pow(10, (1.35 * HPF / 127 - 1.0 * (1 - LPF / 127)) / 20);
    double gb = pow(m, 0.7) * 0.01041, gn = pow(1 - m, 0.7) * 0.01841;
    double th = (open ? 23.0 + 6.3 * DEC : 2.5 + 1.457 * DEC) / 1000, tr = 0.540;
    double tau = open ? 0.0164 * pow(2, GAP / 30.4) : 0.0165 * pow(2, GAP / 61.0);
    double aknee = 1 - th / tr, dk = exp(-1 / (tau * FS)), env = 1;
    int ph[7];
    bq_t hp, lp;
    uint32_t i;
    bank_counts(&v0.ms.tm, ph);
    trng = v0.rng;
    bq_set(&hp, 1, 3993 * pow(2, 0.733 * HPF / 127), 1.82);
    bq_set(&lp, 0, 4439 * pow(2, 1.207 * LPF / 127), 3.24);
    for (i = 0; i < n; i++) {
        double t = i / (double)FS, x = 0;
        if (MT > 0)
            x = gb * bank_tick(ph, W);
        if (MT < 127)
            x += gn * twhite();
        env = t < th ? 1 - t / tr : (i && t - 1 / (double)FS < th ? aknee : env * dk);
        y[i] = bq_run(&lp, bq_run(&hp, x)) * env * lev;
    }
}

/* trx-cy: knobs TUNE DECAY RICH TOP TTUN SIZE PEAK */
static const double RT[17][7] = {
    {.832, .562, .115, .412, .219, .759, .367}, {.841, .750, .209, .064, .164, .759, .248},
    {.851, .902, .447, .184, .123, .767, .172}, {.861, 1.0, .617, .372, .078, .759, .126},
    {.794, 1.0, .700, .479, .257, .708, .097}, {.759, 1.0, .759, .556, .380, .668, .140},
    {.724, 1.0, .804, .617, .473, .638, .292}, {.700, 1.0, .832, .661, .550, .617, .407},
    {.676, 1.0, .861, .700, .610, .596, .507}, {.603, 1.0, .881, .741, .661, .531, .589},
    {.543, .977, .881, .750, .692, .473, .646}, {.457, .851, .813, .708, .668, .398, .646},
    {.403, .750, .767, .676, .646, .351, .646}, {.367, .676, .724, .653, .631, .320, .646},
    {.347, .631, .668, .638, .617, .306, .646}, {.343, .596, .624, .610, .603, .299, .646},
    {.347, .589, .610, .596, .589, .306, .646}};
static const double RG[17] = {-6.72, -5.68, -5.14, -4.34, -3.34, -2.20, -1.24, -0.75, 0.00, 0.32, 0.67, 1.62, 2.21,
                              2.60, 2.69, 2.80, 2.55};
static const double PK[17] = {0, 2.3, 5.2, 7.9, 9.4, 11.6, 13.5, 14.7, 16.4, 17.9, 18.8, 20.1, 21.3, 22.1, 22.7, 23.5,
                              23.7};
static void ref_cy(const int16_t *p, double *y, uint32_t n)
{
    double DEC = p[1], RICH = p[2], TOP = p[3], TTUN = p[4], SIZE = p[5], PEAK = p[6], W[7], pr;
    double x, fr, gl, qh, ql, sz, s2, pf, qs, gs, pq, pa, pw, pal, pn0, pn1, pn2, pd0, pd2;
    double px1 = 0, px2 = 0, py1 = 0, py2 = 0, gt, gtt, rm, rt, dm, dt, em = 1, et = 1, g = 0.004377;
    int ph[7], i, k;
    bq_t hp, lp, th;
    uint32_t j;
    x = RICH / 8, i = x >= 15 ? 15 : (int)x, fr = i == 15 ? (RICH - 120) / 7 : x - i;
    gl = pow(10, (RG[i] + (RG[i + 1] - RG[i]) * fr) / 20);
    for (k = 0; k < 7; k++)
        W[k] = (RT[i][k] + (RT[i + 1][k] - RT[i][k]) * fr) * gl;
    bank_counts(&v0.ms.tm, ph);
    x = PEAK / 8, i = x >= 15 ? 15 : (int)x, fr = i == 15 ? (PEAK - 120) / 7 : x - i;
    pr = pow(10, (PK[i] + (PK[i + 1] - PK[i]) * fr) / 20);
    qh = 2.058 * pow(pr, 0.2615), ql = 1.813;
    sz = SIZE / 64, s2 = SIZE < 64 ? 0 : (SIZE - 64) / 63;
    pf = SIZE < 64 ? 5558 + (5230 - 5558) * sz : 5230 + (5399 - 5230) * s2;
    qs = SIZE < 64 ? 1.84 + (2.29 - 1.84) * sz : 2.29 + (0.92 - 2.29) * s2;
    gs = SIZE < 64 ? 0.76 + (1 - 0.76) * sz : 1 + (0.54 - 1) * s2;
    pq = (1.55 + 0.0058 * PEAK) * qs / 2.29;
    pa = pow(10, 20 * log10(pr) * gs / 40), pw = 2 * M_PI * pf / FS, pal = sin(pw) / (2 * pq);
    pn0 = 1 + pal * pa, pn1 = -2 * cos(pw), pn2 = 1 - pal * pa, pd0 = 1 + pal / pa, pd2 = 1 - pal / pa;
    bq_set(&hp, 1, 767 * pow(2, 1.589 * SIZE / 127), qh);
    bq_set(&lp, 0, 4497 * pow(2, 1.023 * SIZE / 127), ql);
    bq_set(&th, 1, 7438 * pow(2, 1.193 * TTUN / 127), 0.7071);
    gt = -0.077 + 1.1145 * TOP / 127, gtt = pow(10, 17.16 * (TTUN - 64) / 127 / 20);
    rm = 6.617 - 0.04162 * DEC, rt = 14.72 - 0.0734 * DEC;
    dm = exp(-rm / FS), dt = exp(-rt / FS);
    for (j = 0; j < n; j++) {
        double bank = bank_tick(ph, W), m0 = bq_run(&lp, bq_run(&hp, bank)), mn, top;
        mn = (pn0 * m0 + pn1 * px1 + pn2 * px2 - pn1 * py1 - pd2 * py2) / pd0;
        px2 = px1, px1 = m0, py2 = py1, py1 = mn;
        top = bq_run(&th, bank) * gt * gtt;
        y[j] = g * (mn * em + top * et);
        em *= dm;
        et *= dt;
    }
}

/* trx-cb: knobs TUNE DECAY ENH DAMP TONE BUMP PTCH (TUNE moves the tones and ENH's resonator); the tones from the
 * voice's phases, its noise (a white floor draw,
 * then a pink one while the hold or ENH's grit lasts, as the voice) */
static double p0, p1, p2, p3, p4, p5, p6, phx, phy;
static uint32_t tr;                                     /* the voice's draw this sample: the floor its high 16 bits,
                                                         * the pink its low 16 */
static double pink(void)
{
    double w = ((int32_t)(tr << 16) >> 16) / 32768.0, y;
    p0 = 0.99886 * p0 + w * 0.0555179; p1 = 0.99332 * p1 + w * 0.0750759; p2 = 0.96900 * p2 + w * 0.1538520;
    p3 = 0.86650 * p3 + w * 0.3104856; p4 = 0.55000 * p4 + w * 0.5329522; p5 = -0.7616 * p5 - w * 0.0168980;
    y = p0 + p1 + p2 + p3 + p4 + p5 + p6 + w * 0.5362;
    p6 = w * 0.115926;
    phy = 0.9789 * (phy + y - phx); phx = y;
    return phy * 0.11;
}
typedef struct { double s1, s2, g, k; } svf;
static void svf_set(svf *f, double fc, double q) { f->g = tan(M_PI * fc / FS); f->k = 1 / q; f->s1 = f->s2 = 0; }
static double svf_bp(svf *f, double x)
{
    double v1 = (f->s1 + f->g * (x - f->s2)) / (1 + f->g * (f->g + f->k)), v2 = f->s2 + f->g * v1;
    f->s1 = 2 * v1 - f->s1; f->s2 = 2 * v2 - f->s2;
    return f->k * v1;
}
static double tab5(const double *t, double v)
{
    int k = v >= 96 ? 3 : (int)(v / 32);
    double x0 = 32 * k, x1 = k == 3 ? 127 : x0 + 32;
    return t[k] + (t[k + 1] - t[k]) * (v - x0) / (x1 - x0);
}
static void ref_cb(const int16_t *p, double *y, uint32_t n)
{
    static const double BH[8] = {1, 0.141, 0.040, 0, 0.0137, 0.0180, 0.0158, 0.0127};
    static const int AHN[8] = {1, 3, 5, 7, 9, 11, 13, 15};
    static const double AH[8] = {0.009, 1, 0.97, 0.05, 0.101, 0.141, 0.138, 0.105};
    static const double ER[5] = {0, 3.542, 6.327, 6.766, 4.910}, EA[5] = {1, 0.668, 0.426, 0.234, 0.269},
                        EF[5] = {1079.1, 1079.1, 1072.3, 1053.9, 1046.5};
    double D = p[1], E = p[2], M = p[3], T = p[4], U = p[5];
    double fb = v0.ms.cb.ib * (FS / 4294967296.0), fa = v0.ms.cb.ia * (FS / 4294967296.0);   /* (PTCH: its own test) */
    double tau = 0.0234 * pow(2.0, (D - 64) / 16), hold = 0.008 * pow(2.0, (D - 64) / 15), lvl = 0.0707;
    double gb = lvl * (61.8 + T) / 125.8, ga = lvl * pow(10, -14.0 / 20) * (188 - T) / 124;
    double er = tab5(ER, E), ea = tab5(EA, E), ef = tab5(EF, E) * pow(2, p[0] / 12.0), m = 1 - pow(1 - M / 127, 2.4);
    double bump = 0.04 * U / 127, btau = 0.008, hlp = 1 - exp(-2 * M_PI * 5000 / FS), hl = 0, eatk = 0.020;
    double eg = 3.5 * (E < 24 ? E / 24 : 1), etau = 0.010, elp = 1 - exp(-2 * M_PI * 900 / FS), el = 0;
    double pb, pa;
    long i, pre = (long)(0.2 * FS), nh = (long)v0.ms.cb.nh;
    svf damp, damp2, enh;
    svf_set(&damp, fb, 50);
    svf_set(&damp2, fb, 50);
    svf_set(&enh, ef, 21.5);
    p0 = p1 = p2 = p3 = p4 = p5 = p6 = phx = phy = 0;
    trng = v0.rng;
    pb = fmod(v0.ms.cb.pb * (2 * M_PI / 4294967296.0) - pre * 2 * M_PI * fb / FS, 2 * M_PI);   /* at 0: the voice's */
    pa = fmod(v0.ms.cb.pa * (2 * M_PI / 4294967296.0) - pre * 2 * M_PI * fa / FS, 2 * M_PI);
    for (i = -pre; i < (long)n; i++) {
        double t = i / (double)FS, k = 1 + (i >= 0 ? bump * exp(-t / btau) : 0), b = 0, a = 0, dry, full, x, xf, e, yy, pk = 0;
        int h;
        for (h = 0; h < 8; h++)
            b += BH[h] * (h & 1 ? -1 : 1) * sin((2 * h + 1) * pb);
        for (h = 0; h < 8; h++)
            a += AH[h] * sin(AHN[h] * pa);
        pb += 2 * M_PI * fb * k / FS;
        pa += 2 * M_PI * fa * k / FS;
        dry = gb * b + ga * ea * a, full = gb * b + ga * a;
        x = m > 0 ? (1 - m) * dry + m * svf_bp(&damp, dry) : dry;
        xf = m > 0 ? (1 - m) * full + m * svf_bp(&damp2, full) : full;
        if (i < 0)
            continue;
        e = t < hold ? 1 : exp(-(t - hold) / tau);
        tr = noise32(&trng);
        yy = x * e + lvl * 0.012 * (((int32_t)tr >> 16) / 32768.0) * e;
        if (i < nh || (E > 0 && i < (long)CB_NG))
            pk = pink();
        if (i < nh) {
            hl += hlp * (pk - hl);
            yy += lvl * 0.45 * hl;
        }
        if (er > 0)
            yy += er * svf_bp(&enh, xf * e * (1 - exp(-t / eatk)));
        if (eg > 0 && i < (long)CB_NG) {
            el += elp * (pk - el);
            yy += lvl * eg * el * exp(-t / etau);
        }
        y[i] = yy;
    }
}

/* the voice against its twin over [0, n): the least-squares gain and the residual (dB re the twin) */
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
static double vs_twin(uint32_t mi, const int16_t *k, uint32_t n, double *gain)
{
    hit(mi, k, 127, n);
    if (mi == DM_CBTRX)
        ref_cb(k, ref, n);
    else if (mi == DM_CYTRX)
        ref_cy(k, ref, n);
    else
        ref_hat(k, mi == DM_OHTRX, ref, n);
    return resid(n, gain);
}

static void twin_set(uint32_t mi, const char *name, const int16_t (*K)[8], uint32_t nk, double secs)
{
    double worst = -999, g, g0 = 0, gw = 1;
    uint32_t i;
    for (i = 0; i < nk; i++) {
        double e = vs_twin(mi, K[i], SECS(secs), &g);
        if (!i)
            g0 = g;
        if (fabs(g / g0 - 1) > fabs(gw - 1))
            gw = g / g0;
        if (e > worst)
            worst = e;
        if (e > -30)
            printf("     knobs %d %d %d %d %d %d %d: residual %.1f dB\n", K[i][0], K[i][1], K[i][2], K[i][3], K[i][4],
                   K[i][5], K[i][6], e);
    }
    printf("     %s vs its float twin (%u settings, %.1f s): worst residual %.1f dB, gain %.0f / unit (spread %.2f %%)\n",
           name, nk, nk ? secs : 0, worst, g0, 100 * (gw - 1));
    {
        char b[160];
        snprintf(b, sizeof b, "%s: every setting matches its float twin (residual < -30 dB, one gain within 1 %%)", name);
        check(b, worst < -30 && fabs(gw - 1) < 0.01);
    }
}

static void test_twins(void)
{
    static const int16_t H[][8] = {{0, 64, 64, 0, 127, 64}, {0, 0, 64, 0, 127, 64},   {0, 127, 64, 0, 127, 64},
                                   {0, 64, 0, 0, 127, 64},  {0, 64, 127, 0, 127, 64}, {0, 64, 64, 64, 127, 64},
                                   {0, 64, 64, 127, 127, 64}, {0, 64, 64, 0, 0, 64},  {0, 64, 64, 0, 64, 64},
                                   {0, 64, 64, 0, 127, 0},  {0, 64, 64, 0, 127, 32},  {0, 64, 64, 0, 127, 127},
                                   {0, 96, 127, 127, 0, 100}};
    static const int16_t C[][8] = {{0, 64, 64, 64, 64, 64, 0},   {0, 64, 0, 64, 64, 64, 0},   {0, 64, 32, 64, 64, 64, 0},
                                   {0, 64, 127, 64, 64, 64, 0},  {0, 0, 64, 64, 64, 64, 0},   {0, 127, 64, 64, 64, 64, 0},
                                   {0, 64, 64, 0, 64, 64, 0},    {0, 64, 64, 127, 64, 64, 0}, {0, 64, 64, 64, 0, 64, 0},
                                   {0, 64, 64, 64, 127, 64, 0},  {0, 64, 64, 64, 64, 0, 0},   {0, 64, 64, 64, 64, 127, 0},
                                   {0, 64, 64, 64, 64, 64, 64},  {0, 64, 64, 64, 64, 64, 127}, {0, 64, 64, 64, 64, 0, 127},
                                   {0, 64, 64, 64, 64, 127, 127}, {0, 100, 127, 127, 127, 32, 96}};
    static const int16_t B[][8] = {{0, 64, 0, 0, 64, 0, 64},   {0, 0, 0, 0, 64, 0, 64},   {0, 32, 0, 0, 64, 0, 64},
                                   {0, 96, 0, 0, 64, 0, 64},   {0, 127, 0, 0, 64, 0, 64}, {0, 64, 64, 0, 64, 0, 64},
                                   {0, 64, 127, 0, 64, 0, 64}, {0, 64, 16, 0, 64, 0, 64}, {0, 64, 0, 32, 64, 0, 64},
                                   {0, 64, 0, 64, 64, 0, 64},  {0, 64, 0, 127, 64, 0, 64}, {0, 64, 0, 0, 0, 0, 64},
                                   {0, 64, 0, 0, 127, 0, 64},  {0, 64, 0, 0, 64, 64, 64}, {0, 64, 0, 0, 64, 127, 64},
                                   {0, 64, 0, 0, 64, 0, 0},    {0, 64, 0, 0, 64, 0, 127}, {0, 100, 90, 60, 30, 127, 20},
                                   {0, 127, 64, 0, 64, 0, 64}, {24, 64, 64, 64, 64, 64, 64}};
    twin_set(DM_CHTRX, "CHTRX", H, sizeof H / sizeof H[0], 0.3);
    twin_set(DM_OHTRX, "OHTRX", H, sizeof H / sizeof H[0], 0.6);   /* (later: under -45 dB, its last bits) */
    twin_set(DM_CYTRX, "CYTRX", C, sizeof C / sizeof C[0], 1.0);
    twin_set(DM_CBTRX, "CBTRX", B, sizeof B / sizeof B[0], 0.3);
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

/* the RMS over the hit: from its start to its last sample above -40 dB re its peak */
static double hit_rms(uint32_t n)
{
    int32_t pk = 0;
    uint32_t i, last = 0;
    for (i = 0; i < n; i++)
        pk = abs(wl[i]) > pk ? abs(wl[i]) : pk;
    for (i = 0; i < n; i++)
        if (abs(wl[i]) > pk / 100)
            last = i;
    return rms(wl, 0, last + 1);
}

/* the levels at the defaults (RMS over the hit) within +-3 dB of the existing model of the same instrument */
static void test_levels(void)
{
    static const uint32_t L[4][2] = {{DM_CHTRX, DM_HATC}, {DM_OHTRX, DM_HATO}, {DM_CYTRX, DM_CYMB}, {DM_CBTRX, DM_COWB}};
    static const int16_t DEF[8] = {-99, -99, -99, -99, -99, -99, -99, -99};
    uint32_t i, ok = 1;
    for (i = 0; i < 4u; i++) {
        double a, b;
        hit(L[i][0], DEF, 127, SECS(3));
        a = hit_rms(SECS(3));
        hit(L[i][1], DEF, 127, SECS(3));
        b = hit_rms(SECS(3));
        printf("     %s vs %s at the defaults (RMS over the hit, to -40 dB): %+.1f dB\n", N_MODEL[L[i][0]], N_MODEL[L[i][1]],
               20 * log10(a / b));
        ok &= fabs(20 * log10(a / b)) <= 3;
    }
    check("levels: CHTRX ~ HATC, OHTRX ~ HATO, CYTRX ~ CYMB, CBTRX ~ COWB at the defaults (+-3 dB)", ok);
}

/* a decay's time constant from the RMS of two windows (w s) at t0 and t0 + dt (s) */
static double tau_w(double t0, double dt, double w)
{
    return dt / log(rms(wl, SECS(t0), SECS(t0) + SECS(w)) / rms(wl, SECS(t0 + dt), SECS(t0 + dt) + SECS(w)));
}
static double tau_at(double t0, double dt) { return tau_w(t0, dt, 0.02); }

/* the hats: TUNE moves the bank by semitones (the 102-sample square's 11th, 4756.4 Hz, an octave up at +12, and a
 * step's note the same); GAP's tau after the hold (CH 16.5 ms x 2 ^ (GAP / 61), OH 16.4 ms x 2 ^ (GAP / 30.4));
 * OH's long hold dips to silence at 540 ms and comes back */
static void test_hat_laws(void)
{
    static const int16_t T0[8] = {0, 127, 127, 0, 127, 127}, T12[8] = {12, 127, 127, 0, 127, 127};
    static const int G[3] = {0, 64, 127};
    double a, off, b, boff, w[6], dip, before, after;
    uint32_t i, ok = 1;
    hit(DM_CHTRX, T0, 127, SECS(0.2));
    a = goertzel(wl, SECS(0.18), 44100.0 / 102 * 11);
    off = fmax(goertzel(wl, SECS(0.18), 44100.0 / 102 * 11 * 1.01), goertzel(wl, SECS(0.18), 44100.0 / 102 * 11 * 0.99));
    hit(DM_CHTRX, T12, 127, SECS(0.2));
    b = goertzel(wl, SECS(0.18), 44100.0 / 102 * 22);
    boff = fmax(goertzel(wl, SECS(0.18), 44100.0 / 102 * 22 * 1.01), goertzel(wl, SECS(0.18), 44100.0 / 102 * 22 * 0.99));
    printf("     the 102-sample square's 11th: TUNE 0 %.1f dB over +-1 %%, TUNE +12 an octave up %.1f dB over\n",
           20 * log10(a / off), 20 * log10(b / boff));
    check("CHTRX: TUNE 0 has the bank's lines (k x 44100 / 102 Hz), TUNE +12 an octave up", a > 10 * off && b > 10 * boff);
    for (i = 0; i < 3u; i++) {
        int16_t k[8] = {0, 0, 0, 0, 127, 127};
        double tc = 0.0165 * pow(2, G[i] / 61.0), to = 0.0164 * pow(2, G[i] / 30.4);
        k[2] = (int16_t)G[i];
        hit(DM_CHTRX, k, 127, SECS(1.5));
        w[i] = tau_at(0.01, tc * 1.5) / tc;
        hit(DM_OHTRX, k, 127, SECS(2.5));
        w[3 + i] = tau_at(0.03, to * 1.5) / to;
        ok &= fabs(w[i] - 1) < 0.1 && fabs(w[3 + i] - 1) < 0.1;
    }
    printf("     GAP 0 / 64 / 127 heard: CH tau %.2f / %.2f / %.2f, OH %.2f / %.2f / %.2f x the laws\n", w[0], w[1], w[2],
           w[3], w[4], w[5]);
    check("CHTRX, OHTRX: GAP's decay follows the measured laws within 10 %", ok);
    {
        static const int16_t D[8] = {0, 127, 64, 0, 127, 127};
        hit(DM_OHTRX, D, 127, SECS(1.0));
        before = rms(wl, SECS(0.30), SECS(0.32));
        dip = rms(wl, SECS(0.535), SECS(0.545));
        after = rms(wl, SECS(0.79), SECS(0.81));
        printf("     OH DECAY 127: 300 ms %.1f dB, 540 ms %.1f dB, 800 ms %.1f dB\n", 20 * log10(before),
               20 * log10(dip + 1e-9), 20 * log10(after));
        check("OHTRX: DECAY 127 dips to silence at 540 ms and comes back before its knee (as measured)",
              dip < before / 10 && after > dip * 10);
    }
}

/* the cymbal's main decay: 1 / tau = 6.62 - 0.0416 DECAY per second (TOP 0: the main path) */
static void test_cy_laws(void)
{
    static const int D[3] = {0, 64, 127};
    double w[3];
    uint32_t i, ok = 1;
    for (i = 0; i < 3u; i++) {
        int16_t k[8] = {0, 0, 64, 0, 64, 64, 0};
        double want = 1 / (6.617 - 0.04162 * D[i]);
        k[1] = (int16_t)D[i];
        hit(DM_CYTRX, k, 127, SECS(3.0));
        w[i] = tau_w(0.4, want, 0.2) / want;            /* (200 ms: the bank's beating) */
        ok &= fabs(w[i] - 1) < 0.1;
    }
    printf("     CY DECAY 0 / 64 / 127 heard: tau %.2f / %.2f / %.2f x the law\n", w[0], w[1], w[2]);
    check("CYTRX: DECAY's decay follows 1 / tau = 6.62 - 0.0416 DECAY within 10 %", ok);
}

/* the cowbell: B 1117.3 Hz (DAMP 127 keeps it), PTCH +0.37 % over the knob, TUNE +12 an octave; TONE B +9.7 dB, A's
 * 3rd -9.8 dB over 0..127; DECAY's tau 23.4 ms x 2 ^ ((DECAY - 64) / 16) after its hold */
static void test_cb_laws(void)
{
    static const int16_t B0[8] = {0, 127, 0, 127, 127, 0, 64}, P0[8] = {0, 127, 0, 127, 127, 0, 0},
                         P1[8] = {0, 127, 0, 127, 127, 0, 127}, TU[8] = {12, 127, 0, 127, 127, 0, 64};
    double f, f0, f1, fu, b0, b1, a0, a1, w[2];
    uint32_t i;
    hit(DM_CBTRX, B0, 127, SECS(0.12));
    f = zc_freq(wl, SECS(0.01), SECS(0.12));
    hit(DM_CBTRX, P0, 127, SECS(0.12));
    f0 = zc_freq(wl, SECS(0.01), SECS(0.12));
    hit(DM_CBTRX, P1, 127, SECS(0.12));
    f1 = zc_freq(wl, SECS(0.01), SECS(0.12));
    hit(DM_CBTRX, TU, 127, SECS(0.12));
    fu = zc_freq(wl, SECS(0.01), SECS(0.12));
    printf("     B: %.1f Hz (PTCH 0 / 127: %.1f / %.1f; TUNE +12: %.1f)\n", f, f0, f1, fu);
    check("CBTRX: B 1117.3 Hz (+-0.3 %), PTCH 0..127 +0.37 % (+-0.1), TUNE +12 an octave (+-0.3 %)",
          fabs(f / 1117.27 - 1) < 0.003 && fabs(f1 / f0 - 1.0037) < 0.001 && fabs(fu / f / 2 - 1) < 0.003);
    {
        int16_t k[8] = {0, 127, 0, 0, 0, 0, 64};
        hit(DM_CBTRX, k, 127, SECS(0.12));
        b0 = goertzel(wl + SECS(0.01), SECS(0.1), 1117.27);
        a0 = goertzel(wl + SECS(0.01), SECS(0.1), 3 * 350.48);
        k[4] = 127;
        hit(DM_CBTRX, k, 127, SECS(0.12));
        b1 = goertzel(wl + SECS(0.01), SECS(0.1), 1117.27);
        a1 = goertzel(wl + SECS(0.01), SECS(0.1), 3 * 350.48);
    }
    printf("     TONE 0 -> 127: B %+.1f dB, A's 3rd %+.1f dB\n", 20 * log10(b1 / b0), 20 * log10(a1 / a0));
    check("CBTRX: TONE crossfades as measured (B +9.7, A -9.8 dB, +-1 dB)",
          fabs(20 * log10(b1 / b0) - 9.7) < 1 && fabs(20 * log10(a1 / a0) + 9.8) < 1);
    for (i = 0; i < 2u; i++) {
        int d = i ? 96 : 64;
        int16_t k[8] = {0, 0, 0, 127, 64, 0, 64};
        double want = 0.0234 * pow(2, (d - 64) / 16.0), hold = 0.008 * pow(2, (d - 64) / 15.0);
        k[1] = (int16_t)d;
        hit(DM_CBTRX, k, 127, SECS(1.0));
        w[i] = tau_at(hold + 0.005, want) / want;
    }
    printf("     DECAY 64 / 96 heard: tau %.2f / %.2f x the law\n", w[0], w[1]);
    check("CBTRX: DECAY's tau follows 23.4 ms x 2 ^ ((DECAY - 64) / 16) within 10 %", fabs(w[0] - 1) < 0.1 &&
          fabs(w[1] - 1) < 0.1);
}

/* every knob at its ends (TUNE -24 / +24), a step's note +-24, velocity 1 / 127: bounded, the voice ends in 6 s */
static void test_extremes(void)
{
    static const uint32_t M[4] = {DM_CBTRX, DM_CHTRX, DM_OHTRX, DM_CYTRX}, NK[4] = {7, 6, 6, 7};
    uint32_t j, c, i, ok = 1, ended = 1;
    for (j = 0; j < 4u; j++)
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
            if (trk[0].v[0].active) {
                printf("     %s: knobs %u, note %+d: still sounding at 6 s\n", N_MODEL[M[j]], c, trk[0].nt);
                ended = 0;
            }
        }
    check("every knob at its ends, notes +-24, velocity 1 / 127: bounded, the voice ends within 6 s (the four models)",
          ok && ended);
}

/* re-hits 5 ms apart at every knob 127 and a switch to each model while another rings: bounded */
static void test_rehits(void)
{
    static const uint32_t M[4] = {DM_CBTRX, DM_CHTRX, DM_OHTRX, DM_CYTRX};
    static const int16_t K[8] = {0, 127, 127, 127, 127, 127, 127, 127};
    uint32_t j, i, ok = 1;
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
        old_set_model(&trk[0], DM_CYMB);
        drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
        render_track(&trk[0], wl, SECS(0.05));
        old_set_model(&trk[0], M[j]);
        drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
        render_track(&trk[0], wl, SECS(0.3));
        for (i = 0; i < SECS(0.3); i++)
            ok &= abs(wl[i]) <= 4 * VOICE_FS;
    }
    check("re-hits 5 ms apart and a model switch while ringing: bounded (the four models)", ok);
}

/* the hats' choke: CHTRX and OHTRX start in choke group 1 (as HATC / HATO): a closed hat cuts a ringing open one */
static void test_choke(void)
{
    host_init();
    old_set_model(&trk[1], DM_OHTRX);
    old_set_model(&trk[2], DM_CHTRX);
    old_set_model(&trk[3], DM_CYTRX);
    drum_hit(&trk[1], 127);
    render_track(&trk[1], wl, SECS(0.05));
    drum_hit(&trk[2], 127);
    check("CHTRX / OHTRX in choke group 1 (CYTRX and CBTRX none): a closed hat cuts the open one",
          trk[1].p[P_CHOKE] == 1 && trk[2].p[P_CHOKE] == 1 && !trk[1].v[0].active && trk[3].p[P_CHOKE] == 0 &&
              DMODELS[DM_CBTRX].choke == 0);
}

/* the A/B set: the emulator renders' names and knobs (the machines' own knobs; TUNE 0), the voice / its twin gain */
static void write_wav(const char *path, uint32_t n, double g)
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
static void write_wavs(const char *dir)
{
    static const struct { uint32_t mi; const char *pre, *name; int16_t k[8]; } W[] = {
        {DM_CHTRX, "ch", "gap_064", {0, 64, 64, 0, 127, 64}}, {DM_CHTRX, "ch", "gap_000", {0, 64, 0, 0, 127, 64}},
        {DM_CHTRX, "ch", "gap_127", {0, 64, 127, 0, 127, 64}}, {DM_CHTRX, "ch", "dec_000", {0, 0, 64, 0, 127, 64}},
        {DM_CHTRX, "ch", "dec_127", {0, 127, 64, 0, 127, 64}}, {DM_CHTRX, "ch", "hpf_064", {0, 64, 64, 64, 127, 64}},
        {DM_CHTRX, "ch", "hpf_127", {0, 64, 64, 127, 127, 64}}, {DM_CHTRX, "ch", "lpf_000", {0, 64, 64, 0, 0, 64}},
        {DM_CHTRX, "ch", "lpf_064", {0, 64, 64, 0, 64, 64}}, {DM_CHTRX, "ch", "mtal_000", {0, 64, 64, 0, 127, 0}},
        {DM_CHTRX, "ch", "mtal_032", {0, 64, 64, 0, 127, 32}}, {DM_CHTRX, "ch", "mtal_127", {0, 64, 64, 0, 127, 127}},
        {DM_OHTRX, "oh", "gap_064", {0, 64, 64, 0, 127, 64}}, {DM_OHTRX, "oh", "gap_000", {0, 64, 0, 0, 127, 64}},
        {DM_OHTRX, "oh", "gap_127", {0, 64, 127, 0, 127, 64}}, {DM_OHTRX, "oh", "dec_000", {0, 0, 64, 0, 127, 64}},
        {DM_OHTRX, "oh", "dec_096", {0, 96, 64, 0, 127, 64}}, {DM_OHTRX, "oh", "dec_127", {0, 127, 64, 0, 127, 64}},
        {DM_OHTRX, "oh", "hpf_127", {0, 64, 64, 127, 127, 64}}, {DM_OHTRX, "oh", "lpf_000", {0, 64, 64, 0, 0, 64}},
        {DM_OHTRX, "oh", "mtal_000", {0, 64, 64, 0, 127, 0}}, {DM_OHTRX, "oh", "mtal_127", {0, 64, 64, 0, 127, 127}},
        {DM_CYTRX, "cy", "rich_064", {0, 64, 64, 64, 64, 64, 0}}, {DM_CYTRX, "cy", "rich_000", {0, 64, 0, 64, 64, 64, 0}},
        {DM_CYTRX, "cy", "rich_032", {0, 64, 32, 64, 64, 64, 0}}, {DM_CYTRX, "cy", "rich_127", {0, 64, 127, 64, 64, 64, 0}},
        {DM_CYTRX, "cy", "dec_000", {0, 0, 64, 64, 64, 64, 0}}, {DM_CYTRX, "cy", "dec_127", {0, 127, 64, 64, 64, 64, 0}},
        {DM_CYTRX, "cy", "top_000", {0, 64, 64, 0, 64, 64, 0}}, {DM_CYTRX, "cy", "top_127", {0, 64, 64, 127, 64, 64, 0}},
        {DM_CYTRX, "cy", "ttun_000", {0, 64, 64, 64, 0, 64, 0}}, {DM_CYTRX, "cy", "ttun_127", {0, 64, 64, 64, 127, 64, 0}},
        {DM_CYTRX, "cy", "size_000", {0, 64, 64, 64, 64, 0, 0}}, {DM_CYTRX, "cy", "size_127", {0, 64, 64, 64, 64, 127, 0}},
        {DM_CYTRX, "cy", "peak_064", {0, 64, 64, 64, 64, 64, 64}}, {DM_CYTRX, "cy", "peak_127", {0, 64, 64, 64, 64, 64, 127}},
        {DM_CBTRX, "cb", "tone_064", {0, 64, 0, 0, 64, 0, 64}}, {DM_CBTRX, "cb", "dec_000", {0, 0, 0, 0, 64, 0, 64}},
        {DM_CBTRX, "cb", "dec_032", {0, 32, 0, 0, 64, 0, 64}}, {DM_CBTRX, "cb", "dec_096", {0, 96, 0, 0, 64, 0, 64}},
        {DM_CBTRX, "cb", "dec_127", {0, 127, 0, 0, 64, 0, 64}}, {DM_CBTRX, "cb", "enh_064", {0, 64, 64, 0, 64, 0, 64}},
        {DM_CBTRX, "cb", "enh_127", {0, 64, 127, 0, 64, 0, 64}}, {DM_CBTRX, "cb", "damp_032", {0, 64, 0, 32, 64, 0, 64}},
        {DM_CBTRX, "cb", "damp_064", {0, 64, 0, 64, 64, 0, 64}}, {DM_CBTRX, "cb", "damp_127", {0, 64, 0, 127, 64, 0, 64}},
        {DM_CBTRX, "cb", "tone_000", {0, 64, 0, 0, 0, 0, 64}}, {DM_CBTRX, "cb", "tone_127", {0, 64, 0, 0, 127, 0, 64}},
        {DM_CBTRX, "cb", "ptch_000", {0, 64, 0, 0, 64, 0, 0}}, {DM_CBTRX, "cb", "ptch_127", {0, 64, 0, 0, 64, 0, 127}},
        {DM_CBTRX, "cb", "bump_064", {0, 64, 0, 0, 64, 64, 64}}, {DM_CBTRX, "cb", "bump_127", {0, 64, 0, 0, 64, 127, 64}}};
    uint32_t w, j;
    for (w = 0; w < sizeof W / sizeof W[0]; w++) {
        char path[512];
        double g;
        int16_t k0[8];
        for (j = 0; j < 8u; j++)
            k0[j] = W[w].k[j];
        vs_twin(W[w].mi, k0, SECS(0.3), &g);           /* the firmware's units per prototype unit */
        hit(W[w].mi, k0, 127, SECS(2.0));
        snprintf(path, sizeof path, "%s/%s_%s.wav", dir, W[w].pre, W[w].name);
        write_wav(path, SECS(2.0), g);
    }
    printf("     wrote %u WAVs to %s\n", (unsigned)(sizeof W / sizeof W[0]), dir);
}

/* review fix (2026-10-18): TUNE +24 with a step's note +24 (pitch +48 semitones) and the heaviest knobs: no signed
 * overflow (this test runs under UBSan), bounded, ends within 6 s; every MODEL_ of this file at TUNE +-24, notes +-24 */
static void test_note48(uint32_t model, const int16_t *k, const char *what)
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
        ok &= !trk[0].v[0].active;
    }
    check(what, ok);
}

int main(int argc, char **argv)
{
    if (argc > 9 && !strcmp(argv[1], "-k")) {         /* (debug: -k MODEL k0..k7 SECS: one setting vs its twin) */
        int16_t k[8];
        double g, e;
        uint32_t i;
        for (i = 0; i < 8u; i++)
            k[i] = (int16_t)atoi(argv[3 + i]);
        e = vs_twin((uint32_t)atoi(argv[2]), k, SECS(atof(argv[11])), &g);
        {
            double pk = 0;
            for (i = 0; i < SECS(atof(argv[11])); i++)
                pk = fmax(pk, fabs(ref[i]));
            printf("residual %.1f dB, gain %.0f, twin peak %.4f\n", e, g, pk);
            if (getenv("TWINWAV")) {                    /* (the twin and the voice, both in the twin's units) */
                char pth[600];
                uint32_t nn = SECS(atof(argv[11]));
                write_wav(getenv("TWINWAV"), nn, g);
                for (i = 0; i < nn; i++)
                    wl[i] = (int32_t)lrint(ref[i] * 1e9);
                snprintf(pth, sizeof pth, "%s.twin.wav", getenv("TWINWAV"));
                write_wav(pth, nn, 1e9);
            }
            if (getenv("WIN")) {
                uint32_t a, w = SECS(0.01);
                for (a = 0; a + w <= SECS(atof(argv[11])); a += w) {
                    double ee = 0, rr = 1e-30;
                    for (i = a; i < a + w; i++) {
                        ee += (wl[i] - g * ref[i]) * (wl[i] - g * ref[i]);
                        rr += g * g * ref[i] * ref[i];
                    }
                    printf("  %3u ms: %6.1f dB (level %.1f dB)\n", a * 1000 / (uint32_t)FS, 10 * log10(ee / rr + 1e-30), 10 * log10(rr / w));
                }
            }
        }
        return 0;
    }
    test_twins();
    test_levels();
    test_hat_laws();
    test_cy_laws();
    test_cb_laws();
    test_extremes();
    test_rehits();
    test_choke();
    if (argc > 1)
        write_wavs(argv[1]);
        {
        static const int16_t K[8] = {24, 127, 127, 127, 127, 127, 127, 0};
        test_note48(DM_CBTRX, K, "CBTRX: TUNE +-24 with notes +-24, DAMP TONE BUMP ENH 127: no overflow, bounded, ends");
    }
    printf(fails ? "trxmetal_test: %d FAILED\n" : "trxmetal_test: all passed\n", fails);
    return fails != 0;
}
