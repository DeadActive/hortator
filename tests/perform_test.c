/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* PERFORM (perform.c): the FX hold layer's effects on the master. The effects alone on synthetic signals
 * (perf_begin + perf_block, as mix_block runs them), and in the whole mix (REPEAT STEP, the SLICER's buffer, the 1/16). */
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
        old_set_model(&trk[i], MODEL[i]);
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

/* REPEAT STEP (the white track keys F3 .. F4 on the layer): a track retriggers its step's sound on the grid of the
 * rate (D5 1/4, E5 1/8, F5 1/16, G5 1/32), its own steps paused while held. The hits are logged by block (hit_age) */
#define RH 512u
static uint32_t rh_at[NTRK][RH], rh_n[NTRK], rh_blk;
static uint32_t rh_vel[NTRK][RH];
static int8_t rh_nt[NTRK][RH];
static void rh_reset(void)
{
    memset(rh_n, 0, sizeof rh_n);
    rh_blk = 0;
}
static uint32_t newest_vel(const track_t *t)
{
    uint32_t i, a = 0, v = 0;
    for (i = 0; i < NDV; i++)
        if (t->v[i].active && t->v[i].age >= a) {
            a = t->v[i].age;
            v = t->v[i].vel;
        }
    return v;
}
static void rh_run(uint32_t blocks)                  /* render, logging each track's hits (block, velocity, note) */
{
    uint32_t b, k, age[NTRK];
    for (b = 0; b < blocks; b++, rh_blk++) {
        for (k = 0; k < NTRK; k++)
            age[k] = hit_age(&trk[k]);
        render_mix(0, 0, CTL);
        for (k = 0; k < NTRK; k++)
            if (hit_age(&trk[k]) != age[k] && rh_n[k] < RH) {
                rh_vel[k][rh_n[k]] = newest_vel(&trk[k]);
                rh_nt[k][rh_n[k]] = trk[k].nt;
                rh_at[k][rh_n[k]++] = rh_blk;
            }
    }
}
/* the hits of track a from block from on are the hits of track b from there (within tol blocks), at least min */
static int rh_same(uint32_t a, uint32_t b, uint32_t from, uint32_t to, uint32_t tol, uint32_t min)
{
    uint32_t i = 0, j = 0, n = 0;
    while (i < rh_n[a] && rh_at[a][i] < from)
        i++;
    while (j < rh_n[b] && rh_at[b][j] < from)
        j++;
    for (; i < rh_n[a] && rh_at[a][i] < to; i++, j++, n++) {
        if (j >= rh_n[b] || rh_at[b][j] + tol < rh_at[a][i] || rh_at[a][i] + tol < rh_at[b][j]) {
            printf("      T%u hit %u at block %u, T%u's at %u\n", a + 1u, n, rh_at[a][i], b + 1u,
                   j < rh_n[b] ? rh_at[b][j] : 0u);
            return 0;
        }
    }
    if (j < rh_n[b] && rh_at[b][j] < to) {
        printf("      T%u missed T%u's hit at block %u\n", a + 1u, b + 1u, rh_at[b][j]);
        return 0;
    }
    return n >= min;
}
static uint32_t rh_count(uint32_t k, uint32_t from, uint32_t to)
{
    uint32_t i, n = 0;
    for (i = 0; i < rh_n[k]; i++)
        n += rh_at[k][i] >= from && rh_at[k][i] < to;
    return n;
}
static uint32_t rh_first(uint32_t k, uint32_t from)
{
    uint32_t i;
    for (i = 0; i < rh_n[k]; i++)
        if (rh_at[k][i] >= from)
            return rh_at[k][i];
    return 0xFFFFFFFFu;
}
/* the kit, and track 8 a reference: every every-th step on at division div (its hits: the grid of that rate) */
static void rep_kit_n(uint32_t div, uint32_t every)
{
    uint32_t k;
    kit();
    old_set_model(&trk[7], DM_HATC);
    trk[7].p[P_LEVEL] = 0;
    trk[7].p[P_SDIV] = (int16_t)div;
    for (k = 0; k < NSTEP; k++)
        trk[7].step[k].on = k % every == 0u;
}
static void rep_kit(uint32_t div) { rep_kit_n(div, 1u); }
static const uint8_t RATE_DIV[4] = {0, 1, 2, 3};     /* PR_4 .. PR_32 as a track's DIV: 1/4 1/8 1/16 1/32 */
static const char *const RATE_NAME[4] = {"1/4", "1/8", "1/16", "1/32"};

/* while playing: each rate's hits on its grid, the first on the next grid point after the press (as track 8 at that
 * division plays); the track's own steps none; after the release the pattern as without the hold */
static void rep_grid_case(uint32_t r, int32_t bpm, int32_t swing, uint32_t tol)
{
    uint32_t press, rel, ok;
    char what[160];
    pf_fresh(bpm);
    song.g[G_SWING] = (int16_t)swing;
    if (swing && r <= PR_8)
        rep_kit_n(2, r == PR_8 ? 2u : 4u);           /* SWING moves the odd 1/16s only: the 1/8s and beats straight
                                                      * (a 1/8 track would swing its own 1/8s) */
    else
        rep_kit(RATE_DIV[r]);
    perf_rate = (uint8_t)r;
    rh_reset();
    transport_req = 1;
    rh_run(97);                                       /* mid-1/16 (3104 samples in) */
    press = rh_blk;
    hold(PF_T1 + 2u);                                 /* track 3 (the hats) */
    rh_run(2000);
    rel = rh_blk;
    let_go(PF_T1 + 2u);
    rh_run(10);
    ok = rh_same(2, 7, press, rel, tol, 3u) && rh_first(2, press) == rh_first(7, press);
    snprintf(what, sizeof what, "REPEAT STEP %s at %d BPM%s: track 3's hits on the grid of the rate (as a %s track), "
             "the first on the next grid point", RATE_NAME[r], bpm, swing ? ", SWING" : "", RATE_NAME[r]);
    check(what, ok);
}
static void test_rep_grid(void)
{
    uint32_t r;
    for (r = 0; r < 4u; r++)
        rep_grid_case(r, 120, 0, 0);
    rep_grid_case(PR_16, 97, 0, 0);
    rep_grid_case(PR_8, 97, 0, 0);
    rep_grid_case(PR_32, 97, 0, 1);
    rep_grid_case(PR_16, 120, 40, 0);
    rep_grid_case(PR_8, 120, 40, 0);
    rep_grid_case(PR_4, 120, 40, 0);
}

/* SWING at 1/32: the even ones on the swung 1/16s, the odd ones halfway through each 1/16 */
static void test_rep_swing32(void)
{
    uint32_t press, i, j, ok = 1, n = 0;
    pf_fresh(120);
    song.g[G_SWING] = 40;
    rep_kit(2);                                       /* track 8: the 1/16s */
    perf_rate = PR_32;
    rh_reset();
    transport_req = 1;
    rh_run(97);
    press = rh_blk;
    hold(PF_T1 + 2u);
    rh_run(2500);
    for (i = 0; i + 1u < rh_n[7]; i++) {             /* each 1/16 of track 8 after the press: a hit on it, one between */
        uint32_t a = rh_at[7][i], b = rh_at[7][i + 1u], on = 0, mid = 0;
        if (a <= press)
            continue;
        for (j = 0; j < rh_n[2]; j++) {
            on += rh_at[2][j] == a;
            mid += rh_at[2][j] > a && rh_at[2][j] < b && rh_at[2][j] + 1u >= (a + b) / 2u &&
                   rh_at[2][j] <= (a + b + 1u) / 2u + 1u;
        }
        ok &= on == 1u && mid == 1u;
        n++;
    }
    check("REPEAT STEP 1/32 with SWING: on each swung 1/16 and halfway through it", ok && n > 10u);
    let_go(PF_T1 + 2u);
}

/* under a MIDI clock (CLK USB): the clock's grid */
static void test_rep_clock(void)
{
    double pulse = FS * 60.0 / 120.0 / 24.0, next = 0.0;
    uint64_t now = 0;
    uint32_t b, press = 0, age[NTRK], k;
    pf_fresh(120);
    rep_kit(2);
    perf_rate = PR_16;
    song.g[G_CLOCK] = 1;
    render_mix(0, 0, CTL);
    rh_reset();
    fm1_ms = 0;
    midi_enqueue(0x0Fu | 0xFAu << 8, 1u);
    for (b = 0; b < 3000u; b++, rh_blk++) {
        while (next < (double)(now + CTL)) {
            fm1_ms = (uint32_t)(next * 1000.0 / FS);
            midi_enqueue(0x0Fu | 0xF8u << 8, 1u);
            next += pulse * 1.07;                     /* the clock a bit slower than 120 */
        }
        fm1_ms = (uint32_t)((now + CTL) * 1000u / FS);
        if (b == 700u) {
            press = rh_blk;
            hold(PF_T1 + 2u);
        }
        for (k = 0; k < NTRK; k++)
            age[k] = hit_age(&trk[k]);
        render_mix(0, 0, CTL);
        now += CTL;
        for (k = 0; k < NTRK; k++)
            if (hit_age(&trk[k]) != age[k] && rh_n[k] < RH)
                rh_at[k][rh_n[k]++] = rh_blk;
    }
    check("CLK USB: REPEAT STEP 1/16 on the clock's 1/16s (as a 1/16 track following it)",
          rh_same(2, 7, press, rh_blk, 0, 10u));
    let_go(PF_T1 + 2u);
    song.g[G_CLOCK] = 0;
    render_mix(0, 0, CTL);
}

/* the sound: the step under the playhead at the press (its note, its accent; no PROB, no RATCH); a step off: the
 * track's last note at 96 */
static void test_rep_sound(void)
{
    uint32_t press, i, ok = 1, n;
    pf_fresh(120);
    rep_kit(2);
    for (i = 0; i < 16u; i++)
        trk[2].step[i].on = 0;
    trk[2].step[4] = (step_t){.on = 1, .acc = 1, .nt = 5, .rat = 3, .cond = 2};   /* step 5: accent, +5, RATCH 4, PROB 5 % */
    trk[2].step[2] = (step_t){.on = 1, .nt = -3};     /* step 3: -3, plays */
    perf_rate = PR_16;
    rh_reset();
    transport_req = 1;
    while (trk[2].seq_idx != 4u || trk[2].seq_pos >= 0x7FFFFFFFu || trk[2].seq_pos < 1000u)
        rh_run(1);                                    /* into step 5 */
    press = rh_blk;
    hold(PF_T1 + 2u);
    rh_run(2000);
    n = rh_count(2, press, rh_blk);
    for (i = 0; i < rh_n[2]; i++)
        if (rh_at[2][i] > press)
            ok &= rh_vel[2][i] == 127u && rh_nt[2][i] == 5;
    check("REPEAT STEP: the held step's note and accent, every 1/16 (no PROB skip, no RATCH roll)",
          ok && n == rh_count(7, press, rh_blk) && n > 8u);
    let_go(PF_T1 + 2u);
    rh_run(1);
    while (trk[2].seq_idx != 7u || trk[2].seq_pos >= 0x7FFFFFFFu)
        rh_run(1);                                    /* step 8: off (step 3 played last: -3) */
    press = rh_blk;
    hold(PF_T1 + 2u);
    rh_run(800);
    for (ok = 1, i = 0; i < rh_n[2]; i++)
        if (rh_at[2][i] > press)
            ok &= rh_vel[2][i] == 96u && rh_nt[2][i] == -3;
    check("REPEAT STEP on a step that is off: the track's last note, at 96", ok && rh_count(2, press, rh_blk) > 3u);
    let_go(PF_T1 + 2u);
}

/* the track's own steps paused while held (1/4 over 1/8 hats: only the beats), the pattern back in time after; the
 * other tracks untouched; two tracks at once */
static void test_rep_paused(void)
{
    static uint32_t ref_at[NTRK][RH], ref_n[NTRK];
    uint32_t press, rel, i, k, ok = 1, beats;
    pf_fresh(120);
    rep_kit(0);                                       /* track 8: the beats */
    rh_reset();
    transport_req = 1;
    rh_run(4300);
    memcpy(ref_at, rh_at, sizeof ref_at);
    memcpy(ref_n, rh_n, sizeof ref_n);
    pf_fresh(120);
    rep_kit(0);
    perf_rate = PR_4;
    rh_reset();
    transport_req = 1;
    rh_run(300);
    press = rh_blk;
    hold(PF_T1 + 2u);
    hold(PF_T1 + 0u);                                 /* the kick too: two tracks at once */
    rh_run(2800);
    rel = rh_blk;
    let_go(PF_T1 + 2u);
    let_go(PF_T1 + 0u);
    rh_run(1200);
    beats = rh_count(7, press, rel);
    check("REPEAT STEP 1/4 on the 1/8 hats: only the beats while held (its own steps paused)",
          rh_same(2, 7, press, rel, 0, 3u) && rh_count(2, press, rel) == beats);
    check("two tracks at once: the kick on the beats too", rh_same(0, 7, press, rel, 0, 3u));
    for (k = 0; k < NTRK; k++) {                      /* after the release: as without the hold */
        uint32_t a = 0, b = 0;
        while (a < rh_n[k] && rh_at[k][a] < rel + 1u)
            a++;
        while (b < ref_n[k] && ref_at[k][b] < rel + 1u)
            b++;
        for (i = 0; a + i < rh_n[k]; i++)
            ok &= b + i < ref_n[k] && rh_at[k][a + i] == ref_at[k][b + i];
        ok &= rh_n[k] - a == ref_n[k] - b;
    }
    check("let go: the pattern goes on, on time (every track's hits as without the hold)", ok);
    for (ok = 1, k = 1; k < 7u; k++) {                /* the others while held */
        uint32_t a = 0, b = 0;
        if (k == 2u)
            continue;
        while (a < rh_n[k] && rh_at[k][a] < press)
            a++;
        while (b < ref_n[k] && ref_at[k][b] < press)
            b++;
        for (; a < rh_n[k] && rh_at[k][a] < rel; a++, b++)
            ok &= b < ref_n[k] && rh_at[k][a] == ref_at[k][b];
    }
    check("the other tracks play on as before while held", ok);
}

/* stopped: free-running at the tempo from the press, the first hit at once; a rate change on the next one */
static void test_rep_stopped(void)
{
    uint32_t press, i, ok = 1, P = 22050u / 4u, n;
    pf_fresh(120);
    rep_kit(2);
    trk[2].step[0] = (step_t){.on = 1, .nt = 2};
    trk[2].seq_idx = 0;
    perf_rate = PR_16;
    rh_reset();
    rh_run(10);
    press = rh_blk;
    hold(PF_T1 + 2u);
    rh_run(1400);                                     /* ~1 s */
    n = rh_count(2, press, rh_blk);
    for (i = 0; i < rh_n[2]; i++) {
        double want = press + (double)i * P / CTL;
        ok &= rh_at[2][i] + 1.0 >= want && rh_at[2][i] <= want + 1.0;
    }
    check("stopped: REPEAT STEP free-running, the first at once, then every 1/16 of the tempo",
          rh_n[2] && rh_at[2][0] == press && ok && n == 1u + 1400u * CTL / P);
    check("stopped: the step under the playhead's note", rh_nt[2][0] == 2 && rh_nt[2][n - 1u] == 2);
    perf_rate = PR_4;
    i = rh_blk;
    rh_run(1400);
    n = rh_count(2, i, rh_blk);
    check("stopped: the rate changed to 1/4: a hit a beat", n >= 1u && n <= 2u);
    let_go(PF_T1 + 2u);
    i = rh_blk;
    rh_run(1400);
    check("stopped: let go, no more", rh_count(2, i, rh_blk) == 0u && rh_count(7, 0, rh_blk) == 0u);
}

/* a rate change while repeating: from the next grid point of the new rate */
static void test_rep_rate_change(void)
{
    uint32_t press, ch, first;
    pf_fresh(120);
    rep_kit(3);                                       /* track 8: the 1/32s */
    perf_rate = PR_16;
    rh_reset();
    transport_req = 1;
    rh_run(97);
    press = rh_blk;
    hold(PF_T1 + 2u);
    rh_run(800);
    while (gclk.half || gclk.pos < 300u)              /* the start of a 1/16 */
        rh_run(1);
    ch = rh_blk;
    perf_rate = PR_32;
    rh_run(1000);
    first = rh_first(2, ch);
    check("rate 1/16 -> 1/32 while held: the next hit on the next 1/32 (halfway), then every 1/32",
          first == rh_first(7, ch) && rh_same(2, 7, ch, rh_blk, 0, 10u) && rh_count(2, press, ch) > 3u);
    let_go(PF_T1 + 2u);
}

/* the rate keys: D5 E5 F5 G5 (key 21 23 24 26) on the layer: a tap selects 1/4 1/8 1/16 1/32; nothing held, no note */
static void fx_down(int on);
static uint32_t mo_count(void);
static void test_rate_keys(void)
{
    static const uint8_t KEY[4] = {21, 23, 24, 26};
    uint32_t i, ok = 1, mo, age;
    pf_fresh(120);
    usb.config = 1;
    check("REPEAT STEP's rate at power-on: 1/16", perf_rate == PR_16);
    fx_down(1);
    mo = mo_count();
    age = hit_age(&trk[7]);
    for (i = 0; i < 4u; i++) {
        fm1_in.notes = 1u << KEY[(i + 1u) & 3u];
        render_mix(0, 0, CTL);
        ok &= perf_rate == ((i + 1u) & 3u) && !perf_held;
        fm1_in.notes = 0;
        render_mix(0, 0, CTL);
        ok &= perf_rate == ((i + 1u) & 3u) && !perf_held && !kb_layer;
    }
    check("FX + D5 E5 F5 G5: the rate 1/4 1/8 1/16 1/32 (kept after the key), nothing held", ok);
    check("the rate keys: no note, no MIDI", mo_count() == mo && hit_age(&trk[7]) == age);
    fx_down(0);
    perf_mask = 0;
    fm1_in.notes = 1u << 26;                          /* G5 without FX: not the layer's (no rate) */
    render_mix(0, 0, CTL);
    fm1_in.notes = 0;
    render_mix(0, 0, CTL);
    check("G5 without FX: the rate stays", perf_rate == PR_4);
    usb.config = 0;
}

/* idle: no key held, every track's hits as before (the same blocks) and the mix bit-identical: test_idle_identical */

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

/* spec §7: the knob effects stay on under the menu / a dialog (perf_kill stops only the buffer effects) */
static void test_kill_keeps_knobs(void)
{
    uint32_t i, ok = 1;
    pf_fresh(120);
    sig_sine(100.0, 8000.0);
    hold(PF_R16);
    perf_k[1] = 100;                                  /* CRUSH 100 */
    perf_kill = 1;
    fx_run(0, 22050u);
    for (i = 1024u; i < 22050u; i++)
        ok &= (Y[i] & 2047) == 1024;
    check("off (the menu): CRUSH (a knob) keeps working, the held REPEAT stops", ok && !perf_act);
    pf_fresh(120);
    sig_sine(8000.0, 8000.0);
    perf_k[0] = -100;
    perf_kill = 1;
    fx_run(0, 44100u);
    check("off (the menu): FILTER (a knob) keeps working: the LPF", rms(Y, 22050u, 44100u) < 0.1 * 8000.0 / sqrt(2.0));
    perf_kill = 0;
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
        ok &= perf_key(WHITE[i]) == PF_T1 + i && WHITE[i] == KEY_TRK_KEY[i];
    check("keys: the white track keys F3 .. F4 repeat tracks 1..8", ok);
    check("keys: F#5 and the white keys G4 .. C5 do nothing",
          perf_key(25) == PF_N && perf_key(14) == PF_N && perf_key(16) == PF_N && perf_key(18) == PF_N &&
          perf_key(19) == PF_N);
    for (ok = 1, i = 0; i < 4u; i++)
        ok &= perf_key(STEP_KEY[12u + i]) == PF_N && perf_rate_key(STEP_KEY[12u + i]) == i;
    check("keys: D5 E5 F5 G5 (the last four white keys) the rates 1/4 1/8 1/16 1/32, no effect",
          ok && STEP_KEY[12] == 21u && STEP_KEY[15] == 26u && perf_rate_key(0) > PR_32 && perf_rate_key(25) > PR_32);
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
    check("FX + a track key: the key is the layer's, its track repeating", (kb_layer & 1u) && ((perf_held >> PF_T1) & 1u));
    fm1_in.notes = 0;
    render_mix(0, 0, CTL);
    check("the layer key let go: no note-off sent, the repeat off", mo_count() == mo && !kb_layer && !perf_held);
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
    test_rep_grid();
    test_rep_swing32();
    test_rep_clock();
    test_rep_sound();
    test_rep_paused();
    test_rep_stopped();
    test_rep_rate_change();
    test_rate_keys();
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
    test_kill_keeps_knobs();
    test_sixteenth();
    test_sixteenth_clock();
    test_key_map();
    test_layer_keys();
    test_fx_released_first();
    test_key_before_fx();
    printf(fails ? "perform_test: %d FAILED\n" : "perform_test: all passed\n", fails);
    return fails != 0;
}
