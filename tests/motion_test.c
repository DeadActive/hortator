/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* MOTION (motion.c): knob moves recorded per step while armed and playing, played back with the pattern, the base
 * back at step 0 and STOP, the LFOs' copy respected. */
#include "drum_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

#define STEP16 (22050u / 4u)                       /* a 1/16 at 120 BPM: 5512 samples */
static void fresh(void)                            /* stopped, 120 BPM, track 1 a kick on every 1/4, selected */
{
    uint32_t k;
    host_init();
    song.g[G_BPM] = 120;
    for (k = 0; k < 16u; k += 4u)
        trk[0].step[k].on = 1;
    song.sel = 0;
}
static void play(void) { transport_req = 1; render_mix(0, 0, CTL); }
static void stop(void) { transport_req = 2; render_mix(0, 0, CTL); }
static void to_step(uint32_t s, uint32_t into)   /* render until track 1 is `into` samples into step s */
{
    uint32_t guard = 0;
    while ((trk[0].seq_idx != s || trk[0].seq_pos < into) && guard++ < 200000u)
        render_mix(0, 0, CTL);
}
static void turn_to(uint32_t id, int16_t v)      /* what ui_input does after a knob edit */
{
    trk[0].p[id] = v;
    motion_capture(&trk[0], id);
}
static int has_event(uint32_t k, uint32_t s, uint32_t id, int v)
{
    uint32_t i;
    for (i = 0; i < mo.s.count; i++)
        if (mo.s.ev[i].trk == k && mo.s.ev[i].step == s && mo.s.ev[i].param == id && mo.s.ev[i].value == v)
            return 1;
    return 0;
}

static void test_record_nearest(void)
{
    fresh();
    song.rec = 1u;                                    /* track 1 armed */
    play();
    to_step(4, 1000);                                 /* early in step 4 */
    turn_to(P_E1, 90);
    check("record: a turn early in step 4 lands on step 4", has_event(0, 4, P_E1, 90) && motion_on(0));
    to_step(5, STEP16 / 2u + 400u);                   /* past the half of step 5 */
    turn_to(P_E1, 50);
    check("record: a turn past the half of step 5 lands on step 6", has_event(0, 6, P_E1, 50));
    to_step(4, 2000);                                 /* (the next loop) */
    turn_to(P_E1, 70);
    check("record: the same knob on the same step again replaces the value",
          has_event(0, 4, P_E1, 70) && !has_event(0, 4, P_E1, 90) && motion_count(0) == 2u);
    stop();
}

static void test_not_recording(void)
{
    uint32_t n;
    fresh();
    play();
    to_step(2, 100);
    turn_to(P_E1, 33);                                /* not armed */
    check("not armed: no event, the turn is the new base", !mo.s.count && mo.base[0][P_E1] == 33);
    song.rec = 1u;
    song.sel = 1;                                     /* armed track 1, but track 2 selected */
    trk[1].p[P_E1] = 40;
    motion_capture(&trk[1], P_E1);
    song.sel = 0;
    check("another track's knob: not recorded", !mo.s.count);
    turn_to(P_MODEL, 3);
    turn_to(P_SLEN, 8);
    turn_to(P_LFO1 + LF_WAVE, 2);
    n = mo.s.count;
    check("MODEL, LEN and an LFO's WAVE are never recorded", n == 0u);
    trk[0].p[P_MODEL] = TP[P_MODEL].def;
    trk[0].p[P_SLEN] = 16;
    stop();
    turn_to(P_E1, 20);
    check("stopped: no event", !mo.s.count);
}

static void test_recordable_list(void)
{
    static const uint8_t YES[] = {P_E0, P_E7, P_LEVEL, P_PAN, P_DIST, P_REV, P_SLPAT, P_SLRATE, P_SLDEPTH, P_LLEVEL,
                                  P_LDEC, P_LFO1 + LF_RATE, P_LFO2 + LF_MORPH, P_LFO2 + LF_DEPTH, P_RTUNE, P_RPOS};
    static const uint8_t NO[] = {P_MODEL, P_MUTE, P_CHOKE, P_NOTE, P_SLCR, P_SLEN, P_SDIV, P_SSWING, P_LSET, P_LKEY,
                                 P_SRC, P_DUCK, P_LFO1 + LF_WAVE, P_LFO1 + LF_MODE, P_LFO2 + LF_DEST,
                                 P_LFO2 + LF_TRIG, P_LFO1 + LF_PHASE, P_RMODEL};
    uint32_t i, ok = 1;
    for (i = 0; i < sizeof YES; i++)
        ok &= motion_param(YES[i]);
    for (i = 0; i < sizeof NO; i++)
        ok &= !motion_param(NO[i]);
    check("the recordable knobs are the sound knobs of the spec, the rest not", ok);
}

static void test_full(void)
{
    uint32_t s, id = P_E0;
    fresh();
    for (s = 0; mo.s.count < MOTION_MAX; s++) {       /* 128 events: steps 0..63 x E0, E1 */
        motion_add(0, s % 64u, id, 10);
        if (s == 63u)
            id = P_E1;
    }
    song.rec = 1u;
    play();
    to_step(3, 100);
    turn_to(P_E2, 99);                                /* a new place */
    check("128 events: the next is refused, flagged full", mo.s.count == MOTION_MAX && mo.full && !has_event(0, 3, P_E2, 99));
    turn_to(P_E0, 77);                                /* an existing place: replaced, not full */
    check("full: a place already used is still replaced", has_event(0, 3, P_E0, 77));
    stop();
}

static void test_playback(void)
{
    fresh();
    trk[0].p[P_SLEN] = 8;
    trk[0].p[P_E1] = 64;                              /* the base */
    motion_add(0, 4, P_E1, 100);
    mo.s.on = 1u;
    play();
    to_step(3, 100);
    check("playback: before its step, the base", trk[0].p[P_E1] == 64);
    to_step(4, 100);
    check("playback: at step 4 the recorded value", trk[0].p[P_E1] == 100);
    to_step(7, 100);
    check("playback: it holds through the following steps", trk[0].p[P_E1] == 100);
    to_step(0, 100);
    check("playback: the loop start puts the base back", trk[0].p[P_E1] == 64);
    to_step(5, 100);
    motion_set_play(0, 0);
    check("PLAY OFF: the base back, the events kept", trk[0].p[P_E1] == 64 && mo.s.count == 1u);
    to_step(4, 100);
    check("PLAY OFF: the events do not play", trk[0].p[P_E1] == 64);
    motion_set_play(0, 1);
    to_step(5, 100);
    turn_to(P_E1, 30);                                /* not armed: a turn while motion plays */
    to_step(0, 100);
    check("a turn while motion plays becomes the base the loop start comes back to", trk[0].p[P_E1] == 30);
    stop();
}

/* Review Focus 1 */
static void test_stop_restores(void)
{
    fresh();
    trk[0].p[P_E1] = 64;
    song.rec = 1u;
    play();
    to_step(2, 100);
    turn_to(P_E1, 120);                               /* recorded and sounding */
    stop();
    check("STOP: the knob is the patch's again (a save holds the patch)", trk[0].p[P_E1] == 64);
}

/* Review Focus 2 */
static void test_lfo_and_motion(void)
{
    fresh();
    trk[0].p[P_E1] = 64;
    trk[0].p[P_LFO1 + LF_DEST] = 2;                   /* LFO 1 on the model's 2nd knob (lfo.c lfo_dest_param: 2 = P_E1) */
    trk[0].p[P_LFO1 + LF_DEPTH] = 60;
    motion_add(0, 2, P_E1, 100);
    mo.s.on = 1u;
    play();
    to_step(2, 64);
    check("LFO + motion: after the block the knob rests on motion's value", trk[0].p[P_E1] == 100);
    to_step(0, 64);
    check("LFO + motion: the loop start puts the patch's value back", trk[0].p[P_E1] == 64);
    stop();
}

/* Review Focus 5 */
static void test_len_shortened(void)
{
    fresh();
    trk[0].p[P_E1] = 64;
    motion_add(0, 12, P_E1, 100);
    motion_add(0, 1, P_E2, 9);
    mo.s.on = 1u;
    trk[0].p[P_SLEN] = 8;
    play();
    to_step(7, 100);
    to_step(1, 100);
    check("LEN below a recorded step: it does not play, the others do", trk[0].p[P_E1] == 64 && trk[0].p[P_E2] == 9);
    to_step(0, 100);
    check("LEN below: step 0 still restores", trk[0].p[P_E2] == mo.base[0][P_E2]);
    stop();
}

static void test_clamp(void)
{
    uint32_t m, j, found = 0;
    for (m = 0; m < NMODELS && !found; m++)
        for (j = 0; j < 8u && !found; j++)
            if (DMODELS[m].edit[j].max < 127 && DMODELS[m].edit[j].max > DMODELS[m].edit[j].min) {
                fresh();
                drum_set_model(&trk[0], m);
                motion_add(0, 1, P_E0 + j, 127);
                mo.s.on = 1u;
                play();
                to_step(1, 100);
                found = 1;
                check("a recorded value outside the model's range is clamped when played",
                      trk[0].p[P_E0 + j] == DMODELS[m].edit[j].max);
                stop();
            }
    check("(a model knob with a narrower range exists for the clamp test)", found);
}

static void test_grids_track(void)
{
    fresh();
    trk[0].p[P_SRC] = 1;                              /* track 1 plays Grids' kick */
    trk[0].p[P_E1] = 64;
    motion_add(0, 3, P_E1, 111);
    mo.s.on = 1u;
    play();
    to_step(3, 100);
    check("a Grids track: its step clock plays its motion", trk[0].p[P_E1] == 111);
    stop();
}

static void clk_send(uint32_t status, double at)
{
    fm1_ms = (uint32_t)(at * 1000.0 / FS);
    midi_enqueue(0x0Fu | status << 8, 1u);
}
static void test_midi_clock(void)
{
    double pulse = FS * 60.0 / 120.0 / 24.0, next = 0.0;
    uint64_t now = 0;
    uint32_t b, seen = 0;
    fresh();
    trk[0].p[P_E1] = 64;
    motion_add(0, 2, P_E1, 101);
    mo.s.on = 1u;
    song.g[G_CLOCK] = 1;
    render_mix(0, 0, CTL);
    clk_send(0xFAu, 0.0);
    for (b = 0; b < 2000u && !seen; b++) {
        while (next < (double)(now + CTL)) {
            clk_send(0xF8u, next);
            next += pulse;
        }
        fm1_ms = (uint32_t)((now + CTL) * 1000u / FS);
        render_mix(0, 0, CTL);
        now += CTL;
        seen = trk[0].seq_idx == 2u && trk[0].p[P_E1] == 101;
    }
    check("CLK USB: the motion plays on the clock's steps", seen);
    song.g[G_CLOCK] = 0;
    render_mix(0, 0, CTL);
}

static void test_clear(void)
{
    fresh();
    trk[0].p[P_E1] = 64;
    motion_add(0, 1, P_E1, 100);
    motion_add(1, 1, P_E1, 100);
    mo.s.on = 3u;
    play();
    to_step(2, 100);
    motion_clear(0);
    check("clear: track 1's events gone and its knob back, track 2's kept",
          motion_count(0) == 0u && motion_count(1) == 1u && trk[0].p[P_E1] == 64 && !motion_on(0) && motion_on(1));
    stop();
}

/* review: an LFO (FREE) on a recorded knob: the event's own hit reads motion's value, not the step before's */
static void test_lfo_hit_value(void)
{
    uint32_t b, fired = 0;
    int32_t v = 0;
    fresh();
    trk[0].p[P_E1] = 40;
    trk[0].p[P_LFO1 + LF_DEST] = 2;                   /* E1 */
    trk[0].p[P_LFO1 + LF_DEPTH] = 1;
    trk[0].p[P_LFO1 + LF_TRIG] = LT_FREE;
    trk[0].step[2].on = 1;
    motion_add(0, 2, P_E1, 120);
    play();
    to_step(1, 100);
    for (b = 0; b < 1000u && !fired; b++) {          /* mix_block's order: the LFOs, the events, (the hits), back */
        lfo_apply(CTL);
        events_block(CTL);
        fired = trk[0].seq_idx == 2u;
        v = trk[0].p[P_E1];
        lfo_restore();
    }
    check("LFO (FREE) + motion: step 2's hit reads motion's value (within the LFO's depth of 120)",
          fired && v >= 110 && v <= 127);
    stop();
}

/* review: MIDI Continue takes the base, so a turn recorded after it comes back on STOP */
static void test_continue_base(void)
{
    fresh();
    trk[0].p[P_E1] = 40;
    song.g[G_CLOCK] = 1;
    render_mix(0, 0, CTL);                            /* the mode change settles */
    midi_clock_transport(0xFBu, fm1_ms);              /* Continue: playing, the steps wait for pulses */
    song.rec = 1u;
    turn_to(P_E1, 100);
    midi_clock_transport(0xFCu, fm1_ms);              /* Stop */
    check("MIDI Continue then a recorded turn: STOP puts the patch's value back", !song.playing && trk[0].p[P_E1] == 40);
    song.g[G_CLOCK] = 0;
    render_mix(0, 0, CTL);
}

int main(void)
{
    test_record_nearest();
    test_not_recording();
    test_recordable_list();
    test_full();
    test_playback();
    test_stop_restores();
    test_lfo_and_motion();
    test_len_shortened();
    test_clamp();
    test_grids_track();
    test_midi_clock();
    test_clear();
    test_lfo_hit_value();
    test_continue_base();
    printf(fails ? "motion_test: %d FAILED\n" : "motion_test: all passed\n", fails);
    return fails != 0;
}
