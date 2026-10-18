/* KTRX (dm_ktrx.c): the fixed-point voice against its float reference (the md-re prototype v4, copied below), the
 * measured laws (pitch, DECAY), NOISE, SUB, the level, the extremes. Each test prints one line; any failure exits 1.
 *   ktrx_test [OUTDIR]   OUTDIR: also writes the A/B set as float WAVs in the prototype's units (md-re renders' names) */
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

static double tau_k(uint32_t k) { return -1 / (FS * log(k / 65536.0)); }   /* a Q16 per-sample factor's tau, s */
static double tau_d32(uint32_t d) { return -1 / (FS * log(1 - d / 4294967296.0)); }
static double tau_dec(int d) { return tau_d32(0xFFFFFFFFu / ktrx_tau(2293u, (d - 64) * 1518 / 100, 309u)); }
static double tau_sdec(int d) { return tau_d32(0xFFFFFFFFu / ktrx_tau(4322u, (d - 72) * 1427 / 100, 221u)); }

/* the prototype (md-re proto/trxbd.c v4) in float, with the firmware's constants as they are rounded (the decays from
 * their Q16 factors, the levels Q15) and TUNE in semitones; NOISE left out (its own noise). The laws themselves:
 * test_ktrx_laws. */
static void ref_render(const int16_t *p, double *y, uint32_t n)
{
    double f0 = inc_tune(HZ(61.4), p[0]) * (FS / 4294967296.0);   /* the voice's own pitch (Hortator's pitch table:
                                                                   * TUNE's accuracy is test_ktrx_laws') */
    double sc = pow(2, -p[0] / 24.0), R = p[2], S = p[4];
    double H = p[6], C = p[7];
    double afix = KTRX_DROP_HZ * sc, tfix = tau_k(KTRX_K_DROP), afast = KTRX_FAST_HZ * sc, tfast = tau_k(KTRX_K_FAST);
    double tr_a = KTRX_TR / 32768.0, tr_f = KTRX_TR_HZ, tr_tau = tau_k(KTRX_K_TR), atk = tau_k(KTRX_K_ATK);
    double aramp = 679.0 * pow(R / 127, 3) * (f0 / 61.4), tr = tau_sdec(p[3]), ta = tau_dec(p[1]);
    double amp = KTRX_AMP / 32768.0, sq = S / 127, sqT = 1 / 156.0;
    double th = 1 - 402 / 32768.0 * (C - 64), cgain = 1;
    double hl = H / 127 * 0.285, ph = 0;
    int first = 1;
    uint32_t i;
    if (C > 64) {                                       /* the measured gain, between its table points */
        double c = C - 64, x = c >= 56 ? 7 : floor(c / 8), f = x == 7 ? (c - 56) / 7 : c / 8 - x;
        cgain = (KTRX_CLIPG[(int)x] + (KTRX_CLIPG[(int)x + 1] - KTRX_CLIPG[(int)x]) * f) / 4096;
    }
    for (i = 0; i < n; i++) {
        double t = i / (double)FS, fr = f0 + afix * exp(-t / tfix) + afast * exp(-t / tfast) + aramp * exp(-t / tr);
        double body = -sin(ph), env = exp(-t / ta), v;
        if (hl > 0) {
            double h = 0.5 * (ph + M_PI);
            body += hl * exp(-t / tau_k(KTRX_K_SUB)) * (sin(h + 1.2) + 0.25 * sin(3 * h + 0.75) + 0.05 * sin(5 * h + 0.95));
        }
        ph += 2 * M_PI * fr / FS;
        if (ph >= 2 * M_PI)
            first = 0;
        if (!first && C > 64)
            v = fmax(-th, fmin(th, body)) * cgain * amp;
        else
            v = body * amp;
        if (t < sqT && sq > 0)
            v = (1 - sq) * v + sq * (t < sqT / 2 ? -0.30 : 0.47) * exp(-t / 0.02);
        v *= env * (1 - exp(-t / atk));
        v += -tr_a * sin(2 * M_PI * tr_f * t) * exp(-t / tr_tau);
        y[i] = v;
    }
}

/* track 0 = KTRX with knobs k (-99: the old default), one hit at vel, frames rendered into wl */
static void kt_hit(const int16_t *k, uint32_t vel, uint32_t frames)
{
    uint32_t i;
    host_init();
    old_set_model(&trk[0], DM_KTRX);
    for (i = 0; i < 8u; i++)
        if (k[i] != -99)
            trk[0].p[P_E0 + i] = k[i];
    drum_play(&trk[0], vel, trk[0].nt);   /* (drum_hit plays note 0) */
    render_track(&trk[0], wl, frames);
}

/* the firmware against the reference over [0, n): the least-squares gain and the residual (dB re the reference) */
static double vs_ref(const int16_t *k, uint32_t n, double *gain)
{
    double xy = 0, yy = 0, e = 0, r = 1e-30;
    uint32_t i;
    kt_hit(k, 127, n);
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

static void test_ktrx_vs_ref(void)
{
    static const int16_t K[][8] = {{0, 64, 0, 64, 0, 0, 0, 0},     {-24, 64, 0, 64, 0, 0, 0, 0},
                                   {24, 64, 0, 64, 0, 0, 0, 0},    {0, 96, 0, 64, 0, 0, 0, 0},
                                   {0, 20, 0, 64, 0, 0, 0, 0},     {0, 64, 64, 32, 0, 0, 0, 0},
                                   {0, 64, 127, 0, 0, 0, 0, 0},    {0, 64, 127, 127, 0, 0, 0, 0},
                                   {12, 64, 100, 64, 0, 0, 0, 0},  {0, 64, 0, 64, 127, 0, 0, 0},
                                   {0, 64, 0, 64, 64, 0, 0, 0},    {0, 96, 0, 64, 0, 0, 127, 0},
                                   {-12, 96, 0, 64, 0, 0, 64, 0},  {0, 64, 0, 64, 0, 0, 0, 127},
                                   {0, 64, 0, 64, 0, 0, 0, 96},    {0, 64, 0, 64, 0, 0, 0, 64},
                                   {-24, 100, 127, 127, 127, 0, 127, 127}};
    double worst = -999, worst_clip = -999, g, g0 = 0, gw = 1;
    uint32_t i;
    for (i = 0; i < sizeof K / sizeof K[0]; i++) {
        double e = vs_ref(K[i], SECS(0.3), &g);
        if (!i)
            g0 = g;
        if (fabs(g / g0 - 1) > fabs(gw - 1))
            gw = g / g0;
        if (K[i][7] > 64) {                             /* CLIP: a hard square's edges move by a sample on the
                                                         * sine's last bits (at 15 Hz one sample is ~-27 dB alone) */
            if (e > worst_clip)
                worst_clip = e;
        } else if (e > worst)
            worst = e;
        if (e > (K[i][7] > 64 ? -20 : -30))
            printf("     knobs %d %d %d %d %d %d %d %d: residual %.1f dB\n", K[i][0], K[i][1], K[i][2], K[i][3], K[i][4],
                   K[i][5], K[i][6], K[i][7], e);
    }
    printf("     vs the float reference (17 settings, 0.3 s): worst residual %.1f dB (with CLIP %.1f dB), gain %.0f / unit "
           "(spread %.2f %%)\n", worst, worst_clip, g0, 100 * (gw - 1));
    check("KTRX: every setting matches the float reference (residual < -30 dB, CLIP's edges < -20 dB, one gain within "
          "1 %)", worst < -30 && worst_clip < -20 && fabs(gw - 1) < 0.01);
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

/* the measured laws: TUNE 0 = 61.4 Hz, +-12 an octave; DECAY tau = 52 ms x 1.55 ^ ((d - 64) / 8) */
static void test_ktrx_laws(void)
{
    static const int16_t T0[8] = {0, 110, 0, 64, 0, 0, 0, 0}, TU[8] = {12, 110, 0, 64, 0, 0, 0, 0},
                         TD[8] = {-12, 110, 0, 64, 0, 0, 0, 0};
    static const int D[3] = {48, 64, 96};
    double f, fu, fd, tau[3];
    uint32_t i;
    kt_hit(T0, 127, SECS(0.8));
    f = zc_freq(wl, SECS(0.3), SECS(0.8));
    kt_hit(TU, 127, SECS(0.8));
    fu = zc_freq(wl, SECS(0.3), SECS(0.8));
    kt_hit(TD, 127, SECS(0.8));
    fd = zc_freq(wl, SECS(0.3), SECS(0.8));
    printf("     TUNE -12 / 0 / +12: %.2f / %.2f / %.2f Hz\n", fd, f, fu);
    check("KTRX: TUNE 0 is 61.4 Hz (+-0.5 %), +-12 an octave", fabs(f / 61.4 - 1) < 0.005 &&
          fabs(fu / f / 2 - 1) < 0.005 && fabs(f / fd / 2 - 1) < 0.005);
    for (i = 0; i < 3u; i++) {
        int16_t k[8] = {0, 0, 0, 64, 0, 0, 0, 0};
        double want = 0.052 * pow(1.55, (D[i] - 64) / 8.0);
        k[1] = (int16_t)D[i];
        kt_hit(k, 127, SECS(1.2));
        tau[i] = (want * 2) / log(rms(wl, SECS(0.04), SECS(0.04) + SECS(0.05)) /
                                  rms(wl, SECS(0.04 + want * 2), SECS(0.04 + want * 2) + SECS(0.05)));
        tau[i] /= want;
    }
    printf("     DECAY 48 / 64 / 96 heard: tau %.2f / %.2f / %.2f x the law\n", tau[0], tau[1], tau[2]);
    check("KTRX: DECAY heard follows tau = 52 ms x 1.55 ^ ((d - 64) / 8) within 10 %", fabs(tau[0] - 1) < 0.1 &&
          fabs(tau[1] - 1) < 0.1 && fabs(tau[2] - 1) < 0.1);
    {
        double wd = 0, ws = 0;
        int d;
        for (d = 0; d <= 127; d++) {
            double ld = fmin(0.58, fmax(0.007, 0.052 * pow(1.55, (d - 64) / 8.0)));   /* (capped: 4 s to -60 dB) */
            double ls = fmin(0.58, fmax(0.005, 0.098 * pow(1.51, (d - 72) / 8.0)));
            wd = fmax(wd, fabs(tau_dec(d) / ld - 1));
            ws = fmax(ws, fabs(tau_sdec(d) / ls - 1));
        }
        printf("     DECAY / SDEC factors vs the laws, 0..127: worst %.1f %% / %.1f %%\n", 100 * wd, 100 * ws);
        check("KTRX: DECAY's and SDEC's factors follow the measured laws within 1 % over the whole knob (to 0.58 s)",
              wd < 0.01 && ws < 0.01);
    }
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

/* SUB 127: f0 / 2 at ~-12 dB re f0 early (the emulator: -12.5 dB over 60-200 ms), ~-20 dB by 0.6-0.9 s */
static void test_ktrx_sub(void)
{
    static const int16_t S[8] = {0, 96, 0, 64, 0, 0, 127, 0}, S0[8] = {0, 96, 0, 64, 0, 0, 0, 0};
    double e, l, z;
    kt_hit(S, 127, SECS(1.0));
    e = 20 * log10(goertzel(wl + SECS(0.06), SECS(0.14), 30.7) / goertzel(wl + SECS(0.06), SECS(0.14), 61.4));
    l = 20 * log10(goertzel(wl + SECS(0.6), SECS(0.3), 30.7) / goertzel(wl + SECS(0.6), SECS(0.3), 61.4));
    kt_hit(S0, 127, SECS(1.0));
    z = 20 * log10(goertzel(wl + SECS(0.06), SECS(0.14), 30.7) / goertzel(wl + SECS(0.06), SECS(0.14), 61.4));
    printf("     SUB 127: f0/2 %.1f dB early, %.1f dB late; SUB 0: %.1f dB\n", e, l, z);
    check("KTRX: SUB 127 adds f0/2 at -12 +-3 dB early, -20 +-3 dB late (fading on its own); SUB 0 none (< -40 dB)",
          fabs(e + 12) < 3 && fabs(l + 20) < 3 && z < -40);
}

/* NOISE: a burst at the hit (more energy above 3 kHz in the first 10 ms), gone by 100 ms */
static double hf_energy(const int32_t *x, uint32_t a, uint32_t b)   /* the first difference's energy */
{
    double d = 0;
    uint32_t i;
    for (i = a + 1; i < b; i++)
        d += (double)(x[i] - x[i - 1]) * (x[i] - x[i - 1]);
    return d;
}

static void test_ktrx_noise(void)
{
    static const int16_t N0[8] = {0, 64, 0, 64, 0, 0, 0, 0}, N1[8] = {0, 64, 0, 64, 0, 127, 0, 0};
    double a0, a1, b0, b1;
    kt_hit(N0, 127, SECS(0.2));
    a0 = hf_energy(wl, 0, SECS(0.01));
    b0 = hf_energy(wl, SECS(0.1), SECS(0.15));
    kt_hit(N1, 127, SECS(0.2));
    a1 = hf_energy(wl, 0, SECS(0.01));
    b1 = hf_energy(wl, SECS(0.1), SECS(0.15));
    printf("     NOISE 127: first 10 ms brightness x%.1f, at 100-150 ms x%.2f\n", a1 / a0, b1 / b0);
    check("KTRX: NOISE 127 adds a burst at the hit (x2 brightness), gone by 100 ms (within 5 %)",
          a1 > 2 * a0 && fabs(b1 / b0 - 1) < 0.05);
}

/* CLIP as measured (md-re tools/clipfit.py, the emulator over 100-300 ms): odd harmonics re f0 at 96 and 127 */
static void test_ktrx_clip(void)
{
    static const int C[2] = {96, 127};
    static const double H3[2] = {-15.1, -10.2}, H5[2] = {-59.5, -16.0}, H7[2] = {-31.0, -21.3};
    uint32_t i, ok = 1;
    for (i = 0; i < 2u; i++) {
        int16_t k[8] = {0, 64, 0, 64, 0, 0, 0, 0};
        double f, h3, h5, h7;
        k[7] = (int16_t)C[i];
        kt_hit(k, 127, SECS(0.3));
        f = goertzel(wl + SECS(0.1), SECS(0.2), 61.4);
        h3 = 20 * log10(goertzel(wl + SECS(0.1), SECS(0.2), 3 * 61.4) / f);
        h5 = 20 * log10(goertzel(wl + SECS(0.1), SECS(0.2), 5 * 61.4) / f);
        h7 = 20 * log10(goertzel(wl + SECS(0.1), SECS(0.2), 7 * 61.4) / f);
        printf("     CLIP %d: 3rd %.1f (%.1f), 5th %.1f (%.1f), 7th %.1f (%.1f) dB\n", C[i], h3, H3[i], h5, H5[i], h7, H7[i]);
        ok &= fabs(h3 - H3[i]) < 1.5 && fabs(h7 - H7[i]) < 3 && (H5[i] < -40 ? h5 < -40 : fabs(h5 - H5[i]) < 2);
    }
    check("KTRX: CLIP's harmonics as measured (3rd +-1.5 dB, 5th's null at 96, 7th +-3 dB)", ok);
}

/* NOISE's colour (the emulator's octave bands over the first 46 ms, as md-re measured them: 8 Hann frames of 256, hop
 * 128, from the onset, the bins' power summed per band): 8-16 kHz 5.7 dB under 4-8 kHz, 2-4 kHz 0.4 dB under */
static double band(const int32_t *x, double lo, double hi)
{
    double e = 0;
    uint32_t fr, b, i;
    for (fr = 0; fr < 8u; fr++)
        for (b = 0; b < 128u; b++) {
            double f = b * (double)FS / 256, re = 0, im = 0;
            if (f < lo || f >= hi)
                continue;
            for (i = 0; i < 256u; i++) {
                double w = x[128 * fr + i] * (0.5 - 0.5 * cos(2 * M_PI * i / 255));
                re += w * cos(2 * M_PI * b * i / 256);
                im -= w * sin(2 * M_PI * b * i / 256);
            }
            e += re * re + im * im;
        }
    return 10 * log10(e + 1e-30);
}

static void test_ktrx_noise_colour(void)
{
    static const int16_t N[8] = {0, 64, 0, 64, 0, 127, 0, 0};
    double b2, b4, b8;
    kt_hit(N, 127, SECS(0.1));
    b2 = band(wl, 2000, 4000);
    b4 = band(wl, 4000, 8000);
    b8 = band(wl, 8000, 16000);
    printf("     NOISE 127: 2-4 / 4-8 / 8-16 kHz: %+.1f / 0 / %+.1f dB (the emulator -0.4 / 0 / -5.7)\n", b2 - b4, b8 - b4);
    check("KTRX: NOISE's colour as measured (8-16 kHz -5.7 +-2 dB re 4-8, 2-4 kHz within 1.5 dB)",
          fabs(b8 - b4 + 5.7) < 2 && fabs(b2 - b4 + 0.4) < 1.5);
}

/* the level at the defaults within +-3 dB of K909's */
static void test_ktrx_level(void)
{
    static const int16_t DEF[8] = {-99, -99, -99, -99, -99, -99, -99, -99};
    double a, b;
    kt_hit(DEF, 127, SECS(0.3));
    a = rms(wl, 0, SECS(0.3));
    host_init();
    old_set_model(&trk[0], DM_K909);
    drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
    render_track(&trk[0], wl, SECS(0.3));
    b = rms(wl, 0, SECS(0.3));
    printf("     KTRX vs K909 at the defaults: %+.1f dB\n", 20 * log10(a / b));
    check("KTRX: the level at the defaults within +-3 dB of K909's", fabs(20 * log10(a / b)) <= 3);
}

/* the extremes: every knob at 0 / 127 (TUNE -24 / +24) and a step's note +-24, velocity 1 and 127: bounded, ends */
static void test_ktrx_extremes(void)
{
    uint32_t c, i, ok = 1, ended = 1;
    for (c = 0; c < 512u; c++) {
        int16_t k[8];
        for (i = 0; i < 8u; i++)
            k[i] = (int16_t)((c >> i & 1u) ? (i ? 127 : 24) : (i ? 0 : -24));
        host_init();
        old_set_model(&trk[0], DM_KTRX);
        for (i = 0; i < 8u; i++)
            trk[0].p[P_E0 + i] = k[i];
        trk[0].nt = (c >> 8 & 1u) ? 24 : -24;
        drum_play(&trk[0], (c & 3u) == 3u ? 1 : 127, trk[0].nt);   /* (drum_hit plays note 0) */
        render_track(&trk[0], wl, SECS(6));
        for (i = 0; i < SECS(6); i++)
            ok &= abs(wl[i]) <= 4 * VOICE_FS;
        ended &= !trk[0].v[0].active;
    }
    check("KTRX: every knob at its ends, notes +-24, velocity 1 / 127: bounded, the voice ends within 6 s",
          ok && ended);
}

/* re-hits 5 ms apart and a switch to KTRX while another model rings: bounded */
static void test_ktrx_rehits(void)
{
    static const int16_t K[8] = {0, 127, 127, 127, 127, 127, 127, 127};
    uint32_t i, ok = 1;
    kt_hit(K, 127, SECS(0.005));
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
    old_set_model(&trk[0], DM_KTRX);
    drum_play(&trk[0], 127, trk[0].nt);   /* (drum_hit plays note 0) */
    render_track(&trk[0], wl, SECS(0.3));
    for (i = 0; i < SECS(0.3); i++)
        ok &= abs(wl[i]) <= 4 * VOICE_FS;
    check("KTRX: re-hits 5 ms apart and a model switch while ringing: bounded", ok);
}

/* the A/B set: the md-re renders' names and knobs (PTCH 0 / 64 / 127 = TUNE -12 / 0 / +12 at 61.4 Hz) */
static void write_wavs(const char *dir)
{
    static const struct { const char *name; int16_t k[8]; } W[] = {
        {"ptch_000", {-12, 64, 0, 64, 0, 0, 0, 0}}, {"ptch_064", {0, 64, 0, 64, 0, 0, 0, 0}},
        {"ptch_127", {12, 64, 0, 64, 0, 0, 0, 0}},  {"ramp_064", {0, 64, 64, 64, 0, 0, 0, 0}},
        {"ramp_127", {0, 64, 127, 64, 0, 0, 0, 0}}, {"dec_096", {0, 96, 0, 64, 0, 0, 0, 0}},
        {"strt_127", {0, 64, 0, 64, 127, 0, 0, 0}}, {"nois_127", {0, 64, 0, 64, 0, 127, 0, 0}},
        {"nois_096", {0, 64, 0, 64, 0, 96, 0, 0}},
        {"clip_127", {0, 64, 0, 64, 0, 0, 0, 127}}, {"harm_064", {0, 96, 0, 64, 0, 0, 64, 0}},
        {"harm_127", {0, 96, 0, 64, 0, 0, 127, 0}}, {"harm_127_p000", {-12, 96, 0, 64, 0, 0, 127, 0}}};
    static const int16_t K0[8] = {0, 64, 0, 64, 0, 0, 0, 0};
    double g;
    uint32_t w, i, n = SECS(2.0);
    vs_ref(K0, SECS(0.3), &g);                       /* the firmware's units per prototype unit */
    for (w = 0; w < sizeof W / sizeof W[0]; w++) {
        char path[512];
        FILE *f;
        uint32_t v;
        snprintf(path, sizeof path, "%s/%s.wav", dir, W[w].name);
        f = fopen(path, "wb");
        if (!f)
            continue;
        kt_hit(W[w].k, 127, n);
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
    test_ktrx_vs_ref();
    test_ktrx_laws();
    test_ktrx_sub();
    test_ktrx_noise();
    test_ktrx_noise_colour();
    test_ktrx_clip();
    test_ktrx_level();
    test_ktrx_extremes();
    test_ktrx_rehits();
    if (argc > 1)
        write_wavs(argv[1]);
    printf(fails ? "ktrx_test: %d FAILED\n" : "ktrx_test: all passed\n", fails);
    return fails != 0;
}
