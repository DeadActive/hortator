/* Drum fork host checks. Each test prints one line; any failure exits 1. */
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
static int32_t wl[SECS(8)], wr[SECS(8)];

static int32_t peak_of(const int32_t *x, uint32_t a, uint32_t b)
{
    int32_t p = 0;
    for (; a < b; a++)
        if (abs(x[a]) > p)
            p = abs(x[a]);
    return p;
}

/* first sample index from which the track is inactive and silent to the end of the render */
static uint32_t end_of(const int32_t *x, uint32_t frames)
{
    uint32_t e = frames;
    while (e > 0 && x[e - 1] == 0)
        e--;
    return e;
}

static int track_idle(const track_t *t)
{
    uint32_t i;
    for (i = 0; i < NDV; i++)
        if (t->v[i].active || t->lv[i].active)
            return 0;
    return t->dtail == 0;
}

/* a default hit of model mi at velocity vel on track 0: rendered into wl, returns the end sample */
static uint32_t hit_model(uint32_t mi, uint32_t vel, uint32_t frames)
{
    host_init();
    drum_set_model(&trk[0], mi);
    drum_hit(&trk[0], vel);
    render_track(&trk[0], wl, frames);
    return end_of(wl, frames);
}

static void test_tables(void)
{
    uint32_t i, k, ok = 1;
    for (i = 0; i < NMODELS; i++) {
        const dmodel_t *m = &DMODELS[i];
        ok &= strcmp(N_MODEL[i], m->name) == 0 && strlen(m->name) <= 5;
        ok &= m->voices >= 1 && m->voices <= NDV && m->choke <= 4 && m->trigger && m->render;
        for (k = 0; k < 8; k++)
            ok &= m->edit[k].def >= m->edit[k].min && m->edit[k].def <= m->edit[k].max && strlen(m->edit[k].label) <= 5;
    }
    check("model table: names, voices, choke, defaults in range, labels <= 5 chars", ok);
}

static void test_idle_silence(void)
{
    host_init();
    render_mix(wl, wr, SECS(1));
    check("idle mix (no hits) is digital silence", peak_of(wl, 0, SECS(1)) == 0 && peak_of(wr, 0, SECS(1)) == 0);
}

static void test_sample_hit(void)
{
    uint32_t e = hit_model(DM_SMPL, 127, SECS(6));
    check("SAMPLE: GM kick (key 36) is audible", peak_of(wl, 0, e) > 1500);
    check("SAMPLE: ends within 6 s and stays at 0", e < SECS(6) - SECS(0.5) && track_idle(&trk[0]));
}

static void test_declick_cut(void)
{
    int32_t a[CTL], b[CTL], last;
    host_init();
    drum_set_model(&trk[0], DM_SMPL);
    drum_hit(&trk[0], 127);
    render_track(&trk[0], 0, SECS(0.02));
    drum_block_begin();
    track_render(&trk[0], a, CTL);
    last = a[CTL - 1];
    drum_cut(&trk[0]);
    drum_block_begin();
    track_render(&trk[0], b, CTL);
    check("cut: the next sample continues from the last one (declick tail)", abs(b[0] - last) <= 1);
    render_track(&trk[0], wl, SECS(0.02));
    check("cut: the tail is gone within 20 ms", end_of(wl, SECS(0.02)) < SECS(0.02) && track_idle(&trk[0]));
}

static void test_two_voices(void)
{
    host_init();
    drum_set_model(&trk[0], DM_SMPL);
    trk[0].p[P_E5] = 49;                             /* a long crash */
    drum_hit(&trk[0], 127);
    render_track(&trk[0], 0, SECS(0.1));
    drum_hit(&trk[0], 127);
    check("2-voice model: the first hit's tail survives a retrigger", trk[0].v[0].active && trk[0].v[1].active);
}

static void test_choke(void)
{
    int32_t last;
    host_init();
    drum_set_model(&trk[0], DM_SMPL);
    drum_set_model(&trk[1], DM_SMPL);
    trk[0].p[P_E5] = 46;
    trk[0].p[P_CHOKE] = trk[1].p[P_CHOKE] = 1;
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(0.05));
    last = wl[SECS(0.05) - 1];
    drum_hit(&trk[1], 127);                          /* same group: cuts track 0 */
    render_track(&trk[0], wl, SECS(0.01));
    check("choke: the other track's voices stop", !trk[0].v[0].active && !trk[0].v[1].active);
    check("choke: silent within 100 samples (<= 1 % of its level), 0 after 300",
          abs(wl[100]) <= abs(last) / 100 + 1 && peak_of(wl, 300, SECS(0.01)) == 0);
}

static void test_mute(void)
{
    host_init();
    trk[0].p[P_MUTE] = 1;
    drum_hit(&trk[0], 127);
    check("mute: a muted track ignores hits", !trk[0].v[0].active && !trk[0].v[1].active);
}

static void test_empty_slot(void)
{
    host_init();
    drum_set_model(&trk[0], DM_SMPL);
    trk[0].p[P_E4] = SMP_NSETS;                      /* USR1, nothing uploaded */
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(0.1));
    check("SAMPLE: an empty user slot is silent, no voice left running",
          peak_of(wl, 0, SECS(0.1)) == 0 && track_idle(&trk[0]));
}

static void test_mix_health(void)
{
    uint32_t i;
    host_init();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        t->p[P_DIST] = 60;
        t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 60;
        t->p[P_SLCR] = 1;                            /* GATE */
        drum_hit(t, 127);
    }
    render_mix(wl, wr, SECS(8));
    check("mix: 8 tracks + FX stay within full scale", peak_of(wl, 0, SECS(8)) <= 32767 && peak_of(wr, 0, SECS(8)) <= 32767);
    check("mix: back to (near) silence after the FX tails (last 1 s <= 2)",
          peak_of(wl, SECS(7), SECS(8)) <= 2 && peak_of(wr, SECS(7), SECS(8)) <= 2);
}

/* blocks at which track t was hit while the mix runs for frames samples */
static uint32_t hits_at(uint32_t ti, uint32_t frames, uint32_t *at, uint32_t max)
{
    uint32_t f, n = 0, a = hit_age(&trk[ti]);
    for (f = 0; f < frames; f += CTL) {
        render_mix(0, 0, CTL);
        if (hit_age(&trk[ti]) != a) {
            a = hit_age(&trk[ti]);
            if (n < max)
                at[n] = f;
            n++;
        }
    }
    return n;
}

static void play(void) { transport_req = 1; }
#define CEILB(x) (((x) + CTL - 1) / CTL)            /* a step fires in the first block at or after its start */

static void test_seq_timing(void)
{
    uint32_t at[8], n, p = FS * 60 / 120 / 4;      /* 1/16 at 120 BPM: 5512 samples */
    host_init();
    trk[0].step[0].on = trk[0].step[4].on = 1;
    play();
    n = hits_at(0, 17 * p, at, 8);
    check("seq: steps 0 and 4 of 16 fire at 0 and 4 steps, then loop at 16",
          n == 3 && at[0] == 0 && at[1] / CTL == CEILB(4 * p) && at[2] / CTL == CEILB(16 * p));
    host_init();
    trk[1].p[P_SLEN] = 3;
    trk[1].step[0].on = 1;
    play();
    n = hits_at(1, 7 * p, at, 8);
    check("seq: per-track length 3 loops every 3 steps", n == 3 && at[1] / CTL == CEILB(3 * p) && at[2] / CTL == CEILB(6 * p));
    host_init();
    trk[2].p[P_SDIV] = 1;                           /* 1/8 */
    trk[2].step[1].on = 1;
    play();
    n = hits_at(2, 3 * p, at, 8);
    check("seq: per-track rate 1/8 puts step 1 at one eighth", n == 1 && at[0] / CTL == CEILB(div_samples(1)));
}

static void test_seq_edges(void)
{
    uint32_t at[80], n, d, ok = 1;
    for (d = 0; d < 6; d++) {
        host_init();                                 /* length 1, no swing: a hit every step */
        trk[0].p[P_SDIV] = (int16_t)d;
        trk[0].p[P_SLEN] = 1;
        trk[0].step[0].on = 1;
        play();
        n = hits_at(0, 8 * div_samples(d), at, 80);
        ok &= n >= 7 && n <= 9;
        host_init();                                 /* length 2, swing 100 + 100: long / short pairs */
        trk[0].p[P_SDIV] = (int16_t)d;
        trk[0].p[P_SLEN] = 2;
        trk[0].p[P_SSWING] = 100;
        song.g[G_SWING] = 100;
        trk[0].step[0].on = trk[0].step[1].on = 1;
        play();
        n = hits_at(0, 8 * div_samples(d), at, 80);
        ok &= n >= 7 && n <= 9;
    }
    check("seq: lengths 1 and 2, every division, swing 100+100: one hit per step, never stuck", ok);
    host_init();
    trk[0].p[P_SLEN] = 64;
    trk[0].step[63].on = 1;
    play();
    n = hits_at(0, 65 * (FS * 60 / 120 / 4), at, 80);
    check("seq: length 64 reaches step 63 once", n == 1);
}

static void test_keys(void)
{
    uint32_t a1, a0;
    host_init();
    a0 = hit_age(&trk[0]);
    a1 = hit_age(&trk[1]);
    fm1_in.notes = 1u << KEY_TRK_KEY[1];
    render_mix(0, 0, CTL);
    check("keys: white key 2 (G3) hits track 2 only", hit_age(&trk[1]) != a1 && hit_age(&trk[0]) == a0);
    fm1_in.notes = 1u << 1;                          /* F#3, a black key */
    a0 = dvage;
    render_mix(0, 0, CTL);
    check("keys: black keys do nothing", dvage == a0);
}

static void midi_in(uint32_t st, uint32_t ch, uint32_t d1, uint32_t d2)
{
    midi_in_q[mi_w % MQ] = 0x09u | (st | ch) << 8 | d1 << 16 | d2 << 24;
    mi_w++;
}

static void test_midi(void)
{
    uint32_t a0, a3;
    host_init();
    a0 = hit_age(&trk[1]);
    midi_in(0x90, 9, 38, 90);                        /* ch 10, the snare note */
    render_mix(0, 0, CTL);
    check("MIDI: note 38 on ch 10 hits the track with NOTE 38 at its velocity",
          hit_age(&trk[1]) != a0 && (trk[1].v[0].vel == 90 || trk[1].v[1].vel == 90));
    a0 = dvage;
    midi_in(0x90, 0, 38, 90);                        /* ch 1 */
    render_mix(0, 0, CTL);
    check("MIDI: other channels are ignored", dvage == a0);
    trk[3].p[P_NOTE] = 38;
    a0 = hit_age(&trk[1]);
    a3 = hit_age(&trk[3]);
    midi_in(0x90, 9, 38, 1);
    render_mix(0, 0, CTL);
    check("MIDI: two tracks on one note both fire, velocity 1 accepted",
          hit_age(&trk[1]) != a0 && hit_age(&trk[3]) != a3);
}

static void test_live_record(void)
{
    uint32_t p = FS * 60 / 120 / 4, a, at[4], n;
    host_init();
    song.rec = 1u;                                   /* track 1 armed */
    trk[0].step[2].cond = 7;
    trk[0].step[2].rat = 2;
    play();
    render_mix(0, 0, p + p * 3 / 4 / CTL * CTL);     /* 3/4 into step 1: records into step 2 */
    a = hit_age(&trk[0]);
    fm1_in.notes = 1u << KEY_TRK_KEY[0];
    render_mix(0, 0, CTL);
    fm1_in.notes = 0;
    check("live record: a late hit goes into the next step", trk[0].step[2].on && !trk[0].step[1].on);
    check("live record: the recorded step plays always, one hit (PROB 100 %, RATCH 1)",
          trk[0].step[2].cond == 0 && trk[0].step[2].rat == 0);
    n = hits_at(0, p, at, 4);
    check("live record: that step does not hit again this time round", hit_age(&trk[0]) != a && n == 0);
}

static void test_accent(void)
{
    host_init();
    trk[0].step[0].on = trk[0].step[0].acc = 1;
    play();
    render_mix(0, 0, CTL);
    check("seq: an accented step hits at velocity 127", trk[0].v[0].vel == 127 || trk[0].v[1].vel == 127);
}

static uint32_t rising(const int32_t *x, uint32_t a, uint32_t b)   /* upward zero crossings */
{
    uint32_t c = 0;
    for (a++; a < b; a++)
        if (x[a - 1] < 0 && x[a] >= 0)
            c++;
    return c;
}

static double freq_of(const int32_t *x, uint32_t a, uint32_t b) { return rising(x, a, b) * (double)FS / (b - a); }

/* default hit at velocity 127: audible, bounded, ends within 6 s, then stays silent */
static void model_health(uint32_t mi)
{
    char what[96];
    uint32_t e = hit_model(mi, 127, SECS(7));
    int32_t pk = peak_of(wl, 0, SECS(7));
    snprintf(what, sizeof what, "%s: audible, bounded, ends in 6 s (peak %d, end %.2f s)", N_MODEL[mi], pk, e / (double)FS);
    check(what, pk >= 1500 && pk <= 3 * VOICE_FS && e < SECS(6) && track_idle(&trk[0]));
}

static void test_kicks(void)
{
    double f0, f12, fs;
    model_health(DM_K909);
    model_health(DM_K808);
    model_health(DM_KBOOM);
    model_health(DM_KPUNC);
    host_init();
    drum_set_model(&trk[0], DM_K909);
    trk[0].p[P_E1] = 127;                            /* long, for a precise pitch */
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(1.2));
    f0 = freq_of(wl, SECS(0.1), SECS(1.1));
    fs = freq_of(wl, 0, SECS(0.015));
    trk[0].p[P_E0] = 12;
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(1.2));
    f12 = freq_of(wl, SECS(0.1), SECS(1.1));
    check("909 kick: settles near 52 Hz", f0 > 49 && f0 < 55);
    check("909 kick: TUNE +12 doubles it", f12 / f0 > 1.9 && f12 / f0 < 2.1);
    check("909 kick: the start sweeps from above", fs > 1.5 * f0);
    hit_model(DM_K808, 127, SECS(0.01));
    check("808 kick: starts without a click (|first sample| < 2 % of full)", abs(wl[0]) < VOICE_FS / 50);
    hit_model(DM_K909, 127, SECS(1));
    {
        int32_t hi = peak_of(wl, 0, SECS(1));
        hit_model(DM_K909, 40, SECS(1));
        check("909 kick: velocity 40 is clearly quieter than 127", peak_of(wl, 0, SECS(1)) * 2 < hi);
    }
}

static void test_model_change(void)
{
    host_init();
    drum_set_model(&trk[0], DM_K808);
    drum_hit(&trk[0], 127);
    render_track(&trk[0], 0, SECS(0.05));
    trk[0].p[P_MODEL] = DM_K909;                     /* the knob turns while it sounds */
    render_track(&trk[0], wl, SECS(0.05));
    check("model change: the old voices stop, the tail fades, nothing hangs",
          !trk[0].v[0].active && !trk[0].v[1].active && trk[0].model == DM_K909 && end_of(wl, SECS(0.05)) < SECS(0.02));
}

static double hf_of(const int32_t *x, uint32_t a, uint32_t b)   /* high-frequency energy proxy */
{
    double s = 0;
    for (a++; a < b; a++)
        s += abs(x[a] - x[a - 1]);
    return s;
}

static double snappy_ratio(uint32_t mi)
{
    double lo, hi;
    host_init();
    drum_set_model(&trk[0], mi);
    trk[0].p[P_E3] = 0;
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(0.3));
    lo = hf_of(wl, 0, SECS(0.3));
    trk[0].p[P_E3] = 127;
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(0.3));
    hi = hf_of(wl, 0, SECS(0.3));
    return hi / (lo + 1);
}

static uint32_t clap_bursts(uint32_t mi)       /* 1 ms energy maxima > 30 % in the first 35 ms */
{
    double w[40] = {0}, mx = 0;
    uint32_t i, n = 0;
    hit_model(mi, 127, SECS(0.05));
    for (i = 0; i < SECS(0.035); i++)
        w[i / 44] += abs(wl[i]);
    for (i = 0; i < 40; i++)
        if (w[i] > mx)
            mx = w[i];
    for (i = 1; i + 1 < 40; i++)
        if (w[i] > 0.3 * mx && w[i] >= w[i - 1] && w[i] > w[i + 1])
            n++;
    return n;
}

static void test_snares_claps(void)
{
    uint32_t e0, e1;
    model_health(DM_S808);
    model_health(DM_S909);
    model_health(DM_C808);
    model_health(DM_C909);
    model_health(DM_SSNAP);
    model_health(DM_SCRAK);
    check("808 snare: SNAPPY 127 has > 3x the noise of SNAPPY 0", snappy_ratio(DM_S808) > 3);
    check("909 snare: SNAPPY 127 has > 3x the noise of SNAPPY 0", snappy_ratio(DM_S909) > 3);
    host_init();
    drum_set_model(&trk[0], DM_S808);
    trk[0].p[P_E1] = 0;
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(3));
    e0 = end_of(wl, SECS(3));
    trk[0].p[P_E1] = 127;
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(3));
    e1 = end_of(wl, SECS(3));
    check("808 snare: DECAY 127 rings > 1.5x longer than DECAY 0", e1 > e0 * 3 / 2);
    check("808 clap: >= 3 bursts in the first 35 ms", clap_bursts(DM_C808) >= 3);
    check("909 clap: >= 3 bursts in the first 35 ms", clap_bursts(DM_C909) >= 3);
}

static void test_metal(void)
{
    uint32_t ec, eo;
    int32_t last;
    model_health(DM_HATC);
    model_health(DM_HATO);
    model_health(DM_CYMB);
    model_health(DM_COWB);
    model_health(DM_HMETL);
    model_health(DM_HNOIS);
    host_init();
    drum_set_model(&trk[0], DM_HNOIS);
    drum_set_model(&trk[1], DM_HMETL);
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(0.05));
    drum_hit(&trk[1], 127);
    render_track(&trk[0], wl, SECS(0.01));
    check("HMETL / HNOIS: choke group 1 by default, one cuts the other",
          trk[0].p[P_CHOKE] == 1 && trk[1].p[P_CHOKE] == 1 && !trk[0].v[0].active);
    ec = hit_model(DM_HATC, 127, SECS(3));
    eo = hit_model(DM_HATO, 127, SECS(3));
    check("hats: the open hat rings > 2x longer than the closed one", eo > 2 * ec);
    host_init();                                     /* power-on kit: tracks 4 / 5 are HATC / HATO, group 1 */
    check("hats: closed and open default to choke group 1", trk[3].p[P_CHOKE] == 1 && trk[4].p[P_CHOKE] == 1);
    drum_hit(&trk[4], 127);
    render_track(&trk[4], wl, SECS(0.05));
    last = wl[SECS(0.05) - 1];
    drum_hit(&trk[3], 127);
    render_track(&trk[4], wl, SECS(0.01));
    check("hats: a closed hat silences the open one within 100 samples",
          !trk[4].v[0].active && abs(wl[100]) <= abs(last) / 100 + 1 && peak_of(wl, 300, SECS(0.01)) == 0);
    host_init();
    drum_set_model(&trk[0], DM_CYMB);
    drum_hit(&trk[0], 127);
    render_track(&trk[0], 0, SECS(0.2));
    drum_hit(&trk[0], 127);
    check("cymbal: 2 voices, the first keeps ringing over a retrigger", trk[0].v[0].active && trk[0].v[1].active);
}

static double late_freq(uint32_t mi, double a, double b)   /* DECAY 127, pitch between a and b s */
{
    host_init();
    drum_set_model(&trk[0], mi);
    trk[0].p[P_E1] = 127;
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(b));
    return freq_of(wl, SECS(a), SECS(b));
}

static void test_perc(void)
{
    double f;
    model_health(DM_TOM);
    model_health(DM_CONGA);
    model_health(DM_RIM);
    model_health(DM_CLAVE);
    f = late_freq(DM_TOM, 0.1, 0.4);
    check("tom: settles near 120 Hz", f > 112 && f < 128);
    f = late_freq(DM_CONGA, 0.05, 0.2);
    check("conga: settles near 310 Hz", f > 295 && f < 325);
    f = late_freq(DM_CLAVE, 0.005, 0.045);
    check("claves: near 2.5 kHz", f > 2400 && f < 2600);
}

static void test_layer(void)
{
    double a, b;
    host_init();
    drum_set_model(&trk[0], DM_K909);
    drum_hit(&trk[0], 127);
    check("layer: LLEVEL 0 starts no layer voice", !trk[0].lv[0].active && !trk[0].lv[1].active);
    render_track(&trk[0], wl, SECS(0.3));
    a = hf_of(wl, 0, SECS(0.3));
    host_init();
    drum_set_model(&trk[0], DM_K909);
    trk[0].p[P_LLEVEL] = 100;
    trk[0].p[P_LSET] = 0;                            /* PERC */
    trk[0].p[P_LKEY] = 38;                           /* the GM snare sample on top of the kick */
    drum_hit(&trk[0], 127);
    check("layer: a hit starts the model and the layer", trk[0].v[0].active && trk[0].lv[0].active);
    render_track(&trk[0], wl, SECS(6));
    b = hf_of(wl, 0, SECS(0.3));
    check("layer: adds the sample (more high-frequency energy)", b > 1.5 * a);
    check("layer: ends with the hit, no voice left", track_idle(&trk[0]));
    host_init();
    drum_set_model(&trk[0], DM_K909);
    trk[0].p[P_LLEVEL] = 100;
    trk[0].p[P_LSET] = SMP_NSETS;                    /* USR1, empty */
    drum_hit(&trk[0], 127);
    check("layer: an empty user slot adds nothing", !trk[0].lv[0].active && trk[0].v[0].active);
}

static void test_extremes(void)
{
    static const int16_t V[] = {-24, 0, 127};
    uint32_t mi, k, j, ok = 1;
    for (mi = 0; mi < NMODELS; mi++)
        for (k = 0; k < 8; k++)
            for (j = 0; j < 3; j++) {
                const param_desc_t *d = &DMODELS[mi].edit[k];
                int32_t val = clamp(V[j], d->min, d->max);
                uint32_t e;
                host_init();
                drum_set_model(&trk[0], mi);
                trk[0].p[P_E0 + k] = (int16_t)val;
                drum_hit(&trk[0], 127);
                render_track(&trk[0], wl, SECS(7));
                e = end_of(wl, SECS(7));
                if (e >= SECS(6.5) || !track_idle(&trk[0]) || peak_of(wl, 0, SECS(7)) > 4 * VOICE_FS) {
                    printf("     %s %s=%d: end %.2f s peak %d\n", N_MODEL[mi], d->label, val, e / (double)FS,
                           peak_of(wl, 0, SECS(7)));
                    ok = 0;
                }
            }
    check("every model, every knob at min / 0 / max: bounded and ends", ok);
}

static void test_stress(void)
{
    uint32_t i, k, ok = 1;
    host_init();
    for (i = 0; i < NTRK; i++)
        drum_set_model(&trk[i], i % 2 ? DM_CYMB : DM_TOM);
    for (k = 0; k < 400; k++) {                      /* two hits per block on every track, 400 blocks */
        for (i = 0; i < NTRK; i++) {
            drum_hit(&trk[i], 127);
            drum_hit(&trk[i], 1 + k % 127);
        }
        render_mix(wl, wr, CTL);
        ok &= peak_of(wl, 0, CTL) <= 32767;
    }
    render_mix(wl, wr, SECS(7));
    for (i = 0; i < NTRK; i++)
        ok &= track_idle(&trk[i]);
    check("stress: double hits every block on 8 tracks: bounded, every voice ends", ok);
}

/* the cost of 8 tracks of model mi kept busy (1/32 at 240 BPM, DECAY 127, every FX), with or without layers */
static double kit_cost(uint32_t mi, int layers)
{
    uint64_t i0;
    uint32_t i;
    host_init();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        drum_set_model(t, mi);
        t->p[P_E1] = 127;
        if (mi == DM_SMPL) {                         /* the heaviest sample: 3 octaves up, driven */
            t->p[P_E0] = 24;
            t->p[P_E3] = 127;
            t->p[P_E5] = 38;
        }
        t->p[P_LLEVEL] = layers ? 100 : 0;
        t->p[P_LKEY] = 38;
        t->p[P_DIST] = 60;
        t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 60;
        t->p[P_SLCR] = 1;
        t->p[P_SDIV] = 3;
        memset(t->step, 0, sizeof t->step);
        t->step[0].on = t->step[1].on = 1;
        t->p[P_SLEN] = 2;
    }
    song.g[G_BPM] = 240;
    transport_req = 1;
    render_mix(0, 0, SECS(0.5));
    i0 = instr_now();
    render_mix(wl, wr, SECS(2));
    return i0 ? (double)(instr_now() - i0) / SECS(2) : 0;
}

static double cost_ref(const char *key)               /* tests/drum_cost_ref.txt "key value" */
{
    FILE *f = fopen("tests/drum_cost_ref.txt", "r");
    char line[160], k[32];
    double v, r = 0;
    while (f && fgets(line, sizeof line, f))
        if (line[0] != '#' && sscanf(line, "%31s %lf", k, &v) == 2 && !strcmp(k, key))
            r = v;
    if (f)
        fclose(f);
    return r;
}

static void test_cost(void)
{
    static const char *const PAT[NTRK] = {
        "x...x...x...x..x", "....x.......x...", "....x.......x..x", "x.x.x.x.x.x.x.x.",
        "..x...x...x...x.", "......x....x....", ".x.....x..x.....", "x...............",
    };
    double ref = cost_ref("ref"), emax = cost_ref("extreme_max"), worst = 0, real, c;
    uint32_t mi, wm = 0, lay, wl_ = 0, i, k;
    uint64_t i0;
#ifdef DM_QCHECK
    printf("     cost: measured by the build without overflow checks (drum_test)\n");
    return;
#endif
    host_init();                                     /* realistic heavy use: the demo with everything on */
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        for (k = 0; k < 16; k++)
            t->step[k].on = PAT[i][k] == 'x';
        t->p[P_LLEVEL] = 100;
        t->p[P_LKEY] = 38;
        t->p[P_DIST] = 40;
        t->p[P_SLCR] = 1;
        t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 40;
    }
    transport_req = 1;
    render_mix(0, 0, SECS(0.5));
    i0 = instr_now();
    render_mix(wl, wr, SECS(4));
    real = i0 ? (double)(instr_now() - i0) / SECS(4) : 0;
    for (mi = 0; mi < NMODELS; mi++)                 /* the extreme: every model kept busy on 8 tracks */
        for (lay = 0; lay < 2; lay++) {
            c = kit_cost(mi, (int)lay);
            if (c > worst) {
                worst = c;
                wm = mi;
                wl_ = lay;
            }
        }
    printf("     realistic heavy kit: %.0f host instructions / sample (reference %.0f)\n", real, ref);
    printf("     extreme kit: %.0f (8 x %s%s; limit %.0f, above the reference by design: device shedding)\n", worst,
           N_MODEL[wm], wl_ ? " + layers" : "", emax);
    check("cost: realistic heavy use within the stock Felucca reference", !i0 || ref == 0 || real <= ref);
    check("cost: the extreme case has not grown past its recorded limit", !i0 || emax == 0 || worst <= emax);
}

static uint32_t fnv(const int32_t *x, uint32_t n)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < n; i++)
        h = (h ^ (uint32_t)x[i]) * 16777619u;
    return h;
}

static void test_golden(void)
{
    FILE *f;
    char name[32];
    unsigned want;
    uint32_t mi, ok = 1, have[NMODELS], upd = getenv("GOLDEN_UPDATE") != 0;
    for (mi = 0; mi < NMODELS; mi++) {
        hit_model(mi, 127, SECS(1));
        have[mi] = fnv(wl, SECS(1));
    }
    if (upd) {
        f = fopen("tests/drum_golden.txt", "w");
        for (mi = 0; mi < NMODELS; mi++)
            fprintf(f, "%s %08x\n", N_MODEL[mi], have[mi]);
        fclose(f);
        check("golden: tests/drum_golden.txt rewritten", 1);
        return;
    }
    f = fopen("tests/drum_golden.txt", "r");
    if (!f) {
        check("golden: tests/drum_golden.txt exists (GOLDEN_UPDATE=1 writes it)", 0);
        return;
    }
    {
        uint32_t seen[NMODELS] = {0};
        while (fscanf(f, "%31s %x", name, &want) == 2)
            for (mi = 0; mi < NMODELS; mi++)
                if (!strcmp(name, N_MODEL[mi])) {
                    seen[mi] = 1;
                    if (have[mi] != want) {
                        printf("     %s render changed: %08x, golden %08x\n", name, have[mi], want);
                        ok = 0;
                    }
                }
        for (mi = 0; mi < NMODELS; mi++)
            if (!seen[mi]) {
                printf("     %s has no golden entry (GOLDEN_UPDATE=1 adds it; check the diff adds only it)\n", N_MODEL[mi]);
                ok = 0;
            }
    }
    fclose(f);
    check("golden: every model's default render is unchanged", ok);
}

static uint32_t voices_sounding(void)
{
    uint32_t i, k, n = 0;
    for (i = 0; i < NTRK; i++)
        for (k = 0; k < NDV; k++)
            n += trk[i].v[k].active + trk[i].lv[k].active;
    return n;
}

static void test_voice_cap(void)
{
    uint32_t i, k, ok = 1, mx = 0;
    host_init();
    for (i = 0; i < NTRK; i++) {
        drum_set_model(&trk[i], i % 2 ? DM_CYMB : DM_TOM);
        trk[i].p[P_LLEVEL] = 100;
        trk[i].p[P_LKEY] = 38;
    }
    for (k = 0; k < 200; k++) {
        for (i = 0; i < NTRK; i++) {
            drum_hit(&trk[i], 127);
            ok &= voices_sounding() <= DRUM_MAXV;
        }
        if (voices_sounding() > mx)
            mx = voices_sounding();
        render_mix(wl, wr, CTL);
    }
    printf("     max voices sounding: %u (cap %u)\n", mx, (unsigned)DRUM_MAXV);
    check("voice cap: never more than DRUM_MAXV voices sound at once", ok && mx <= DRUM_MAXV);
    host_init();
    for (i = 0; i < NTRK; i++)
        drum_set_model(&trk[i], DM_CYMB);
    for (i = 0; i < DRUM_MAXV; i++)
        drum_hit(&trk[i % NTRK], 127);
    render_track(&trk[0], 0, CTL);
    drum_hit(&trk[NTRK - 1], 127);                   /* one over the cap: the oldest (track 1) is stolen */
    check("voice cap: the oldest voice is stolen with the declick tail", trk[0].dtail != 0 || !DRUM_MAXV);
}

static void test_swing_grid(void)
{
    uint32_t at[80], n1, n3, p = FS * 60 / 120 / 4;
    host_init();
    song.g[G_SWING] = 50;
    trk[0].p[P_SLEN] = 1;
    trk[0].step[0].on = 1;
    trk[1].p[P_SLEN] = 3;
    trk[1].step[0].on = 1;
    play();
    n1 = hits_at(0, 48 * p - p / 2, at, 80);
    host_init();
    song.g[G_SWING] = 50;
    trk[1].p[P_SLEN] = 3;
    trk[1].step[0].on = 1;
    play();
    n3 = hits_at(1, 48 * p - p / 2, at, 80);
    printf("     swing 50, 48 sixteenths: length 1 -> %u hits, length 3 -> %u hits\n", n1, n3);
    check("swing: length-1 and length-3 tracks stay on the bar grid (48 / 16 hits)", n1 == 48 && n3 == 16);
}

static void test_silent_sample_keeps_voices(void)
{
    uint32_t i, before;
    host_init();
    for (i = 0; i < NTRK - 1; i++)
        drum_set_model(&trk[i], DM_CYMB);
    for (i = 0; i < DRUM_MAXV; i++)                  /* the cap is full of sounding cymbals */
        drum_hit(&trk[i % (NTRK - 1)], 127);
    render_mix(0, 0, CTL);
    before = voices_sounding();
    drum_set_model(&trk[NTRK - 1], DM_SMPL);
    trk[NTRK - 1].p[P_E4] = SMP_NSETS;               /* USR1, empty */
    drum_hit(&trk[NTRK - 1], 127);
    trk[NTRK - 1].p[P_E4] = 0;
    trk[NTRK - 1].p[P_E5] = 0;                       /* PERC, a key with no sample */
    drum_hit(&trk[NTRK - 1], 127);
    check("SAMPLE with nothing to play does not take a voice from another track",
          before == DRUM_MAXV && voices_sounding() == DRUM_MAXV && !trk[NTRK - 1].v[0].active && !trk[NTRK - 1].v[1].active);
}

/* every model on 8 tracks: the sequencer at 1/32, 240 BPM, MIDI flams (two note-ons per block), layers on
 * half the tracks, DECAY 127: the cap holds every block, the mix stays in range, every voice ends */
static void test_stress_seq(void)
{
    uint32_t mi, i, k, ok = 1;
    for (mi = 0; mi < NMODELS; mi++) {
        host_init();
        for (i = 0; i < NTRK; i++) {
            track_t *t = &trk[i];
            drum_set_model(t, mi);
            t->p[P_E1] = 127;
            t->p[P_NOTE] = (int16_t)(36 + (i & 3));
            t->p[P_LLEVEL] = i & 1 ? 100 : 0;
            t->p[P_LKEY] = 38;
            t->p[P_SDIV] = 3;
            t->p[P_SLEN] = 1;
            t->step[0].on = 1;
            t->step[0].acc = (uint8_t)(i & 1);
        }
        song.g[G_BPM] = 240;
        play();
        for (k = 0; k < 600; k++) {
            midi_in(0x90, 9, 36 + k % 4, 1 + k % 127);
            midi_in(0x90, 9, 36 + k % 4, 127);
            render_mix(wl, wr, CTL);
            ok &= voices_sounding() <= DRUM_MAXV && peak_of(wl, 0, CTL) <= 32767;
        }
        transport_req = 2;
        render_mix(wl, wr, SECS(7));
        for (i = 0; i < NTRK; i++)
            if (!track_idle(&trk[i])) {
                printf("     %s: track %u still sounding after stop\n", N_MODEL[mi], i + 1);
                ok = 0;
            }
    }
    check("stress (sequencer 1/32 @ 240, MIDI flams, layers, every model): cap holds, bounded, all voices end", ok);
}

static void test_shed(void)
{
    uint32_t before;
    host_init();
    drum_set_model(&trk[0], DM_CYMB);
    drum_set_model(&trk[1], DM_CYMB);
    drum_hit(&trk[0], 127);                          /* the oldest */
    render_mix(0, 0, CTL);
    drum_hit(&trk[1], 127);
    drum_hit(&trk[1], 127);
    render_mix(0, 0, CTL);
    before = voices_sounding();
    drum_shed();
    check("shed: one voice fewer, the oldest (track 1) with its declick tail",
          before == 3 && voices_sounding() == 2 && !trk[0].v[0].active && trk[0].dtail != 0);
    host_init();
    drum_shed();
    check("shed: nothing sounding is a no-op", voices_sounding() == 0);
}

static void test_step_mode_keys(void)
{
    uint32_t a;
    host_init();
    song.seq_mode = 1;                               /* the STEP grid owns the keys */
    a = dvage;
    fm1_in.notes = 1u << KEY_TRK_KEY[0];
    render_mix(0, 0, CTL);
    check("STEP grid open: keys do not hit drums", dvage == a);
    song.seq_mode = 0;
    fm1_in.notes = 0;
    render_mix(0, 0, CTL);
    fm1_in.notes = 1u << KEY_TRK_KEY[0];
    render_mix(0, 0, CTL);
    check("STEP grid closed: keys hit drums again", dvage != a);
}

static void test_step_mode_note_off(void)
{
    uint32_t a, n;
    host_init();
    usb.config = 1;
    mo_w = mo_r = 0;
    fm1_in.notes = 1u << KEY_TRK_KEY[0];             /* key held on HOME: note-on out */
    render_mix(0, 0, CTL);
    song.seq_mode = 1;                               /* SEQ opens the STEP grid while it is held */
    fm1_in.notes = 0;
    render_mix(0, 0, CTL);
    n = mo_w;
    check("STEP grid opened while a key is held: its release still sends the note-off",
          n == 2 && (midi_out_q[1] & 0xF0FFu) == 0x8008u);
    a = dvage;
    fm1_in.notes = 1u << KEY_TRK_KEY[0];
    render_mix(0, 0, CTL);
    check("STEP grid open: a press sends no note-on", mo_w == n && dvage == a);
    usb.config = 0;
    song.seq_mode = 0;
}

#ifndef DM_QCHECK
static uint32_t dm_qover;                        /* counted only by the -DDM_QCHECK build (drum_test_q) */
#endif

static double svf_ref(double g, double r, double in, double *s1, double *s2)   /* stmlib Svf in double: bp */
{
    double h = 1.0 / (1.0 + r * g + g * g), hp = (in - (r + g) * *s1 - *s2) * h, bp = g * hp + *s1, lp;
    *s1 = g * hp + bp;
    lp = g * bp + *s2;
    *s2 = g * bp + lp;
    return bp;
}

static void test_q24(void)
{
    double e, emax, s1 = 0, s2 = 0, ps = 0;
    int32_t i, lp, bp, out[1];
    uint32_t st = 1;
    qsvf_t f;
    qpole_t p;
    dvoice_t v;
    check("q24: qm / qdiv on exact values",
          qm(QONE, 12345) == 12345 && qm(-QONE / 2, 3 * QONE) == -3 * QONE / 2 && qdiv(QONE, 4 * QONE) == QONE / 4);
    for (emax = 0, i = -256; i <= 256; i++) {
        double x = i / 4.0;
        e = fabs(qsat(Q24(x)) / (double)QONE - x / (1 + fabs(x)));
        emax = e > emax ? e : emax;
    }
    check("q24: qsat = x / (1 + |x|) within 1e-6", emax < 1e-6);
    for (emax = 0, i = -400; i <= 400; i++) {
        double x = i / 100.0, r = x < -3 ? -1 : x > 3 ? 1 : x * (27 + x * x) / (27 + 9 * x * x);
        e = fabs(qsoftclip(Q24(x)) / (double)QONE - r);
        emax = e > emax ? e : emax;
    }
    check("q24: qsoftclip = stmlib SoftClip within 1e-6", emax < 1e-6);
    for (emax = 0, i = -960; i <= 1080; i += 7) {                /* -96 .. +108 semitones */
        double s = i / 10.0, v0 = s < 0 ? 64.0 * QONE : QONE / 1024.0;
        e = fabs(qratio((int32_t)v0, Q24(s)) / v0 / pow(2, s / 12) - 1);
        emax = e > emax ? e : emax;
    }
    check("q24: qratio = 2^(st / 12) within 2e-4 over -96..108 semitones", emax < 2e-4);
    for (emax = 0, i = 1; i <= 450; i++) {                       /* f = 0.001 .. 0.45 */
        double fq = i / 1000.0, x = M_PI * fq;
        e = fabs(qtan_dirty(Q24(fq)) / (double)QONE / (x * (1 + 0.3736 * x * x)) - 1);
        e = fmax(e, fabs(qtan_fast(Q24(fq)) / (double)QONE / (x * (1 + x * x * (0.326 + 0.1823 * x * x))) - 1));
        e = fmax(e, fabs(qtan_acc(Q24(fq)) / (double)QONE /
                            (x * (1 + x * x * (3.333314036e-01 + x * x * (1.333923995e-01 + x * x * (5.33740603e-02 +
                             x * x * (2.900525e-03 + x * x * 9.5168091e-03)))))) - 1));
        emax = e > emax ? e : emax;
    }
    check("q24: qtan_dirty / fast / acc = the stmlib approximations within 1e-4", emax < 1e-4);
    for (emax = 0, i = 0; i <= 400; i++) {
        e = fabs(qsqrt(Q24(i / 100.0)) / (double)QONE - sqrt(i / 100.0));
        emax = e > emax ? e : emax;
    }
    check("q24: qsqrt within 1e-6", emax < 1e-6);
    check("q24: qrand is stmlib's LCG", qrand(&st) == (int32_t)((1u * 1664525u + 1013904223u) >> 8));
    qsvf_set(&f, qtan_acc(Q24(50.0 / 44100.0)), Q24(0.01));      /* 50 Hz, Q 100: the hardest case of the models */
    f.s1 = f.s2 = 0;
    qpole_set(&p, qtan_fast(Q24(1000.0 / 44100.0)));
    p.s = 0;
    for (emax = 0, i = 0; i < 4410; i++) {
        double g = tan(M_PI * 50.0 / 44100.0), in = i == 0 ? 1.0 : 0.0, gp = qtan_fast(Q24(1000.0 / 44100.0)) / (double)QONE;
        double ref = svf_ref(g, 0.01, in, &s1, &s2), lpr = (gp * in + ps) / (1 + gp);
        ps = gp * (in - lpr) + lpr;
        qsvf_tick(&f, i == 0 ? QONE : 0, &lp, &bp);
        e = fmax(fabs(bp / (double)QONE - ref), fabs(qpole_lp(&p, i == 0 ? QONE : 0) / (double)QONE - lpr));
        emax = e > emax ? e : emax;
    }
    check("q24: qsvf (50 Hz, Q 100) and qpole follow the double filters within 1e-4 for 0.1 s", emax < 1e-4);
    qsvf_set(&f, Q24(10.4), Q24(2.0));                           /* a cutoff near Nyquist: 1 + r g + g^2 > 128 */
    check("q24: qsvf_set damping term exact for a near-Nyquist cutoff (g 10.4, r 2)",
          fabs(f.h / (double)QONE * (1 + 2.0 * 10.4 + 10.4 * 10.4) - 1) < 1e-3);
    for (emax = 0, i = 1; i <= 500; i++) {                       /* the 48 kHz cutoff in Hz, f48 = 0.001 .. 0.5 */
        double f48 = i / 1000.0, x = M_PI * f48, g48 = x * (1 + x * x * (0.326 + 0.1823 * x * x));
        e = fabs(qtan48(Q24(f48)) / (double)QONE / tan(atan(g48) * 48000.0 / 44100.0) - 1);
        emax = e > emax ? e : emax;
    }
    check("q24: qtan48 keeps a 48 kHz FAST-tan filter's cutoff in Hz within 1e-3 (up to f48 0.5)", emax < 1e-3);
    for (emax = 0, i = 0; i <= 120; i++)                           /* exact phase increments (the hats' squares) */
        emax = fmax(emax, fabs(qnote_inc(i) - 4294967296.0 * 440.0 * pow(2, (i - 69) / 12.0) / 44100.0));
    check("q24: qnote_inc = 2^32 f of a MIDI note, rounded (within 0.5, notes 0..120)", emax <= 0.5 + 1e-6);
    check("q24: qnote(69) = 440 Hz at 44.1 kHz", fabs(qnote(69) / (double)QONE * 44100.0 / 440.0 - 1) < 1e-4);
    memset(&v, 0, sizeof v);
    out[0] = 0;
    dm_putq(&v, out, 0, QONE);
    e = out[0];
    v.t = LIFE_A + (LIFE_B - LIFE_A) / 2;
    out[0] = 0;
    dm_putq(&v, out, 0, QONE);
    check("q24: dm_putq maps 1.0 to DM_FLOAT1 x VOICE_FS, half way through the fade to half",
          fabs(e - DM_FLOAT1 * (double)VOICE_FS / 32768) <= 1 && fabs(out[0] - e / 2) <= 1);
    v.t = LIFE_B;
    out[0] = 0;
    dm_putq(&v, out, 0, QONE);
    check("q24: dm_putq is silent from LIFE_B", out[0] == 0);
#ifdef DM_QCHECK
    dm_qover = 0;
    qm(Q24(100), Q24(100));
    check("q24: an overflowing product is counted (DM_QCHECK)", dm_qover == 1);
    dm_qover = 0;
#else
    (void)dm_qover;
#endif
    {
        int32_t x = QONE, n = 0;                          /* a decay reaches 0 (no rounding fixed point) */
        while (x && n < 200000) {
            x = qdecay(x, Q24(1.0 - 1.0 / 4410.0));
            n++;
        }
        check("q24: qdecay reaches 0 (rounding alone would stop at ~2200 LSB)", x == 0);
    }
}

/* heavy models count 2 toward DRUM_MAXV (user decision, M1-C): 8 tracks of KBOOM held long ring as 4 */
static void test_heavy_cap(void)
{
    track_t *ot;
    dvoice_t *ov;
    uint32_t i;
    host_init();
    for (i = 0; i < NTRK; i++) {
        drum_set_model(&trk[i], DM_KBOOM);
        trk[i].p[P_E1] = 127;
        drum_hit(&trk[i], 127);
        render_mix(0, 0, CTL);
    }
    check("voice cap: a heavy model counts 2 (8 long KBOOM hits: 4 sound, weight 8)",
          voices_sounding() == DRUM_MAXV / 2 && dv_oldest(&ot, &ov) == DRUM_MAXV);
}

/* every M1-C model at velocity 1 and 127, each knob at min and max: it ends by 6.0 s, faded (no click) */
static void test_m1c_ends(void)
{
    static const uint32_t M[] = {DM_KBOOM, DM_KPUNC, DM_SSNAP, DM_SCRAK, DM_HMETL, DM_HNOIS};
    uint32_t a, k, j, vv, ok = 1;
    for (a = 0; a < 6; a++)
        for (vv = 0; vv < 2; vv++)
            for (k = 0; k < 4; k++)
                for (j = 0; j < 2; j++) {
                    const param_desc_t *d = &DMODELS[M[a]].edit[k];
                    int32_t val = j ? d->max : d->min;
                    uint32_t e;
                    host_init();
                    drum_set_model(&trk[0], M[a]);
                    trk[0].p[P_E0 + k] = (int16_t)val;
                    drum_hit(&trk[0], vv ? 127u : 1u);
                    render_track(&trk[0], wl, SECS(6.5));
                    e = end_of(wl, SECS(6.5));
                    if (e > SECS(6.01) || !track_idle(&trk[0]) || peak_of(wl, SECS(5.98), SECS(6.0)) > VOICE_FS / 25) {
                        printf("     %s %s=%d vel %u: end %.2f s, last 20 ms peak %d\n", N_MODEL[M[a]], d->label, val,
                               vv ? 127u : 1u, e / (double)FS, peak_of(wl, SECS(5.98), SECS(6.0)));
                        ok = 0;
                    }
                }
    check("M1-C models: velocity 1 and 127, every knob at min and max: end by 6.0 s, faded", ok);
}

/* when a single hit's voice ends (s), rendered block by block up to 6.5 s */
static double voice_end_s(uint32_t m, const int16_t knobs[4], uint32_t vel)
{
    uint32_t f, k;
    host_init();
    drum_set_model(&trk[0], m);
    for (k = 0; k < 4; k++)
        trk[0].p[P_E0 + k] = knobs[k];
    drum_hit(&trk[0], vel);
    for (f = 0; f < SECS(6.5); f += CTL) {
        render_track(&trk[0], wl, CTL);
        if (track_idle(&trk[0]))
            return (f + CTL) / (double)FS;
    }
    return 6.5;
}

/* a hit that has decayed below hearing ends then, not at the 6 s lifetime (a voice kept alive by an integer
 * tail holds a voice slot and its cost; heavy models count 2) */
static void test_m1c_ends_early(void)
{
    static const struct { uint32_t m; int16_t k[4]; uint32_t vel; double by; } C[] = {
        {DM_HMETL, {0, 96, 80, 40}, 127, 3.0},       /* hat envelope reaches 0 (DECAY 96: ~2.4 s) */
        {DM_HMETL, {0, 72, 80, 40}, 1, 1.5},
        {DM_HNOIS, {0, 127, 100, 40}, 127, 5.0},
        {DM_KBOOM, {-12, 0, 100, 100}, 127, 1.0},    /* an integer DC fixed point after the drive */
        {DM_SSNAP, {12, 100, 127, 0}, 127, 4.0},     /* a +-300 LSB limit cycle of the shell resonators */
    };
    uint32_t i, ok = 1;
    for (i = 0; i < sizeof C / sizeof C[0]; i++) {
        double e = voice_end_s(C[i].m, C[i].k, C[i].vel);
        if (e > C[i].by) {
            printf("     %s TUNE %d DECAY %d TONE %d CHAR %d vel %u: ends at %.2f s (want < %.1f)\n", N_MODEL[C[i].m],
                   C[i].k[0], C[i].k[1], C[i].k[2], C[i].k[3], C[i].vel, e, C[i].by);
            ok = 0;
        }
    }
    check("M1-C: a hit decayed below hearing ends then, not at the 6 s lifetime", ok);
}

/* a ringing KBOOM, the model swapped to SSNAP (the voice state union is reused), a hit: clean */
static void test_m1c_swap(void)
{
    uint32_t q0 = dm_qover;
    host_init();
    drum_set_model(&trk[0], DM_KBOOM);
    trk[0].p[P_E1] = 127;
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(0.2));
    drum_set_model(&trk[0], DM_SSNAP);               /* as model_step does (the IRQ off on the device) */
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(1));
    check("M1-C: model swap while a voice rings: the new model starts clean (bounded, no overflow)",
          peak_of(wl, 0, SECS(1)) <= 3 * VOICE_FS && dm_qover == q0 && trk[0].model == DM_SSNAP);
}

/* a percent shows the share of the knob's own range (swing 0..100 is 60 % at 60, 100 % at its end) */
static void test_percent_display(void)
{
    char a[8], b[8], c[8], d[8];
    const char *u;
    param_format(&TP[P_SSWING], 60, a, &u);
    param_format(&TP[P_SSWING], 100, b, &u);
    param_format(&GP[G_DFDBK], 120, c, &u);
    param_format(&TP[P_REV], 127, d, &u);
    check("display: swing 60 shows 60 %, swing 100 shows 100 %, feedback at its end 100 %, a 0..127 knob at 127 100 %",
          str_eq(a, "60") && str_eq(b, "100") && str_eq(c, "100") && str_eq(d, "100") && str_eq(u, "%"));
}

/* H4: the first seconds cost no more than upstream's (spec: device readiness) */
static void test_boot_cost(void)
{
    double up = cost_ref("upstream_idle"), idle;
    uint64_t i0, init;
#ifdef DM_QCHECK
    return;
#endif
    i0 = instr_now();
    host_init();                                     /* drum_tracks_init and the power-on state */
    init = instr_now() - i0;
    render_mix(0, 0, SECS(0.5));
    i0 = instr_now();
    render_mix(0, 0, SECS(4));
    idle = i0 ? (double)(instr_now() - i0) / SECS(4) : 0;
    printf("     boot: power-on init %llu host instructions; idle audio %.0f / sample (upstream idle %.0f)\n",
           (unsigned long long)init, idle, up);
    check("boot cost: power-on init under 2,000,000 host instructions", !i0 || init < 2000000u);
    check("boot cost: idle audio within 1.25 x upstream's idle (tests/drum_cost_ref.txt upstream_idle)",
          !i0 || (up > 0 && idle <= 1.25 * up));
}

/* a LEN change while playing: the track stays on the shared clock (its step = steps since PLAY mod LEN), so
 * setting LEN back puts it where an untouched track is (user report: it stayed out of sync) */
static void test_len_change_sync(void)
{
    uint32_t p = FS * 60 / 120 / 4, k, ok = 1;     /* a 1/16 at 120 BPM */
    host_init();
    trk[0].p[P_SLEN] = trk[1].p[P_SLEN] = 16;
    play();
    render_mix(0, 0, p * 29 + p / 2);                /* bar 2, step 13 (count 29) */
    trk[1].p[P_SLEN] = 12;                           /* mid-loop: shorter */
    render_mix(0, 0, p * 3);
    ok &= trk[1].seq_idx == trk[1].seq_cnt % 12u;
    trk[1].p[P_SLEN] = 16;                           /* and back */
    for (k = 0; k < 5; k++) {
        render_mix(0, 0, p);
        ok &= trk[1].seq_idx == trk[0].seq_idx;
    }
    check("seq: LEN changed while playing (16 -> 12 -> 16): the track stays in sync with the others", ok);
}

/* PROB as a chance: n loops of a 1-step track through step_plays (the decision the sequencer makes) */
static uint32_t chance_hits(uint32_t ti, uint32_t pct, uint32_t n, uint64_t *bits)
{
    track_t *t = &trk[ti];
    step_t s = {1, 0, 0, 0};
    uint32_t k, hits = 0;
    s.cond = (uint8_t)cond_store(pct / 5u);
    seq_start();                                     /* seeds the tracks' random sequences */
    *bits = 0;
    for (k = 0; k < n; k++) {
        int p;
        t->seq_cnt = k;
        p = step_plays(t, &s, 1u);
        hits += (uint32_t)p;
        if (k < 64u && p)
            *bits |= 1ull << k;
    }
    return hits;
}

static void test_prob_chance(void)
{
    uint64_t b0, b1, b2;
    uint32_t h50, h25;
    host_init();
    h50 = chance_hits(0, 50, 4000, &b0);
    h25 = chance_hits(0, 25, 4000, &b2);
    printf("     PROB 50 %%: %u / 4000, 25 %%: %u / 4000\n", h50, h25);
    check("PROB: 50 % and 25 % play about that often (4000 loops, +-5 %)",
          h50 >= 1800u && h50 <= 2200u && h25 >= 800u && h25 <= 1200u);
    check("PROB: 0 % never plays, 100 % always", chance_hits(0, 0, 500, &b2) == 0u && chance_hits(0, 100, 500, &b2) == 500u);
    chance_hits(0, 50, 64, &b1);
    chance_hits(1, 50, 64, &b2);
    check("PROB: the same after every PLAY (per track), different tracks differ", b0 == b1 && b0 != b2);
}

/* conditions: A/B plays on loop A of every B (loop = steps since PLAY / LEN), 1-SHOT on loop 0; no random draw */
static void test_cond_loops(void)
{
    track_t *t = &trk[0];
    uint32_t a, b, n = 0, loop, len = 3, ok = 1, r0;
    host_init();
    seq_start();
    r0 = t->rng;
    for (b = 2; b <= 8u; b++)
        for (a = 1; a <= b; a++, n++) {
            step_t s = {1, 0, 0, 0};
            s.cond = (uint8_t)(22u + n);
            for (loop = 0; loop < 2u * 8u * 8u; loop++) {
                t->seq_cnt = loop * len + 1u;        /* step 1 of each loop */
                ok &= step_plays(t, &s, len) == (loop % b == a - 1u);
            }
        }
    check("PROB: every A/B condition plays exactly on loop A of every B (128 loops)", ok && t->rng == r0);
    {
        step_t s = {1, 0, COND_1SHOT, 0};
        ok = 1;
        for (loop = 0; loop < 16u; loop++) {
            t->seq_cnt = loop * len;
            ok &= step_plays(t, &s, len) == (loop == 0u);
        }
        check("PROB: 1-SHOT plays only on the first loop after PLAY", ok);
    }
    {
        step_t s = {1, 0, 22, 0};                    /* 1/2 */
        t->seq_cnt = 9;                              /* LEN 4: loop 2 (plays); LEN 3: loop 3 (does not) */
        check("PROB: conditions count loops with the LEN of the moment", step_plays(t, &s, 4) && !step_plays(t, &s, 3));
    }
}

/* a 2/2 step on a 1-step track: hits on every other step, on time */
static void test_cond_render(void)
{
    uint32_t at[8], n, p = FS * 60 / 120 / 4;
    host_init();
    trk[0].p[P_SLEN] = 1;
    trk[0].step[0].on = 1;
    trk[0].step[0].cond = 23;                        /* 2/2 */
    play();
    n = hits_at(0, 8 * p - p / 2, at, 8);
    check("PROB 2/2 on a 1-step track: steps 1, 3, 5, 7", n == 4 && at[0] / CTL == CEILB(p) && at[3] / CTL == CEILB(7 * p));
}

/* RATCH R: R hits at k * length / R of the step, the swung (long / short) steps too */
static void test_ratchet_times(void)
{
    uint32_t r, sw, ok = 1, p = FS * 60 / 120 / 4;
    for (sw = 0; sw <= 50u; sw += 50u)
        for (r = 2; r <= 4u; r++) {
            uint32_t at[16], want[16], n, k, m = 0, l0, l1;
            int32_t s = (int32_t)sw * (int32_t)p / 250;
            host_init();
            song.g[G_SWING] = (int16_t)sw;
            l0 = p + (uint32_t)s;
            l1 = p - (uint32_t)s;
            trk[0].p[P_SLEN] = 2;
            trk[0].step[0].on = trk[0].step[1].on = 1;
            trk[0].step[0].rat = trk[0].step[1].rat = (uint8_t)(r - 1u);
            play();
            n = hits_at(0, l0 + l1 - CTL, at, 16);
            for (k = 0; k < r; k++)
                want[m++] = k * l0 / r;
            for (k = 0; k < r; k++)
                want[m++] = l0 + k * l1 / r;
            ok &= n == 2u * r;
            for (k = 0; k < m && k < n; k++)
                ok &= at[k] / CTL == CEILB(want[k]);
            if (n != 2u * r)
                printf("     RATCH %u swing %u: %u hits\n", r, sw, n);
        }
    check("RATCH: 2, 3, 4 hits evenly over each step, swung steps too", ok);
}

/* one decision per roll: a 50 % step with 4 hits plays all 4 or none; every hit at the step's velocity */
static void test_ratchet_one_decision(void)
{
    uint32_t at[300], cnt[64] = {0}, n, i, p = FS * 60 / 120 / 4, ok = 1, none = 0, all = 0, vel = 1;
    host_init();
    trk[0].p[P_SLEN] = 1;
    trk[0].step[0].on = trk[0].step[0].acc = 1;
    trk[0].step[0].rat = 3;
    trk[0].step[0].cond = (uint8_t)cond_store(10);   /* 50 % */
    play();
    n = hits_at(0, 64 * p - CTL, at, 300);
    for (i = 0; i < n && i < 300u; i++) {
        cnt[at[i] / p]++;
    }
    for (i = 0; i < 64u; i++) {
        ok &= cnt[i] == 0u || cnt[i] == 4u;
        none += cnt[i] == 0u;
        all += cnt[i] == 4u;
    }
    for (i = 0; i < NDV; i++)
        if (trk[0].v[i].active)
            vel &= trk[0].v[i].vel == 127u;
    check("RATCH + PROB: one decision per roll (0 or 4 hits a step, both occur), accented hits all at 127",
          ok && none > 10u && all > 10u && vel);
}

/* a tempo change in a roll: hits past the step's new end are dropped, the next steps play their rolls */
static void test_ratchet_tempo_change(void)
{
    uint32_t p = FS * 60 / 120 / 4, a;
    host_init();
    trk[0].p[P_SLEN] = 1;
    trk[0].step[0].on = 1;
    trk[0].step[0].rat = 3;                          /* 4 hits a step */
    play();
    a = dvage;
    render_mix(0, 0, p / 2);                         /* hits at 0 and p/4 */
    song.g[G_BPM] = 240;                             /* the step is now p/2 long: its p/2 hit is dropped */
    render_mix(0, 0, p - 2 * CTL);                   /* two new steps of 4 hits */
    printf("     RATCH 4, BPM 120 -> 240 mid-step: %u hits\n", dvage - a);
    check("RATCH: a tempo change mid-roll drops the late hit, then 4 hits per step (2 + 8)", dvage - a == 10u);
}

/* Grids engine (grids.c), structure only: the bit-exact check against the original is tests/grids_fidelity.c */
static void test_grids_engine(void)
{
    uint32_t k, a, bits, ok = 1, hits = 0, acc = 0, seq1[64], same = 1;
    host_init();
    song.g[G_GFILL1] = song.g[G_GFILL2] = song.g[G_GFILL3] = 0;
    song.g[G_GCHAOS] = 127;
    grids_start();
    for (k = 0; k < 64u; k++)
        ok &= (grids_step() & 7u) == 0u;
    check("Grids MAP: fill 0 is silent, chaos or not", ok);
    song.g[G_GMODE] = 1;                             /* EUCLID, kick LEN 4, full fill: 1/16s, accent every 4th */
    song.g[G_GLEN1] = 4;
    song.g[G_GFILL1] = 127;
    grids_start();
    for (k = 0; k < 32u; k++) {
        bits = grids_step();
        hits += bits & 1u;
        acc += (bits >> 3) & 1u;
        ok &= (k & 1u) ? !(bits & 1u) : 1;           /* nothing on the odd 1/32s */
    }
    check("Grids EUCLID: plays on 1/16s only; LEN 4 full: 16 hits, 4 accents a bar", ok && hits == 16u && acc == 4u);
    song.g[G_GMODE] = 0;                             /* MAP, chaos: the same after every start */
    song.g[G_GFILL1] = song.g[G_GFILL2] = song.g[G_GFILL3] = 90;
    grids_start();
    for (k = 0; k < 64u; k++)
        seq1[k] = grids_step();
    grids_start();
    for (k = 0; k < 64u; k++)
        same &= grids_step() == seq1[k];
    check("Grids: a start reseeds the chaos (a session repeats) and the step wraps at 32", same && grids.step == 0u);
    ok = 1;
    song.g[G_GCHAOS] = 0;                            /* without chaos the preview is what plays */
    grids_start();
    for (k = 0; k < 32u; k++) {
        bits = grids_step();
        for (a = 0; a < 3u; a++)
            ok &= grids_preview(a, k) == (((bits >> a) & 1u) | ((bits >> (a + 3u)) & 1u) << 1);
    }
    check("Grids MAP: the preview (no chaos) is exactly what the engine plays; 32 steps", ok && grids_len(0) == 32u);
}

/* MAP <-> EUCLID and LEN changes while running: positions stay inside the length */
static void test_grids_mode_switch(void)
{
    uint32_t k, ch, ok = 1;
    host_init();
    grids_start();
    for (k = 0; k < 300u; k++)
        grids_step();                                /* MAP: the Euclidean counters run on (uint8, as the original) */
    song.g[G_GMODE] = 1;
    song.g[G_GLEN1] = 32;
    song.g[G_GLEN2] = 1;
    song.g[G_GLEN3] = 7;
    for (k = 0; k < 70u; k++) {
        grids_step();
        if (k == 30u)
            song.g[G_GLEN1] = 3;
        for (ch = 0; ch < 3u; ch++)
            ok &= grids_pos(ch) < grids_len(ch) && grids_preview(ch, grids_pos(ch)) < 4u;
    }
    check("Grids: MAP -> EUCLID and a LEN change while running keep every position inside its length", ok);
}

/* EUCLID 1/1 full on G-KCK: a hit every 1/16 on the same samples as a 1/16 step track (global swing 50);
 * MAP: 32 Grids steps = one bar */
static void test_grids_clock(void)
{
    uint32_t a1[40], a2[40], n1, n2, i, ok, p = FS * 60 / 120 / 4;
    host_init();
    song.g[G_SWING] = 50;
    song.g[G_GMODE] = 1;
    song.g[G_GLEN1] = 1;
    song.g[G_GFILL1] = 127;
    trk[0].p[P_SRC] = 1;
    play();
    n1 = hits_at(0, 32 * p - p / 2, a1, 40);
    host_init();
    song.g[G_SWING] = 50;
    trk[0].p[P_SLEN] = 1;
    trk[0].step[0].on = 1;
    play();
    n2 = hits_at(0, 32 * p - p / 2, a2, 40);
    ok = n1 == 32u && n2 == 32u;
    for (i = 0; ok && i < n1; i++)
        ok &= a1[i] == a2[i];
    check("Grids: EUCLID LEN 1 full plays every 1/16 on the same samples as a 1/16 step track (swing 50)", ok);
    host_init();
    play();
    render_mix(0, 0, 16 * p - p / 4);
    check("Grids: MAP steps are 1/32s: 32 steps in 16 sixteenths", gclk.cnt == 15u && gclk.half && grids.step == 0u);
}

/* two tracks on G-SNR play the same hits; their own steps are silent meanwhile; SRC back: the steps play in sync */
static void test_grids_follow(void)
{
    uint32_t f, a1, a4, n = 0, same = 1, p = FS * 60 / 120 / 4, k;
    host_init();
    song.g[G_GFILL2] = 110;
    song.g[G_GCHAOS] = 127;
    trk[1].p[P_SRC] = trk[4].p[P_SRC] = 2;
    for (k = 0; k < 16u; k++)
        trk[1].step[k].on = 1;                       /* its own steps: every step, silent while it follows */
    play();
    a1 = hit_age(&trk[1]);
    a4 = hit_age(&trk[4]);
    for (f = 0; f < 32u * p; f += CTL) {
        uint32_t h1, h4;
        render_mix(0, 0, CTL);
        h1 = hit_age(&trk[1]) != a1;
        h4 = hit_age(&trk[4]) != a4;
        same &= h1 == h4;
        n += h1;
        a1 = hit_age(&trk[1]);
        a4 = hit_age(&trk[4]);
    }
    printf("     Grids snare (fill 110, chaos 127): %u hits in 2 bars\n", n);
    check("Grids: two tracks on one channel play the same hits, not their own steps", same && n > 4u);
    trk[1].p[P_SRC] = 0;
    check("Grids: back to STEP, the track's step is the others' (in sync)", trk[1].seq_idx == trk[0].seq_idx);
    a1 = hit_age(&trk[1]);
    render_mix(0, 0, p);
    check("Grids: back to STEP, its own steps play again", hit_age(&trk[1]) != a1);
}

/* chaos: the same session after every PLAY */
static void test_grids_repeat(void)
{
    uint32_t r1[64], r2[64], n1, n2, i, same, p = FS * 60 / 120 / 4;
    host_init();
    song.g[G_GCHAOS] = 127;
    song.g[G_GFILL3] = 90;
    trk[3].p[P_SRC] = 3;
    play();
    n1 = hits_at(3, 32 * p, r1, 64);
    transport_req = 2;
    render_mix(0, 0, CTL);
    play();
    n2 = hits_at(3, 32 * p, r2, 64);
    same = n1 == n2 && n1 > 0u;
    for (i = 0; same && i < n1 && i < 64u; i++)
        same &= r1[i] == r2[i];
    check("Grids: with chaos, every PLAY repeats the same hats", same);
}

/* live recording on a Grids track: its key plays, no step is written */
static void test_grids_rec_skip(void)
{
    uint32_t p = FS * 60 / 120 / 4, a;
    host_init();
    trk[0].p[P_SRC] = 1;
    song.rec = 1u;
    play();
    render_mix(0, 0, p + p * 3 / 4 / CTL * CTL);
    a = hit_age(&trk[0]);
    fm1_in.notes = 1u << KEY_TRK_KEY[0];
    render_mix(0, 0, CTL);
    fm1_in.notes = 0;
    check("live record: a Grids track's key plays it but writes no step",
          hit_age(&trk[0]) != a && !trk[0].step[1].on && !trk[0].step[2].on);
}

/* review #1: BPM turned while playing (120 -> 240 -> 90, with swing): Grids stays on the 1/16 grid of the step
 * tracks (it used to drift: each 1/32 half took its own tempo) */
static void test_grids_tempo_ramp(void)
{
    uint32_t f, a0, a1, n = 0, same = 1, bpm = 120;
    host_init();
    song.g[G_SWING] = 30;
    song.g[G_GMODE] = 1;
    song.g[G_GLEN1] = 1;
    song.g[G_GFILL1] = 127;                          /* G-KCK: a hit every 1/16 */
    trk[0].p[P_SRC] = 1;
    trk[1].p[P_SLEN] = 1;
    trk[1].step[0].on = 1;                           /* a 1/16 step track */
    play();
    a0 = hit_age(&trk[0]);
    a1 = hit_age(&trk[1]);
    for (f = 0; f < 20u * FS; f += CTL) {
        uint32_t h0, h1;
        if ((f / CTL) % 97u == 0u) {                 /* a knob sweep: one BPM step every 97 blocks */
            bpm = bpm >= 240u ? 90u : bpm + 3u;
            song.g[G_BPM] = (int16_t)bpm;
        }
        render_mix(0, 0, CTL);
        h0 = hit_age(&trk[0]) != a0;
        h1 = hit_age(&trk[1]) != a1;
        same &= h0 == h1;
        n += h1;
        a0 = hit_age(&trk[0]);
        a1 = hit_age(&trk[1]);
    }
    printf("     BPM sweep 120..240..90, swing 30: %u sixteenths\n", n);
    check("Grids: BPM turned while playing, Grids hits stay on the 1/16 step track's blocks", same && n > 100u);
}

/* PROB codes (params.c): knob position 0..56 <-> stored value; a zeroed step is 100 % */
static void test_cond_codes(void)
{
    uint32_t pos, a, b, n = 0, ok = 1;
    char s[8];
    for (pos = 0; pos <= COND_MAX; pos++)
        ok &= cond_pos(cond_store(pos)) == pos;
    ok &= cond_store(COND_POS_100) == 0u && cond_pos(0) == COND_POS_100 && cond_pos(200) == COND_MAX;
    check("PROB: knob position <-> stored value round trip; stored 0 = 100 %", ok);
    ok = 1;
    for (b = 2; b <= 8u; b++)                        /* 1/2 2/2 1/3 2/3 3/3 1/4 .. 8/8 */
        for (a = 1; a <= b; a++) {
            uint32_t ga, gb;
            cond_ab(22u + n, &ga, &gb);
            ok &= ga == a && gb == b;
            n++;
        }
    check("PROB: codes 22..56 are 1/2, 2/2, 1/3 .. 8/8 in order", ok && n == 35u && 21u + n == COND_MAX);
    cond_format(0, s);
    ok = !strcmp(s, "100%");
    cond_format(cond_store(15), s);
    ok &= !strcmp(s, "75%");
    cond_format(cond_store(0), s);
    ok &= !strcmp(s, "0%");
    cond_format(COND_1SHOT, s);
    ok &= !strcmp(s, "1-SHOT");
    cond_format(33, s);                              /* 22 + 2 + 3 + 4 + 2: the third of B = 5 */
    ok &= !strcmp(s, "3/5");
    check("PROB: shown as 100% / 75% / 0% / 1-SHOT / 3/5", ok);
}

int main(void)
{
    test_cond_codes();
    test_percent_display();
    test_q24();
    test_tables();
    test_idle_silence();
    test_sample_hit();
    test_declick_cut();
    test_two_voices();
    test_choke();
    test_mute();
    test_empty_slot();
    test_mix_health();
    test_seq_timing();
    test_seq_edges();
    test_keys();
    test_midi();
    test_live_record();
    test_len_change_sync();
    test_prob_chance();
    test_cond_loops();
    test_cond_render();
    test_ratchet_times();
    test_ratchet_one_decision();
    test_ratchet_tempo_change();
    test_grids_engine();
    test_grids_mode_switch();
    test_grids_clock();
    test_grids_follow();
    test_grids_repeat();
    test_grids_rec_skip();
    test_grids_tempo_ramp();
    test_accent();
    test_kicks();
    test_model_change();
    test_snares_claps();
    test_metal();
    test_perc();
    test_layer();
    test_extremes();
    test_stress();
    test_cost();
    test_boot_cost();
    test_golden();
    test_voice_cap();
    test_heavy_cap();
    test_swing_grid();
    test_silent_sample_keeps_voices();
    test_stress_seq();
    test_shed();
    test_step_mode_keys();
    test_step_mode_note_off();
    test_m1c_ends();
    test_m1c_ends_early();
    test_m1c_swap();
#ifdef DM_QCHECK
    check("q24: no Q24 overflow in any drum test render", dm_qover == 0);
#endif
    printf(fails ? "drum_test: %d FAILED\n" : "drum_test: all passed\n", fails);
    return fails ? 1 : 0;
}
