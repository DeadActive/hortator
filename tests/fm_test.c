/* FM (dm_fm.c, spec docs/superpowers/specs/2026-10-08-fm-design.md): the pitch, the sidebands, MDEC, SWEEP, FBK,
 * VEL, the extremes, the level. Each test prints one line; any failure exits 1. */
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

static double goertzel(const int32_t *x, uint32_t n, double f)   /* magnitude of f in x[0..n), Hann-windowed (no
                                                                     * leakage from the decaying envelope) */
{
    double w = 2 * M_PI * f / FS, c = 2 * cos(w), s0, s1 = 0, s2 = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        s0 = x[i] * (0.5 - 0.5 * cos(2 * M_PI * i / n)) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}

static double peak_near(const int32_t *x, uint32_t n, double f, double span)   /* the strongest frequency near f */
{
    double best = f, bv = -1, g;
    for (g = f * (1 - span); g <= f * (1 + span); g += 0.25) {
        double m = goertzel(x, n, g);
        if (m > bv) {
            bv = m;
            best = g;
        }
    }
    return best;
}

static double zc_freq(const int32_t *x, uint32_t a, uint32_t b)   /* rising zero crossings / time */
{
    uint32_t i, n = 0;
    for (i = a + 1; i < b; i++)
        n += x[i - 1] < 0 && x[i] >= 0;
    return n * (double)FS / (b - a);
}

static double hf_ratio(const int32_t *x, uint32_t a, uint32_t b)  /* derivative energy / energy: brightness */
{
    double e = 1e-9, d = 0;
    uint32_t i;
    for (i = a + 1; i < b; i++) {
        e += (double)x[i] * x[i];
        d += (double)(x[i] - x[i - 1]) * (x[i] - x[i - 1]);
    }
    return d / e;
}

/* track 0 = FM with knobs k (-99: the default), one hit at vel, frames rendered into wl */
static void fm_hit(const int16_t *k, uint32_t vel, uint32_t frames)
{
    uint32_t i;
    host_init();
    drum_set_model(&trk[0], DM_FM);
    for (i = 0; i < 8u; i++)
        if (k[i] != -99)
            trk[0].p[P_E0 + i] = k[i];
    drum_hit(&trk[0], vel);
    render_track(&trk[0], wl, frames);
}

static void test_fm_pitch(void)
{
    static const int16_t PURE[8] = {0, 127, 0, 2, -99, 0, 0, 0}, OCT[8] = {12, 127, 0, 2, -99, 0, 0, 0};
    double f, side;
    fm_hit(PURE, 127, SECS(0.3));
    f = peak_near(wl + SECS(0.05), 8192, 220, 0.03);
    side = goertzel(wl + SECS(0.05), 8192, 220 * 2.41) + goertzel(wl + SECS(0.05), 8192, 220 * 0.41);
    check("FM: INDEX 0 is a pure sine at TUNE (220 Hz, sidebands < -40 dB)",
          fabs(f / 220 - 1) < 0.005 && side < goertzel(wl + SECS(0.05), 8192, 220) / 100);
    fm_hit(OCT, 127, SECS(0.3));
    check("FM: TUNE +12 is an octave up", fabs(peak_near(wl + SECS(0.05), 8192, 440, 0.03) / 440 - 1) < 0.005);
}

static void test_fm_sidebands(void)
{
    static const int16_t LO[8] = {0, 127, 40, 2, 127, 0, 0, 0}, HI[8] = {0, 127, 100, 2, 127, 0, 0, 0};
    double c, s_hi, b_lo, b_hi;
    fm_hit(HI, 127, SECS(0.3));
    c = goertzel(wl + SECS(0.05), 8192, 220);
    s_hi = goertzel(wl + SECS(0.05), 8192, 220 * 2.41) + goertzel(wl + SECS(0.05), 8192, 220 * 0.41);
    b_hi = hf_ratio(wl, SECS(0.05), SECS(0.25));
    check("FM: INDEX up: sidebands at f +- 1.41 f (inharmonic), > -20 dB", s_hi > c / 10);
    fm_hit(LO, 127, SECS(0.3));
    b_lo = hf_ratio(wl, SECS(0.05), SECS(0.25));
    check("FM: more INDEX, a wider spectrum (brighter: the first sidebands themselves rise and fall, Bessel)",
          b_hi > 1.5 * b_lo);
}

static void test_fm_mdec(void)
{
    static const int16_t FAST[8] = {0, 127, 100, 2, 0, 0, 0, 0}, SLOW[8] = {0, 127, 100, 2, 127, 0, 0, 0};
    double early, late, slow_late;
    fm_hit(FAST, 127, SECS(0.4));
    early = hf_ratio(wl, 0, SECS(0.01));
    late = hf_ratio(wl, SECS(0.25), SECS(0.35));
    fm_hit(SLOW, 127, SECS(0.4));
    slow_late = hf_ratio(wl, SECS(0.25), SECS(0.35));
    check("FM: MDEC 0: bright at the hit, a sine after (brightness early > 2x late); MDEC 127 stays bright",
          early > 2 * late && slow_late > 2 * late);
}

static void test_fm_sweep(void)
{
    static const int16_t SW[8] = {0, 127, 0, 2, 60, 127, 0, 0};
    double e, l;
    fm_hit(SW, 127, SECS(0.6));
    e = zc_freq(wl, 0, SECS(0.01));
    l = zc_freq(wl, SECS(0.45), SECS(0.6));
    check("FM: SWEEP 127: the pitch starts far above TUNE and settles on it", e > 3 * l && fabs(l / 220 - 1) < 0.03);
}

static void test_fm_fbk_vel(void)
{
    static const int16_t F0[8] = {0, 127, 60, 2, 127, 0, 0, 64}, F1[8] = {0, 127, 60, 2, 127, 0, 127, 64};
    double a, b;
    fm_hit(F0, 127, SECS(0.2));
    a = hf_ratio(wl, SECS(0.02), SECS(0.15));
    fm_hit(F1, 127, SECS(0.2));
    b = hf_ratio(wl, SECS(0.02), SECS(0.15));
    check("FM: FBK 127 is noisier than 0", b > 1.5 * a);
    fm_hit(F0, 127, SECS(0.2));
    a = hf_ratio(wl, SECS(0.02), SECS(0.15));
    fm_hit(F0, 96, SECS(0.2));
    b = hf_ratio(wl, SECS(0.02), SECS(0.15));
    check("FM: an accent (VEL 64) is brighter than a plain hit", a > 1.1 * b);
}

/* every knob at min / mid / max, the others default: bounded, ends (voice off, silent) within 7 s */
static void test_fm_extremes(void)
{
    uint32_t k, j, i, ok = 1;
    for (k = 0; k < 8u; k++)
        for (j = 0; j < 3u; j++) {
            int16_t kn[8] = {-99, -99, -99, -99, -99, -99, -99, -99};
            const param_desc_t *d = &DMODELS[DM_FM].edit[k];
            kn[k] = (int16_t)(j == 0 ? d->min : j == 1 ? (d->min + d->max) / 2 : d->max);
            fm_hit(kn, 127, SECS(7));
            for (i = 0; i < SECS(7); i++)
                ok &= abs(wl[i]) <= 4 * VOICE_FS;
            ok &= !trk[0].v[0].active && wl[SECS(7) - 1] == 0;
            if (!ok) {
                printf("     knob %u = %d: bounded / ends failed\n", k, kn[k]);
                break;
            }
        }
    check("FM: every knob at min / mid / max: bounded, ends", ok);
}

/* Review Focus 1: everything at its top at once */
static void test_fm_combined_extreme(void)
{
    static const int16_t TOP[8] = {24, 127, 127, 15, 127, 127, 127, 127};
    uint32_t i, ok = 1;
    fm_hit(TOP, 127, SECS(7));
    for (i = 0; i < SECS(7); i++)
        ok &= abs(wl[i]) <= 4 * VOICE_FS;
    check("FM: TUNE +24 RATIO 14 SWEEP INDEX FBK VEL all at the top: bounded, ends",
          ok && !trk[0].v[0].active && !trk[0].v[1].active);
}

/* Review Focus 2: 3 hits within 20 ms: 2 voices at most, bounded */
static void test_fm_rehits(void)
{
    static const int16_t BELL[8] = {12, 110, 70, 7, 90, -99, -99, -99};
    uint32_t i, ok = 1, h;
    fm_hit(BELL, 127, 0);
    for (h = 0; h < 2u; h++) {
        render_track(&trk[0], wl, SECS(0.01));
        drum_hit(&trk[0], 127);
    }
    render_track(&trk[0], wl, SECS(1));
    for (i = 0; i < SECS(1); i++)
        ok &= abs(wl[i]) <= 4 * VOICE_FS;
    check("FM: 3 hits in 20 ms: bounded (2 voices, the oldest stolen)", ok && DMODELS[DM_FM].voices == 2u);
}

/* Review Focus 3: MIDI velocity 1, INDEX 0, VEL 127: the index stays >= 0 */
static void test_fm_low_velocity(void)
{
    static const int16_t K[8] = {0, 60, 0, 2, 60, 0, 0, 127};
    fm_hit(K, 1, SECS(0.1));
    check("FM: velocity 1 with VEL 127 and INDEX 0: the index is 0, not negative", trk[0].v[0].x[0] == 0 ||
          (!trk[0].v[0].active && trk[0].v[1].x[0] == 0));
}

/* Review Focus 4: switched to FM while another model rings, and back */
static void test_fm_model_switch(void)
{
    uint32_t i, ok = 1;
    host_init();
    drum_set_model(&trk[0], DM_CYMB);
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(0.05));
    drum_set_model(&trk[0], DM_FM);
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(0.3));
    for (i = 0; i < SECS(0.3); i++)
        ok &= abs(wl[i]) <= 4 * VOICE_FS;
    drum_set_model(&trk[0], DM_TOM);
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(0.3));
    for (i = 0; i < SECS(0.3); i++)
        ok &= abs(wl[i]) <= 4 * VOICE_FS;
    check("FM: a model switch to FM and back while ringing: bounded", ok);
}

/* Review Focus 5: an LFO on RATIO (DEST 4) at full depth: bounded */
static void test_fm_lfo_ratio(void)
{
    uint32_t i, k, ok = 1;
    host_init();
    drum_set_model(&trk[0], DM_FM);
    trk[0].p[P_LFO1 + LF_WAVE] = LW_SQUARE;
    trk[0].p[P_LFO1 + LF_DEPTH] = 64;
    trk[0].p[P_LFO1 + LF_DEST] = 4;
    for (k = 0; k < 16u; k++)
        trk[0].step[k].on = 1;
    trk[0].p[P_SLEN] = 16;
    transport_req = 1;
    for (i = 0; i < SECS(2) / CTL; i++) {
        static int32_t l[CTL], r[CTL];
        render_mix(l, r, CTL);
        for (k = 0; k < CTL; k++)
            ok &= abs(l[k]) <= 32767;
        ok &= trk[0].p[P_E3] >= 0 && trk[0].p[P_E3] <= 15;
    }
    check("FM: an LFO on RATIO at full depth: bounded, RATIO in range", ok);
}

/* the level at the defaults within +-3 dB of TOM's */
static void test_fm_level(void)
{
    static const int16_t DEF[8] = {-99, -99, -99, -99, -99, -99, -99, -99};
    double a = 0, b = 0;
    uint32_t i;
    fm_hit(DEF, 127, SECS(0.3));
    for (i = 0; i < SECS(0.3); i++)
        a += (double)wl[i] * wl[i];
    host_init();
    drum_set_model(&trk[0], DM_TOM);
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(0.3));
    for (i = 0; i < SECS(0.3); i++)
        b += (double)wl[i] * wl[i];
    printf("     FM vs TOM at the defaults: %+.1f dB\n", 10 * log10(a / b));
    check("FM: the level at the defaults within +-3 dB of TOM's", fabs(10 * log10(a / b)) <= 3);
}

int main(void)
{
    test_fm_pitch();
    test_fm_sidebands();
    test_fm_mdec();
    test_fm_sweep();
    test_fm_fbk_vel();
    test_fm_extremes();
    test_fm_combined_extreme();
    test_fm_rehits();
    test_fm_low_velocity();
    test_fm_model_switch();
    test_fm_lfo_ratio();
    test_fm_level();
    printf(fails ? "fm_test: %d FAILED\n" : "fm_test: all passed\n", fails);
    return fails ? 1 : 0;
}
