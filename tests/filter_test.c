/* FILTER (filter.c, spec docs/superpowers/specs/2026-10-08-filter-design.md): the response of each TYPE, RESO,
 * the envelope, the tail, the extremes. Each test prints one line; any failure exits 1. */
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
static int32_t wl[SECS(4)], wr[SECS(4)];

static void flt_set(track_t *t, int ty, int cut, int reso, int env, int dec)
{
    t->p[P_FTYPE] = (int16_t)ty;
    t->p[P_FCUT] = (int16_t)cut;
    t->p[P_FRESO] = (int16_t)reso;
    t->p[P_FENV] = (int16_t)env;
    t->p[P_FDEC] = (int16_t)dec;
}

/* the gain (dB) of a steady sine of f Hz, amplitude a, through track 0's filter (after 0.2 s to settle) */
static double tone_db(int ty, int cut, int reso, double f, int32_t a)
{
    static int32_t b[CTL];
    uint32_t blk, i, ph = 0;
    double in2 = 0, out2 = 0;
    track_t *t = &trk[0];
    host_init();
    flt_set(t, ty, cut, reso, 0, 40);
    for (blk = 0; blk < SECS(0.4) / CTL; blk++) {
        for (i = 0; i < CTL; i++, ph++)
            b[i] = (int32_t)(a * sin(2 * M_PI * f * ph / FS));
        if (blk >= SECS(0.2) / CTL)
            for (i = 0; i < CTL; i++)
                in2 += (double)b[i] * b[i];
        trk_filter(t, b, CTL);
        if (blk >= SECS(0.2) / CTL)
            for (i = 0; i < CTL; i++)
                out2 += (double)b[i] * b[i];
    }
    return 10 * log10((out2 + 1e-9) / in2);
}

static void test_response(void)
{
    double fc = CUTOFF_HZ[71], a = 20000;
    check("LP: 250 Hz passes, 4 kHz >= 18 dB down (CUT ~1 kHz)",
          tone_db(FT_LP, 71, 0, 250, a) > -1.5 && tone_db(FT_LP, 71, 0, 4000, a) < -18);
    check("HP: 4 kHz passes, 250 Hz >= 18 dB down",
          tone_db(FT_HP, 71, 0, 4000, a) > -1.5 && tone_db(FT_HP, 71, 0, 250, a) < -18);
    check("BP: peaks near the cutoff (RESO 64: 10 dB over 2 octaves away)",
          tone_db(FT_BP, 71, 64, fc, a) > tone_db(FT_BP, 71, 64, fc / 4, a) + 10 &&
          tone_db(FT_BP, 71, 64, fc, a) > tone_db(FT_BP, 71, 64, fc * 4, a) + 10);
    check("NOTCH: >= 20 dB down at the cutoff, 250 Hz and 4 kHz within 3 dB",
          tone_db(FT_NOTCH, 71, 0, fc, a) < -20 && tone_db(FT_NOTCH, 71, 0, 250, a) > -3 &&
          tone_db(FT_NOTCH, 71, 0, 4000, a) > -3);
    {
        double best = 0, bf = 0, f;                  /* one octave of CUT (14 units) moves the notch an octave */
        for (f = 1.5 * fc; f < 2.7 * fc; f *= 1.01) {
            double g = tone_db(FT_NOTCH, 85, 0, f, a);
            if (g < best) {
                best = g;
                bf = f;
            }
        }
        check("CUT: +14 moves the notch one octave (within a semitone)", fabs(log2(bf / fc) - 1) < 1.0 / 12);
    }
    check("RESO: 127 raises the LP's gain at the cutoff by >= 12 dB",
          tone_db(FT_LP, 71, 127, fc, a) > tone_db(FT_LP, 71, 0, fc, a) + 12);
}

/* every TYPE x CUT x RESO extreme, full-level noise and a full-level sine at the cutoff: bounded output, the
 * states never near their clamp (Review Focus: headroom) */
static void test_extremes(void)
{
    static const int CUTS[3] = {0, 64, 127}, RES[3] = {0, 64, 127};
    static int32_t b[CTL];
    uint32_t ok = 1, ty, c, r, blk, i, rng = 1;
    for (ty = FT_LP; ty < FT_N; ty++)
        for (c = 0; c < 3u; c++)
            for (r = 0; r < 3u; r++) {
                track_t *t = &trk[0];
                double fc = CUTOFF_HZ[CUTS[c]];
                host_init();
                flt_set(t, (int)ty, CUTS[c], RES[r], 0, 40);
                for (blk = 0; blk < 400u; blk++) {
                    for (i = 0; i < CTL; i++) {
                        rng = rng * 1664525u + 1013904223u;
                        b[i] = blk < 200u ? (int32_t)(rng >> 13) - 262144 - 262144 + 1
                                          : (int32_t)(524287 * sin(2 * M_PI * fc * (blk * CTL + i) / FS));
                    }
                    trk_filter(t, b, CTL);
                    for (i = 0; i < CTL; i++)
                        ok &= b[i] >= -524287 && b[i] <= 524287;
                    ok &= abs(t->fs[0]) < (1 << 26) && abs(t->fs[1]) < (1 << 26);
                }
            }
    check("extremes: every TYPE x CUT x RESO bounded, the states far from their clamp", ok);
}

/* a track with a sustained noise source: HATO's noise at DECAY 127, hit once */
static double hf_energy(const int32_t *x, uint32_t a, uint32_t b)
{
    double e = 0;
    for (; a + 1 < b; a++)
        e += (double)(x[a + 1] - x[a]) * (x[a + 1] - x[a]);
    return e;
}

static void hit_render(int env, int dec, uint32_t vel, uint32_t frames)
{
    host_init();
    drum_set_model(&trk[0], DM_HNOIS);
    trk[0].p[P_E1] = 127;
    flt_set(&trk[0], FT_LP, 50, 20, env, dec);
    drum_hit(&trk[0], vel);
    render_mix(wl, wr, frames);
}

/* blocks until the envelope is below 1 %, the filter fed a steady signal (the envelope runs while it does) */
static uint32_t env_blocks(int dec)
{
    static int32_t b[CTL];
    uint32_t n = 0, i;
    host_init();
    flt_set(&trk[0], FT_LP, 50, 20, 40, dec);
    trk_filter_hit(&trk[0], 127);
    while (trk[0].fenv > (1 << 24) / 100 && n < 100000u) {
        for (i = 0; i < CTL; i++)
            b[i] = (i & 1u) ? 1000 : -1000;
        trk_filter(&trk[0], b, CTL);
        n++;
    }
    return n;
}

static void test_envelope(void)
{
    double e0, e1, a, p;
    uint32_t n0 = 0, n1 = 0;
    hit_render(40, 40, 127, SECS(0.6));
    e0 = hf_energy(wl, 0, SECS(0.01));
    e1 = hf_energy(wl, SECS(0.4), SECS(0.41));
    check("ENV +40: brighter at the hit than after DECAY", e0 > 3 * e1);
    hit_render(-40, 40, 127, SECS(0.6));
    e0 = hf_energy(wl, 0, SECS(0.01));
    e1 = hf_energy(wl, SECS(0.4), SECS(0.41));
    check("ENV -40: darker at the hit than after DECAY", e0 * 3 < e1);
    hit_render(40, 60, 127, SECS(0.02));
    a = hf_energy(wl, 0, SECS(0.01));
    hit_render(40, 60, 96, SECS(0.02));
    p = hf_energy(wl, 0, SECS(0.01)) * (127.0 * 127) / (96.0 * 96);   /* the plain hit's level taken out */
    check("ENV: an accent sweeps further than a plain hit", a > 1.3 * p);
    n0 = env_blocks(0);                              /* the filter driven directly: a voice may end first */
    n1 = env_blocks(127);
    check("DECAY: 0 vs 127 to 1 % are ~400x apart (250..600)", n0 && n1 > 250u * n0 && n1 < 600u * n0);
}

/* a resonant LP rings after the hit's voice has ended, then the track is silent and idle */
static void test_tail(void)
{
    uint32_t rang = 0, blk;
    host_init();
    drum_set_model(&trk[0], DM_RIM);
    flt_set(&trk[0], FT_LP, 40, 127, 0, 40);
    drum_hit(&trk[0], 127);
    for (blk = 0; blk < SECS(3) / CTL; blk++) {
        render_mix(wl + blk * CTL, wr + blk * CTL, CTL);
        rang |= !trk[0].v[0].active && !trk[0].dtail && trk[0].fring;
    }
    check("tail: the filter rings on after the voice, then the track is silent and idle",
          rang && !trk[0].fring && !trk[0].v[0].active && wl[SECS(3) - 1] == 0 && wl[SECS(2.5)] == 0);
}

/* Review Focus 1: TYPE changed while ringing; from OFF the filter starts from silence */
static void test_type_switch(void)
{
    uint32_t blk, ok = 1;
    host_init();
    drum_set_model(&trk[0], DM_RIM);
    flt_set(&trk[0], FT_LP, 40, 127, 0, 40);
    drum_hit(&trk[0], 127);
    render_mix(wl, wr, SECS(0.05));
    trk[0].p[P_FTYPE] = FT_NOTCH;
    for (blk = 0; blk < 20u; blk++) {
        render_mix(wl, wr, CTL);
        ok &= abs(wl[0]) <= 32767;
    }
    trk[0].p[P_FTYPE] = FT_OFF;
    render_mix(wl, wr, CTL);
    ok &= trk[0].fs[0] == 0 && trk[0].fs[1] == 0 && !trk[0].fring;
    trk[0].p[P_FTYPE] = FT_LP;
    render_mix(wl, wr, SECS(0.5));
    check("TYPE switched while ringing: bounded; OFF clears it, LP again starts from silence", ok);
}

/* Review Focus 2: a choke cuts the track while its filter rings: the ring ends, the track goes idle */
static void test_choke_ring_ends(void)
{
    host_init();
    drum_set_model(&trk[0], DM_RIM);
    drum_set_model(&trk[1], DM_RIM);
    trk[0].p[P_CHOKE] = trk[1].p[P_CHOKE] = 2;
    flt_set(&trk[0], FT_BP, 50, 127, 0, 40);
    drum_hit(&trk[0], 127);
    render_mix(wl, wr, SECS(0.02));
    drum_hit(&trk[1], 127);
    render_mix(wl, wr, SECS(3));
    check("a choke while the filter rings: the ring ends, the track idle", !trk[0].fring && !trk[0].v[0].active);
}

/* Review Focus 5: 8 loud kicks through HP at RESO 127: the mix bounded */
static void test_eight_resonant(void)
{
    uint32_t i, ok = 1;
    host_init();
    for (i = 0; i < NTRK; i++) {
        drum_set_model(&trk[i], DM_K909);
        trk[i].p[P_LEVEL] = 127;
        flt_set(&trk[i], FT_HP, 30, 127, 63, 60);
        drum_hit(&trk[i], 127);
    }
    render_mix(wl, wr, SECS(1));
    for (i = 0; i < SECS(1); i++)
        ok &= abs(wl[i]) <= 32767 && abs(wr[i]) <= 32767;
    check("8 loud kicks through HP RESO 127: the mix bounded", ok);
}

static void test_decay_display(void)
{
    char v[8];
    const char *u0, *u1;
    param_format(&TP[P_FDEC], 0, v, &u0);
    param_format(&TP[P_FDEC], 127, v, &u1);
    check("DECAY shows ms at 0, s at 127", str_eq(u0, "ms") && str_eq(u1, "s"));
}

int main(void)
{
    test_response();
    test_extremes();
    test_envelope();
    test_tail();
    test_type_switch();
    test_choke_ring_ends();
    test_eight_resonant();
    test_decay_display();
    printf(fails ? "filter_test: %d FAILED\n" : "filter_test: all passed\n", fails);
    return fails ? 1 : 0;
}
