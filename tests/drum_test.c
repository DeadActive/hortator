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
          n == 3 && at[0] == 0 && at[1] / CTL == CEILB(4u * 11025u / 2u) && at[2] / CTL == CEILB(16u * 11025u / 2u));   /* exact 1/16s */
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

/* the USB-MIDI ring (usb_app.c): an overflow drops the broken backlog and the ring works again; Clock / Start /
 * Stop are queued but nothing uses them yet (the MIDI clock feature will) */
static void test_midi_ring(void)
{
    uint32_t i, a0;
    host_init();
    for (i = 0; i <= MQ; i++)                        /* one more than fits: the overflow flag */
        midi_enqueue(0x09u | 0x99u << 8 | 38u << 16 | 90u << 24, 1u);
    a0 = dvage;
    render_mix(0, 0, CTL);
    check("MIDI ring overflow: the backlog is dropped (no hits), the ring empty and open again",
          midi_in_overflow == 0 && mi_r == mi_w && dvage == a0);
    a0 = hit_age(&trk[1]);
    midi_enqueue(0x09u | 0x99u << 8 | 38u << 16 | 90u << 24, 1u);
    render_mix(0, 0, CTL);
    check("MIDI ring after an overflow: a new note plays", hit_age(&trk[1]) != a0);
    a0 = dvage;
    midi_enqueue(0x0Fu | 0xFAu << 8, 1u);           /* Start, Clock, Stop */
    midi_enqueue(0x0Fu | 0xF8u << 8, 1u);
    midi_enqueue(0x0Fu | 0xFCu << 8, 1u);
    render_mix(0, 0, CTL);
    check("MIDI realtime (Start / Clock / Stop) is read and ignored for now: no hit, transport unchanged",
          mi_r == mi_w && dvage == a0 && !song.playing);
}

/* safe start (SEQ at power-on, the recovery mode) plays nothing but still empties the MIDI ring: a DAW's notes or
 * clock must not fill it, or usb_app.c holds every USB packet back (the installer's SysEx too) */
static void test_midi_safe_start(void)
{
    static const uint8_t KEY[8] = {0x04, 0xF0, 0x22, 0x24, 0x07, 0x35, 0x7D, 0xF7};   /* UBOOT soft key, 2 packets */
    uint32_t i, a0;
    int took;
    host_init();
    safe_start = 1;
    for (i = 0; i < 48u; i++)                        /* more than 40: fewer than 16 + 8 slots free */
        midi_enqueue(0x09u | 0x99u << 8 | 38u << 16 | 90u << 24, 1u);
    a0 = dvage;
    render_mix(0, 0, CTL);
    usb.uboot_req = 0;
    took = ep1_take(KEY, 8);
    check("safe start: no hits, the MIDI ring emptied, USB SysEx (the UBOOT key) still taken",
          dvage == a0 && mi_r == mi_w && took == 1 && usb.uboot_req == 1);
    usb.uboot_req = 0;
    safe_start = 0;
}

/* TRS MIDI (midi_uart.c, on by default since stage 2 step 4): bytes from the jack reach the same ring as USB
 * (midi_enqueue, source TRS) and play exactly as USB's */
static void trs(const char *p, uint32_t n)
{
    while (n--)
        um_byte((uint8_t)*p++);
}

static void test_midi_trs(void)
{
    /* ch 10 note 60, a Clock and an Active Sensing inside the running status, note 61; ch 1 note 62; ch 10 note
     * 63 at velocity 0 (a keyboard's note-off) */
    static const char IN[] = "\x99\x3C\x5A\xF8\xFE\x3D\x64\x90\x3E\x5A\x99\x3F";
    uint32_t i, a[NTRK], a0, ok;
    host_init();
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_NOTE] = 60 + (int)i;
        a[i] = hit_age(&trk[i]);
    }
    a0 = mi_w;
    trs(IN, sizeof IN - 1u);
    um_byte(0);                                      /* the velocity 0 (a NUL ends the string above) */
    check("TRS MIDI: notes, the Clock between them, another channel and a velocity-0 note queued (source TRS)",
          mi_w == a0 + 5u && (midi_in_q[(a0 + 1u) % MQ] & 0xFFFFu) == (0x0Fu | 0xF8u << 8) &&
          midi_in_source[a0 % MQ] == 2u && midi_in_source[(a0 + 4u) % MQ] == 2u);
    render_mix(0, 0, CTL);
    ok = 1;
    for (i = 2; i < NTRK; i++)
        ok &= hit_age(&trk[i]) == a[i];
    check("TRS MIDI: the drum channel's notes play at their velocity, also after a Clock / Active Sensing byte",
          hit_age(&trk[0]) != a[0] && (trk[0].v[0].vel == 90 || trk[0].v[1].vel == 90) &&
          hit_age(&trk[1]) != a[1] && (trk[1].v[0].vel == 100 || trk[1].v[1].vel == 100));
    check("TRS MIDI: another channel and a velocity-0 note-on play nothing; the ring empties", ok && mi_r == mi_w);

    a[4] = hit_age(&trk[4]);
    a[5] = hit_age(&trk[5]);
    a0 = mi_w;
    midi_enqueue(0x09u | 0x99u << 8 | 64u << 16 | 90u << 24, 1u);       /* USB: note 64 */
    trs("\x99\x41\x5A", 3);                                              /* TRS: note 65 */
    render_mix(0, 0, CTL);
    check("TRS + USB MIDI in one block: both notes play, each with its source (USB 1, TRS 2)",
          hit_age(&trk[4]) != a[4] && hit_age(&trk[5]) != a[5] &&
          midi_in_source[a0 % MQ] == 1u && midi_in_source[(a0 + 1u) % MQ] == 2u);

    a[6] = hit_age(&trk[6]);
    for (i = 0; i < 48u; i++) {                      /* 2 beats of Clock with a note in the middle */
        um_byte(0xF8u);
        if (i == 24u)
            trs("\x99\x42\x5A", 3);                  /* note 66 */
    }
    render_mix(0, 0, CTL);
    check("TRS MIDI: a dense Clock stream around a note: the note plays, the ring empties",
          hit_age(&trk[6]) != a[6] && mi_r == mi_w);

    memset((void *)um_ring, 0x40, UM_RING);          /* data bytes only, and more than the ring: bytes lost */
    uart_midi_take(UM_RING + 10u);
    ok = midi_in_overflow == 1;
    a0 = dvage;
    render_mix(0, 0, CTL);
    check("TRS ring overflow: the stream marked broken, then the backlog dropped and the ring open again",
          ok && midi_in_overflow == 0 && mi_r == mi_w && dvage == a0);
    a[7] = hit_age(&trk[7]);
    trs("\x99\x43\x5A", 3);                          /* note 67 */
    render_mix(0, 0, CTL);
    check("TRS after an overflow: a new note plays", hit_age(&trk[7]) != a[7]);

    safe_start = 1;
    for (i = 0; i < 48u; i++)
        um_byte(0xF8u);
    trs("\x99\x3C\x5A", 3);
    a0 = dvage;
    render_mix(0, 0, CTL);
    check("safe start with a TRS Clock and notes: no hits, the ring emptied", dvage == a0 && mi_r == mi_w);
    safe_start = 0;
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

static int kit_filter;                               /* kit_cost: the FILTER on all 8 (test_cost) */
/* the cost of 8 tracks of model mi kept busy (1/32 at 240 BPM, DECAY 127, every FX), with or without layers */
static double kit_cost(uint32_t mi, int layers, int reson)
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
        t->p[P_RMODEL] = (int16_t)(!reson || i >= 4u ? RS_OFF : i < 2u ? RS_CHORD : reson == 2 ? RS_MODAL : RS_STRNG);
                                                     /* RESON at most: 4 tracks, 2 CHORD (+ 2 MODAL: reson 2) */
        t->p[P_RDECAY] = 127;
        if (kit_filter) {
            t->p[P_FTYPE] = FT_LP;
            t->p[P_FCUT] = 70;
            t->p[P_FRESO] = 60;
            t->p[P_FENV] = 30;
        }
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
    double ref = cost_ref("ref"), emax = cost_ref("extreme_max"), rmax = cost_ref("extreme_reson_max"), worst = 0, real, c;
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
        t->p[P_RMODEL] = (int16_t)(i == 1u ? RS_CHORD : i == 3u ? RS_STRNG : RS_OFF);   /* RESON on 2 tracks */
    }
    transport_req = 1;
    render_mix(0, 0, SECS(0.5));
    i0 = instr_now();
    render_mix(wl, wr, SECS(4));
    real = i0 ? (double)(instr_now() - i0) / SECS(4) : 0;
    for (mi = 0; mi < NMODELS; mi++)                 /* the extreme: every model kept busy on 8 tracks */
        for (lay = 0; lay < 2; lay++) {
            c = kit_cost(mi, (int)lay, 0);
            if (c > worst) {
                worst = c;
                wm = mi;
                wl_ = lay;
            }
        }
    c = kit_cost(wm, (int)wl_, 1);                   /* the same worst kit, every track ringing (2 CHORD) */
    printf("     extreme kit with RESON on 4 tracks (2 CHORD): %.0f (its own limit %.0f)\n", c, rmax);
    check("cost: the extreme kit with RESON at most (4 tracks, 2 CHORD) within its recorded limit", !i0 || rmax == 0 || c <= rmax);
    c = kit_cost(wm, (int)wl_, 2);                   /* the same, the other 2 MODAL (PHYS) */
    printf("     extreme kit with RESON on 4 tracks (2 CHORD + 2 MODAL): %.0f (its own limit %.0f)\n", c, rmax);
    check("cost: the extreme kit with RESON at most (2 CHORD + 2 MODAL) within its recorded limit", !i0 || rmax == 0 || c <= rmax);
    kit_filter = 1;
    c = kit_cost(wm, (int)wl_, 0);                   /* the same worst kit, the FILTER on all 8 */
    kit_filter = 0;
    printf("     extreme kit with the FILTER on 8: %.0f (its own limit %.0f)\n", c, cost_ref("extreme_filter_max"));
    check("cost: the extreme kit with the FILTER on 8 within its recorded limit",
          !i0 || cost_ref("extreme_filter_max") == 0 || c <= cost_ref("extreme_filter_max"));
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

/* M3 compressor engine (comp.c): the VCA model, the knob mapping, the gain computer's shape, the 44.1 kHz times.
 * The bit-exact check against Streams is tests/comp_fidelity.c. */
static void test_comp_engine(void)
{
    comp_set_t s = {2, 26, 45, 26, 1, 0};
    comp_cfg_t c;
    int64_t det = 0;
    int32_t gr = 0, i, ok = 1;
    uint32_t g;
    double worst = 0;
    check("COMP VCA: unity is 1.0, +990 steps doubles, -1980 quarters (Q16, within 0.3 %)",
          comp_lin(32767) == 65536 && abs(comp_lin(32767 + 990) - 131072) < 400 && abs(comp_lin(32767 - 1980) - 16384) < 60);
    {
        uint32_t v, mono = 1;
        comp_cfg_t a, b;
        for (v = 0; v < 127u; v++) {                 /* RATIO: no jump, only steeper (user: no cliff) */
            comp_set_t s0 = {2, 26, (int32_t)v, 26, 1, 0}, s1 = {2, 26, (int32_t)v + 1, 26, 1, 0};
            comp_configure(&s0, &a);
            comp_configure(&s1, &b);
            mono &= b.ratio <= a.ratio && a.ratio - b.ratio <= 8;
        }
        check("COMP knobs: RATIO 0 = 1:1 .. 127 = Streams' steepest, continuous (no cliff); 16-bit mapping",
              mono && comp_ratio16(0) == 32767u && comp_ratio16(127) == 0u && comp_k16(127) == 65535u && comp_k16(0) == 0u);
    }
    comp_configure(&s, &c);
    check("COMP defaults: THRSH 26 = -24 dB (Streams log2 units), RATIO 45 = 4.0:1, MKUP 0 no makeup",
          c.thr == (-1280 + 5 * 52) * 256 && c.ratio > 60 && c.ratio < 70 && c.makeup == 0);
    for (i = 0; i < 4410; i++)                       /* 0.1 s of silence: unity */
        g = comp_process(&c, &det, &gr, 0);
    ok &= g == 32767u;
    for (i = 0; i < 4410; i++)                       /* a loud tone: cut */
        g = comp_process(&c, &det, &gr, (i & 32) ? 30000 : -30000);
    ok &= g < 32767u - 990u && gr < 0;
    check("COMP: silence passes at unity, a loud source cuts by more than 6 dB", ok);
    s.mkup = 127;
    comp_configure(&s, &c);
    check("COMP: MKUP 127 is Streams' limiter (instant attack, ratio 0, the threshold made up to 0 dB)",
          c.atk == -1 && c.ratio == 0 && c.makeup == -c.thr);
    {
        comp_cfg_t r;
        s.mkup = 0;
        comp_configure(&s, &r);
        s.mkup = 100;
        s.thr = 0;
        comp_configure(&s, &c);
        check("COMP: MKUP adds gain and keeps the RATIO knob's ratio (THRSH 0, MKUP 100)",
              c.atk != -1 && c.ratio == r.ratio && c.makeup > 0);
        s.thr = 127;
        comp_configure(&s, &c);
        check("COMP: makeup never lifts the knee above 0 dB (THRSH 127: makeup = -threshold)", c.makeup == -c.thr);
    }
    for (i = 4; i < 640; i++) {                      /* the 44.1 kHz table keeps Streams' times */
        double t = 0.001 * pow(10.0, i / 128.0);
        double tau = -1.0 / log(1.0 - COMP_LP_COEF[i] / 2147483648.0) / 44100.0;
        double e = fabs(tau / t - 1.0);
        if (e > worst)
            worst = e;
    }
    printf("     COMP attack / release times at 44.1 kHz: worst %.3f %% off Streams' vactrol_time\n", worst * 100.0);
    check("COMP: every attack / release time within 2 % of Streams' at 44.1 kHz", worst < 0.02);
}

/* the COMP knobs as the columns show them */
static void test_comp_formats(void)
{
    char v[12];
    const char *u;
    int ok;
    host_init();
    param_format(&GP[G_CTHR], GP[G_CTHR].def, v, &u);
    ok = !strcmp(v, "-24") && !strcmp(u, "dB");                  /* whole dB from -10 down: the column fits */
    param_format(&GP[G_CRAT], GP[G_CRAT].def, v, &u);
    ok &= !strcmp(v, "4.0") && !strcmp(u, ":1");
    param_format(&GP[G_CRAT], 0, v, &u);
    ok &= !strcmp(v, "1.0") && !strcmp(u, ":1");
    param_format(&GP[G_CMKUP], 0, v, &u);
    ok &= !strcmp(v, "0.0") && !strcmp(u, "dB");
    param_format(&GP[G_CMKUP], 127, v, &u);
    ok &= !strcmp(v, "LIMIT");
    song.g[G_CTHR] = 0;
    param_format(&GP[G_CMKUP], 64, v, &u);
    ok &= !strcmp(v, "+10.1") && !strcmp(u, "dB");
    param_format(&GP[G_CATK], GP[G_CATK].def, v, &u);
    ok &= !strcmp(v, "1.1") && !strcmp(u, "ms");
    param_format(&GP[G_CREL], GP[G_CREL].def, v, &u);
    ok &= !strcmp(v, "151") && !strcmp(u, "ms");
    param_format(&GP[G_CSRC], 1, v, &u);
    ok &= !strcmp(v, "T1");
    check("COMP columns: THRSH -24 dB, RATIO 4.0:1 / 1.0:1, MKUP 0.0 dB / LIMIT / +10.1 dB (THRSH 0, MKUP 64), ATK 1.1 ms, REL 151 ms, SRC T1", ok);
}

/* a kick on T1 (the COMP source) under a long cymbal on T2: T2's peak (post LEVEL) right after the kick and later,
 * after a fresh cymbal hit; options: T2 ducked, the source muted / at LEVEL 0, T2's reverb send measured */
/* (src_before: the SRC during the first 0.25 s, then src; src_duck: DUCK on the source itself) */
typedef struct { int32_t after, late, send, src_peak; } comp_meas_t;
static int16_t scene_ghost = CG_KEEP;               /* GHOST for comp_scene */
static comp_meas_t comp_scene(int src, int duck, int src_muted, int src_level0, int src_duck, int src_before)
{
    comp_meas_t m = {0, 0, 0, 0};
    uint32_t f, i;
    host_init();
    drum_set_model(&trk[0], DM_K909);
    drum_set_model(&trk[1], DM_CYMB);
    trk[1].p[P_REV] = 127;
    song.g[G_CSRC] = (int16_t)src_before;
    song.g[G_CGHOST] = scene_ghost;
    trk[1].p[P_DUCK] = (int16_t)duck;
    trk[0].p[P_DUCK] = (int16_t)src_duck;
    trk[0].p[P_MUTE] = (int16_t)src_muted;
    if (src_level0)
        trk[0].p[P_LEVEL] = 0;
    drum_hit(&trk[1], 127);
    render_mix(0, 0, SECS(0.25));
    song.g[G_CSRC] = (int16_t)src;
    drum_hit(&trk[0], 127);
    for (f = 0; f < SECS(0.06); f += CTL) {
        trk[1].peak = trk[0].peak = 0;
        render_mix(0, 0, CTL);
        if (trk[1].peak > m.after)
            m.after = trk[1].peak;
        if (trk[0].peak > m.src_peak)
            m.src_peak = trk[0].peak;
        for (i = 0; i < CTL; i++)
            if (abs(send_r[i]) > m.send)
                m.send = abs(send_r[i]);
    }
    render_mix(0, 0, SECS(1.6));
    drum_hit(&trk[1], 127);
    for (f = 0; f < SECS(0.05); f += CTL) {
        trk[1].peak = 0;
        render_mix(0, 0, CTL);
        if (trk[1].peak > m.late)
            m.late = trk[1].peak;
    }
    return m;
}

static void test_comp_ducks(void)
{
    comp_meas_t dry = comp_scene(1, 0, 0, 0, 0, 1), duck = comp_scene(1, 1, 0, 0, 0, 1);
    printf("     COMP: T2 peak after the kick %d -> %d, its reverb send %d -> %d, later %d -> %d\n", dry.after,
           duck.after, dry.send, duck.send, dry.late, duck.late);
    check("COMP: a DUCK track drops by more than 6 dB under the source's hit", duck.after * 2 < dry.after);
    check("COMP: its FX send is ducked too", duck.send * 2 < dry.send);
    check("COMP: it comes back after the release (within 1 dB)", duck.late * 10 > dry.late * 9);
}

static void test_comp_off_identical(void)
{
    static int32_t a[SECS(1)], b[SECS(1)];
    uint32_t k;
    for (k = 0; k < 2; k++) {
        uint32_t i;
        host_init();
        for (i = 0; i < NTRK; i++)
            trk[i].p[P_DUCK] = (int16_t)k;           /* every track ducked, but SRC OFF */
        trk[0].step[0].on = trk[1].step[4].on = trk[3].step[2].on = 1;
        play();
        render_mix(k ? b : a, 0, SECS(1));
    }
    check("COMP: SRC OFF leaves the mix bit-identical, DUCK or not", !memcmp(a, b, sizeof a));
}

static void test_comp_source_not_ducked(void)
{
    comp_meas_t a = comp_scene(1, 1, 0, 0, 0, 1), b = comp_scene(1, 1, 0, 0, 1, 1);
    check("COMP: the source is never ducked by itself (DUCK on the source changes nothing)",
          a.src_peak == b.src_peak && a.after == b.after);
}

static void test_comp_ghost(void)
{
    comp_meas_t heard = comp_scene(1, 1, 0, 0, 0, 1), ghost = comp_scene(1, 1, 1, 0, 0, 1);
    comp_meas_t quiet = comp_scene(1, 1, 0, 1, 0, 1);
    check("COMP ghost key: a muted source is silent and still ducks exactly as heard",
          ghost.src_peak == 0 && ghost.after == heard.after && ghost.late == heard.late);
    check("COMP: a source at LEVEL 0 still ducks exactly as heard", quiet.after == heard.after);
}

/* GHOST: MUTE (a muted source is muted: silent, ducks nothing), KEEP (above), HIDE (never heard, always ducks) */
static void test_comp_ghost_modes(void)
{
    comp_meas_t dry = comp_scene(1, 0, 0, 0, 0, 1), heard = comp_scene(1, 1, 0, 0, 0, 1), m_open, m_muted, h_open, h_muted;
    scene_ghost = CG_MUTE;
    m_open = comp_scene(1, 1, 0, 0, 0, 1);
    m_muted = comp_scene(1, 1, 1, 0, 0, 1);
    scene_ghost = CG_HIDE;
    h_open = comp_scene(1, 1, 0, 0, 0, 1);
    h_muted = comp_scene(1, 1, 1, 0, 0, 1);
    scene_ghost = CG_KEEP;
    printf("     GHOST: T2 after the kick: dry %d, ducked %d; MUTE muted %d, HIDE %d / muted %d; source peak MUTE %d HIDE %d\n",
           dry.after, heard.after, m_muted.after, h_open.after, h_muted.after, m_open.src_peak, h_open.src_peak);
    check("COMP GHOST MUTE: unmuted, the source is heard and ducks as with KEEP",
          m_open.src_peak == heard.src_peak && m_open.after == heard.after && m_open.late == heard.late);
    check("COMP GHOST MUTE: a muted source is silent and ducks nothing (within 0.5 dB of no DUCK)",
          m_muted.src_peak == 0 && m_muted.after * 20 >= dry.after * 19 && m_muted.late * 20 >= dry.late * 19);
    check("COMP GHOST HIDE: the source is never heard, muted or not, and ducks exactly as heard",
          h_open.src_peak == 0 && h_muted.src_peak == 0 && h_open.after == heard.after && h_open.late == heard.late &&
              h_muted.after == heard.after);
}

/* GHOST changed while the source rings (KEEP -> HIDE -> KEEP -> MUTE with the source muted): 5 ms fades, no click */
static void test_comp_ghost_switch(void)
{
    static int32_t ref[SECS(0.12)], out[SECS(0.12)];
    int32_t dref = 0, dout = 0, hid = 0;
    uint32_t r, f, i;
    for (r = 0; r < 2u; r++) {
        int32_t *o = r ? out : ref;
        host_init();
        drum_set_model(&trk[0], DM_K909);
        song.g[G_CSRC] = 1;
        drum_hit(&trk[0], 127);
        for (f = 0; f < SECS(0.12); f += CTL) {
            if (r && f / CTL == SECS(0.01) / CTL)
                song.g[G_CGHOST] = CG_HIDE;
            if (r && f / CTL == SECS(0.04) / CTL)
                song.g[G_CGHOST] = CG_KEEP;
            if (r && f / CTL == SECS(0.07) / CTL) {
                trk[0].p[P_MUTE] = 1;            /* muted while KEEP (ghost), then MUTE: it stays silent */
                song.g[G_CGHOST] = CG_MUTE;
            }
            trk[0].peak = 0;
            render_mix(o + f, 0, CTL);
            if (r && ((f >= SECS(0.02) && f + CTL <= SECS(0.04)) || f >= SECS(0.08)) && trk[0].peak > hid)
                hid = trk[0].peak;                /* the source in the mix (the output keeps the DC blocker's tail) */
        }
    }
    for (i = SECS(0.008); i < SECS(0.12); i++) {     /* after the kick's attack: the switches' fades */
        dref = abs(ref[i] - ref[i - 1]) > dref ? abs(ref[i] - ref[i - 1]) : dref;
        dout = abs(out[i] - out[i - 1]) > dout ? abs(out[i] - out[i - 1]) : dout;
    }
    printf("     GHOST switched while ringing: largest step %d (unswitched %d), hidden / muted level %d\n",
           (int)dout, (int)dref, (int)hid);
    check("COMP GHOST changed while the source rings: no click, hidden while HIDE, silent once muted in MUTE",
          dout <= 2 * dref + 64 && hid == 0);
}

/* SRC changed while playing: T2 (the cymbal) as the source for 0.25 s charges the detector, then T1: the same
 * as T1 from the start (a fresh detector); and OFF is unity at once */
static void test_comp_src_change(void)
{
    comp_meas_t a = comp_scene(1, 1, 0, 0, 0, 1), b = comp_scene(1, 1, 0, 0, 0, 2);
    uint32_t i, unity = 1;
    song.g[G_CSRC] = 0;
    render_mix(0, 0, CTL);
    for (i = 0; i < CTL; i++)
        unity &= comp.gain[i] == 65536;
    check("COMP: SRC changed while playing restarts the detector; SRC OFF is unity at once",
          a.after == b.after && a.late == b.late && unity);
}

static void test_comp_extremes(void)
{
    static const int16_t S[4][2] = {{0, 127}, {0, 100}, {127, 100}, {127, 127}};   /* THRSH, MKUP */
    uint32_t c, i, ok = 1;
    for (c = 0; c < 4u; c++) {
        int32_t pk;
        host_init();
        song.g[G_CSRC] = 1;
        song.g[G_CTHR] = S[c][0];
        song.g[G_CMKUP] = S[c][1];
        for (i = 0; i < NTRK; i++) {
            uint32_t k;
            trk[i].p[P_DUCK] = 1;
            trk[i].p[P_LEVEL] = 127;
            for (k = 0; k < 16u; k++)
                trk[i].step[k].on = 1;
        }
        play();
        render_mix(wl, wr, SECS(2));
        pk = peak_of(wl, 0, SECS(2));
        ok &= pk <= 32767 && peak_of(wr, 0, SECS(2)) <= 32767;
    }
    check("COMP extremes (THRSH 0 / 127 x MKUP 100 / 127, all tracks ducked and busy): output bounded", ok);
}

/* COMP values under 1 dB keep their leading zero ("-0.1", "0.0", "+0.5") */
static void test_comp_db_text(void)
{
    char v[12];
    const char *u;
    int ok;
    comp_fmt_db10(v, 0);
    ok = !strcmp(v, "0.0");
    comp_fmt_db10(v, -1);
    ok &= !strcmp(v, "-0.1");
    comp_fmt_db10(v, -103);
    ok &= !strcmp(v, "-10.3");
    host_init();
    param_format(&GP[G_CTHR], 127, v, &u);
    ok &= !strcmp(v, "-0.1");
    check("COMP dB values keep the leading zero (0.0, -0.1, -10.3; THRSH 127 = -0.1)", ok);
}

/* review: muting / unmuting a sounding COMP source must not click (the heard part fades; the detector keeps it) */
static int32_t comp_mute_step(int src, int change, int mute_first)
{
    int32_t worst = 0, prev;
    uint32_t i, n = SECS(0.03), m = SECS(0.02);
    host_init();
    drum_set_model(&trk[0], DM_K909);
    trk[0].p[P_LEVEL] = 127;
    song.g[G_CSRC] = (int16_t)src;
    trk[0].p[P_MUTE] = (int16_t)mute_first;
    drum_hit(&trk[0], 127);
    render_mix(wl, wr, n);
    prev = wl[n - 1];
    if (change) {
        trk[0].p[P_MUTE] = (int16_t)!mute_first;
        if (!mute_first)
            panic_req |= 1u;                         /* as the TRACKS quick mute does */
    }
    render_mix(wl, wr, m);
    for (i = 0; i < m; i++) {
        int32_t d = abs(wl[i] - prev);
        if (d > worst)
            worst = d;
        prev = wl[i];
    }
    return worst;
}

static void test_comp_mute_click(void)
{
    int32_t base = comp_mute_step(1, 0, 0), plain = comp_mute_step(0, 1, 0), off = comp_mute_step(1, 1, 0);
    int32_t on = comp_mute_step(1, 1, 1);
    printf("     COMP source mute: largest step %d playing on, %d quick mute (SRC OFF), %d muting the source, %d unmuting\n",
           base, plain, off, on);
    check("COMP: muting a sounding source steps no more than any quick mute; unmuting fades in",
          off <= plain + 100 && on <= base + 200);
}

/* LFO waveforms (lfo.c): range, shape at fixed phases, MORPH */
static void test_lfo_shapes(void)
{
    uint32_t w, k, ok = 1, m;
    static const int32_t M[3] = {0, 64, 127};
    for (w = 0; w < LW_COUNT; w++)                   /* every deterministic wave in range, every morph */
        for (m = 0; m < 3u; m++)
            for (k = 0; k < 256u; k++) {
                int32_t y = lfo_shape(w, M[m], k << 24);
                ok &= y >= -32767 && y <= 32767;
            }
    check("LFO waves: every wave at MORPH 0 / 64 / 127 stays in -1 .. +1", ok);
    check("LFO SINE: +1 at a quarter cycle, -1 at three quarters, 0 at the start",
          lfo_shape(LW_SINE, 0, 1u << 30) > 32000 && lfo_shape(LW_SINE, 0, 3u << 30) < -32000 &&
              abs(lfo_shape(LW_SINE, 0, 0)) < 200);
    check("LFO SQUARE: hard at MORPH 0 (+1 at 1/8 of the cycle, -1 at 5/8)",
          lfo_shape(LW_SQUARE, 0, 1u << 29) > 32000 && lfo_shape(LW_SQUARE, 0, 5u << 29) < -32000);
    ok = 1;
    for (k = 0; k < 256u; k++)                       /* SQUARE MORPH max = SINE MORPH 0; SINE max = SQUARE 0 */
        ok &= abs(lfo_shape(LW_SQUARE, 127, k << 24) - lfo_shape(LW_SINE, 0, k << 24)) < 400 &&
              abs(lfo_shape(LW_SINE, 127, k << 24) - lfo_shape(LW_SQUARE, 0, k << 24)) < 400;
    check("LFO: SQUARE's MORPH softens to a sine, SINE's MORPH sharpens to a square", ok);
    ok = 1;
    for (k = 1; k < 256u; k++)                       /* SAW rises, RSAW = -SAW */
        ok &= lfo_shape(LW_SAW, 0, k << 24) >= lfo_shape(LW_SAW, 0, (k - 1u) << 24) &&
              lfo_shape(LW_RSAW, 64, k << 24) == -lfo_shape(LW_SAW, 64, k << 24);
    check("LFO SAW rises -1 .. +1 (0 at half), REV SAW is its mirror",
          ok && lfo_shape(LW_SAW, 0, 0) < -32000 && abs(lfo_shape(LW_SAW, 0, 1u << 31)) < 300 &&
              lfo_shape(LW_SAW, 0, 0xFF000000u) > 32000);
    check("LFO SAW tension: MORPH 127 bends the ramp (below 0 at half: x^2)",
          lfo_shape(LW_SAW, 127, 1u << 31) < -15000);
    check("LFO TRI tides: MORPH 64 = triangle (peak at half), 0 = falling ramp, 127 = rising ramp",
          lfo_shape(LW_TRI, 64, 1u << 31) > 32000 && lfo_shape(LW_TRI, 64, 0) < -32000 &&
              lfo_shape(LW_TRI, 0, 0) > 32000 && lfo_shape(LW_TRI, 127, 0xFF000000u) > 32000);
    check("LFO EXP+: MORPH 0 = straight rise, 127 = strongly curved (far below 0 at half); EXP- falls",
          abs(lfo_shape(LW_EXPUP, 0, 1u << 31)) < 300 && lfo_shape(LW_EXPUP, 127, 1u << 31) < -25000 &&
              lfo_shape(LW_EXPDN, 0, 0) > 32000 && lfo_shape(LW_EXPDN, 0, 0xFF000000u) < -32000);
}

/* LFO rates: SYNC follows the tempo (1 bar at 120 BPM = 88200 samples), Hz and TIME ends */
static void test_lfo_rates(void)
{
    int16_t q[LF_N] = {LW_SINE, LM_SYNC, 23, 0, 0, 0, LT_PLAY, 0};
    uint32_t inc;
    host_init();
    song.g[G_BPM] = 120;
    inc = lfo_inc(q);
    check("LFO SYNC: RATE 23 = 1 bar = 88200 samples at 120 BPM", inc == 0xFFFFFFFFu / 88200u);
    song.g[G_BPM] = 240;
    check("LFO SYNC: the tempo doubled, the rate doubles at once", lfo_inc(q) == 0xFFFFFFFFu / 44100u);
    q[LF_RATE] = 127;
    check("LFO SYNC: RATE 127 = 1/64 (6 of 96 ticks a quarter)", lfo_inc(q) == 0xFFFFFFFFu / (11025u * 6u / 96u));
    q[LF_MODE] = LM_HZ;
    check("LFO Hz: RATE 127 = 40 Hz, RATE 0 = 0.02 Hz",
          lfo_inc(q) == LR_HZ_INC[127] && LR_HZ_X100[127] == 4000u && LR_HZ_X100[0] == 2u);
    q[LF_MODE] = LM_TIME;
    q[LF_RATE] = 0;
    check("LFO TIME: RATE 0 = 25 ms a cycle, 127 = 60 s", lfo_inc(q) == LR_TIME_INC[0] && LR_TIME_MS[0] == 25u &&
                                                             LR_TIME_MS[127] == 60000u);
    check("LFO DEST: 1..8 the engine's knobs, 9 LVL, 10 PAN, 0 none",
          lfo_dest_param(1) == P_E0 && lfo_dest_param(8) == P_E7 && lfo_dest_param(9) == P_LEVEL &&
              lfo_dest_param(10) == P_PAN && lfo_dest_param(0) == 0xFFu);
}

static int16_t *lfo_q(uint32_t ti, uint32_t l) { return &trk[ti].p[l ? P_LFO2 : P_LFO1]; }

/* one block's modulation on track ti, as the ISR does it: apply, read the knob, restore */
static int32_t lfo_probe(uint32_t ti, uint32_t pid)
{
    int32_t v;
    lfo_apply(CTL);
    v = trk[ti].p[pid];
    lfo_restore();
    return v;
}

static void test_lfo_apply(void)
{
    int16_t *q;
    int32_t lo = 999, hi = -999, v;
    uint32_t k;
    host_init();
    q = lfo_q(0, 0);
    q[LF_WAVE] = LW_SQUARE;
    q[LF_MODE] = LM_HZ;
    q[LF_RATE] = 100;
    q[LF_DEST] = 3;                                  /* E2 (TONE / SWEEP ...) */
    q[LF_DEPTH] = 32;                                /* +50 % */
    trk[0].p[P_E2] = 64;
    for (k = 0; k < 2000u; k++) {
        v = lfo_probe(0, P_E2);
        lo = v < lo ? v : lo;
        hi = v > hi ? v : hi;
    }
    printf("     LFO square +50 %% on a knob at 64: %d .. %d\n", lo, hi);
    check("LFO: the knob moves around its set value by DEPTH (square +50 %: about 0 .. 127)", lo <= 2 && hi >= 125);
    check("LFO: outside the audio block the knob is its set value again", trk[0].p[P_E2] == 64);
    q[LF_DEPTH] = -32;
    v = lfo_probe(0, P_E2);
    check("LFO: a negative DEPTH moves the other way", v != 64);
}

static void test_lfo_off_identical(void)
{
    static int32_t a[SECS(1)], b[SECS(1)];
    uint32_t k;
    for (k = 0; k < 3u; k++) {
        int16_t *q;
        host_init();
        q = lfo_q(1, 0);
        if (k == 1u) {
            q[LF_DEST] = 9;                          /* LVL, DEPTH 0 */
            q[LF_WAVE] = LW_SQUARE;
        } else if (k == 2u) {
            q[LF_DEPTH] = 64;                        /* DEST OFF */
        }
        trk[0].step[0].on = trk[1].step[4].on = 1;
        play();
        render_mix(k ? b : a, 0, SECS(1));
        if (k)
            check(k == 1u ? "LFO: DEPTH 0 leaves the mix bit-identical" : "LFO: DEST OFF leaves the mix bit-identical",
                  !memcmp(a, b, sizeof a));
    }
}

/* review focus 4: both LFOs on one knob add and clamp */
static void test_lfo_two_on_one(void)
{
    uint32_t l, k;
    int32_t hi = -999;
    host_init();
    for (l = 0; l < 2u; l++) {
        int16_t *q = lfo_q(0, l);
        q[LF_WAVE] = LW_SQUARE;
        q[LF_MODE] = LM_HZ;
        q[LF_RATE] = 90;
        q[LF_DEST] = 9;                              /* LVL */
        q[LF_DEPTH] = 64;
        q[LF_TRIG] = LT_PLAY;
    }
    trk[0].p[P_LEVEL] = 100;
    lfo_start();
    for (k = 0; k < 500u; k++) {
        int32_t v = lfo_probe(0, P_LEVEL);
        hi = v > hi ? v : hi;
    }
    check("LFO: both LFOs on LVL add and stay in its range (max 127)", hi == 127 && trk[0].p[P_LEVEL] == 100);
}

/* review focus 2: an engine without the targeted extra knob */
static void test_lfo_absent_dest(void)
{
    int16_t *q;
    host_init();
    drum_set_model(&trk[0], DM_C808);                /* no E4 */
    q = lfo_q(0, 0);
    q[LF_DEST] = 5;                                  /* E4 */
    q[LF_DEPTH] = 64;
    q[LF_WAVE] = LW_SQUARE;
    trk[0].p[P_E4] = 0;
    {
        uint32_t k, wrote = 0, diff = 0;
        for (k = 0; k < 200u; k++) {
            lfo_apply(CTL);
            wrote |= trk[0].lnum != 0u || trk[0].p[P_E4] != 0;
            lfo_restore();
        }
        check("LFO: a DEST the engine lacks writes nothing", !wrote);
        drum_set_model(&trk[0], DM_K909);            /* has E4 (SWPT) */
        for (k = 0; k < 200u; k++)
            diff |= lfo_probe(0, P_E4) != trk[0].p[P_E4];
        check("LFO: the same setting acts again with an engine that has the knob", diff);
    }
}

/* review focus 1: a knob set while an LFO targets it keeps the new value (the restore puts back the set value
 * of that block, and the UI only runs between blocks) */
static void test_lfo_edit_while_modulated(void)
{
    int16_t *q;
    host_init();
    q = lfo_q(0, 0);
    q[LF_DEST] = 3;
    q[LF_DEPTH] = 64;
    q[LF_WAVE] = LW_SQUARE;
    render_mix(0, 0, CTL * 10);
    trk[0].p[P_E2] = 11;                             /* the user turns the knob between blocks */
    render_mix(0, 0, CTL * 10);
    check("LFO: a knob turned while modulated keeps the new value", trk[0].p[P_E2] == 11);
}

/* TRIG: HIT restarts on each hit, PLAY at PLAY, FREE never; PHASE offsets */
static void test_lfo_trig(void)
{
    int16_t *q;
    host_init();
    q = lfo_q(0, 0);
    q[LF_DEST] = 9;
    q[LF_DEPTH] = 64;
    q[LF_WAVE] = LW_SAW;
    q[LF_MODE] = LM_HZ;
    q[LF_RATE] = 60;
    q[LF_TRIG] = LT_HIT;
    render_mix(0, 0, SECS(0.3));
    drum_hit(&trk[0], 100);
    check("LFO TRIG HIT: a hit restarts the cycle", trk[0].lfo[0].ph == 0u);
    q[LF_TRIG] = LT_FREE;
    render_mix(0, 0, SECS(0.1));
    {
        uint32_t ph = trk[0].lfo[0].ph;
        play();
        render_mix(0, 0, CTL);
        check("LFO TRIG FREE: PLAY does not restart it", trk[0].lfo[0].ph != 0u && trk[0].lfo[0].ph != ph);
    }
    q[LF_TRIG] = LT_PLAY;
    lfo_start();
    check("LFO TRIG PLAY: PLAY restarts it", trk[0].lfo[0].ph == 0u);
    q[LF_PHASE] = 32;                                /* a quarter cycle */
    lfo_start();
    lfo_apply(0);
    lfo_restore();
    check("LFO PHASE: the restart starts a quarter into the cycle (SAW at -0.5)",
          abs(lfo_out(&trk[0], 0) + 16384) < 300);
}

/* random waves: S&H holds / glides, WANDER and RWALK move without jumps, all repeat after PLAY */
static int32_t lfo_run_ph(uint32_t wave, int32_t morph, int32_t phase, int32_t *out, uint32_t n);
static int32_t lfo_run(uint32_t wave, int32_t morph, int32_t *out, uint32_t n) { return lfo_run_ph(wave, morph, 0, out, n); }
static int32_t lfo_run_ph(uint32_t wave, int32_t morph, int32_t phase, int32_t *out, uint32_t n)
{
    int16_t *q;
    uint32_t k;
    int32_t jump = 0, prev;
    host_init();
    q = lfo_q(0, 0);
    q[LF_WAVE] = (int16_t)wave;
    q[LF_MORPH] = (int16_t)morph;
    q[LF_MODE] = LM_HZ;
    q[LF_RATE] = 80;
    q[LF_DEST] = 9;
    q[LF_DEPTH] = 64;
    q[LF_PHASE] = (int16_t)phase;
    lfo_start();
    lfo_apply(0);
    lfo_restore();
    prev = lfo_out(&trk[0], 0);
    for (k = 0; k < n; k++) {
        lfo_apply(CTL);
        lfo_restore();
        out[k] = lfo_out(&trk[0], 0);
        if (abs(out[k] - prev) > jump)
            jump = abs(out[k] - prev);
        prev = out[k];
    }
    return jump;
}

static void test_lfo_random(void)
{
    static int32_t a[3000], b[3000];
    int32_t j0 = lfo_run(LW_SH, 0, a, 3000), j1 = lfo_run(LW_SH, 127, b, 3000), jw, jr, jr0, k, varied = 0;
    printf("     LFO S&H largest step per block: %d (MORPH 0), %d (MORPH 127)\n", j0, j1);
    check("LFO S&H: MORPH 0 jumps, MORPH 127 glides", j0 > 8000 && j1 < 2000);
    for (k = 1; k < 3000; k++)
        varied += a[k] != a[k - 1];
    check("LFO S&H: new values keep coming", varied > 3);
    jw = lfo_run(LW_WANDER, 64, a, 3000);
    lfo_run(LW_WANDER, 64, b, 3000);
    check("LFO WANDER: smooth (small steps), the same after every PLAY", jw < 2000 && !memcmp(a, b, sizeof a));
    jr0 = lfo_run(LW_RWALK, 0, a, 3000);
    jr = lfo_run(LW_RWALK, 127, b, 3000);
    printf("     LFO RWALK largest step per block: %d (MORPH 0), %d (MORPH 127)\n", jr0, jr);
    check("LFO RWALK: MORPH sets the step size (small drift .. larger moves), no jumps", jr0 < jr && jr < 4000);
}

/* LVL modulation moves inside a sound; a start-of-hit knob differs per hit */
static void test_lfo_sound(void)
{
    uint32_t f, w, quiet = 0, loud = 0;
    int16_t *q;
    host_init();
    drum_set_model(&trk[0], DM_CYMB);
    trk[0].p[P_E1] = 127;                            /* long */
    q = lfo_q(0, 0);
    q[LF_WAVE] = LW_SQUARE;
    q[LF_MODE] = LM_SYNC;
    q[LF_RATE] = 100;                                /* 1/16 .. */
    q[LF_DEST] = 9;
    q[LF_DEPTH] = -64;                               /* LVL down to 0 half the cycle */
    play();
    drum_hit(&trk[0], 127);
    for (f = 0; f < 40u; f++) {                      /* 40 windows of 512 samples */
        int32_t pk = 0;
        render_mix(wl, wr, 512);
        for (w = 0; w < 512u; w++)
            pk = abs(wl[w]) > pk ? abs(wl[w]) : pk;
        quiet += pk < 20;
        loud += pk > 300;
    }
    check("LFO on LVL: a ringing cymbal is gated on and off inside the sound", quiet > 4u && loud > 4u);
}

/* review: the PLAY block's hit sees the restarted LFO (phase 0), not the value before PLAY */
static void test_lfo_play_first_hit(void)
{
    int16_t *q;
    host_init();
    q = lfo_q(0, 0);
    q[LF_WAVE] = LW_SAW;
    q[LF_RATE] = 23;                                 /* SYNC 1 bar */
    q[LF_DEST] = 9;
    q[LF_DEPTH] = 64;
    q[LF_TRIG] = LT_PLAY;
    trk[0].p[P_LEVEL] = 64;
    render_mix(0, 0, SECS(0.7));                     /* running before PLAY: mid-cycle */
    play();
    render_mix(0, 0, CTL);                           /* the PLAY block (its step 0 hits) */
    printf("     LFO SAW on LVL 64, the PLAY block: %d\n", trk[0].lval[0]);
    check("LFO TRIG PLAY: the PLAY block already uses phase 0 (SAW at -1: LVL near 0)", trk[0].lval[0] <= 2);
}

/* review: the random waves stay continuous with PHASE set; a restart draws a new value */
static void test_lfo_random_phase_restart(void)
{
    static int32_t a[3000];
    int32_t js = lfo_run_ph(LW_SH, 127, 32, a, 3000), jr = lfo_run_ph(LW_RWALK, 127, 32, a, 3000);
    int32_t jw = lfo_run_ph(LW_WANDER, 40, 32, a, 3000), k, changes = 0, prev;
    int16_t *q;
    printf("     LFO with PHASE 90: largest step S&H %d, RWALK %d, WANDER %d\n", js, jr, jw);
    check("LFO PHASE: S&H (glide), RWALK and WANDER stay continuous with PHASE set", js < 2000 && jr < 4000 && jw < 2000);
    host_init();                                     /* S&H 1 bar, TRIG HIT, hit every 1/16 */
    q = lfo_q(0, 0);
    q[LF_WAVE] = LW_SH;
    q[LF_DEST] = 9;
    q[LF_DEPTH] = 64;
    q[LF_TRIG] = LT_HIT;
    for (k = 0; k < 16; k++)
        trk[0].step[k].on = 1;
    play();
    render_mix(0, 0, CTL);
    prev = lfo_out(&trk[0], 0);
    for (k = 0; k < 2 * 16 * 172; k++) {             /* 2 bars */
        render_mix(0, 0, CTL);
        changes += lfo_out(&trk[0], 0) != prev;
        prev = lfo_out(&trk[0], 0);
    }
    check("LFO S&H with TRIG HIT: every hit draws a new value (faster than its cycle)", changes >= 16);
    host_init();                                     /* S&H, TRIG PLAY: modulates from the first block */
    q = lfo_q(0, 0);
    q[LF_WAVE] = LW_SH;
    q[LF_DEST] = 9;
    q[LF_DEPTH] = 64;
    play();
    render_mix(0, 0, CTL * 4);
    check("LFO S&H after PLAY: a value from the start (not 0 for the first cycle)", lfo_out(&trk[0], 0) != 0);
}

/* review: a SYNC LFO turned on while playing (DEPTH 0 -> 64) is on the bar */
static void test_lfo_activate_on_bar(void)
{
    int16_t *q;
    uint32_t ph;
    host_init();
    q = lfo_q(0, 0);
    q[LF_RATE] = 23;                                 /* SYNC 1 bar = 88200 samples */
    q[LF_DEST] = 9;
    q[LF_TRIG] = LT_PLAY;
    play();
    render_mix(0, 0, 22050);                         /* a quarter bar with DEPTH 0 */
    q[LF_DEPTH] = 64;
    render_mix(0, 0, CTL);
    ph = trk[0].lfo[0].ph;
    printf("     LFO turned on after a quarter bar: phase %.3f of the cycle\n", ph / 4294967296.0);
    check("LFO SYNC turned on while playing: on the bar (a quarter cycle in)", ph > 0x3C000000u && ph < 0x44000000u);
}

/* TRACKS "mute on the next bar" with a kick on step 1 (and step 16, still ringing at the bar): the muted bar has no
 * part of the kick and no click, the unmuted bar's kick has its full attack. A normal track, and the COMP source
 * (a muted source still plays for the compressor, its mute fades) */
#define MBAR (FS * 60 / 120 * 4)
static int32_t mb_ref[5 * MBAR], mb_out[5 * MBAR];
static void mute_bar_run(int32_t *o, int mute, int src)
{
    uint32_t f;
    host_init();
    trk[0].step[0].on = trk[0].step[15].on = 1;
    if (src) {
        song.g[G_CSRC] = 1;
        song.g[G_CGHOST] = src == 2 ? CG_MUTE : CG_KEEP;
        trk[1].p[P_DUCK] = 1;
    }
    play();
    for (f = 0; f < 5u * MBAR; f += CTL) {
        if (mute && (f / CTL == (MBAR + MBAR / 2) / CTL || f / CTL == (2 * MBAR + MBAR / 2) / CTL))
            song.mute_q |= 1;                        /* as TRACKS: mute for bar 2, unmute for bar 3 */
        render_mix(o + f, 0, CTL);
    }
}

static void test_mute_next_bar(void)
{
    int src;
    static const char *const WHO[3] = {"track", "COMP source", "COMP source, GHOST MUTE"};
    char what[96];
    for (src = 0; src < 3; src++) {
        int32_t on2 = 2 * MBAR - 64, on3 = 3 * MBAR - 64, i, leak = 0, pr = 0, pb = 0, dref = 0, dout = 0;
        mute_bar_run(mb_ref, 0, src);
        mute_bar_run(mb_out, 1, src);
        while (abs(mb_ref[on2]) <= 64 || abs(mb_ref[on2] - mb_ref[on2 - 1]) < 200)   /* the downbeat kick's onset */
            on2++;
        while (abs(mb_ref[on3]) <= 64 || abs(mb_ref[on3] - mb_ref[on3 - 1]) < 200)
            on3++;
        for (i = on2 + 64; i < on2 + 2000; i++)      /* after the old tail's declick (~30 samples); a kick that
                                                         * leaks lasts the 5 ms mute fade */
            leak += abs(mb_out[i]) > 64;
        for (i = on2 - 300; i < on2 - 4; i++)        /* the step-16 kick's tail just before the bar */
            dref = abs(mb_ref[i] - mb_ref[i - 1]) > dref ? abs(mb_ref[i] - mb_ref[i - 1]) : dref;
        for (i = on2 - 300; i < on2 + 2000; i++)
            dout = abs(mb_out[i] - mb_out[i - 1]) > dout ? abs(mb_out[i] - mb_out[i - 1]) : dout;
        for (i = on3; i < on3 + 64; i++) {
            pr = abs(mb_ref[i]) > pr ? abs(mb_ref[i]) : pr;
            pb = abs(mb_out[i]) > pb ? abs(mb_out[i]) : pb;
        }
        printf("     mute on the bar (%s): muted bar %d loud samples, largest step %d (tail %d); unmuted attack %d / %d\n",
               WHO[src], (int)leak, (int)dout, (int)dref, (int)pb, (int)pr);
        snprintf(what, sizeof what, "mute on the bar (%s): the muted bar has no part of the kick, no click", WHO[src]);
        check(what, leak == 0 && dout <= 2 * dref + 64);
        snprintf(what, sizeof what, "unmute on the bar (%s): the kick's full attack", WHO[src]);
        check(what, pb * 10 >= pr * 9);
    }
}

/* RESON parameters (params.c): TUNE as a note, DECAY as a time, STRCT named CHORD (with chord names) on CHORD */
static void test_reson_params(void)
{
    char v[12];
    const char *u;
    host_init();
    param_format(&TP[P_RTUNE], 48, v, &u);
    check("RESON TUNE 48 = C3", str_eq(v, "C3"));
    param_format(&TP[P_RTUNE], 96, v, &u);
    check("RESON TUNE 96 = C7", str_eq(v, "C7"));
    param_format(&TP[P_RTUNE], 25, v, &u);
    check("RESON TUNE 25 = C#1", str_eq(v, "C#1"));
    param_format(&TP[P_RDECAY], 0, v, &u);
    check("RESON DECAY 0 = 10 ms", str_eq(v, "10") && str_eq(u, "ms"));
    param_format(&TP[P_RDECAY], 127, v, &u);
    check("RESON DECAY 127 = 10.0 s", str_eq(v, "10.0") && str_eq(u, "s"));
    check("RESON defaults: OFF, C3, MIX 50 %, STRCT 0",
          trk[0].p[P_RMODEL] == RS_OFF && trk[0].p[P_RTUNE] == 48 && trk[0].p[P_RMIX] == 64 && trk[0].p[P_RSTRCT] == 0);
    check("RESON STRCT on STRNG is STRCT", str_eq(track_desc(&trk[0], P_RSTRCT)->label, "STRCT"));
    trk[0].p[P_RMODEL] = RS_CHORD;
    param_format(track_desc(&trk[0], P_RSTRCT), 3 * 128 / RS_NCHORD + 1, v, &u);
    check("RESON STRCT on CHORD is CHORD, with chord names (MAJ)",
          str_eq(track_desc(&trk[0], P_RSTRCT)->label, "CHORD") && str_eq(v, "MAJ"));
    param_format(track_desc(&trk[0], P_RSTRCT), 127, v, &u);
    check("RESON CHORD 127 = CLUST", str_eq(v, "CLUST"));
}

/* RESON (reson.c) helpers: a rim click through track 0's resonator, ring only, bright, no stiffness */
static void rs_setup(uint32_t model, int32_t tune)
{
    host_init();
    drum_set_model(&trk[0], DM_RIM);
    trk[0].p[P_RMODEL] = (int16_t)model;
    trk[0].p[P_RTUNE] = (int16_t)tune;
    trk[0].p[P_RDECAY] = 110;
    trk[0].p[P_RMIX] = 127;
    trk[0].p[P_RTONE] = 127;
    trk[0].p[P_RSTRCT] = 0;
    trk[0].p[P_RPOS] = 0;
}

static double rs_goertzel(const int32_t *x, uint32_t n, double f)   /* magnitude of f in x[0..n) */
{
    double w = 2 * M_PI * f / FS, c = 2 * cos(w), s0, s1 = 0, s2 = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}

static double rs_freq(const int32_t *x, uint32_t n, double f0)       /* the autocorrelation peak near FS / f0 */
{
    uint32_t lo = (uint32_t)(FS / f0 * 0.8), hi = (uint32_t)(FS / f0 * 1.25) + 2, lag, best = lo, i, j;
    double bv = -1e300, c[3], d;
    for (lag = lo; lag <= hi; lag++) {
        double s = 0;
        for (i = 0; i < n; i++)
            s += (double)x[i] * x[i + lag];
        if (s > bv) {
            bv = s;
            best = lag;
        }
    }
    for (j = 0; j < 3; j++) {
        c[j] = 0;
        for (i = 0; i < n; i++)
            c[j] += (double)x[i] * x[i + best - 1 + j];
    }
    d = c[0] - 2 * c[1] + c[2];
    return FS / (best + (d != 0 ? 0.5 * (c[0] - c[2]) / d : 0));
}

static double rs_note_hz(double note) { return 440.0 * pow(2.0, (note - 69) / 12); }

static void test_reson_pitch(void)
{
    static const int32_t NOTES[] = {24, 36, 48, 60, 72, 84};
    uint32_t i, ok = 1;
    double worst = 0;
    for (i = 0; i < sizeof NOTES / sizeof NOTES[0]; i++) {
        double f, want = rs_note_hz(NOTES[i]), cents;
        rs_setup(RS_STRNG, NOTES[i]);
        drum_hit(&trk[0], 127);
        render_mix(wl, wr, SECS(0.6));
        f = rs_freq(wl + SECS(0.2), 8192, want);
        cents = fabs(1200 * log2(f / want));
        worst = cents > worst ? cents : worst;
        ok &= cents < 10;
    }
    printf("     RESON STRNG pitch C1..C6: worst %.1f cents\n", worst);
    check("RESON STRNG: the ring is at TUNE (C1..C6, within 10 cents)", ok);
    rs_setup(RS_PIPE, 48);
    drum_hit(&trk[0], 127);
    render_mix(wl, wr, SECS(0.6));
    {
        double f0 = rs_note_hz(48), h1 = rs_goertzel(wl + SECS(0.2), 16384, f0), h2 = rs_goertzel(wl + SECS(0.2), 16384, 2 * f0);
        double h3 = rs_goertzel(wl + SECS(0.2), 16384, 3 * f0), f = rs_freq(wl + SECS(0.2), 8192, f0);
        printf("     RESON PIPE C3: %.1f Hz, harmonics 1 / 2 / 3: %.0f / %.0f / %.0f\n", f, h1, h2, h3);
        check("RESON PIPE: at TUNE, odd harmonics only", fabs(1200 * log2(f / f0)) < 10 && h2 * 10 < h1 && h2 * 5 < h3);
    }
}

static void test_reson_decay(void)
{
    static const int16_t DK[2] = {40, 80};
    uint32_t j;
    for (j = 0; j < 2u; j++) {
        uint32_t w, pkw = 0, t40 = 0;
        double pk = 0, want = RS_T60_MS10[DK[j]] / 10000.0, got;
        rs_setup(RS_STRNG, 48);
        trk[0].p[P_RDECAY] = DK[j];
        drum_hit(&trk[0], 127);
        render_mix(wl, wr, SECS(3));
        for (w = 0; w + 441 <= SECS(3); w += 441) {   /* 10 ms windows: RMS */
            double s = 0;
            uint32_t i;
            for (i = 0; i < 441; i++)
                s += (double)wl[w + i] * wl[w + i];
            s = sqrt(s / 441);
            if (s > pk) {
                pk = s;
                pkw = w;
            }
            if (!t40 && pk > 0 && w > pkw && s < pk / 100)
                t40 = w - pkw;
        }
        got = t40 * 1.5 / FS;                        /* -40 dB x 1.5 = -60 dB */
        printf("     RESON DECAY %d: 60 dB in %.3f s (knob %.3f s)\n", DK[j], got, want);
        check(j ? "RESON DECAY 80: the ring's 60 dB time as the knob (25 %)" : "RESON DECAY 40: the ring's 60 dB time as the knob (25 %)",
              t40 && fabs(got - want) <= 0.25 * want);
    }
}

static void test_reson_chord(void)
{
    static const int8_t MAJ[4] = {0, 4, 7, 12}, OFF[3] = {2, 6, 10};
    uint32_t i, ok = 1;
    double lo = 1e300, hi = 0;
    rs_setup(RS_CHORD, 48);
    trk[0].p[P_RSTRCT] = 3 * 128 / RS_NCHORD + 1;   /* MAJ */
    drum_hit(&trk[0], 127);
    render_mix(wl, wr, SECS(0.8));
    for (i = 0; i < 4u; i++) {                       /* 2nd harmonics: a click hardly excites the fundamentals here */
        double m = rs_goertzel(wl + SECS(0.1), 16384, 2 * rs_note_hz(48 + MAJ[i]));
        lo = m < lo ? m : lo;
    }
    for (i = 0; i < 3u; i++) {                       /* (D4 F#4 A#4: no partial of C3 E3 G3 C4) */
        double m = rs_goertzel(wl + SECS(0.1), 16384, 2 * rs_note_hz(48 + OFF[i]));
        hi = m > hi ? m : hi;
    }
    ok = lo > 4 * hi;
    printf("     RESON CHORD MAJ C3: weakest chord note %.0f, strongest other note %.0f\n", lo, hi);
    check("RESON CHORD MAJ on C3: C3 E3 G3 C4 ring, D3 F#3 A#3 do not", ok);
    rs_setup(RS_CHORD, 30);                           /* TUNE below C3: plays as C3 */
    drum_hit(&trk[0], 127);
    render_mix(wl, wr, SECS(0.6));
    check("RESON CHORD below C3 plays at C3", fabs(1200 * log2(rs_freq(wl + SECS(0.2), 8192, rs_note_hz(48)) / rs_note_hz(48))) < 15);
}

static void test_reson_mix_off_tail(void)
{
    static int32_t dry[SECS(1)];
    uint32_t i, same = 1;
    int32_t late;
    rs_setup(RS_OFF, 48);                             /* MIX 0 == OFF, bit for bit */
    drum_hit(&trk[0], 127);
    render_mix(dry, 0, SECS(1));
    rs_setup(RS_STRNG, 48);
    trk[0].p[P_RMIX] = 0;
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(1));
    for (i = 0; i < SECS(1); i++)
        same &= wl[i] == dry[i];
    check("RESON MIX 0: the dry sound, bit for bit", same);
    rs_setup(RS_STRNG, 48);                           /* the ring outlives the voice */
    trk[0].p[P_RDECAY] = 100;
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(1));
    late = peak_of(wl, SECS(0.8), SECS(1));
    check("RESON: the ring sounds on after the hit's voice (0.8 s on)", late > 200 && trk[0].rs.ring);
    rs_setup(RS_STRNG, 48);
    trk[0].p[P_RDECAY] = 20;
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(1.5));
    check("RESON: a short ring ends (the track stops computing it)", !trk[0].rs.ring);
}

static void test_reson_switch_and_cut(void)
{
    int32_t before = 0, after = 0, i, pk;
    rs_setup(RS_STRNG, 48);                           /* MODEL changed while ringing: the old ring fades out */
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(0.3));
    for (i = SECS(0.3) - 2000; i < (int32_t)SECS(0.3); i++)
        before = abs(wl[i] - wl[i - 1]) > before ? abs(wl[i] - wl[i - 1]) : before;
    trk[0].p[P_RMODEL] = RS_CHORD;
    render_mix(wl, 0, CTL * 4);
    for (i = 1; i < CTL * 4; i++)
        after = abs(wl[i] - wl[i - 1]) > after ? abs(wl[i] - wl[i - 1]) : after;
    printf("     RESON model switch while ringing: largest step %d (ring before %d)\n", after, before);
    check("RESON: switching MODEL while ringing does not click", after <= before + 64);
    trk[0].p[P_RMODEL] = RS_OFF;                      /* OFF while ringing, then on again without a hit */
    render_mix(wl, 0, CTL * 4);
    trk[0].p[P_RMODEL] = RS_STRNG;
    render_mix(wl, 0, SECS(0.2));
    pk = peak_of(wl, 0, SECS(0.2));
    check("RESON: no stale ring after OFF and on again", pk < 64);
    rs_setup(RS_STRNG, 48);                           /* a cut (mute) fades the ring out within a block */
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(0.3));
    drum_cut(&trk[0]);
    render_mix(wl, 0, CTL * 3);
    check("RESON: a cut (mute) ends the ring within a block", peak_of(wl, CTL * 2, CTL * 3) < 64 && !trk[0].rs.ring);
}

/* spec §7: TONE darkens the ring, STRCT stretches its overtones, POS thins harmonics (POS 64: a tap at a quarter
 * of the line, every 4th harmonic notched) */
static double rs_peak_near(const int32_t *x, uint32_t n, double f, double span)   /* the strongest frequency near f */
{
    double best = f, bv = -1, g;
    for (g = f * (1 - span); g <= f * (1 + span); g += 0.5) {
        double m = rs_goertzel(x, n, g);
        if (m > bv) {
            bv = m;
            best = g;
        }
    }
    return best;
}

/* COWB: TUNE moves the pitch (its own 540 / 800 Hz squares, 2^(TUNE / 12)), not only the band-pass: at TUNE -12 /
 * +12 the partials sit at half / double, and the untuned 540 Hz is gone */
static void test_cowb_tune(void)
{
    static const int32_t TUNES[3] = {-12, 0, 12};
    uint32_t i, ok = 1;
    for (i = 0; i < 3u; i++) {
        double r = pow(2.0, TUNES[i] / 12.0), lo, hi, off;
        host_init();
        drum_set_model(&trk[0], DM_COWB);
        trk[0].p[P_E0] = (int16_t)TUNES[i];
        trk[0].p[P_E1] = 127;
        drum_hit(&trk[0], 127);
        render_track(&trk[0], wl, SECS(0.3));
        lo = rs_goertzel(wl + SECS(0.02), 8192, 540.0 * r);
        hi = rs_goertzel(wl + SECS(0.02), 8192, 800.0 * r);
        off = TUNES[i] ? rs_goertzel(wl + SECS(0.02), 8192, 540.0) : 0;
        if (lo < 4 * off || hi < 4 * off || lo < 20 || hi < 20 ||
            fabs(rs_peak_near(wl + SECS(0.02), 8192, 540.0 * r, 0.03) / (540.0 * r) - 1) > 0.005) {
            printf("     COWB TUNE %d: |%.0f Hz| %.1f, |%.0f Hz| %.1f, |540 Hz| %.1f, peak near %.0f: %.1f Hz\n",
                   TUNES[i], 540 * r, lo, 800 * r, hi, off, 540 * r, rs_peak_near(wl + SECS(0.02), 8192, 540.0 * r, 0.03));
            ok = 0;
        }
    }
    check("COWB: TUNE moves its pitch (540 / 800 Hz x 2^(TUNE / 12)), not only its filter", ok);
}

static void test_reson_knobs(void)
{
    double f0 = rs_note_hz(48), h1, h6, dark, bright, h4, h4pos, f4, stretch;
    rs_setup(RS_STRNG, 48);                           /* TONE: the 6th harmonic against the 1st */
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(0.5));
    h1 = rs_goertzel(wl + SECS(0.1), 16384, f0);
    h6 = rs_goertzel(wl + SECS(0.1), 16384, 6 * f0);
    h4 = rs_goertzel(wl + SECS(0.1), 16384, 4 * f0);
    bright = h6 / h1;
    rs_setup(RS_STRNG, 48);
    trk[0].p[P_RTONE] = 30;
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(0.5));
    dark = rs_goertzel(wl + SECS(0.1), 16384, 6 * f0) / rs_goertzel(wl + SECS(0.1), 16384, f0);
    printf("     RESON TONE: 6th / 1st harmonic bright %.3f, dark %.3f\n", bright, dark);
    check("RESON TONE: darker damps the upper harmonics", dark * 3 < bright);
    rs_setup(RS_STRNG, 48);                           /* STRCT: the 4th partial moves off 4 x f0 */
    trk[0].p[P_RSTRCT] = 127;
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(0.5));
    f4 = rs_peak_near(wl + SECS(0.1), 16384, 4 * rs_freq(wl + SECS(0.1), 8192, f0), 0.08);
    stretch = 1200 * log2(f4 / (4 * rs_freq(wl + SECS(0.1), 8192, f0)));
    printf("     RESON STRCT 127: the 4th partial %.0f cents off 4 x f0\n", stretch);
    check("RESON STRCT: stretches the overtones (4th partial > 20 cents off)", fabs(stretch) > 20);
    rs_setup(RS_STRNG, 48);                           /* POS 64: the 4th harmonic notched */
    trk[0].p[P_RPOS] = 64;
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(0.5));
    h4pos = rs_goertzel(wl + SECS(0.1), 16384, 4 * f0) / rs_goertzel(wl + SECS(0.1), 16384, f0);
    printf("     RESON POS: 4th / 1st harmonic at POS 0 %.3f, POS 64 %.3f\n", h4 / h1, h4pos);
    check("RESON POS 64: thins the 4th harmonic", h4pos * 4 < h4 / h1);
}

/* review focus 3: sustained loud input at DECAY max with DIST after: bounded, and the ring dies after */
static void test_reson_sustain_bounded(void)
{
    uint32_t k;
    int32_t pk, after;
    rs_setup(RS_STRNG, 48);
    drum_set_model(&trk[0], DM_HNOIS);
    trk[0].p[P_RMODEL] = RS_STRNG;
    trk[0].p[P_RDECAY] = 127;
    trk[0].p[P_DIST] = 127;
    for (k = 0; k < 16u; k++)
        trk[0].step[k].on = 1;
    trk[0].p[P_SDIV] = 3;                             /* 1/32 */
    song.g[G_BPM] = 240;
    play();
    render_mix(wl, 0, SECS(8));
    pk = peak_of(wl, 0, SECS(8));
    transport_req = 2;
    render_mix(wl, 0, SECS(8));
    for (k = 0; k < 4u; k++)
        render_mix(wl, 0, SECS(8));                   /* 40 s after the input stopped */
    after = peak_of(wl, SECS(6), SECS(8));
    printf("     RESON DECAY max, noisy hits every 1/32, DIST: peak %d, 40 s later %d\n", pk, after);
    check("RESON at DECAY max: bounded, and the ring dies after the input stops", pk <= 32767 && after * 100 < pk);
}

/* review focus 4: a muted COMP source with RESON: the ring keys the compressor, the mix does not hear it */
static void test_reson_ghost_source(void)
{
    uint32_t k;
    rs_setup(RS_STRNG, 48);
    song.g[G_CSRC] = 1;
    trk[0].p[P_MUTE] = 1;
    for (k = 0; k < 16u; k += 4u)
        trk[0].step[k].on = 1;
    play();
    render_mix(wl, 0, SECS(2));
    check("RESON on a muted COMP source: not heard in the mix", peak_of(wl, SECS(0.1), SECS(2)) < 64);
}

static void test_reson_lfo(void)
{
    static const char *const NAME[6] = {"R.TUN", "R.DCY", "R.MIX", "R.TON", "R.STR", "R.POS"};
    char v[12];
    const char *u;
    uint32_t i, ok = 1, k;
    int32_t mv = 0, before = 0, during = 0;
    uint32_t len0, lmin = 0xFFFFFFFFu, lmax = 0, jump = 0, prev;
    host_init();
    for (i = 0; i < 6u; i++) {
        param_format(&TP[P_LFO1 + LF_DEST], 11 + (int32_t)i, v, &u);
        ok &= str_eq(v, NAME[i]);
    }
    param_format(&TP[P_LFO1 + LF_DEST], 0, v, &u);
    ok &= str_eq(v, "OFF");
    check("LFO DEST 11..16: R.TUN R.DCY R.MIX R.TON R.STR R.POS; 0 OFF (the knob runs on to F.CUT / F.RES, 18)",
          ok && TP[P_LFO1 + LF_DEST].max == 18);
    rs_setup(RS_STRNG, 48);                           /* R.TUN: a slow sweep moves the line length smoothly */
    trk[0].p[P_RDECAY] = 127;
    trk[0].p[P_LFO1 + LF_WAVE] = LW_TRI;
    trk[0].p[P_LFO1 + LF_MODE] = LM_TIME;
    trk[0].p[P_LFO1 + LF_RATE] = 100;
    trk[0].p[P_LFO1 + LF_DEST] = 11;
    trk[0].p[P_LFO1 + LF_DEPTH] = 64;
    trk[0].p[P_LFO1 + LF_TRIG] = LT_FREE;
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, CTL);
    len0 = prev = trk[0].rs.len[0];
    for (k = 0; k < 4000u; k++) {                     /* ~2.9 s */
        uint32_t l;
        render_mix(wl, 0, CTL);
        l = trk[0].rs.len[0];
        lmin = l < lmin ? l : lmin;
        lmax = l > lmax ? l : lmax;
        jump = (l > prev ? l - prev : prev - l) > jump ? (l > prev ? l - prev : prev - l) : jump;
        prev = l;
        if (k % 400u == 0u)
            drum_hit(&trk[0], 127);
    }
    printf("     RESON R.TUN sweep: line %u .. %u (Q8), largest block step %u (start %u)\n", lmin, lmax, jump, len0);
    check("LFO R.TUN: the resonator pitch sweeps smoothly (no semitone steps, over a semitone in total)",
          lmax - lmin > len0 / 17u && jump * 300u < len0);
    check("LFO R.TUN: the TUNE knob is untouched", trk[0].p[P_RTUNE] == 48);
    check("LFO R.TUN: the EDIT marker reports the live pitch", lfo_live(&trk[0], P_RTUNE, &mv) && mv != 48);
    rs_setup(RS_STRNG, 48);                           /* R.MIX: the modulated copy */
    trk[0].p[P_LFO1 + LF_WAVE] = LW_SQUARE;
    trk[0].p[P_LFO1 + LF_DEST] = 13;
    trk[0].p[P_LFO1 + LF_DEPTH] = -64;
    render_mix(wl, 0, CTL * 4);
    check("LFO R.MIX: modulates RESON MIX", lfo_live(&trk[0], P_RMIX, &mv) && mv != trk[0].p[P_RMIX]);
    rs_setup(RS_CHORD, 48);                           /* review focus 5: R.STR sweeping the chord types */
    trk[0].p[P_RDECAY] = 127;
    trk[0].p[P_LFO1 + LF_WAVE] = LW_SAW;
    trk[0].p[P_LFO1 + LF_MODE] = LM_HZ;
    trk[0].p[P_LFO1 + LF_RATE] = 90;
    trk[0].p[P_LFO1 + LF_DEST] = 15;
    trk[0].p[P_LFO1 + LF_DEPTH] = 64;
    trk[0].p[P_RSTRCT] = 64;
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(0.2));
    before = peak_of(wl, 0, SECS(0.2));
    render_mix(wl, 0, SECS(3));
    during = peak_of(wl, 0, SECS(3));
    check("LFO R.STR on CHORD: sweeping the chords under a ring stays bounded", during <= 32767 && during < before * 4 + 1000);
}

/* final review 1: every ring ends (the line all zero) after the input stops, at DECAY max and any pitch / TONE /
 * STRCT / POS; a mute after a long silence does not thump (no stuck DC in the line) */
static void test_reson_ring_ends(void)
{
    static const int16_t NOTE[3] = {24, 48, 96}, END[2] = {0, 127}, POS[2] = {0, 64};
    uint32_t m, a, b, c, d, i, stuck = 0, runs = 0, nz;
    int32_t pk;
    for (m = RS_STRNG; m < RS_NMODEL; m++)
        for (a = 0; a < 3u; a++)
            for (b = 0; b < 2u; b++)
                for (c = 0; c < 2u; c++)
                    for (d = 0; d < 2u; d++) {
                        host_init();
                        drum_set_model(&trk[0], DM_K909);
                        trk[0].p[P_RMODEL] = (int16_t)m;
                        trk[0].p[P_RTUNE] = NOTE[a];
                        trk[0].p[P_RTONE] = END[b];
                        trk[0].p[P_RSTRCT] = END[c];
                        trk[0].p[P_RPOS] = POS[d];
                        trk[0].p[P_RDECAY] = 127;
                        drum_hit(&trk[0], 127);
                        render_mix(0, 0, SECS(20));
                        for (i = 0, nz = 0; i < RS_LEN; i++)
                            nz += rs_buf[0][i] != 0;
                        runs++;
                        if (trk[0].rs.ring || nz) {
                            if (stuck < 4u)
                                printf("     ring stuck: %s note %d TONE %d STRCT %d POS %d: ring %u, %u non-zero\n",
                                       N_RMODEL[m], NOTE[a], END[b], END[c], POS[d], trk[0].rs.ring, nz);
                            stuck++;
                        }
                    }
    printf("     RESON ring end at DECAY max: %u of %u settings stuck after 20 s\n", stuck, runs);
    check("RESON: every ring ends after the input stops (DECAY max, any pitch / TONE / STRCT / POS)", stuck == 0);
    rs_setup(RS_STRNG, 48);                           /* a mute 20 s after the hit: nothing left to thump */
    drum_set_model(&trk[0], DM_K909);
    trk[0].p[P_RSTRCT] = 127;
    trk[0].p[P_RDECAY] = 127;
    drum_hit(&trk[0], 127);
    render_mix(0, 0, SECS(20));
    drum_cut(&trk[0]);
    render_mix(wl, 0, SECS(0.2));
    pk = peak_of(wl, 0, SECS(0.2));
    printf("     RESON mute 20 s after a hit (STRNG C3 STRCT 127): output peak %d\n", pk);
    check("RESON: a mute long after a hit does not thump", pk < 16);
}

/* final review 2: a cut (mute / choke) ends the ring, and the cut voice's declick tail starts no new one */
static void test_reson_cut_no_reexcite(void)
{
    static const uint32_t MODEL[2] = {DM_K909, DM_TOM};
    uint32_t j;
    for (j = 0; j < 2u; j++) {
        int32_t pk;
        uint32_t i, zc = 0;
        host_init();
        drum_set_model(&trk[0], MODEL[j]);
        trk[0].p[P_RMODEL] = RS_STRNG;
        trk[0].p[P_RTUNE] = 36;
        trk[0].p[P_RDECAY] = 127;
        drum_hit(&trk[0], 127);
        render_mix(0, 0, SECS(0.03));
        drum_cut(&trk[0]);
        render_mix(wl, 0, SECS(2));
        pk = peak_of(wl, SECS(0.5), SECS(2));        /* (before 0.5 s the master DC blocker settles from the cut, as
                                                         * without RESON: no zero crossing) */
        for (i = SECS(0.1); i < SECS(2); i++)
            zc += (wl[i] > 0) != (wl[i - 1] > 0);
        printf("     RESON cut 30 ms after a %s hit: peak 0.5..2 s %d, zero crossings after 0.1 s %u, ring %u\n",
               N_MODEL[MODEL[j]], pk, zc, trk[0].rs.ring);
        check(j ? "RESON: a cut while a TOM sounds: no new ring from its declick" : "RESON: a cut while a K909 sounds: no new ring from its declick",
              pk < 16 && zc <= 2u && !trk[0].rs.ring);   /* (a ring at C2 would cross zero hundreds of times) */
    }
}

/* upstream issue #31: track swing + global swing (each up to 100) is capped at 100 (1.4 / 0.6 steps), not 200
 * (1.8 / 0.2) */
static void test_swing_cap(void)
{
    uint32_t at[4], n = 0, f, a;
    double r;
    host_init();
    trk[0].step[0].on = trk[0].step[1].on = trk[0].step[2].on = 1;
    trk[0].p[P_SSWING] = 100;
    song.g[G_SWING] = 100;
    play();
    a = hit_age(&trk[0]);
    for (f = 0; f < SECS(2) && n < 3u; f += CTL) {
        render_mix(0, 0, CTL);
        if (hit_age(&trk[0]) != a) {
            a = hit_age(&trk[0]);
            at[n++] = f;
        }
    }
    r = n == 3u ? (double)(at[1] - at[0]) / (double)(at[2] - at[1]) : 0;
    printf("     swing 100 + 100: long / short step %.2f (capped 1.4 / 0.6 = 2.33)\n", r);
    check("swing: track + global swing capped at 100 (#31)", n == 3u && r > 2.1 && r < 2.6);
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

/* sound pack: the slow divisions (ids 6..9: 1/2 1/1 2BAR 4BAR) */
static void test_slow_divisions(void)
{
    static const uint32_t W120[10] = {22050, 11025, 5512, 2756, 7350, 3675, 44100, 88200, 176400, 352800};
    static const uint32_t W40[10] = {66150, 33075, 16537, 8268, 22050, 11025, 132300, 264600, 529200, 1058400};
    static const uint32_t W240[10] = {11025, 5512, 2756, 1378, 3675, 1837, 22050, 44100, 88200, 176400};
    uint32_t d, ok = 1, n0 = 0, n1 = 0, f, bar = FS * 60 / 240 * 4, a0, a1, at0[12], at1[4];
    host_init();
    for (d = 0; d < 10u; d++) {
        song.g[G_BPM] = 120;
        ok &= div_samples(d) == W120[d];
        song.g[G_BPM] = 40;
        ok &= div_samples(d) == W40[d];
        song.g[G_BPM] = 240;
        ok &= div_samples(d) == W240[d];
    }
    check("divisions: all ten exact at 40, 120, 240 BPM (1/2 .. 4BAR = 2 .. 16 beats)", ok);

    host_init();                                     /* 240 BPM: a bar is 44100 samples */
    song.g[G_BPM] = 240;
    trk[0].step[0].on = 1;                           /* 1/16, one hit a bar */
    trk[1].p[P_SDIV] = 9;                            /* 4BAR, every step on: one hit every 4 bars */
    for (d = 0; d < 16u; d++)
        trk[1].step[d].on = 1;
    play();
    a0 = hit_age(&trk[0]);
    a1 = hit_age(&trk[1]);
    for (f = 0; f < 9u * bar; f += CTL) {
        render_mix(0, 0, CTL);
        if (hit_age(&trk[0]) != a0 && n0 < 12u)
            at0[n0++] = f, a0 = hit_age(&trk[0]);
        if (hit_age(&trk[1]) != a1 && n1 < 4u)
            at1[n1++] = f, a1 = hit_age(&trk[1]);
    }
    check("divisions: a 4BAR track fires once every 4 bars, on the 1/16 track's bar hits",
          n0 >= 9u && n1 == 3u && at1[0] == at0[0] && at1[1] == at0[4] && at1[2] == at0[8]);

    host_init();
    song.g[G_BPM] = 40;
    trk[0].p[P_SDIV] = 9;
    trk[0].p[P_SSWING] = 100;
    {
        uint64_t p = 1058400u, sw = p * (uint64_t)track_swing(&trk[0]) / 250u;
        check("divisions: swing on a 4BAR step at 40 BPM (no overflow)",
              track_swing(&trk[0]) > 0 && step_samples(&trk[0], 1058400u, 0) == p + sw &&
              step_samples(&trk[0], 1058400u, 1) == p - sw);
    }

    host_init();
    song.g[G_BPM] = 240;
    trk[0].p[P_SDIV] = 8;                            /* 2BAR: 88200 samples a step */
    trk[0].step[0].on = 1;
    trk[0].step[0].rat = 1;                          /* 2 hits */
    play();
    {
        uint32_t at[4];
        check("divisions: a ratchet on a 2BAR step rolls 2 hits inside the step", hits_at(0, 88200u, at, 4) == 2u);
    }

    host_init();
    song.g[G_BPM] = 240;
    trk[0].p[P_SDIV] = 8;
    song.rec = 1u;
    play();
    render_mix(0, 0, 88200u + 88200u * 3u / 4u / CTL * CTL);   /* 3/4 into step 1 */
    fm1_in.notes = 1u << KEY_TRK_KEY[0];
    render_mix(0, 0, CTL);
    fm1_in.notes = 0;
    check("divisions: live record on a 2BAR step: a late hit goes into the next step",
          trk[0].step[2].on && !trk[0].step[1].on);

    host_init();
    song.g[G_BPM] = 120;
    song.g[G_DTIME] = 8;                             /* 2BAR = 4 s: longer than the delay line */
    check("divisions: delay TIME 2BAR is cut to the delay line (1.49 s)", delay_samples() == DLY_LEN - 1u);
    song.g[G_DTIME] = 6;                             /* 1/2 at 120 BPM = 1 s: fits */
    check("divisions: delay TIME 1/2 at 120 BPM is 1 s", delay_samples() == 44100u);
}

/* review focus 3: DIV 1/16 -> 4BAR -> 1/16 while playing: the track keeps playing */
static void test_div_change_while_playing(void)
{
    uint32_t at[8], d;
    host_init();
    song.g[G_BPM] = 240;
    for (d = 0; d < 16u; d++)
        trk[0].step[d].on = 1;
    play();
    render_mix(0, 0, 3u * 2756u);
    trk[0].p[P_SDIV] = 9;
    render_mix(0, 0, 2u * 2756u);
    trk[0].p[P_SDIV] = 2;
    check("divisions: 1/16 -> 4BAR -> 1/16 while playing: hits resume within two 1/16 steps",
          hits_at(0, 2u * 2756u + CTL, at, 8) >= 1u);
}

/* review focus 4: a BPM change in the middle of a 4BAR step: the next step comes at the new length */
static void test_bpm_change_in_slow_step(void)
{
    uint32_t at[4], d;
    host_init();
    song.g[G_BPM] = 120;
    trk[0].p[P_SDIV] = 9;                            /* 4BAR at 120 BPM: 352800 samples */
    for (d = 0; d < 16u; d++)
        trk[0].step[d].on = 1;
    play();
    render_mix(0, 0, CTL);                           /* step 0 */
    render_mix(0, 0, 100000u / CTL * CTL);
    song.g[G_BPM] = 240;                             /* 4BAR now 176400 */
    check("divisions: BPM doubled inside a 4BAR step: the next step within the new length",
          hits_at(0, 176400u, at, 4) == 1u);
}

/* exact step timing: a division is num / den beats; each track carries its step-length remainder, so den steps last
 * exactly num beats: no drift between divisions (or against an external clock) */
static void test_exact_timing(void)
{
    static const int16_t BPMS[4] = {97, 120, 133, 171};
    uint32_t b, d, ok = 1, f, a0, a1, n0 = 0, n1 = 0, bad = 0, h0[1100], h1[300];
    for (b = 0; b < 4u; b++) {
        host_init();
        song.g[G_BPM] = BPMS[b];
        for (d = 0; d < 10u; d++) {
            static const uint32_t NUM[10] = {1, 1, 1, 1, 1, 1, 2, 4, 8, 16}, DEN[10] = {1, 2, 4, 8, 3, 6, 1, 1, 1, 1};
            uint32_t k, rem = 0, sum = 0, beat = (uint32_t)FS * 60u / (uint32_t)BPMS[b];
            for (k = 0; k < DEN[d]; k++) {
                sum += div_period(d, rem);
                rem = div_rem_next(d, rem);
            }
            ok &= sum == beat * NUM[d] && rem == 0u;
        }
    }
    check("exact timing: den steps of every division last exactly num beats (97, 120, 133, 171 BPM)", ok);

    host_init();                                     /* 120 BPM: a 1/16 and a 1/4 track over 256 beats */
    trk[0].p[P_SDIV] = 2;
    trk[1].p[P_SDIV] = 0;
    for (d = 0; d < 16u; d++)
        trk[0].step[d].on = trk[1].step[d].on = 1;
    play();
    a0 = hit_age(&trk[0]);
    a1 = hit_age(&trk[1]);
    for (f = 0; f < 256u * 22050u; f += CTL) {
        render_mix(0, 0, CTL);
        if (hit_age(&trk[0]) != a0) {
            a0 = hit_age(&trk[0]);
            if (n0 < 1100u)
                h0[n0] = f;
            n0++;
        }
        if (hit_age(&trk[1]) != a1) {
            a1 = hit_age(&trk[1]);
            if (n1 < 300u)
                h1[n1] = f;
            n1++;
        }
    }
    for (b = 0; b < 256u && 4u * b < 1100u; b++)
        bad += h0[4u * b] != h1[b];
    check("exact timing: a 1/16 and a 1/4 track at 120 BPM stay together for 256 beats (every 4th 1/16 on the 1/4)",
          n1 >= 256u && n0 >= 1024u && bad == 0u);

    host_init();                                     /* review focus 5: swing 60 on a 1/16 track: pairs keep their total */
    trk[0].p[P_SDIV] = 2;
    trk[0].p[P_SSWING] = 60;
    for (d = 0; d < 16u; d++)
        trk[1].step[d].on = trk[0].step[d].on = 1;
    trk[1].p[P_SDIV] = 0;
    play();
    a0 = hit_age(&trk[0]);
    a1 = hit_age(&trk[1]);
    n0 = n1 = 0;
    for (f = 0; f < 64u * 22050u; f += CTL) {
        render_mix(0, 0, CTL);
        if (hit_age(&trk[0]) != a0) {
            a0 = hit_age(&trk[0]);
            if (n0 < 1100u)
                h0[n0] = f;
            n0++;
        }
        if (hit_age(&trk[1]) != a1) {
            a1 = hit_age(&trk[1]);
            if (n1 < 300u)
                h1[n1] = f;
            n1++;
        }
    }
    bad = 0;
    for (b = 0; b < 64u; b++)
        bad += h0[4u * b] != h1[b];
    check("exact timing: with swing, every 4th 1/16 still lands on the 1/4 (64 beats)", n1 >= 64u && bad == 0u);

    host_init();                                     /* review focus 1: DIV 1/16 -> 8T -> 1/16 while playing */
    trk[0].p[P_SDIV] = 2;
    for (d = 0; d < 16u; d++)
        trk[0].step[d].on = 1;
    play();
    render_mix(0, 0, 3u * 5512u);
    trk[0].p[P_SDIV] = 4;
    render_mix(0, 0, 3u * 7350u);
    trk[0].p[P_SDIV] = 2;
    {
        uint32_t at[8];
        check("exact timing: DIV changed while playing: the track keeps stepping", hits_at(0, 3u * 5513u, at, 8) >= 2u &&
              trk[0].seq_rem < 4u);
    }
}

/* final review: the SLICER keeps the steps' exact timing (its slices carry their remainder too): the k-th slice and
 * the k-th step of the same division keep their offset within a block (the slice is seen about a block before its step) for
 * 2 minutes at 120 BPM (1/8 1/16 1/32 8T 16T); before, the slices fell behind by ~240 samples a minute */
static void test_slicer_exact(void)
{
    static const int16_t SLR[5] = {0, 1, 2, 3, 4}, DIV[5] = {1, 2, 3, 4, 5};   /* SLRATE -> the same step DIV */
    static uint32_t st[4000], sv[4000];
    uint32_t r, ok = 1;
    for (r = 0; r < 5u; r++) {
        uint32_t f, cnt, bad = 0, ns = 0, nv = 0, k;
        uint8_t idx;
        host_init();
        song.g[G_BPM] = 120;
        for (f = 0; f < 16u; f++)
            trk[0].step[f].on = 1;
        trk[0].p[P_SDIV] = DIV[r];
        trk[0].p[P_SLCR] = 1;                        /* GATE */
        trk[0].p[P_SLRATE] = SLR[r];
        play();
        cnt = trk[0].seq_cnt;
        idx = sl[0].idx;
        for (f = 0; f < 120u * FS; f += CTL) {
            render_mix(0, 0, CTL);
            if (trk[0].seq_cnt != cnt && ns < 4000u)
                st[ns++] = f;
            if (sl[0].idx != idx && nv < 4000u)
                sv[nv++] = f;
            cnt = trk[0].seq_cnt;
            idx = sl[0].idx;
        }
        for (k = 1; k < ns && k < nv; k++)
            {   /* within a block of the first offset (a boundary on a block edge is seen a block apart) */
                int32_t d = (int32_t)(sv[k] - st[k]) - (int32_t)(sv[0] - st[0]);
                bad += d > (int32_t)CTL || d < -(int32_t)CTL;
            }
        if (bad)
            printf("     SLRATE %d: %u of %u slices off their step's offset (last %d samples, first %d)\n", SLR[r], bad, ns,
                   (int32_t)(sv[ns < nv ? ns - 1u : nv - 1u] - st[ns < nv ? ns - 1u : nv - 1u]), (int32_t)(sv[0] - st[0]));
        ok &= bad == 0u && ns > 400u && nv >= ns - 1u;
    }
    check("exact timing: SLICER slices keep their steps' timing for 2 minutes at 120 BPM (5 rates)", ok);
}

int main(void)
{
    test_cond_codes();
    test_comp_engine();
    test_comp_formats();
    test_comp_db_text();
    test_comp_ducks();
    test_comp_off_identical();
    test_comp_source_not_ducked();
    test_comp_ghost();
    test_comp_ghost_modes();
    test_comp_ghost_switch();
    test_comp_src_change();
    test_comp_extremes();
    test_comp_mute_click();
    test_lfo_shapes();
    test_lfo_rates();
    test_lfo_apply();
    test_lfo_off_identical();
    test_lfo_two_on_one();
    test_lfo_absent_dest();
    test_lfo_edit_while_modulated();
    test_lfo_trig();
    test_lfo_random();
    test_lfo_sound();
    test_lfo_play_first_hit();
    test_lfo_random_phase_restart();
    test_lfo_activate_on_bar();
    test_mute_next_bar();
    test_reson_params();
    test_swing_cap();
    test_reson_pitch();
    test_reson_decay();
    test_reson_chord();
    test_reson_mix_off_tail();
    test_reson_switch_and_cut();
    test_reson_knobs();
    test_reson_sustain_bounded();
    test_reson_ghost_source();
    test_reson_lfo();
    test_reson_ring_ends();
    test_reson_cut_no_reexcite();
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
    test_exact_timing();
    test_slicer_exact();
    test_seq_edges();
    test_keys();
    test_midi();
    test_midi_ring();
    test_midi_safe_start();
    test_midi_trs();
    test_live_record();
    test_slow_divisions();
    test_div_change_while_playing();
    test_bpm_change_in_slow_step();
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
    test_cowb_tune();
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
