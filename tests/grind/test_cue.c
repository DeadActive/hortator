/* SPDX-License-Identifier: GPL-3.0-only */
/* T10: a staged Grind pattern published at the native cue boundary, on the real sequencer (seq.c, cue.c). */
#define GRIND_NATIVE 1
#undef NDEBUG
#include <assert.h>
#include "../drum_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

#define BPM 240u
static void fresh(void)
{
    host_init();
    song.g[G_BPM] = BPM;
    gm_runtime_reset();
}
static uint32_t sig(void)
{
    uint32_t h = 2166136261u, k, s;
    for (k = 0; k < NTRK; k++) {
        const uint8_t *b = (const uint8_t *)trk[k].step;
        for (s = 0; s < sizeof trk[k].step; s++)
            h = (h ^ b[s]) * 16777619u;
        h = (h ^ (uint32_t)trk[k].p[P_SLEN]) * 16777619u;
        h = (h ^ (uint32_t)trk[k].p[P_SDIV]) * 16777619u;
    }
    return h;
}
static gm_result submit(uint8_t style, gm_boundary at)
{
    gm_config c;
    gm_factory_config(style, &c);
    return gm_runtime_submit(&c, at, GM_REQUEST_STYLE);
}
static void play_blocks(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        render_mix(0, 0, CTL);
        gm_runtime_poll();                              /* (the main loop between two blocks) */
    }
}
/* the model of the steps' starts at 240 BPM, 1/16 (an independent one, as micro_test's) */
static uint32_t START[40];
static void starts(void)
{
    uint32_t beat = FS * 60u / BPM, rem = 0, c, at = 0;
    for (c = 0; c < 40u; c++) {
        uint32_t per = (beat + rem) / 4u;
        START[c] = at;
        rem = (beat + rem) % 4u;
        at += per;
    }
}
#define LOOP_BLOCKS ((START[16] + CTL - 1u) / CTL)
#define BOUNDARY (START[16])

/* PLAY with the pattern of style a, a request for style b staged early: the block where the pattern changes */
static uint32_t switch_block(uint8_t a, uint8_t b, uint32_t *old_sig, uint32_t *new_sig, uint32_t blocks)
{
    uint32_t i, first = 0xFFFFFFFFu, changes = 0, last;
    gm_config c;
    gm_pattern p;
    fresh();
    gm_factory_config(a, &c);
    gm_factory_pattern(a, &p);
    assert(gm_native_publish(&p) == GM_OK);
    *old_sig = last = sig();
    transport_req = 1;
    play_blocks(60);
    assert(submit(b, GM_BOUNDARY_PATTERN) == GM_OK);
    for (i = 60; i < blocks; i++) {
        play_blocks(1);
        if (sig() != last) {
            if (first == 0xFFFFFFFFu)
                first = i;
            changes++;
            last = sig();
        }
    }
    *new_sig = last;
    return changes == 1u ? first : 0xFFFFFFFEu;
}

static void boundary_exact(void)
{
    uint32_t o, n, b, ok = 1;
    starts();
    b = switch_block(1, 3, &o, &n, LOOP_BLOCKS + 40u);
    ok &= o != n && b != 0xFFFFFFFEu && b != 0xFFFFFFFFu;                 /* one change, to the new pattern */
    ok &= b >= BOUNDARY / CTL - 1u && b <= BOUNDARY / CTL + 1u;            /* the block the pattern ends in */
    check("cue: a pattern staged early publishes exactly once, in the block the longest track's pattern ends in", ok);
    if (!ok)
        printf("     switched in block %u, boundary at sample %u (block %u)\n", b, BOUNDARY, BOUNDARY / CTL);
}

/* Technical in 7/4 (LEN 28) playing, a 4/4 style staged for the end: the switch is at step 28, not at 16 */
static void seven_four_boundary(void)
{
    uint32_t i, first = 0xFFFFFFFFu, changes = 0, last, ok = 1;
    gm_config c;
    gm_pattern p;
    starts();
    fresh();
    gm_factory_config(9, &c);
    c.grid = (gm_grid){7, 4, 16, 1};
    assert(gm_generate(&c, NULL, &p) == GM_OK && gm_native_publish(&p) == GM_OK);
    ok &= trk[0].p[P_SLEN] == 28;
    last = sig();
    transport_req = 1;
    play_blocks(60);
    assert(submit(1, GM_BOUNDARY_PATTERN) == GM_OK);
    for (i = 60; i < START[28] / CTL + 40u; i++) {
        play_blocks(1);
        if (sig() != last) {
            if (first == 0xFFFFFFFFu)
                first = i;
            changes++;
            last = sig();
        }
    }
    ok &= changes == 1u && first >= START[28] / CTL - 1u && first <= START[28] / CTL + 1u && trk[0].p[P_SLEN] == 16;
    ok &= first > START[16] / CTL + 2u;                                    /* not at the 4/4 bar line */
    check("cue: END of a 28-step pattern waits for step 28 (not 16), then publishes once", ok);
    if (!ok)
        printf("     switched in block %u (changes %u), step 28 ends in block %u\n", first, changes, START[28] / CTL);
}

static void nothing_before_boundary(void)
{
    uint32_t i, ok = 1, old, base;
    gm_pattern p;
    starts();
    fresh();
    gm_factory_pattern(1, &p);
    gm_native_publish(&p);
    old = sig();
    transport_req = 1;
    play_blocks(10);
    assert(submit(2, GM_BOUNDARY_PATTERN) == GM_OK);
    base = LOOP_BLOCKS - 4u;
    for (i = 10; i < base; i++) {
        play_blocks(1);
        ok &= sig() == old;                                               /* (the live pattern never half-changes) */
    }
    check("cue: until the boundary every block still plays the outgoing pattern, untouched", ok);
}

/* LEN 16 / 12 / 15 on one DIV: the switch is at the longest track's end (16 steps), not at the LCM (240) */
static void longest_track(void)
{
    uint32_t i, first = 0, ok = 1, last, k;
    gm_pattern p;
    starts();
    fresh();
    gm_factory_pattern(1, &p);
    gm_native_publish(&p);
    trk[0].p[P_SLEN] = 16;
    trk[1].p[P_SLEN] = 12;
    trk[2].p[P_SLEN] = 15;
    for (k = 3; k < NTRK; k++)
        trk[k].p[P_SLEN] = 8;
    last = sig();
    transport_req = 1;
    play_blocks(30);
    assert(submit(2, GM_BOUNDARY_PATTERN) == GM_OK);
    for (i = 30; i < LOOP_BLOCKS + 60u; i++) {
        play_blocks(1);
        if (sig() != last && !first)
            first = i;
        last = sig();
    }
    ok &= first >= BOUNDARY / CTL - 1u && first <= BOUNDARY / CTL + 1u;
    if (!ok)
        printf("     first change block %u (want about %u), status %d, commits %u\n", first, BOUNDARY / CTL,
               (int)gm_runtime_status(), gm_runtime_commits());
    check("cue: LEN 16/12/15 on one division switch at the longest pattern's end (16 steps), not at the LCM", ok);
}

static void next_step_boundary(void)
{
    uint32_t i, first = 0, last, ok = 1;
    gm_pattern p;
    starts();
    fresh();
    gm_factory_pattern(1, &p);
    gm_native_publish(&p);
    last = sig();
    transport_req = 1;
    play_blocks(700);                                                     /* (inside a loop, far from its end) */
    assert(submit(2, GM_BOUNDARY_STEP) == GM_OK);
    for (i = 700; i < 900u; i++) {
        play_blocks(1);
        if (sig() != last && !first)
            first = i;
        last = sig();
    }
    ok &= first >= 700u && first < 700u + (START[1] / CTL) + 3u;          /* within about one step */
    check("cue: a next-step request publishes at the soonest next step, not the pattern end", ok);
}

static void latest_wins(void)
{
    uint32_t i, old, o2, n2, ok = 1, commits;
    gm_pattern p;
    starts();
    fresh();
    gm_factory_pattern(1, &p);
    gm_native_publish(&p);
    old = sig();
    transport_req = 1;
    play_blocks(40);
    assert(submit(2, GM_BOUNDARY_PATTERN) == GM_OK);
    play_blocks(5);
    assert(submit(3, GM_BOUNDARY_PATTERN) == GM_OK);                      /* before the boundary: replaces 2 */
    for (i = 45; i < LOOP_BLOCKS + 40u; i++)
        play_blocks(1);
    commits = gm_runtime_commits();
    n2 = sig();
    {
        gm_pattern q;
        gm_config c;
        gm_factory_config(2, &c);
        gm_factory_pattern(2, &q);                                       /* (the same style from the same base) */
        o2 = old;
        (void)o2;
        ok &= commits == 1u && n2 != old;
    }
    check("cue: a second request before the boundary replaces the first: one publish", ok);
}

static void edit_rejects(void)
{
    uint32_t i, ok = 1, edited;
    gm_pattern p;
    starts();
    fresh();
    gm_factory_pattern(1, &p);
    gm_native_publish(&p);
    transport_req = 1;
    play_blocks(40);
    assert(submit(2, GM_BOUNDARY_PATTERN) == GM_OK);
    play_blocks(5);
    gm_touch();                                                           /* a manual edit before the boundary */
    trk[3].step[5].on = (uint8_t)!trk[3].step[5].on;
    edited = sig();
    for (i = 45; i < LOOP_BLOCKS + 40u; i++)
        play_blocks(1);
    ok &= sig() == edited && gm_runtime_commits() == 0u && gm_runtime_status() == GM_STALE;
    check("cue: an edit after staging rejects the candidate, the edit stands and nothing is overwritten", ok);
}

static void stop_cancels(void)
{
    uint32_t i, ok = 1, old;
    gm_pattern p;
    starts();
    fresh();
    gm_factory_pattern(1, &p);
    gm_native_publish(&p);
    old = sig();
    transport_req = 1;
    play_blocks(40);
    assert(submit(2, GM_BOUNDARY_PATTERN) == GM_OK);
    play_blocks(5);
    transport_req = 2;                                                    /* STOP before the boundary */
    play_blocks(2);
    transport_req = 1;
    play_blocks(LOOP_BLOCKS + 60u);                                       /* a whole loop later: no late commit */
    for (i = 0; i < 1u; i++)
        ok &= sig() == old && gm_runtime_commits() == 0u;
    check("cue: STOP before the boundary drops the staged pattern, no late commit after PLAY", ok);
}

static void stopped_publishes_at_once(void)
{
    uint32_t old, ok = 1;
    gm_pattern p;
    fresh();
    gm_factory_pattern(1, &p);
    gm_native_publish(&p);
    old = sig();
    assert(submit(2, GM_BOUNDARY_PATTERN) == GM_OK);
    play_blocks(1);                                                       /* (stopped: nothing to wait for) */
    ok &= sig() != old && gm_runtime_commits() == 1u && !song.playing;
    check("cue: stopped, a validated request publishes at once under the guard", ok);
}

static void native_cue_wins(void)
{
    uint32_t i, old, ok = 1;
    gm_pattern p;
    starts();
    fresh();
    gm_factory_pattern(1, &p);
    gm_native_publish(&p);
    old = sig();
    transport_req = 1;
    play_blocks(40);
    cue.q_slot = 2;                                                       /* a native slot cue is queued */
    assert(submit(2, GM_BOUNDARY_PATTERN) == GM_OK);
    for (i = 40; i < 80u; i++)
        play_blocks(1);
    ok &= !cue.q_gm && sig() == old && gm_runtime_commits() == 0u;        /* the Grind request waits, never both */
    cue.q_slot = 0;
    for (i = 80; i < LOOP_BLOCKS + 40u; i++)
        play_blocks(1);
    ok &= gm_runtime_commits() == 1u;                                     /* then it goes through, once */
    check("cue: a queued native cue and a Grind request never queue together; the request waits its turn", ok);
}

static void song_refuses(void)
{
    uint32_t ok = 1;
    fresh();
    chain.running = 1;
    ok &= submit(2, GM_BOUNDARY_PATTERN) == GM_UNSUPPORTED;
    chain.running = 0;
    ok &= submit(2, GM_BOUNDARY_BAR) == GM_UNSUPPORTED;                   /* (no native bar boundary yet) */
    check("cue: a running song and a bar boundary are refused, nothing queued", ok);
}

/* the early hit of the outgoing pattern's step 0 (micro -50, played in its last step) must not cross the replacement */
static uint32_t hits_between(uint32_t ti, uint32_t from, uint32_t to)
{
    uint32_t f, n = 0, a = hit_age(&trk[ti]);
    for (f = from; f < to; f += CTL) {
        play_blocks(1);
        if (hit_age(&trk[ti]) != a) {
            a = hit_age(&trk[ti]);
            n++;
        }
    }
    return n;
}
/* the outgoing pattern: only track 0's step 0, early (-50), so it sounds in step 15 round the loop. Hits of track 0 in
 * the last step before the boundary, with a Grind request queued or without (the control) */
static uint32_t last_step_hits(int queued)
{
    uint32_t k;
    gm_pattern p;
    fresh();
    gm_factory_pattern(1, &p);
    gm_native_publish(&p);
    for (k = 0; k < NTRK; k++)
        memset(trk[k].step, 0, sizeof trk[k].step);
    trk[0].step[0].on = 1;
    trk[0].step[0].micro = -50;
    transport_req = 1;
    play_blocks(50);
    if (queued)
        assert(submit(2, GM_BOUNDARY_PATTERN) == GM_OK);
    hits_between(0, 50u * CTL, START[15] + CTL);
    return hits_between(0, START[15] + CTL, START[16] - 2u * CTL);
}
static void early_micro_blocked(void)
{
    uint32_t control, queued;
    starts();
    control = last_step_hits(0);
    queued = last_step_hits(1);
    check("cue: the outgoing step 0's early hit sounds without a cue (control) ...", control == 1u);
    check("cue: .. and is suppressed across a queued Grind replacement (seq_cut_ahead)", queued == 0u);
}

int main(void)
{
    boundary_exact();
    seven_four_boundary();
    nothing_before_boundary();
    longest_track();
    next_step_boundary();
    latest_wins();
    edit_rejects();
    stop_cancels();
    stopped_publishes_at_once();
    native_cue_wins();
    song_refuses();
    early_micro_blocked();
    printf("test_cue: %s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
