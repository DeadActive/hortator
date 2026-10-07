/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* SONG (song.c): rows of {slot, repeat} play the slots' steps, timing and motion with the sounds loaded now; a row
 * lasts its longest track; LOOP; STOP puts the timing and REC back. The slots are set up by hand (chain.src), as
 * project.c chain_prepare does from proj_slot[]. */
#include "drum_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

#define BAR (4u * 22050u)                           /* a bar at 120 BPM */
static step_t ST[4][NTRK][NSTEP];
static uint32_t now_s;                             /* samples rendered since fresh() */
static motion_store_t MS[4];
static void slot_set(uint32_t s, uint32_t k, int16_t len, int16_t div, const char *pat)
{
    uint32_t i;
    for (i = 0; pat[i] && i < NSTEP; i++)
        ST[s][k][i].on = pat[i] != '.';
    chain.src[s].timing[k][0] = len;
    chain.src[s].timing[k][1] = div;
}
static void fresh(void)                            /* stopped, 120 BPM; four empty slots of LEN 16 1/16 */
{
    uint32_t s, k;
    host_init();
    now_s = 0;
    song.g[G_BPM] = 120;
    memset(ST, 0, sizeof ST);
    memset(MS, 0, sizeof MS);
    for (s = 0; s < 4u; s++) {
        chain.src[s].m = &MS[s];
        for (k = 0; k < NTRK; k++) {
            chain.src[s].step[k] = ST[s][k];
            chain.src[s].timing[k][0] = 16;
            chain.src[s].timing[k][1] = 2;
            chain.src[s].timing[k][2] = 0;
            chain.src[s].timing[k][3] = 0;
            chain.src[s].model[k] = (uint8_t)trk[k].p[P_MODEL];
        }
    }
}
static void rows(uint32_t n, const uint8_t *slot, const uint8_t *rep, uint32_t loop)
{
    uint32_t i;
    chain.cfg.count = (uint8_t)n;
    chain.cfg.loop = (uint8_t)loop;
    for (i = 0; i < n; i++) {
        chain.cfg.row[i].slot = slot[i];
        chain.cfg.row[i].repeat = rep[i];
    }
}
static void play_song(void) { chain.armed = 1; transport_req = 1; }   /* (the first block: run_to) */
#define AT(b) ((b) * BAR - 2000u)                    /* just before bar b + 1's first step */
/* render until sample `until` since the first block, noting the block of each track's first hit after `from` */
static void run_to(uint32_t until)
{
    while (now_s < until && (song.playing || transport_req)) {
        render_mix(0, 0, CTL);
        now_s += CTL;
    }
}
static int hit_in(uint32_t k, uint32_t from, uint32_t to)   /* track k hits in [from, to) samples */
{
    uint32_t a;
    run_to(from);
    a = hit_age(&trk[k]);
    run_to(to);
    return hit_age(&trk[k]) != a;
}

static void test_song_rows(void)
{
    static const uint8_t SL[2] = {0, 1}, RP[2] = {2, 1};
    fresh();
    slot_set(0, 0, 16, 2, "x...............");      /* A: the kick on 1 */
    slot_set(1, 1, 16, 2, "x...............");      /* B: the snare on 1 */
    rows(2, SL, RP, 0);
    now_s = 0;
    play_song();
    check("song: row 1 (A x2): the kick in bar 1", hit_in(0, 0, BAR / 2u));
    check("song: ... and again in bar 2", hit_in(0, AT(1u), BAR + BAR / 2u) && chain.row == 0u);
    check("song: row 2 (B x1): the snare in bar 3, not the kick", hit_in(1, AT(2u), 2u * BAR + BAR / 2u) &&
          chain.row == 1u);
    run_to(3u * BAR + 2u * CTL);
    check("song: LOOP OFF: stopped after the last row", !song.playing && !chain.running);
}

static void test_song_loop(void)
{
    static const uint8_t SL[2] = {0, 1}, RP[2] = {1, 1};
    fresh();
    slot_set(0, 0, 16, 2, "x...............");
    slot_set(1, 1, 16, 2, "x...............");
    rows(2, SL, RP, 1);
    now_s = 0;
    play_song();
    check("song: LOOP ON: row 1 again after the last row", hit_in(0, AT(2u), 2u * BAR + BAR / 2u) &&
          song.playing && chain.row == 0u);
    transport_req = 2;
    render_mix(0, 0, CTL);
}

static void test_song_longest(void)
{
    static const uint8_t SL[2] = {0, 1}, RP[2] = {1, 1};
    fresh();
    slot_set(0, 0, 16, 2, "x...............");      /* A: a 1-bar kick, */
    slot_set(0, 3, 64, 2, "x.x.x.x.x.x.x.x.");      /* a 4-bar hats track */
    slot_set(1, 1, 16, 2, "x...............");
    rows(2, SL, RP, 0);
    now_s = 0;
    play_song();
    run_to(CTL);
    check("song: the reference is the longest track (hats, 64 steps)", chain.ref == 3u);
    check("song: no B in bar 2 (the row lasts the hats' 4 bars)", !hit_in(1, AT(1u), 2u * BAR) && chain.row == 0u);
    check("song: B in bar 5", hit_in(1, AT(4u), 4u * BAR + BAR / 2u) && chain.row == 1u);
    transport_req = 2;
    render_mix(0, 0, CTL);
}

static void test_song_stop_restores(void)
{
    static const uint8_t SL[1] = {0}, RP[1] = {4};
    step_t before[NSTEP];
    fresh();
    slot_set(0, 0, 16, 2, "x...x...x...x...");
    trk[0].p[P_SLEN] = 12;
    trk[0].p[P_SDIV] = 1;
    trk[0].p[P_SSWING] = 30;
    trk[0].p[P_SRC] = 2;
    trk[0].step[3].on = 1;
    memcpy(before, trk[0].step, sizeof before);
    song.rec = 5u;
    rows(1, SL, RP, 0);
    play_song();
    run_to(BAR / 2u);
    check("song: a row's timing plays (LEN 16 1/16 SWG 0 STEP), REC off",
          trk[0].p[P_SLEN] == 16 && trk[0].p[P_SDIV] == 2 && trk[0].p[P_SSWING] == 0 && trk[0].p[P_SRC] == 0 &&
          song.rec == 0u);
    check("song: the slot's steps play (seq_steps)", seq_steps(&trk[0]) == ST[0][0]);
    transport_req = 2;
    render_mix(0, 0, CTL);
    check("song: STOP puts LEN DIV SWG SRC and REC back",
          trk[0].p[P_SLEN] == 12 && trk[0].p[P_SDIV] == 1 && trk[0].p[P_SSWING] == 30 && trk[0].p[P_SRC] == 2 &&
          song.rec == 5u && !chain.running && seq_steps(&trk[0]) == trk[0].step);
    check("song: the current steps untouched", !memcmp(before, trk[0].step, sizeof before));
}

static void test_song_motion(void)
{
    static const uint8_t SL[2] = {0, 1}, RP[2] = {1, 1};
    fresh();
    trk[0].p[P_E1] = 40;
    slot_set(0, 0, 16, 2, "x...............");
    slot_set(1, 0, 16, 2, "x...............");
    MS[0].count = 1;
    MS[0].on = 1;
    MS[0].ev[0].trk = 0;
    MS[0].ev[0].step = 0;
    MS[0].ev[0].param = P_E1;
    MS[0].ev[0].value = 99;
    rows(2, SL, RP, 0);
    now_s = 0;
    play_song();
    run_to(BAR / 2u);
    check("song: row A's motion plays (E1 99)", trk[0].p[P_E1] == 99);
    run_to(BAR + BAR / 2u);
    check("song: row B (no motion): E1 back to the patch (40)", chain.row == 1u && trk[0].p[P_E1] == 40);
    transport_req = 2;
    render_mix(0, 0, CTL);
    check("song: STOP: E1 the patch", trk[0].p[P_E1] == 40);
}

static void test_song_model_skip_per_row(void)
{
    static const uint8_t SL[2] = {0, 1}, RP[2] = {1, 1};
    uint32_t i;
    fresh();
    trk[0].p[P_E1] = 40;
    trk[0].p[P_LEVEL] = 100;
    slot_set(0, 0, 16, 2, "x...............");
    slot_set(1, 0, 16, 2, "x...............");
    for (i = 0; i < 2u; i++) {                       /* both slots: E1 99 and LEVEL 50 at step 0 */
        MS[i].count = 2;
        MS[i].on = 1;
        MS[i].ev[0] = (motion_event_t){0, 0, P_E1, 99};
        MS[i].ev[1] = (motion_event_t){0, 0, P_LEVEL, 50};
    }
    chain.src[0].model[0] = (uint8_t)((trk[0].p[P_MODEL] + 1) % NMODELS);   /* A was saved with another model */
    rows(2, SL, RP, 0);
    now_s = 0;
    play_song();
    run_to(BAR / 2u);
    check("song: another model in the slot: its E events skipped, LEVEL plays",
          trk[0].p[P_E1] == 40 && trk[0].p[P_LEVEL] == 50);
    run_to(BAR + BAR / 2u);
    check("song: the next row (same model) plays its E events", chain.row == 1u && trk[0].p[P_E1] == 99);
    transport_req = 2;
    render_mix(0, 0, CTL);
}

static void test_song_no_drift(void)
{
    static const uint8_t SL[2] = {0, 0}, RP[2] = {1, 1};
    uint32_t pos, idx, cnt;
    fresh();
    trk[0].p[P_SSWING] = 0;
    memcpy(trk[0].step, ST[0][0], sizeof trk[0].step);
    song.g[G_BPM] = 133;                             /* a tempo whose 1/16 is not whole samples */
    transport_req = 1;
    run_to(BAR + BAR / 3u);
    pos = trk[0].seq_pos;
    idx = trk[0].seq_idx;
    cnt = trk[0].seq_cnt % 16u;
    transport_req = 2;
    render_mix(0, 0, CTL);
    fresh();
    song.g[G_BPM] = 133;
    rows(2, SL, RP, 0);
    now_s = 0;
    play_song();
    run_to(BAR + BAR / 3u);
    check("song: after a row change the steps sit where a plain loop's do (no drift)",
          chain.row == 1u && trk[0].seq_pos == pos && trk[0].seq_idx == idx && trk[0].seq_cnt % 16u == cnt);
    transport_req = 2;
    render_mix(0, 0, CTL);
}

static void test_song_clock_start(void)
{
    static const uint8_t SL[1] = {0}, RP[1] = {1};
    fresh();
    rows(1, SL, RP, 0);
    song.g[G_CLOCK] = 1;
    render_mix(0, 0, CTL);                           /* (the clock mode settles) */
    chain.armed = 1;
    midi_clock_transport(0xFAu, 0);
    check("song: a MIDI Start while armed starts the song", chain.running && song.playing && !chain.armed);
    chain.row = 0;
    midi_clock_transport(0xFAu, 0);
    check("song: a MIDI Start during the song restarts it at row 1", chain.running && chain.row == 0u);
    midi_clock_transport(0xFCu, 0);
    check("song: a MIDI Stop ends it", !chain.running && !song.playing);
    midi_clock_transport(0xFBu, 0);
    check("song: a MIDI Continue never starts one", !chain.running);
    midi_clock_transport(0xFCu, 0);
    song.g[G_CLOCK] = 0;
}

static void test_song_valid(void)
{
    chain_config_t c;
    memset(&c, 0, sizeof c);
    check("chain_valid: an empty song", chain_valid(&c));
    c.count = 2;
    c.row[0] = (chain_row_t){3, 16};
    c.row[1] = (chain_row_t){0, 1};
    check("chain_valid: slots A..D, repeats 1..16", chain_valid(&c));
    c.row[1].slot = 4;
    check("chain_valid: slot past D refused", !chain_valid(&c));
    c.row[1] = (chain_row_t){0, 0};
    check("chain_valid: repeat 0 refused", !chain_valid(&c));
    c.row[1].repeat = 17;
    check("chain_valid: repeat 17 refused", !chain_valid(&c));
    c.row[1].repeat = 1;
    c.count = 17;
    check("chain_valid: 17 rows refused", !chain_valid(&c));
    c.count = 2;
    c.loop = 2;
    check("chain_valid: LOOP 2 refused", !chain_valid(&c));
    check("names: the NAME set only", name_char_ok('A') && name_char_ok(' ') && name_char_ok('#') &&
          !name_char_ok('a') && !name_char_ok('!') && !name_char_ok(0));
}

int main(void)
{
    test_song_rows();
    test_song_loop();
    test_song_longest();
    test_song_stop_restores();
    test_song_motion();
    test_song_model_skip_per_row();
    test_song_no_drift();
    test_song_clock_start();
    test_song_valid();
    printf(fails ? "song_test: %d FAILED\n" : "song_test: all passed\n", fails);
    return fails ? 1 : 0;
}
