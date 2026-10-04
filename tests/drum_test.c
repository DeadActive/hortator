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
    play();
    render_mix(0, 0, p + p * 3 / 4 / CTL * CTL);     /* 3/4 into step 1: records into step 2 */
    a = hit_age(&trk[0]);
    fm1_in.notes = 1u << KEY_TRK_KEY[0];
    render_mix(0, 0, CTL);
    fm1_in.notes = 0;
    check("live record: a late hit goes into the next step", trk[0].step[2].on && !trk[0].step[1].on);
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

int main(void)
{
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
    test_accent();
    test_kicks();
    test_model_change();
    test_snares_claps();
    test_metal();
    test_perc();
    test_layer();
    printf(fails ? "drum_test: %d FAILED\n" : "drum_test: all passed\n", fails);
    return fails ? 1 : 0;
}
