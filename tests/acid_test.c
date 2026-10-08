/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* ACID (dm_acid.c, spec docs/superpowers/specs/2026-10-09-acid-design.md §4): the pitch, the filter's stability and
 * self-oscillation, the band-limited oscillator, gate, slide, accent, the level, the extremes. One line per test;
 * any failure exits 1. Built with UBSan: a signed overflow aborts. */
#include "drum_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}
#define N (FS * 2)
static int32_t buf[N];

static double goertzel(const int32_t *x, uint32_t n, double f)
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
static double rms(const int32_t *x, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (n ? n : 1));
}
static int32_t peak(const int32_t *x, uint32_t n)
{
    int32_t m = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        m = abs(x[i]) > m ? abs(x[i]) : m;
    return m;
}

static track_t *acid(void)                          /* track 0 as ACID at its defaults, note C4 */
{
    host_init();
    drum_set_model(&trk[0], DM_ACID);
    return &trk[0];
}
static void play(track_t *t, uint32_t vel, int32_t nt, int32_t *out, uint32_t n)   /* a note, the gate open */
{
    memset(out, 0, n * sizeof *out);
    drum_play(t, vel, nt);
    dm_render_n(t, out, n);
}

static void test_pitch(void)                         /* A2 (nt -15): 110 Hz is the strongest partial near it */
{
    track_t *t = acid();
    double f;
    t->p[P_E0 + 2] = 127;                            /* CUTOF open: the fundamental clear */
    play(t, 96, -15, buf, N / 2);
    f = goertzel(buf + FS / 10, N / 2 - FS / 10, 110.0);
    check("acid: A2 sounds at 110 Hz (the step's note + TUNE)",
          f > 4 * goertzel(buf + FS / 10, N / 2 - FS / 10, 104.0) && f > 4 * goertzel(buf + FS / 10, N / 2 - FS / 10, 116.0));
}

static void test_gate(void)                          /* gate closed: silent within ~15 ms; open: still sounding */
{
    track_t *t = acid();
    play(t, 96, 0, buf, FS / 4);
    check("acid: the gate open holds the note (amp not decayed at 250 ms)", rms(buf + FS / 5, FS / 20) > 200.0);
    DMODELS[DM_ACID].gate(t, &t->v[0], 0);
    memset(buf, 0, FS / 4 * sizeof *buf);
    dm_render_n(t, buf, FS / 4);
    check("acid: the gate closed ends the note within 15 ms", rms(buf + FS * 15 / 1000, FS / 20) < 20.0 &&
          !t->v[0].active);
}

static void test_slide(void)                         /* a glide from C3 to C4 reaches C4 in 40..90 ms, no restart */
{
    track_t *t = acid();
    int32_t fe;
    uint32_t i, at = 0;
    play(t, 96, -12, buf, FS / 10);
    fe = t->v[0].env[1];
    t->nt = 0;
    DMODELS[DM_ACID].glide(t, &t->v[0]);
    check("acid: a slide does not restart the filter envelope", t->v[0].env[1] <= fe);
    for (i = 0; i < FS / 5 / CTL; i++) {
        dm_render_n(t, buf, CTL);
        if (!at && abs(t->v[0].x[0] - t->v[0].x[1]) < 16)   /* within 1/16 semitone of C4 */
            at = (i + 1u) * CTL;
    }
    check("acid: a slide reaches the next note in 40..90 ms", at >= FS * 40 / 1000 && at <= FS * 90 / 1000);
}

static void test_accent(void)                        /* ACC: louder and brighter than a plain note */
{
    track_t *t = acid();
    double plain, acc, bp, ba;
    play(t, 96, -24, buf, FS / 5);
    plain = rms(buf, FS / 5);
    bp = goertzel(buf, FS / 5, 65.4 * 8);
    t = acid();
    play(t, 127, -24, buf, FS / 5);
    acc = rms(buf, FS / 5);
    ba = goertzel(buf, FS / 5, 65.4 * 8);
    check("acid: accent is louder (> +2 dB)", acc > plain * 1.26);
    check("acid: accent opens the filter (the 8th harmonic > +3 dB)", ba > bp * 1.41);
}

static void test_self_osc(void)                      /* RESO 127, CUTOF at ~1 kHz: a ringing sine near the cutoff */
{
    track_t *t = acid();
    t->p[P_E0 + 3] = 127;
    t->p[P_E0 + 4] = 0;                              /* ENVMD 0: the cutoff stays */
    play(t, 96, -36, buf, N / 2);
    check("acid: RESO 127 self-oscillates (energy near the cutoff, the output bounded)",
          rms(buf + FS / 2, FS / 2) > 500.0 && peak(buf, N / 2) < 32767 * 4);
}

static void test_alias(void)                         /* a high saw (C7): little energy at an alias frequency */
{
    track_t *t = acid();
    double f0, al;
    t->p[P_E0 + 2] = 127;
    t->p[P_E0 + 3] = 0;
    play(t, 96, 36, buf, FS / 2);
    f0 = goertzel(buf, FS / 2, 2093.0);
    al = goertzel(buf, FS / 2, 44100.0 - 2093.0 * 11);   /* the 11th harmonic folded back */
    check("acid: a high saw is band-limited (the folded 11th < -30 dB of the fundamental)", al < f0 * 0.0316);
}

static void test_extremes(void)                      /* Review Focus 3: no overflow (UBSan), bounded */
{
    static const int16_t V[4] = {0, 1, 64, 127};
    uint32_t a, b, c, d, ok = 1;
    for (a = 0; a < 4u; a++)
        for (b = 0; b < 4u; b++)
            for (c = 0; c < 4u; c++)
                for (d = 0; d < 4u; d++) {
                    track_t *t = acid();
                    t->p[P_E0 + 2] = V[a];
                    t->p[P_E0 + 3] = V[b];
                    t->p[P_E0 + 4] = V[c];
                    t->p[P_E0 + 5] = V[d];
                    t->p[P_E0] = (int16_t)(d & 1u ? 24 : -24);
                    t->p[P_E0 + 6] = (int16_t)(c & 1u);
                    play(t, 127, (a & 1u) ? NT_MAX : NT_MIN, buf, FS / 10);
                    drum_play(t, 127, 0);                 /* an accent chain */
                    dm_render_n(t, buf, FS / 10);
                    if (peak(buf, FS / 10) >= 32767 * 4)
                        ok = 0;
                }
    check("acid: every extreme of CUTOF x RESO x ENVMD x ACCNT x note x TUNE stays bounded", ok);
}

static void test_level(void)                         /* the defaults' peak within +-3 dB of TOM's */
{
    track_t *t = acid();
    int32_t pa, pt;
    play(t, 96, -24, buf, FS / 4);
    pa = peak(buf, FS / 4);
    host_init();
    drum_set_model(&trk[0], DM_TOM);
    play(&trk[0], 96, 0, buf, FS / 4);
    pt = peak(buf, FS / 4);
    check("acid: the defaults' level is within +-3 dB of TOM's", pa > pt * 0.708 && pa < pt * 1.413);
}

int main(void)
{
    test_pitch();
    test_gate();
    test_slide();
    test_accent();
    test_self_osc();
    test_alias();
    test_extremes();
    test_level();
    printf(fails ? "acid: %d FAILED\n" : "acid: all checks passed\n", fails);
    return fails != 0;
}
