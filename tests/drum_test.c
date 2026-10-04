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
    printf(fails ? "drum_test: %d FAILED\n" : "drum_test: all passed\n", fails);
    return fails ? 1 : 0;
}
