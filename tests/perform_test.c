/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* PERFORM (perform.c): the FX hold layer's effects on the master. The effects alone on synthetic signals
 * (perf_begin + perf_block, as mix_block runs them), and in the whole mix (mutes, the SLICER's buffer, the 1/16). */
#include "drum_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

#define NS (8u * 22050u)                           /* 4 s at 44.1 kHz, a multiple of CTL */
static int32_t X[NS], Y[NS];
static int32_t BL[CTL], BR[CTL];

static void pf_fresh(int32_t bpm)                  /* a clean machine, stopped, at bpm */
{
    host_init();
    song.g[G_BPM] = (int16_t)bpm;
}
static void hold(uint32_t e) { perf_press(e, 1); }
static void let_go(uint32_t e) { perf_press(e, 0); }
/* the effects alone: samples [from, to) of X through perf_begin / perf_block (both channels X), left into Y */
static void fx_run(uint32_t from, uint32_t to)
{
    uint32_t f, i;
    for (f = from; f < to; f += CTL) {
        for (i = 0; i < CTL; i++)
            BL[i] = BR[i] = X[f + i];
        if (perf_begin(CTL))
            perf_block(BL, BR, CTL);
        for (i = 0; i < CTL; i++)
            Y[f + i] = BL[i];
    }
}
static void sig_sine(double hz, double amp)
{
    uint32_t i;
    for (i = 0; i < NS; i++)
        X[i] = (int32_t)(amp * sin(2.0 * M_PI * hz * i / FS));
}
static void sig_noise(void)
{
    uint32_t i, r = 12345;
    for (i = 0; i < NS; i++) {
        r = r * 1664525u + 1013904223u;
        X[i] = (int32_t)(r >> 18) - 8192;
    }
}
static double rms(const int32_t *a, uint32_t from, uint32_t to)
{
    double s = 0;
    uint32_t i;
    for (i = from; i < to; i++)
        s += (double)a[i] * a[i];
    return sqrt(s / (to - from));
}
static uint32_t crossings(const int32_t *a, uint32_t from, uint32_t to)   /* with +-500 hysteresis */
{
    uint32_t i, n = 0;
    int up = a[from] > 0;
    for (i = from; i < to; i++)
        if (up && a[i] < -500) {
            up = 0;
            n++;
        } else if (!up && a[i] > 500) {
            up = 1;
            n++;
        }
    return n;
}

/* the 808 kit of the sound pack demos: kick on the beats, snare on 2 / 4, hats on the 1/8s */
static void kit(void)
{
    static const char *const PAT[3] = {"X...x...x...x...", "....X.......X...", "x.x.x.x.x.x.x.x."};
    static const uint32_t MODEL[3] = {DM_K808, DM_S808, DM_HATC};
    uint32_t i, k;
    for (i = 0; i < 3u; i++) {
        drum_set_model(&trk[i], MODEL[i]);
        for (k = 0; k < 16u; k++) {
            trk[i].step[k].on = PAT[i][k] != '.';
            trk[i].step[k].acc = PAT[i][k] == 'X';
        }
    }
}
static int32_t ML[NS], MR[NS], NL[NS], NR[NS];

static void test_idle_identical(void)
{
    uint32_t n = 2u * 4u * 22050u / CTL * CTL;        /* two bars at 120 BPM */
    pf_fresh(120);
    kit();
    transport_req = 1;
    render_mix(ML, MR, n);
    pf_fresh(120);
    kit();
    perf_mask = 1u;                                   /* FX held, no key, knobs at 0 */
    fm1_in.buttons = 1u;
    transport_req = 1;
    render_mix(NL, NR, n);
    fm1_in.buttons = 0;
    check("idle: FX held with no key and no knob, the mix is bit-identical",
          !memcmp(ML, NL, n * sizeof ML[0]) && !memcmp(MR, NR, n * sizeof MR[0]));
    pf_fresh(120);
    sig_noise();
    hold(PF_R16);
    fx_run(0, 22050);
    let_go(PF_R16);
    fx_run(22050, 44100);
    check("an effect let go: the layer goes idle again (nothing left running)", !pf.busy && !perf_begin(CTL));
}

static void test_mute(void)
{
    uint32_t n = 4u * 22050u / CTL * CTL, k;
    pf_fresh(120);
    kit();
    trk[0].p[P_LEVEL] = 0;                            /* the reference: track 1 rendered at LEVEL 0 (the drum noise
                                                       * state the tracks share runs the same) */
    render_mix(0, 0, CTL);                            /* (the same stopped block as below) */
    transport_req = 1;
    render_mix(ML, MR, n);
    pf_fresh(120);
    kit();
    hold(PF_M1 + 0u);                                 /* T1's white key held before PLAY */
    render_mix(0, 0, CTL);                            /* stopped, T1 silent: its gain snaps to 0 */
    transport_req = 1;
    render_mix(NL, NR, n);
    check("mute: a track muted by its white key is silent in the mix; the others as without it",
          !memcmp(ML, NL, n * sizeof ML[0]) && !memcmp(MR, NR, n * sizeof MR[0]));
    let_go(PF_M1 + 0u);
    for (k = 0; k < 8u; k++)
        render_mix(0, 0, CTL);
    check("mute let go: the gain back to full", pf.mg[0] == 32768);
}

/* Review Focus 1: let go while the track is silent, its next hit at full level */
static void test_mute_release_silent(void)
{
    uint32_t k;
    pf_fresh(120);
    kit();
    hold(PF_M1 + 1u);                                 /* T2 (snare) muted */
    for (k = 0; k < 8u; k++)
        render_mix(0, 0, CTL);                        /* stopped: nothing sounds */
    let_go(PF_M1 + 1u);
    render_mix(0, 0, CTL);
    check("a mute let go on a silent track: its gain is full at once (no fade-in on its next hit)",
          pf.mg[1] == 32768);
}

static void test_repeat_period(void)
{
    static const uint32_t E[3] = {PF_R8, PF_R16, PF_R32};
    uint32_t j, t, L, ok;
    for (j = 0; j < 3u; j++) {
        pf_fresh(120);
        sig_noise();
        hold(E[j]);                                   /* stopped: starts at once */
        fx_run(0, NS);
        L = beat_samples() / (2u << j);               /* 11025, 5512, 2756 */
        for (ok = 1, t = L + 256u; t < 3u * L; t++)
            ok &= Y[t] == Y[t + L];
        printf("      REPEAT 1/%u: loop %u samples\n", 8u << j, L);
        check(j == 0 ? "REPEAT 1/8: after one loop recorded, the output repeats every beat / 2" :
              j == 1 ? "REPEAT 1/16: the output repeats every beat / 4" : "REPEAT 1/32: every beat / 8", ok);
    }
}

static void test_reverse(void)
{
    uint32_t i, L, ok = 1;
    pf_fresh(120);
    for (i = 0; i < NS; i++)
        X[i] = (int32_t)(i % 22050u) - 11025;           /* a ramp up, one per half second */
    hold(PF_REV);
    fx_run(0, NS);
    L = beat_samples() / 2u;
    for (i = L + 300u; i + 300u < 2u * L; i++)
        ok &= Y[i + 1u] <= Y[i] + 2;
    check("REVERSE: the recorded 1/8 plays backwards (a rising ramp falls)", ok);
}

/* Review Focus 5 */
static void test_avail(void)
{
    pf_fresh(40);
    check("40 BPM: REPEAT 1/8 and REVERSE unavailable (longer than the 743 ms loop), 1/16 available",
          !((perf_avail() >> PF_R8) & 1u) && !((perf_avail() >> PF_REV) & 1u) && ((perf_avail() >> PF_R16) & 1u));
    pf_fresh(41);
    check("41 BPM: every effect available", (perf_avail() & 1023u) == 1023u);
    pf_fresh(41);
    sig_noise();
    hold(PF_R8);
    fx_run(0, 2u * 22050u);
    song.g[G_BPM] = 40;                              /* the tempo drops while it plays */
    fx_run(2u * 22050u, 3u * 22050u);
    check("REPEAT 1/8 held while the tempo drops to 40: it stops (not running, faded out)",
          !((perf_act >> PF_R8) & 1u) && !pf.w && Y[3u * 22050u - 1u] == X[3u * 22050u - 1u]);
}

static void test_tape(void)
{
    uint32_t b;
    pf_fresh(120);
    sig_sine(220.0, 8000.0);
    hold(PF_TAPE);
    fx_run(0, NS);
    b = beat_samples();
    check("TAPE STOP: still sounding half way", rms(Y, b / 4u, b / 2u) > 1000.0);
    check("TAPE STOP: silent after one beat", rms(Y, b + 512u, b + 8192u) < 8.0);
}

static void test_freeze(void)
{
    uint32_t i;
    pf_fresh(120);
    sig_sine(330.0, 8000.0);
    for (i = 9000u; i < NS; i++)
        X[i] = 0;                                     /* the input stops after 204 ms */
    hold(PF_FRZ);
    fx_run(0, NS);
    check("FREEZE: keeps sounding what it caught after the input stopped", rms(Y, 20000u, 40000u) > 1000.0);
}

static void test_oct(void)
{
    static const uint32_t E[2] = {PF_OUP, PF_ODN};
    uint32_t j, i, cx, cs;
    for (j = 0; j < 2u; j++) {
        pf_fresh(120);
        sig_sine(441.0, 8000.0);
        hold(E[j]);
        fx_run(0, NS);
        for (i = 0; i < NS; i++) {                   /* the shifted copy: out - 0.5625 the live mix */
            int32_t h = X[i] >> 1;
            Y[i] -= h + (h >> 3);
        }
        cx = crossings(X, 8192u, 8192u + 44100u);
        cs = crossings(Y, 8192u, 8192u + 44100u);
        printf("      %s: crossings %u (input %u)\n", j ? "OCT DN" : "OCT UP", cs, cx);
        check(j ? "OCT DN: the shifted copy an octave down (half the crossings, +-15 %)" :
                  "OCT UP: the shifted copy an octave up (twice the crossings, +-15 %)",
              j ? cs * 100u >= cx * 50u * 85u / 100u && cs * 100u <= cx * 50u * 115u / 100u :
                  cs * 100u >= cx * 200u * 85u / 100u && cs * 100u <= cx * 200u * 115u / 100u);
    }
}

static void test_filters(void)
{
    uint32_t bar;
    pf_fresh(120);
    sig_sine(8000.0, 8000.0);
    hold(PF_LPF);
    fx_run(0, NS);
    bar = 4u * beat_samples();
    check("LPF key: an 8 kHz tone gone after its bar-long sweep", rms(Y, bar + 4096u, bar + 12288u) < 0.1 * 8000.0 / sqrt(2.0));
    pf_fresh(120);
    sig_sine(30.0, 8000.0);
    hold(PF_HPF);
    fx_run(0, NS);
    check("HPF key: a 30 Hz tone down to a fifth after its sweep", rms(Y, bar + 4096u, bar + 4u * 4096u) < 0.2 * 8000.0 / sqrt(2.0));
    pf_fresh(120);
    sig_sine(8000.0, 8000.0);
    perf_k[0] = -100;
    fx_run(0, 44100u);
    check("FILTER knob left: the LPF", rms(Y, 22050u, 44100u) < 0.1 * 8000.0 / sqrt(2.0));
    pf_fresh(120);
    sig_sine(30.0, 8000.0);
    perf_k[0] = 100;
    fx_run(0, NS);
    check("FILTER knob right: the HPF", rms(Y, 44100u, NS) < 0.2 * 8000.0 / sqrt(2.0));
}

static void test_crush(void)
{
    uint32_t i, ok = 1;
    pf_fresh(120);
    sig_sine(100.0, 8000.0);
    perf_k[1] = 100;                                  /* 11 bits off, a sample held 8 */
    fx_run(0, 22050u);
    for (i = 1024u; i < 22050u; i++)
        ok &= (Y[i] & 2047) == 1024;
    check("CRUSH 100: every output sample on the coarse grid (the low 11 bits 1024)", ok);
}

static void test_throw(void)
{
    static int32_t ml[CTL], mr[CTL], sd[CTL], sr[CTL];
    uint32_t i, k;
    pf_fresh(120);
    perf_k[2] = 100;
    for (k = 0; k < 8u; k++) {                        /* 256 samples: the share ramped up */
        for (i = 0; i < CTL; i++) {
            ml[i] = mr[i] = 10000;
            sd[i] = sr[i] = 0;
        }
        perf_begin(CTL);
        perf_pre(ml, mr, sd, sr, CTL);
    }
    check("THROW 100: the dry mix sent into the delay and the reverb", sd[CTL - 1] > 9900 && sr[CTL - 1] > 9900);
}

static void test_depth(void)
{
    uint32_t i;
    int32_t dmax = 0;
    pf_fresh(120);
    sig_noise();
    perf_k[3] = 100;                                  /* DEPTH 0 % */
    hold(PF_R16);
    fx_run(0, 44100u);
    for (i = 11025u; i < 44100u; i++) {
        int32_t d = Y[i] - X[i];
        d = d < 0 ? -d : d;
        dmax = d > dmax ? d : dmax;
    }
    check("DEPTH 0 %: the repeat nearly silent, the live mix through", dmax < 200);
}

static void test_stacking(void)
{
    pf_fresh(120);
    sig_noise();
    hold(PF_R16);
    fx_run(0, 11025u);
    check("stack: REPEAT 1/16 plays", pf.src == PF_R16);
    hold(PF_FRZ);
    fx_run(11025u, 11025u + 1024u);
    check("stack: FREEZE pressed last plays", pf.src == PF_FRZ);
    let_go(PF_FRZ);
    fx_run(11025u + 1024u, 11025u + 2048u);
    check("stack: FREEZE let go, the REPEAT held before plays again", pf.src == PF_R16);
}

static void test_slicer_lend(void)
{
    uint32_t k, b, lent_rec = 0;
    pf_fresh(120);
    kit();
    trk[0].p[P_SLCR] = SL_STUT;
    transport_req = 1;
    render_mix(0, 0, 22050u / CTL * CTL);             /* STUT records its live steps */
    check("slicer: STUT records before the layer takes its buffer", sl[0].rec > 0u || sl[0].rec_on);
    hold(PF_R16);
    for (b = 0; b < 22050u / CTL; b++) {
        render_mix(0, 0, CTL);
        for (k = 0; sl_lent && k < NTRK; k++)       /* (from the lend on: it starts on the next 1/16) */
            lent_rec |= sl[k].rec | sl[k].rec_on;
    }
    check("slicer: the buffer lent, STUT records nothing", sl_lent && !lent_rec);
    let_go(PF_R16);
    render_mix(0, 0, 1024u);
    check("slicer: the buffer given back once the effect faded out", !sl_lent);
    render_mix(0, 0, 22050u / CTL * CTL);
    check("slicer: STUT records again", sl[0].rec > 0u || sl[0].rec_on);
}

static void test_kill(void)
{
    pf_fresh(120);
    sig_noise();
    hold(PF_R16);
    fx_run(0, 11025u);
    perf_kill = 1;
    fx_run(11025u, 11025u + 1024u);
    check("off (the menu): the effect stops, faded, though its key is held",
          !perf_act && !pf.w && Y[11025u + 1023u] == X[11025u + 1023u]);
}

/* the 1/16: REPEAT pressed mid-1/16 starts in the block where the Grids clock's next 1/16 starts */
static void test_sixteenth(void)
{
    uint32_t b, cnt, on_blk = 0, cnt_blk = 0;
    pf_fresh(120);
    kit();
    transport_req = 1;
    render_mix(0, 0, 2048u);                          /* into the first 1/16 (5512 samples) */
    hold(PF_R16);
    cnt = gclk.cnt;
    for (b = 1; b <= 400u && !(on_blk && cnt_blk); b++) {
        render_mix(0, 0, CTL);
        if (!cnt_blk && gclk.cnt != cnt)
            cnt_blk = b;
        if (!on_blk && ((perf_act >> PF_R16) & 1u))
            on_blk = b;
    }
    check("REPEAT pressed mid-1/16 while playing: waits, then starts with the next 1/16's block",
          cnt_blk > 1u && on_blk == cnt_blk && pf.split == 0u);
    pf_fresh(120);
    hold(PF_R16);
    render_mix(0, 0, CTL);
    check("REPEAT while stopped: at once", (perf_act >> PF_R16) & 1u);
}

/* the same under a MIDI clock (CLK USB): the 1/16 is the clock's */
static void clk_send(uint32_t status, double at)
{
    fm1_ms = (uint32_t)(at * 1000.0 / FS);
    midi_enqueue(0x0Fu | status << 8, 1u);
}
static void test_sixteenth_clock(void)
{
    double pulse = FS * 60.0 / 120.0 / 24.0, next = 0.0;
    uint64_t now = 0;
    uint32_t b, cnt = 0, on_blk = 0, cnt_blk = 0, pressed = 0;
    pf_fresh(120);
    kit();
    song.g[G_CLOCK] = 1;
    render_mix(0, 0, CTL);                            /* the mode change settles */
    clk_send(0xFAu, 0.0);
    for (b = 1; b < 4000u && !(on_blk && cnt_blk); b++) {
        while (next < (double)(now + CTL)) {
            clk_send(0xF8u, next);
            next += pulse;
        }
        fm1_ms = (uint32_t)((now + CTL) * 1000u / FS);
        render_mix(0, 0, CTL);
        now += CTL;
        if (!pressed && now > 20000u) {               /* a few 1/16s in, mid-1/16 */
            hold(PF_R16);
            cnt = gclk.cnt;
            pressed = 1;
            continue;
        }
        if (pressed && !cnt_blk && gclk.cnt != cnt)
            cnt_blk = b;
        if (pressed && !on_blk && ((perf_act >> PF_R16) & 1u))
            on_blk = b;
    }
    check("CLK USB: REPEAT starts with the clock's next 1/16", cnt_blk && on_blk == cnt_blk);
    song.g[G_CLOCK] = 0;
    render_mix(0, 0, CTL);
}

static void test_key_map(void)
{
    static const uint8_t BLACK[10] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22};
    static const uint8_t WANT[10] = {PF_R8, PF_R16, PF_R32, PF_REV, PF_TAPE, PF_LPF, PF_HPF, PF_FRZ, PF_OUP, PF_ODN};
    static const uint8_t WHITE[8] = {0, 2, 4, 6, 7, 9, 11, 12};
    uint32_t i, ok = 1;
    for (i = 0; i < 10u; i++)
        ok &= perf_key(BLACK[i]) == WANT[i];
    check("keys: the black keys F#3 .. D#5 play REPEAT 1/8 1/16 1/32 REVERSE TAPE LPF HPF FREEZE OCT UP OCT DN", ok);
    for (ok = 1, i = 0; i < 8u; i++)
        ok &= perf_key(WHITE[i]) == PF_M1 + i && WHITE[i] == KEY_TRK_KEY[i];
    check("keys: the white track keys F3 .. F4 mute tracks 1..8", ok);
    check("keys: F#5 and the white keys G4 .. G5 do nothing",
          perf_key(25) == PF_N && perf_key(14) == PF_N && perf_key(26) == PF_N && perf_key(24) == PF_N);
}

static uint32_t mo_count(void) { return mo_w; }
static void fx_down(int on) { perf_mask = 1u; fm1_in.buttons = on ? 1u : 0u; }

static void test_layer_keys(void)
{
    uint32_t age, mo;
    step_t before[NSTEP];
    pf_fresh(120);
    usb.config = 1;
    song.rec = 1u;                                    /* live recording armed on track 1 */
    transport_req = 1;
    render_mix(0, 0, CTL);
    memcpy(before, trk[0].step, sizeof before);
    age = hit_age(&trk[0]);
    mo = mo_count();
    fx_down(1);
    fm1_in.notes = 1u << 0;                           /* F3: track 1's key, with FX */
    render_mix(0, 0, CTL);
    check("FX + a track key: no hit, no MIDI out, nothing recorded",
          hit_age(&trk[0]) == age && mo_count() == mo && !memcmp(before, trk[0].step, sizeof before));
    check("FX + a track key: the key is the layer's, its track muted", (kb_layer & 1u) && ((perf_held >> PF_M1) & 1u));
    fm1_in.notes = 0;
    render_mix(0, 0, CTL);
    check("the layer key let go: no note-off sent, the mute off", mo_count() == mo && !kb_layer && !perf_held);
    fm1_in.notes = 1u << 3;                           /* G#3: REPEAT 1/16 */
    render_mix(0, 0, CTL);
    check("FX + G#3: REPEAT 1/16 held", (perf_held >> PF_R16) & 1u);
    fm1_in.notes = 0;
    fx_down(0);
    render_mix(0, 0, CTL);
    usb.config = 0;
    song.rec = 0;
}

/* Review Focus 2 */
static void test_fx_released_first(void)
{
    uint32_t mo, age;
    pf_fresh(120);
    usb.config = 1;
    fx_down(1);
    fm1_in.notes = 1u << 5;                           /* A#3: REPEAT 1/32 */
    render_mix(0, 0, CTL);
    fx_down(0);
    perf_mask = 0;                                    /* (the UI clears it when FX is let go) */
    mo = mo_count();
    age = hit_age(&trk[0]);
    render_mix(0, 0, 4u * CTL);
    check("FX let go first: the effect lasts while its key is held", (perf_held >> PF_R32) & 1u);
    fm1_in.notes = 0;
    render_mix(0, 0, CTL);
    check("then the key let go: the effect ends, no note, no MIDI", !perf_held && mo_count() == mo &&
          hit_age(&trk[0]) == age);
    usb.config = 0;
}

/* Review Focus 4 */
static void test_key_before_fx(void)
{
    uint32_t mo, age;
    pf_fresh(120);
    usb.config = 1;
    age = hit_age(&trk[0]);
    fm1_in.notes = 1u << 0;                           /* F3 played first */
    render_mix(0, 0, CTL);
    check("a key before FX: its note plays", hit_age(&trk[0]) != age);
    fx_down(1);
    render_mix(0, 0, CTL);
    mo = mo_count();
    fm1_in.notes = 0;
    render_mix(0, 0, CTL);
    check("FX pressed meanwhile: the key stays a note, its release sends the note-off",
          mo_count() == mo + 1u && !kb_layer && !perf_held);
    fx_down(0);
    usb.config = 0;
}

int main(void)
{
    test_idle_identical();
    test_mute();
    test_mute_release_silent();
    test_repeat_period();
    test_reverse();
    test_avail();
    test_tape();
    test_freeze();
    test_oct();
    test_filters();
    test_crush();
    test_throw();
    test_depth();
    test_stacking();
    test_slicer_lend();
    test_kill();
    test_sixteenth();
    test_sixteenth_clock();
    test_key_map();
    test_layer_keys();
    test_fx_released_first();
    test_key_before_fx();
    printf(fails ? "perform_test: %d FAILED\n" : "perform_test: all passed\n", fails);
    return fails != 0;
}
