# SONG + NAME Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A song (rows of {project slot, repeat}, LOOP) plays the four slots' patterns with the sounds loaded now, and
projects get names typed on the device; project format FDR9.

**Architecture:** `firmware/src/song.c` (included by `seq.c` after `motion.c`) holds the song: its config, the
project name, and the ISR side (`chain_start` / `chain_stop` / `chain_tick` / `chain_apply`, `seq_steps`). It reads
slots through pointers (`chain.src[]`) that `project.c`'s `chain_prepare` (main loop) points at `proj_slot[]`, so
the host DSP tests can drive it without `project.c`. `motion.c` plays a row's motion through `mo.src` / `mo.skip`.
`firmware/src/ui_name.c` (included by `ui_input.c`) is the NAME screen: state, input, LEDs and drawing. The SONG
page is drawn by `ui_draw.c` and edited by `ui_input.c`.

**Tech Stack:** C (single translation unit `felucca.c`), host tests in C (`tests/*.c` with `drum_host.h` /
`ui_host.h`), `sh tests/run_drum_tests.sh`, `sh tests/run_tests.sh`, `DRUM_PACKAGE=1 ./build.sh`.

**Spec:** `docs/superpowers/specs/2026-10-07-song-chain-names-design.md`

## Global Constraints

- No frozen file changes: hal/, loader/, ota.c, usb.c, usb_app.c, crt0.S, app.ld; storage.c except ST_MAGIC;
  main.c except felucca_init() and the boot titles; core.h's last 5 lines. The update loader byte-identical.
- New state goes into structs (`chain`, `nm`, `ui`, `mo`), not new small globals (H2).
- Rows: up to 16 (`CHAIN_ROWS`), slot 0..3 (shown A..D), repeat 1..16; LOOP OFF / ON.
- A row: per track the slot's steps, `P_SLEN`, `P_SDIV`, `P_SSWING`, `P_SRC` and its motion; sounds, globals,
  mutes, PERFORM unchanged. Row length: its longest track (LEN x step length at its DIV; ties: the lowest track).
- STOP restores `P_SLEN..P_SRC` and `song.rec`; a song never changes the current steps.
- While a song plays or is armed: step / PATTERN / MOTION / SONG edits, TOOLS' clears, REC arming say
  "STOP TO EDIT"; LOAD says "STOP TO LOAD".
- Names: at most 12 characters from ` ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._/#+`, ends trimmed, empty = none.
- FDR9 = FDR8 + `chain_config_t` (36 B) + `char name[12]` = 3592 B; FDR8 and older convert (empty song, no name);
  an invalid song or name is dropped on load.
- Credit the fork as DEADACTIVE in new file headers. Version after merge: 0.12.0.
- Build env: `export PYTHON=$PWD/.venv/bin/python PATH="$PWD/.venv/bin:$PATH"`; run suites as
  `sh tests/run_drum_tests.sh` / `sh tests/run_tests.sh`; no budget line changes without the user.

## Review Focus

1. A tempo change or a MIDI clock during a song: the row change must still land on the reference track's bar (the
   remainder carried); a clock-following song must start on the clock's first pulse.
2. A model changed on a track during a song: the skip mask is per row; the next row re-reads it (no stale knob).
3. PLAY pressed twice quickly on SONG (armed, then stop before the ISR saw the start): nothing stuck armed.
4. A slot record with garbage but a valid checksum: steps (cond, rat) and timing are safe; motion invalid -> none.
5. The NAME screen while the transport plays: typing keys never sound, record, or start PERFORM; OCT+ refuses
   with STOP TO SAVE and keeps the screen.

Tests added for them: 1 in Task 1 (`test_song_no_drift`, `test_song_clock_start`), 2 in Task 1
(`test_song_model_skip_per_row`), 3 in Task 3 (`test_song_play_twice`), 4 in Task 2 (`song_garbage_slot`),
5 in Task 4 (`test_name_keys_silent`, `test_name_stop_to_save`).

---

### Task 1: the song engine (song.c, seq.c, motion.c)

**Files:**
- Create: `firmware/src/song.c`
- Modify: `firmware/src/seq.c` (include, `step_fire`, `seq_start`, `seq_stop`, `seq_tick`, `events_block`)
- Modify: `firmware/src/motion.c` (`mo.src`, `mo.skip`, `motion_step`)
- Modify: `tests/drum_host.h` (reset `chain`)
- Create: `tests/song_test.c`; Modify: `tests/run_drum_tests.sh`

**Interfaces:**
- Produces: `CHAIN_ROWS`, `NAME_LEN`, `NAME_SET`, `chain_row_t`, `chain_config_t` (36 B), `chain_src_t`,
  `CHAIN_TIMING[4]`, `MOTION_NONE`, `static struct {...} chain` with fields `cfg, name[13], from, src[4], keep,
  armed, running, row, left, ref, slot, rec, cut`; `int chain_valid(const chain_config_t *)`, `int chain_busy(void)`,
  `const step_t *seq_steps(const track_t *)`, `int name_char_ok(char)`, `void name_set(char *dst13, const char *s)`,
  `void chain_start(void)`, `void chain_stop(void)`, `void chain_tick(uint32_t n)`, `void chain_apply(uint32_t x)`.

- [ ] **Step 1: Write the failing tests** — `tests/song_test.c`:

```c
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
static void play_song(void) { chain.armed = 1; transport_req = 1; render_mix(0, 0, CTL); }
/* render until sample `until` since the first block, noting the block of each track's first hit after `from` */
static uint32_t now_s;
static void run_to(uint32_t until)
{
    while (now_s < until && song.playing) {
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
    check("song: ... and again in bar 2", hit_in(0, BAR, BAR + BAR / 2u) && chain.row == 0u);
    check("song: row 2 (B x1): the snare in bar 3, not the kick", hit_in(1, 2u * BAR, 2u * BAR + BAR / 2u) &&
          chain.row == 1u && !trk[0].v[0].active);
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
    check("song: LOOP ON: row 1 again after the last row", hit_in(0, 2u * BAR, 2u * BAR + BAR / 2u) &&
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
    check("song: the reference is the longest track (hats, 64 steps)", chain.ref == 3u);
    check("song: no B in bar 2 (the row lasts the hats' 4 bars)", !hit_in(1, BAR, 2u * BAR) && chain.row == 0u);
    check("song: B in bar 5", hit_in(1, 4u * BAR, 4u * BAR + BAR / 2u) && chain.row == 1u);
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
    now_s = 0;
    transport_req = 1;
    render_mix(0, 0, CTL);
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
```

And in `tests/run_drum_tests.sh` after the motion_test lines:

```sh
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/song_test" tests/song_test.c -lm
"$OUT/song_test"
```

- [ ] **Step 2: Run it to see it fail**

Run: `cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/song_test tests/song_test.c -lm`
Expected: compile errors (`chain` undeclared).

- [ ] **Step 3: Write song.c**

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE (the slots read in place, a row as long as its longest track, LOOP, FDR9) */
/* SONG (upstream Felucca 1.0's song_chain.c, adapted): rows of {project slot, repeat} played in order with the sounds
 * loaded now. A row plays, per track, the slot's steps, its LEN DIV SWG SRC (CHAIN_TIMING) and its motion, read in
 * place from the slot: project.c chain_prepare (main loop, before PLAY) points chain.src at proj_slot[]. The row ends
 * when its longest track (chain.ref) has played its pattern `repeat` times; every track then starts the next row's
 * step 0 together, at that sample (chain.cut: seq_tick keeps the block's rest). After the last row LOOP OFF stops,
 * LOOP ON plays row 1 again. STOP puts the timing and REC arming back; the current steps are never written. The
 * project's name lives here too (project.c, ui_name.c). The ISR runs the song; the main loop edits chain.cfg only
 * while it is off (chain_busy). */
#define CHAIN_ROWS 16u
#define NAME_LEN 12u
typedef struct { uint8_t slot, repeat; } chain_row_t;                 /* slot 0..3 (A..D), repeat 1..16 */
typedef struct { uint8_t count, loop, rsv[2]; chain_row_t row[CHAIN_ROWS]; } chain_config_t;   /* saved (FDR9) */
typedef char chain_config_size[sizeof(chain_config_t) == 36u ? 1 : -1];
typedef struct {                           /* a slot as its rows play it */
    const step_t *step[NTRK];
    const motion_store_t *m;               /* its motion (MOTION_NONE: none, or a broken one) */
    int16_t timing[NTRK][4];               /* CHAIN_TIMING, inside their ranges */
    uint8_t model[NTRK];                   /* its models: another model now = its P_E events skipped */
} chain_src_t;
static const uint8_t CHAIN_TIMING[4] = {P_SLEN, P_SDIV, P_SSWING, P_SRC};
static const motion_store_t MOTION_NONE;
static const char NAME_SET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._/#+";
static struct {
    chain_config_t cfg;                    /* the song (the project's) */
    char name[NAME_LEN + 1u];              /* the project's name, "" = none */
    uint8_t from;                          /* the slot loaded or saved last + 1, 0 = none (its rename renames this) */
    chain_src_t src[4];                    /* the slots (chain_prepare) */
    int16_t keep[NTRK][4];                 /* the timing before the song (STOP puts it back) */
    volatile uint8_t armed, running;       /* PLAY on SONG: armed until seq_start takes it; running */
    uint8_t row, left, ref, slot;          /* the row playing, its repeats left (this one too), its longest track, slot */
    uint8_t rec, cut;                      /* REC before the song; a row changed this block */
} chain;

static int chain_valid(const chain_config_t *c)
{
    uint32_t i;
    if (c->count > CHAIN_ROWS || c->loop > 1u)
        return 0;
    for (i = 0; i < c->count; i++)
        if (c->row[i].slot > 3u || !c->row[i].repeat || c->row[i].repeat > 16u)
            return 0;
    return 1;
}
static int chain_busy(void) { return chain.running || chain.armed; }
static int name_char_ok(char c)
{
    uint32_t i;
    for (i = 0; NAME_SET[i]; i++)
        if (NAME_SET[i] == c)
            return 1;
    return 0;
}
static void name_set(char *d, const char *s)      /* d: NAME_LEN + 1 bytes, 0-padded */
{
    uint32_t i;
    for (i = 0; i <= NAME_LEN; i++)
        d[i] = 0;
    for (i = 0; i < NAME_LEN && s[i]; i++)
        d[i] = s[i];
}

/* ---- the ISR (seq.c) */
static const step_t *seq_steps(const track_t *t)   /* the steps a track plays: a song's row, else its own */
{
    return chain.running ? chain.src[chain.slot].step[t - trk] : t->step;
}
static uint32_t chain_beats(const track_t *t, uint32_t *den)   /* t's pattern: num / den beats */
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
    return len * div_num_den((uint32_t)t->p[P_SDIV], den);
}
static void chain_apply(uint32_t x)                /* row chain.row starts x samples into this block (PLAY: 0) */
{
    const chain_src_t *s;
    uint32_t k, j, best = 0, bn = 0, bd = 1;
    chain.slot = chain.cfg.row[chain.row].slot;
    chain.left = chain.cfg.row[chain.row].repeat;
    s = &chain.src[chain.slot];
    mo.src = s->m ? s->m : &MOTION_NONE;
    mo.skip = 0;
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        uint32_t n, d;
        motion_restore(k);                         /* the last row's motion off (motion.c) */
        for (j = 0; j < 4u; j++)
            t->p[CHAIN_TIMING[j]] = s->timing[k][j];
        if (s->model[k] != t->p[P_MODEL])
            mo.skip |= (uint8_t)(1u << k);
        t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
        t->seq_pos = 0x7FFFFFFFu - x;              /* step 0, x samples into the block (seq_tick, chain.cut) */
        t->seq_cnt = 0xFFFFFFFFu;
        t->seq_rem = 0;
        t->rskip = 0;
        t->rat_n = 0;
        n = chain_beats(t, &d);
        if (n * bd > bn * d) {                     /* the longest; ties: the lowest track */
            best = k;
            bn = n;
            bd = d;
        }
    }
    chain.ref = (uint8_t)best;
}
static void chain_start(void)                      /* seq_start: an armed song from row 1 (a Start during one: again) */
{
    uint32_t k, j;
    if (!chain.armed && !chain.running)
        return;
    if (!chain.running) {
        for (k = 0; k < NTRK; k++)
            for (j = 0; j < 4u; j++)
                chain.keep[k][j] = trk[k].p[CHAIN_TIMING[j]];
        chain.rec = song.rec;
        song.rec = 0;
    }
    chain.armed = 0;
    chain.running = 1;
    chain.row = 0;
    chain_apply(0);
}
static void chain_stop(void)                       /* seq_stop: the timing and REC back */
{
    uint32_t k, j;
    chain.armed = 0;
    if (!chain.running)
        return;
    chain.running = 0;
    for (k = 0; k < NTRK; k++)
        for (j = 0; j < 4u; j++)
            trk[k].p[CHAIN_TIMING[j]] = chain.keep[k][j];
    song.rec = chain.rec;
    mo.src = 0;
    mo.skip = 0;
}
/* before the block's seq_ticks: the reference track's pattern ends in this block -> a repeat counted, or the next row
 * (x = the samples before that end) */
static void chain_tick(uint32_t n)
{
    track_t *t = &trk[chain.ref];
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), cur;
    if (!chain.running || t->seq_pos >= 0x7FFFFFFFu || t->seq_idx + 1u != len)
        return;
    cur = step_samples(t, div_period((uint32_t)t->p[P_SDIV], t->seq_rem), t->seq_cnt);
    if (t->seq_pos + n < cur)
        return;
    if (chain.left > 1u) {
        chain.left--;
        return;
    }
    if (chain.row + 1u < chain.cfg.count)
        chain.row++;
    else if (chain.cfg.loop)
        chain.row = 0;
    else {
        seq_stop();
        return;
    }
    chain.cut = 1;
    chain_apply(cur - t->seq_pos);
}
```

- [ ] **Step 4: Hook it in**

`motion.c`: in the `mo` struct add after `uint8_t base_ok, full;`:
```c
    const motion_store_t *src;       /* a song's row: its slot's motion (song.c), 0 = mo.s */
    uint8_t skip;                    /* .. tracks whose P_E events it skips (the slot had another model) */
```
In `motion_step`, replace `if (!motion_on(k)) return;` and the event loop's store with:
```c
    const motion_store_t *m = mo.src ? mo.src : &mo.s;
    if (!((m->on >> k) & 1u))
        return;
    ...
    for (i = 0; i < m->count; i++) {
        const motion_event_t *e = &m->ev[i];
        if (e->trk == k && e->step == s &&
            !(((mo.skip >> k) & 1u) && e->param >= P_E0 && e->param <= P_E7)) {
```
`seq.c`: after `#include "motion.c"` add `#include "song.c"                   /* SONG: rows of the four slots (upstream 1.0's song chain) */`;
forward-declare before `step_fire`: `static const step_t *seq_steps(const track_t *t);` and `static void seq_stop(void);`
is already needed by song.c (define order: song.c is included after `seq_stop`? No: song.c comes before
`seq_start`, so add `static void seq_stop(void);` before the include). `step_fire`: `const step_t *s = &seq_steps(t)[t->seq_idx];`.
`seq_start`: last line `chain_start();` (after `motion_begin();`). `seq_stop`: `{ song.playing = 0; motion_end(); chain_stop(); }`.
`seq_tick`: `t->seq_pos = song.g[G_CLOCK] || chain.cut ? t->seq_pos - 0x7FFFFFFFu : 0;`.
`events_block`: in `if (run) {`: first `if (chain.running) chain_tick(seq_n);`, after `grids_tick(seq_n);` add `chain.cut = 0;`.
`drum_host.h` `host_reset_fx`: `memset(&chain, 0, sizeof chain);` after the `mo` reset.

- [ ] **Step 5: Run the song test and the drum suite**

Run: `sh tests/run_drum_tests.sh > $JOB/tmp/drum.log 2>&1; tail -3 $JOB/tmp/drum.log; grep FAIL $JOB/tmp/drum.log`
Expected: `song_test: all passed`, `ALL DRUM HOST TESTS PASSED`, kit hashes unchanged (sound_pack_test passes).

- [ ] **Step 6: Commit** — `git add firmware/src/song.c firmware/src/seq.c firmware/src/motion.c tests/song_test.c tests/drum_host.h tests/run_drum_tests.sh && git commit -m "SONG: the engine: rows of the slots, the longest track's length, LOOP, STOP restores"`

---

### Task 2: FDR9 storage, chain_prepare, names in projects (project.c)

**Files:**
- Modify: `firmware/src/project.c`; `firmware/src/ui.c` (forward declarations); `tests/boot_test.c`

**Interfaces:**
- Consumes: Task 1's `chain`, `chain_valid`, `name_char_ok`, `name_set`, `CHAIN_TIMING`, `MOTION_NONE`, `chain_busy`.
- Produces: `PROJ_MAGIC` "FDR9" (0x39524446), `PROJ_MAGIC_V8`, `project_v8_t`, `project_t.song`, `project_t.name[12]`;
  `uint32_t chain_prepare(void)` (0 armed, 1 no rows, 2 busy, 3 + s slot s empty); `int project_name(uint32_t slot,
  char *b13)` (1 used; b the name or ""); `void project_rename(uint32_t slot, const char *name)`;
  `project_load` refuses while `chain_busy()` ("STOP TO LOAD"); `project_save` stores `chain.cfg` + `chain.name`.

- [ ] **Step 1: Write the failing tests** — in `tests/boot_test.c` before `reson_extremes`:

```c
/* FDR9: the song and the name go through a save and a load (flash) */
static int fdr9_round_trip(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    chain.cfg.count = 2;
    chain.cfg.loop = 1;
    chain.cfg.row[0] = (chain_row_t){1, 4};
    chain.cfg.row[1] = (chain_row_t){3, 16};
    name_set(chain.name, "BREAK 2");
    project_save(2);
    memset(&chain, 0, sizeof chain);
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    project_load(2);
    return chain.cfg.count == 2u && chain.cfg.loop == 1u && chain.cfg.row[0].slot == 1u &&
           chain.cfg.row[0].repeat == 4u && chain.cfg.row[1].slot == 3u && chain.cfg.row[1].repeat == 16u &&
           str_eq(chain.name, "BREAK 2") && chain.from == 3u && sizeof(project_t) == 3592u;
}
/* an FDR8 record (3544 B): its motion, an empty song, no name */
static int fdr8_converts(void)
{
    project_v8_t v;
    uint32_t i, k;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(&v, 0, sizeof v);
    v.magic = PROJ_MAGIC_V8;
    v.size = sizeof v;
    for (i = 0; i < G_COUNT; i++)
        v.g[i] = song.g[i];
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < P_COUNT; i++)
            v.t[k].p[i] = trk[k].p[i];
    v.t[2].step[9].on = 1;
    v.motion.count = 1;
    v.motion.on = 1;
    v.motion.ev[0] = (motion_event_t){0, 4, P_E1, 77};
    v.sum = proj_hash(&v, sizeof v - 4u);
    st_save(OBJ_PROJECT0 + 1u, &v, sizeof v);
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    chain.cfg.count = 3;                             /* a song and a name in RAM before: the load replaces them */
    name_set(chain.name, "OLD");
    project_load(1);
    return trk[2].step[9].on && mo.s.count == 1u && mo.s.ev[0].value == 77 && chain.cfg.count == 0u &&
           !chain.name[0] && sizeof v == 3544u;
}
/* a broken song or name: the project loads, they are dropped */
static int fdr9_bad_song_name_dropped(void)
{
    project_t *p = &proj_slot[0];
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    trk[3].step[5].on = 1;
    chain.cfg.count = 1;
    chain.cfg.row[0] = (chain_row_t){0, 1};
    name_set(chain.name, "OK");
    project_save(0);
    p->song.row[0].repeat = 0;                       /* invalid */
    p->name[0] = 'a';                                /* outside the NAME set */
    p->sum = proj_sum(p);
    memset(&chain, 0, sizeof chain);
    trk[3].step[5].on = 0;
    project_load(0);
    return trk[3].step[5].on && chain.cfg.count == 0u && !chain.name[0];
}
/* chain_prepare: rows need used slots; the slots' garbage (cond, rat, timing, motion) is made safe */
static int song_garbage_slot(void)
{
    project_t *p = &proj_slot[1];
    uint32_t b, rc_empty, rc;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    chain.cfg.count = 1;
    chain.cfg.row[0] = (chain_row_t){1, 1};
    rc_empty = chain_prepare();
    project_save(1);
    memset(p->t[0].step, 0xFF, sizeof p->t[0].step);  /* cond 255, rat 255, on 255 */
    p->t[0].p[P_SLEN] = 999;
    p->t[0].p[P_SDIV] = -7;
    p->motion.count = 200;                           /* broken */
    p->sum = proj_sum(p);
    rc = chain_prepare();
    for (b = 0; b < 400u; b++)
        render_mix(L, R, CTL);
    transport_req = 2;
    render_mix(L, R, CTL);
    return rc_empty == 4u && rc == 0u && chain.src[1].timing[0][0] == NSTEP && chain.src[1].timing[0][1] == 0 &&
           chain.src[1].m == &MOTION_NONE && !song.playing;
}
/* LOAD while a song plays: refused */
static int song_blocks_load(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    trk[0].p[P_E1] = 11;
    project_save(0);
    trk[0].p[P_E1] = 64;
    chain.cfg.count = 1;
    chain.cfg.row[0] = (chain_row_t){0, 1};
    chain_prepare();
    render_mix(L, R, CTL);
    project_load(0);
    transport_req = 2;
    render_mix(L, R, CTL);
    return trk[0].p[P_E1] == 64 && str_eq(ui.msg, "STOP TO LOAD");
}
/* a rename writes only the name (flash), and renames the current project when it is that slot */
static int rename_slot(void)
{
    char b[NAME_LEN + 1u];
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    trk[1].step[2].on = 1;
    name_set(chain.name, "A NAME");
    project_save(3);
    project_rename(3, "INTRO");
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    return project_name(3, b) && str_eq(b, "INTRO") && proj_slot[3].t[1].step[2].on && str_eq(chain.name, "INTRO") &&
           !project_name(2, b) && !b[0];
}
```

and in `main` after the motion lines:

```c
    check("project: FDR9 keeps the song and the name through a save and a load", fdr9_round_trip());
    check("project: an FDR8 record (3544 B) loads with its motion, no song, no name", fdr8_converts());
    check("project: a broken song or name is dropped, the project loads", fdr9_bad_song_name_dropped());
    check("song: an empty slot refused; a slot's garbage made safe (ASan)", song_garbage_slot());
    check("song: LOAD while a song plays: STOP TO LOAD", song_blocks_load());
    check("project: a rename writes the name only, the current project's too", rename_slot());
```

- [ ] **Step 2: Run to see it fail** — `sh tests/run_drum_tests.sh` → boot_test does not compile (`project_v8_t`, `chain_prepare`).

- [ ] **Step 3: Implement in project.c**

Header comment: Format "FDR9": ... and the song (song.c) and the name; "FDR8" converted. Magic lines:
```c
#define PROJ_MAGIC 0x39524446u                 /* "FDR9": + the song and the name (song.c) */
#define PROJ_MAGIC_V8 0x38524446u              /* "FDR8": the motion, converted on load */
```
`project_t`: after `motion_store_t motion;` add `chain_config_t song;   /* FDR9: the song (song.c) */` and
`char name[NAME_LEN];   /* FDR9: the name, NAME_SET, 0-padded; "" none */`; after the struct
`typedef char project_size[sizeof(project_t) == 3592u ? 1 : -1];`.
`project_v8_t` after `project_v7_t`:
```c
/* motion's format: everything but the song and the name. 3544 B */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];
    proj_trk_t t[NTRK];
    motion_store_t motion;
    uint32_t sum;
} project_v8_t;
```
the union gets `project_v8_t v8;`. `proj_from_old` starts with:
```c
    if (ver == 8u) {                                 /* the same up to the motion: no song, no name */
        memset(q, 0, sizeof *q);
        memcpy(q, &proj_old.v8, sizeof proj_old.v8 - 4u);
        q->magic = PROJ_MAGIC;
        q->size = sizeof *q;
        q->sum = proj_sum(q);
        return;
    }
```
`proj_fetch`: `MAGIC[9]` / `SIZE[9]` with `PROJ_MAGIC_V8` / `sizeof proj_old.v8`, loop `ver < 9u`.
Name helper (before `project_save`):
```c
/* 12 stored name bytes -> d (NAME_LEN + 1): up to the first 0; a byte outside NAME_SET: no name */
static void proj_name_get(char *d, const char *s)
{
    uint32_t i;
    name_set(d, "");
    for (i = 0; i < NAME_LEN && s[i]; i++) {
        if (!name_char_ok(s[i])) {
            name_set(d, "");
            return;
        }
        d[i] = s[i];
    }
}
```
`project_save`: after `p->motion = mo.s;`: `p->song = chain.cfg;` and `memcpy(p->name, chain.name, NAME_LEN);`
and `chain.from = (uint8_t)((slot & 3u) + 1u);`.
`project_load`: first lines `if (chain_busy()) { ui_message("STOP TO LOAD"); return; }`; after the motion lines:
```c
    if (chain_valid(&p->song))                          /* the song and the name: broken ones dropped */
        chain.cfg = p->song;
    else
        memset(&chain.cfg, 0, sizeof chain.cfg);
    proj_name_get(chain.name, p->name);
    chain.from = (uint8_t)((slot & 3u) + 1u);
```
After `project_used`:
```c
static int project_name(uint32_t slot, char *b)   /* slot's name -> b (NAME_LEN + 1, "" none); 0 = empty slot */
{
    const project_t *p = &proj_slot[slot & 3u];
    name_set(b, "");
    if (!proj_ok(p))
        return 0;
    proj_name_get(b, p->name);
    return 1;
}
static void project_rename(uint32_t slot, const char *name)   /* only the name; the current one too if it is that slot */
{
    project_t *p = &proj_slot[slot & 3u];
    char n[NAME_LEN + 1u];
    if (transport_busy()) {
        ui_message("STOP TO SAVE");
        return;
    }
    if (!proj_ok(p)) {
        ui_message("EMPTY SLOT");
        return;
    }
    name_set(n, name);
    memcpy(p->name, n, NAME_LEN);
    p->sum = proj_sum(p);
    if (chain.from == (slot & 3u) + 1u)
        name_set(chain.name, n);
#if FELUCCA_FLASH
    if (flash_ok) {
        ui_message(st_save(OBJ_PROJECT0 + (slot & 3u), p, sizeof *p) ? "SAVE ERROR" : "RENAMED");
        return;
    }
#endif
    ui_message("RENAMED (RAM)");
}
/* PLAY on SONG (main loop): the rows' slots -> chain.src, armed; the ISR starts it (seq_start). 0 armed, 1 no rows,
 * 2 busy, 3 + s slot s empty */
static uint32_t chain_prepare(void)
{
    uint32_t i, k, j, used = 0;
    if (transport_busy() || chain_busy())
        return 2;
    if (!chain.cfg.count || !chain_valid(&chain.cfg))
        return 1;
    for (i = 0; i < chain.cfg.count; i++) {
        uint32_t s = chain.cfg.row[i].slot;
        if (!project_used(s))
            return 3u + s;
        used |= 1u << s;
    }
    fm1_irq_off();
    for (i = 0; i < 4u; i++)
        if ((used >> i) & 1u) {
            const project_t *p = &proj_slot[i];
            chain_src_t *d = &chain.src[i];
            d->m = motion_valid(&p->motion) ? &p->motion : &MOTION_NONE;   /* (its steps are safe as stored: step_fire
                                                                            * bounds cond and rat) */
            for (k = 0; k < NTRK; k++) {
                d->step[k] = p->t[k].step;
                d->model[k] = (uint8_t)p->t[k].p[P_MODEL];
                for (j = 0; j < 4u; j++) {
                    uint32_t id = CHAIN_TIMING[j];
                    d->timing[k][j] = (int16_t)clamp(p->t[k].p[id], TP[id].min, TP[id].max);
                }
            }
        }
    chain.armed = 1;
    fm1_irq_on();
    transport_req = 1;
    return 0;
}
```
`ui.c` forward declarations after `project_used`: `static uint32_t chain_prepare(void);`, `static int project_name(uint32_t slot, char *b);`,
`static void project_rename(uint32_t slot, const char *name);`, `static int transport_busy(void);`.

- [ ] **Step 4: Run** — `sh tests/run_drum_tests.sh` → the six new boot checks `ok`, `ALL DRUM HOST TESTS PASSED`.

- [ ] **Step 5: Commit** — `git commit -am "SONG + NAME: FDR9 (the song and the name in the project), chain_prepare, rename"`

---

### Task 3: the SONG page (pages.c, ui.c, ui_input.c, ui_draw.c)

**Files:**
- Modify: `firmware/src/pages.c` (GR_SONG, the page after MOTION); `firmware/src/ui.c` (`ui.song_row`, `song_lock`,
  `view_step`, `seq_clear_all`, `init_all`); `firmware/src/ui_input.c` (song_edit, PLAY, REC hold / tap, confirm 3 / 4,
  the locks); `firmware/src/ui_draw.c` (graph_song, song_columns, the head's row, the confirm texts)
- Test: `tests/ui_test.c`; `tests/run_drum_tests.sh` (`build/ui_shots/song`)

**Interfaces:**
- Consumes: Task 1 `chain`, `chain_busy`, `seq_steps`; Task 2 `chain_prepare`, `project_name`.
- Produces: `GR_SONG`; `ui.song_row`; `int song_lock(void)` (1 + "STOP TO EDIT" while a song is busy);
  `void song_edit(uint32_t slot, int32_t steps)`; `void song_play(void)`; `ui.confirm` 3 = DELETE ROW, 4 = CLEAR SONG.

- [ ] **Step 1: Write the failing tests** — in `tests/ui_test.c` before `main` (and calls in `main` after
`test_motion_page();`):

```c
static void song_slot_saved(uint32_t s, const char *pat)   /* slot s: track 1 playing pat */
{
    uint32_t i;
    memset(trk[0].step, 0, sizeof trk[0].step);
    for (i = 0; pat[i]; i++)
        trk[0].step[i].on = pat[i] != '.';
    project_save(s);
}
static void test_song_page(void)
{
    ui_host_init();
    memset(proj_slot, 0, sizeof proj_slot);
    seq_open("SONG");
    check("SEQ reaches the SONG page", !ui.home && str_eq(cur_page()->title, "SONG"));
    snap_page("song/empty");
    turn(EN_K1 + 1, 1);
    ui_frame();
    check("SONG: KNOB 2 on the + row adds row 1 (A x1)", chain.cfg.count == 1u && chain.cfg.row[0].slot == 0u &&
          chain.cfg.row[0].repeat == 1u);
    turn(EN_K1 + 1, 1);
    ui_frame();
    turn(EN_K1 + 2, 3);
    ui_frame();
    check("SONG: KNOB 2 SLOT, KNOB 3 REPEAT", chain.cfg.row[0].slot == 1u && chain.cfg.row[0].repeat == 4u);
    turn(EN_K1, 1);
    ui_frame();
    turn(EN_K1 + 2, 1);
    ui_frame();
    check("SONG: the next + row: a row from the previous slot (B x1)", chain.cfg.count == 2u &&
          chain.cfg.row[1].slot == 1u && chain.cfg.row[1].repeat == 1u);
    turn(EN_K1, 5);
    ui_frame();
    check("SONG: KNOB 1 stops at the + row", ui.song_row == 2u);
    turn(EN_K1 + 3, 1);
    ui_frame();
    check("SONG: KNOB 4 LOOP ON", chain.cfg.loop == 1u);
    snap_page("song/rows");
}
static void test_song_play_ui(void)
{
    uint32_t b;
    ui_host_init();
    memset(proj_slot, 0, sizeof proj_slot);
    song_slot_saved(0, "x...x...x...x...");
    trk[0].p[P_SLEN] = 7;
    seq_open("SONG");
    tap(B_PLAY);
    check("SONG: PLAY with no rows: ADD A SONG ROW", !song.playing && str_eq(ui.msg, "ADD A SONG ROW"));
    chain.cfg.count = 2;
    chain.cfg.row[0] = (chain_row_t){0, 2};
    chain.cfg.row[1] = (chain_row_t){1, 1};
    tap(B_PLAY);
    check("SONG: PLAY with an empty slot: PATTERN B EMPTY", !song.playing && str_eq(ui.msg, "PATTERN B EMPTY"));
    chain.cfg.row[1].slot = 0;
    tap(B_PLAY);
    for (b = 0; b < 4u; b++)
        ui_frame();
    check("SONG: PLAY starts the song", song.playing && chain.running && trk[0].p[P_SLEN] == 16);
    ui.force = 1;
    snap_page("song/playing");
    turn(EN_K1 + 2, 1);
    ui_frame();
    check("SONG: an edit while it plays: STOP TO EDIT", chain.cfg.row[0].repeat == 2u && str_eq(ui.msg, "STOP TO EDIT"));
    seq_open("PATTERN");
    turn(EN_K1, 1);
    ui_frame();
    check("PATTERN while a song plays: STOP TO EDIT", trk[0].p[P_SLEN] == 16 && str_eq(ui.msg, "STOP TO EDIT"));
    seq_open("STEP");
    keys(1u << 2);
    ui_frame();
    keys(0);
    ui_frame();
    check("STEP grid while a song plays: STOP TO EDIT, the step unchanged", !trk[0].step[1].on &&
          str_eq(ui.msg, "STOP TO EDIT"));
    tap(B_PLAY);
    ui_frame();
    check("PLAY again: the song stops, LEN back", !song.playing && !chain.running && trk[0].p[P_SLEN] == 7);
}
static void test_song_play_twice(void)
{
    ui_host_init();
    memset(proj_slot, 0, sizeof proj_slot);
    song_slot_saved(0, "x...............");
    chain.cfg.count = 1;
    chain.cfg.row[0] = (chain_row_t){0, 1};
    seq_open("SONG");
    press(B_PLAY);
    ui_input();                                      /* armed, the ISR has not started it yet */
    release_all();
    press(B_PLAY);
    ui_input();                                      /* stopped again before it began */
    release_all();
    render_mix(0, 0, CTL);
    render_mix(0, 0, CTL);
    check("SONG: PLAY twice before it began: nothing playing, nothing armed", !song.playing && !chain_busy());
}
static void test_song_delete(void)
{
    ui_host_init();
    seq_open("SONG");
    chain.cfg.count = 3;
    chain.cfg.row[0] = (chain_row_t){0, 1};
    chain.cfg.row[1] = (chain_row_t){1, 2};
    chain.cfg.row[2] = (chain_row_t){2, 3};
    ui.song_row = 1;
    press(B_REC);
    host_ticks += 800u * 1000u * FM1_TICKS_PER_US;
    ui_frame();
    release_all();
    ui_frame();
    check("SONG: REC held on a row asks DELETE ROW", ui.confirm == 3u);
    snap_page("song/delete");
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
    check("SONG: OCT+ deletes it, the later rows move up", chain.cfg.count == 2u && chain.cfg.row[1].slot == 2u &&
          chain.cfg.row[1].repeat == 3u);
    ui.song_row = 2;
    press(B_REC);
    host_ticks += 800u * 1000u * FM1_TICKS_PER_US;
    ui_frame();
    release_all();
    ui_frame();
    check("SONG: REC held on the + row asks CLEAR SONG", ui.confirm == 4u);
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
    check("SONG: OCT+ clears the song", chain.cfg.count == 0u && ui.song_row == 0u);
}
```

`main`: `test_song_page(); test_song_play_ui(); test_song_play_twice(); test_song_delete();`.
`run_drum_tests.sh`: add `build/ui_shots/song` to the `mkdir -p` line and `build/ui_shots/song/*.ppm` to the `rm -f` list.

- [ ] **Step 2: Run to see it fail** — ui_test: "SEQ reaches the SONG page" FAIL (and the rest).

- [ ] **Step 3: Implement**

`pages.c`: enum gets `GR_SONG` after `GR_MOTION`; after the MOTION page:
`{"SONG", FAM_SEQ, SC_GLOBAL, GR_SONG, {0xFF, 0xFF, 0xFF, 0xFF}},   /* ROW SLOT REPEAT LOOP (song.c) */`.

`ui.c`: `ui` struct: `uint8_t song_row;   /* SONG: the row selected (count = the + row) */`. After `ui_message`:
```c
static int song_lock(void)                          /* a song plays (or is armed): its patterns stay as they are */
{
    if (!chain_busy())
        return 0;
    ui_message("STOP TO EDIT");
    return 1;
}
```
`view_step`: `return seq_steps(t)[si % NSTEP];` (the playing row's steps while a song plays).
`step_press`: after `ui.step_si[k] = 0xFFFFu;`: `if (song_lock()) return;`.
`seq_clear_all`: end: `memset(&chain.cfg, 0, sizeof chain.cfg); ui.song_row = 0;`.
`init_all`: inside the IRQ-off section after the `mo` memset: `memset(&chain, 0, sizeof chain);   /* no song, no name */`,
after `ui.bank = 0;`: `ui.song_row = 0;`.

`ui_input.c` — new functions before `edit_param`:
```c
/* SONG page: KNOB 1 ROW (up to the + row), 2 SLOT, 3 REPEAT, 4 LOOP; on the + row a turn of 2 / 3 adds a row */
static void song_edit(uint32_t slot, int32_t steps)
{
    chain_config_t *c = &chain.cfg;
    uint32_t r = ui.song_row;
    int32_t dir = steps > 0 ? 1 : -1;
    if (slot == 0u) {
        ui.song_row = (uint8_t)clamp((int32_t)r + dir, 0, c->count < CHAIN_ROWS ? c->count : CHAIN_ROWS - 1u);
        return;
    }
    if (song_lock())
        return;
    if (slot == 3u) {
        c->loop = steps > 0;
        return;
    }
    if (r >= c->count) {
        c->row[r].slot = r ? c->row[r - 1u].slot : 0u;
        c->row[r].repeat = 1;
        c->count = (uint8_t)(r + 1u);
        return;
    }
    if (slot == 1u)
        c->row[r].slot = (uint8_t)clamp((int32_t)c->row[r].slot + dir, 0, 3);
    else
        c->row[r].repeat = (uint8_t)clamp((int32_t)c->row[r].repeat + dir, 1, 16);
}
static void song_play(void)                         /* PLAY on SONG */
{
    uint32_t rc = chain_prepare();
    if (rc >= 3u) {
        char b[16] = "PATTERN A EMPTY";
        b[8] = (char)('A' + rc - 3u);
        ui_message(b);
    } else if (rc == 1u) {
        ui_message("ADD A SONG ROW");
    } else if (rc == 2u) {
        ui_message("STOP FIRST");
    }
    ui.force = 1;
}
static void song_rec_hold(void)                     /* REC held on SONG: delete the row / clear the song */
{
    if (song_lock())
        return;
    if (ui.song_row < chain.cfg.count)
        ui.confirm = 3;
    else if (chain.cfg.count)
        ui.confirm = 4;
    else
        return;
    ui.confirm_trk = ui.song_row;
    ui.force = 1;
}
```
`tracks_edit`: before `id = ...`: `if (slot == 2u && song_lock()) return;`.
`edit_param`: GR_MOTION branch becomes `if (!song_lock()) motion_page_edit(slot, steps);`; after it:
`if (pg->graph == GR_SONG) { song_edit(slot, steps); return; }`; before `d = page_desc(...)`:
`if (pg->graph == GR_STEPS && song_lock()) return;`. In the GO arming `if`, first:
```c
    if ((id == G_CLRSEQ || id == G_CLRALL || id == G_INITALL) && song_lock()) {
        *vp = 0;
        return;
    }
```
`ui_input`: REC hold:
```c
    if (rec == BT_HOLD) {                               /* REC held on SEQ / TRACKS: "clear track n?"; SONG: its row */
        if (!ui.home && cur_page()->graph == GR_SONG) {
            song_rec_hold();
        } else if (!song_lock()) {
            ui.confirm = 1;
            ui.confirm_trk = song.sel;
            ui.force = 1;
        }
    } else if (rec == BT_TAP && !ui.confirm) {
        if (fam == FAM_MIX) {
            if (!song_lock())
                tracks_rec_tap();
        } else {
            open_family(FAM_MIX);
        }
    }
```
The confirm's OCT+ branch: before `if (ui.confirm == 2u)` add
```c
            if (ui.confirm == 3u) {                     /* SONG: the row out, the later ones up */
                uint32_t r = ui.confirm_trk, i;
                for (i = r; i + 1u < chain.cfg.count; i++)
                    chain.cfg.row[i] = chain.cfg.row[i + 1u];
                if (r < chain.cfg.count)
                    chain.cfg.count--;
                ui_message("ROW DELETED");
            } else if (ui.confirm == 4u) {
                memset(&chain.cfg, 0, sizeof chain.cfg);
                ui.song_row = 0;
                ui_message("SONG CLEARED");
            } else
```
(the existing `if (ui.confirm == 2u) {...} else {...}` follows the `else`). B_PLAY:
```c
        case B_PLAY:
            if (song.playing || chain_busy())
                transport_req = 2;
            else if (!ui.home && cur_page()->graph == GR_SONG)
                song_play();
            else
                transport_req = 1;
            break;
```
The knob loop's hot column: `if (ui.home || pg->scope == SC_GRID || pg->scope == SC_MIX || pg->graph == GR_SONG || page_desc(pg, k, &hv))`.

`ui_draw.c` — the head: `sig` += `(chain.running ? (chain.row + 1u) * 104729u : 0u)`; after the BPM text:
```c
    if (chain.running) {                              /* a song: its row */
        str_cpy(b, "SONG ", sizeof b);
        fmt_int(b + 5, (int32_t)chain.row + 1);
        cv_text(84, 1, &FONT_S, b, C_HI);
    }
```
The SONG graph and columns (after `motion_columns`):
```c
/* SONG: 4 rows of the list ("2  B  x4  BREAK"), the + row after the last; the playing row marked, its repeats left */
static void graph_song(void)
{
    const chain_config_t *c = &chain.cfg;
    uint32_t sel = ui.song_row, first = sel > 2u ? sel - 2u : 0u, i, sig = 2166136261u, last;
    char b[16], nm[NAME_LEN + 1u];
    for (i = 0; i < c->count; i++)
        sig = (sig ^ (c->row[i].slot | (uint32_t)c->row[i].repeat << 2)) * 16777619u;
    for (i = 0; i < 4u; i++) {
        project_name(i, nm);
        sig = str_hash(sig, nm) + (uint32_t)project_used(i) * (i + 3u);
    }
    sig += c->count * 7u + sel * 1009u + c->loop * 13u +
           (chain.running ? (chain.row + 1u) * 104729u + chain.left * 65537u : 0u);
    if (!ui.force && sig == ui.graph_sig)
        return;
    ui.graph_sig = sig;
    cv_begin(240, H_GRAPH, C_BLACK);
    cv_oy = 0;
    last = c->count < CHAIN_ROWS ? c->count : CHAIN_ROWS - 1u;
    for (i = first; i <= last && i < first + 4u; i++) {
        int32_t y = 6 + (int32_t)(i - first) * 28;
        int s = i == sel, play = chain.running && i == chain.row;
        uint16_t col = s ? C_WHITE : C_GRAY;
        if (s)
            cv_rect(0, y - 3, 240, 24, C_LINE);
        if (play) {
            uint32_t j;
            for (j = 0; j < 5u; j++)                  /* the play mark */
                cv_rect(4 + (int32_t)j, y + 2 + (int32_t)j, 1, 14 - 2 * (int32_t)j, C_WHITE);
        }
        if (i >= c->count) {
            cv_text(16, y, &FONT_S, "+", col);
            if (!c->count)
                cv_text(44, y, &FONT_S, "KNOB 2: ADD A ROW", C_DIM);
            continue;
        }
        fmt_int(b, (int32_t)i + 1);
        cv_text(16, y, &FONT_S, b, col);
        b[0] = (char)('A' + c->row[i].slot);
        b[1] = 0;
        cv_text(48, y, &FONT_S, b, s ? C_WHITE : C_HI);
        b[0] = 'x';
        fmt_int(b + 1, c->row[i].repeat);
        cv_text(70, y, &FONT_S, b, col);
        if (play) {
            fmt_int(b, chain.left);
            str_cpy(b + str_len(b), " LEFT", 8);
            cv_text(236 - text_w(&FONT_S, b), y, &FONT_S, b, C_AMB);
        } else if (project_name(c->row[i].slot, nm) && nm[0]) {
            char f[NAME_LEN + 1u];
            fit(f, nm, &FONT_S, 124);
            cv_text(108, y, &FONT_S, f, C_DIM);
        }
    }
    cv_blit_from(0, Y_GRAPH, 0);
}
/* SONG: KNOB 1 ROW, 2 SLOT, 3 REPS, 4 LOOP */
static void song_columns(void)
{
    const chain_config_t *c = &chain.cfg;
    uint32_t r = ui.song_row;
    char v[12], u[8];
    if (r >= c->count) {
        draw_column(0, "ROW", "+", "", VAL(0u), -1, ICON_AUTO);
        draw_column(1, "SLOT", "--", "", C_DIM, -1, ICON_AUTO);
        draw_column(2, "REPS", "--", "", C_DIM, -1, ICON_AUTO);
    } else {
        fmt_int(v, (int32_t)r + 1);
        u[0] = '/';
        fmt_int(u + 1, c->count);
        draw_column(0, "ROW", v, u, VAL(0u), -1, ICON_AUTO);
        v[0] = (char)('A' + c->row[r].slot);
        v[1] = 0;
        draw_column(1, "SLOT", v, "", VAL(1u), c->row[r].slot * 1000 / 3, ICON_AUTO);
        v[0] = 'x';
        fmt_int(v + 1, c->row[r].repeat);
        draw_column(2, "REPS", v, "", VAL(2u), (c->row[r].repeat - 1) * 1000 / 15, ICON_AUTO);
    }
    draw_column(3, "LOOP", c->loop ? "ON" : "OFF", "", VAL(3u), -1, ICON_AUTO);
}
```
`draw_graph`: after the GR_MOTION branch: `if (!ui.home && pg->graph == GR_SONG) { graph_song(); ui.graph_top = 1; return; }`.
`draw_columns`: after the GR_MOTION branch: `if (!ui.home && pg->graph == GR_SONG) { song_columns(); return; }`.
The confirm text: replace the `char b[20] ...` block with
```c
            char b[20] = "CLEAR TRACK 1?";
            if (ui.confirm == 2u) {
                str_cpy(b, "CLEAR MOTION T1?", sizeof b);
                b[14] = (char)('1' + ui.confirm_trk);
            } else if (ui.confirm == 3u) {
                str_cpy(b, "DELETE ROW ", sizeof b);
                fmt_int(b + 11, (int32_t)ui.confirm_trk + 1);
                str_cpy(b + str_len(b), "?", 4);
            } else if (ui.confirm == 4u) {
                str_cpy(b, "CLEAR SONG?", sizeof b);
            } else {
                b[12] = (char)('1' + ui.confirm_trk);
            }
```

- [ ] **Step 4: Run** — `sh tests/run_drum_tests.sh`: the SONG checks ok, ALL DRUM HOST TESTS PASSED; look at
`build/ui_shots/song/*.png`.

- [ ] **Step 5: Commit** — `git commit -am "SONG: the SONG page (ROW SLOT REPS LOOP), PLAY, DELETE ROW / CLEAR SONG, edits locked while it plays"`

---

### Task 4: the NAME screen and names on PROJECT (ui_name.c, ui_input.c, ui_draw.c, params.c)

**Files:**
- Create: `firmware/src/ui_name.c`
- Modify: `firmware/src/ui.c` (forward declarations), `firmware/src/ui_input.c` (include, SAVE, G_NAME, modal input,
  `fx_allowed`, LEDs), `firmware/src/ui_draw.c` (NAME screen hook, `graph_slots`, the G_NAME column, the slots
  signature), `firmware/src/params.c` (G_SLOT A..D)
- Test: `tests/ui_test.c` (new tests + `test_no_save_while_playing` saves through NAME); `tests/run_drum_tests.sh`
  (`build/ui_shots/name`)

**Interfaces:**
- Consumes: Task 1 `NAME_SET`, `name_set`, `chain.name`; Task 2 `project_save`, `project_rename`, `project_name`,
  `transport_busy`; `STEP_KEY` (seq.c), `panel_enc`, `enc_drop`.
- Produces: `int name_on(void)`, `void name_open(uint32_t kind, uint32_t slot)` (`NK_SAVE` / `NK_RENAME`),
  `void name_rename(void)`, `void name_input(uint32_t pressed, uint32_t notes, uint32_t now, uint32_t home)`,
  `uint32_t name_leds(uint32_t now)` (key bits), `void draw_name(void)`.

- [ ] **Step 1: Write the failing tests** — `tests/ui_test.c`:

```c
static void name_type(uint32_t key)                  /* one key tap (key index from F3) */
{
    keys(1u << key);
    ui_frame();
    keys(0);
    ui_frame();
}
static void project_page(void)
{
    uint32_t k;
    for (k = 0; k < 8u && (ui.home || page_id(cur_page(), 0) != G_SLOT); k++)
        tap(B_SAVE);
}
static void save_knob(void)                          /* SAVE: GO, and GO again */
{
    turn(EN_K1 + 3, 1);
    ui_frame();
    turn(EN_K1 + 3, 1);
    ui_frame();
}
static void test_name_save(void)
{
    ui_host_init();
    memset(proj_slot, 0, sizeof proj_slot);
    project_page();
    save_knob();
    check("SAVE opens NAME, prefilled PROJECT A, nothing written yet", name_on() && str_eq(nm.s, "PROJECT A") &&
          !project_used(0));
    snap_page("name/abc");
    while (nm.len) {                                 /* C# (key 8): DELETE */
        nm.cur = nm.len;
        name_type(8);
    }
    name_type(0);                                    /* F3: AB -> A */
    name_type(0);                                    /* again within 0.8 s: B */
    host_ticks += 900u * 1000u * FM1_TICKS_PER_US;   /* 0.9 s: kept */
    ui_frame();
    name_type(2);                                    /* G3: CD -> C */
    host_ticks += 900u * 1000u * FM1_TICKS_PER_US;
    ui_frame();
    name_type(3);                                    /* G#3: SPACE */
    name_type(10);                                   /* D#4: 123 */
    snap_page("name/123");
    name_type(0);                                    /* 1 */
    name_type(2);                                    /* 2 */
    check("NAME: multi-tap, the timeout, SPACE, 123", str_eq(nm.s, "BC 12") && nm.cur == 5u);
    name_type(1);                                    /* F#3: left */
    name_type(8);                                    /* DELETE: the 1 */
    check("NAME: cursor left, DELETE", str_eq(nm.s, "BC 2") && nm.cur == 3u);
    turn(EN_K1, -3);
    ui_frame();
    turn(EN_K1 + 1, 1);
    ui_frame();
    check("NAME: KNOB 1 the cursor, KNOB 2 the character", nm.cur == 0u && nm.s[0] == 'C');
    turn(EN_K1, 10);
    ui_frame();
    name_type(3);                                    /* a trailing space: dropped when written */
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
    {
        char b[NAME_LEN + 1u];
        check("NAME OCT+: saved with the name (ends trimmed), the current name", !name_on() && project_used(0) &&
              project_name(0, b) && str_eq(b, "CC 2") && str_eq(chain.name, "CC 2"));
    }
    project_page();
    ui.force = 1;
    snap_page("name/project");
    turn(EN_K1 + 1, 1);
    ui_frame();
    check("NAME knob: renames the selected slot, prefilled with its name", name_on() && str_eq(nm.s, "CC 2"));
    press(B_OCTDN);
    ui_frame();
    release_all();
    ui_frame();
    check("NAME OCT-: cancelled, nothing written", !name_on() && str_eq(chain.name, "CC 2"));
    turn(EN_K1, 1);                                  /* slot B: empty */
    ui_frame();
    turn(EN_K1 + 1, 1);
    ui_frame();
    check("NAME knob on an empty slot: EMPTY SLOT", !name_on() && str_eq(ui.msg, "EMPTY SLOT"));
}
static void test_name_keys_silent(void)
{
    uint32_t a;
    ui_host_init();
    project_page();
    transport_req = 1;
    ui_frame();
    transport_req = 0;
    name_open(NK_SAVE, 0);
    song.rec = 1u;
    a = hit_age(&trk[0]);
    name_type(0);                                    /* F3: track 1's key */
    press(B_FX);
    ui_frame();
    name_type(1);                                    /* F#3 with FX held: not PERFORM */
    release_all();
    ui_frame();
    check("NAME: the keys never sound, record or start PERFORM", hit_age(&trk[0]) == a && !perf_held &&
          !trk[0].step[0].on && !trk[0].step[1].on && str_eq(nm.s, "PROJECT AA"));
}
static void test_name_stop_to_save(void)
{
    ui_host_init();
    memset(proj_slot, 0, sizeof proj_slot);
    project_page();
    name_open(NK_SAVE, 0);
    transport_req = 1;
    ui_frame();
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
    check("NAME OCT+ while playing: STOP TO SAVE, the screen stays", name_on() && !project_used(0) &&
          str_eq(ui.msg, "STOP TO SAVE"));
    tap(B_PLAY);
    ui_frame();
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
    check("... stopped (PLAY works in NAME): saved", !name_on() && project_used(0));
    name_open(NK_SAVE, 1);
    tap(B_HOME);
    ui_frame();
    check("NAME: HOME cancels", !name_on() && !project_used(1));
}
```

`main`: `test_name_save(); test_name_keys_silent(); test_name_stop_to_save();`. `test_no_save_while_playing`'s last
save (`turn(EN_K1 + 3, 1)` twice, then `check("SAVE stopped: saved"`) gets, before the check:
```c
    press(B_OCTUP);                                  /* NAME: OCT+ writes */
    ui_frame();
    release_all();
    ui_frame();
```
`run_drum_tests.sh`: `build/ui_shots/name` in `mkdir -p` and `build/ui_shots/name/*.ppm` in the `rm -f` list.

- [ ] **Step 2: Run to see it fail** — ui_test doesn't compile (`name_on`, `nm`, `NK_SAVE`).

- [ ] **Step 3: Write ui_name.c**

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE (projects only, the fork's keys and screen) */
/* NAME (upstream Felucca 1.0's ui_name.c, adapted; included by ui_input.c): naming a project on the device.
 * SAVE (PROJECT, the second detent) opens it before anything is written, prefilled with the current name ("PROJECT A"
 * when there is none); the NAME knob renames the selected used slot. OCT+ writes (the transport stopped: else STOP
 * TO SAVE and the screen stays), OCT- or HOME cancels. The keys type and never sound, record, send MIDI or play
 * PERFORM (seq.c keyboard_block: song.seq_mode 1; fx_allowed). Upper case, at most 12:
 *   white keys (16, F3..G5)  ABC: AB CD EF GH IJK LM NO PQ RS TU VW XYZ 123 456 789 0-. ; a tap types the group's
 *                            first character, another tap of the same key within 0.8 s the next (cycling); another
 *                            key or 0.8 s keeps it. 123: 1 2 3 4 5 6 7 8 9 0 - . _ / # + , one tap each.
 *   black keys, by name      F# cursor left, G# SPACE, A# cursor right, C# DELETE (held: repeats, as the arrows),
 *                            D# ABC / 123.
 *   KNOB 1 the cursor; KNOB 2 the character at the cursor (NAME_SET; at the end: a new one).
 *   LEDs: every key that types or edits lit; the key being cycled blinks. Spaces at the ends are dropped. */
enum { NK_NONE, NK_SAVE, NK_RENAME };
#define NM_TAP_MS 800u
#define NM_REP_MS 450u
#define NM_RATE_MS 90u
#define NM_T(ms) ((ms) * 1000u * FM1_TICKS_PER_US)
enum { NB_LEFT, NB_SPACE, NB_RIGHT, NB_DEL, NB_MODE, NB_NONE };
static const char *const NM_ABC[16] = {"AB", "CD", "EF", "GH", "IJK", "LM", "NO", "PQ", "RS", "TU", "VW", "XYZ",
                                       "123", "456", "789", "0-."};
static const char NM_NUM[17] = "1234567890-._/#+";
static struct {
    uint8_t kind, slot;                /* NK_*; the slot written */
    uint8_t len, cur;                  /* the name's length; the cursor 0..len */
    uint8_t num;                       /* the white keys: 0 ABC, 1 123 */
    uint8_t key, tap;                  /* the white key (place + 1) being cycled, 0 none; its character's index */
    uint8_t rep;                       /* a held arrow / DELETE (key + 1), 0 none */
    uint32_t t, rep_t;                 /* ticks of the last tap; of the next repeat */
    uint32_t sig;                      /* what was drawn */
    char s[NAME_LEN + 1u];
} nm;

static int name_on(void) { return nm.kind != NK_NONE; }
static void name_close(void)
{
    nm.kind = NK_NONE;
    page_entered();                                /* the keys back to the page (seq_mode) */
}
static int key_black(uint32_t k)
{
    uint32_t pc = (k + 5u) % 12u;                  /* key 0 = F3 */
    return pc == 1u || pc == 3u || pc == 6u || pc == 8u || pc == 10u;
}
static uint32_t key_place(uint32_t k)              /* a white key's place 0..15 (STEP_KEY), 16 = none */
{
    uint32_t p;
    for (p = 0; p < 16u && STEP_KEY[p] != k; p++)
        ;
    return p;
}
static uint32_t nm_black(uint32_t k)
{
    switch ((k + 5u) % 12u) {
    case 6: return NB_LEFT;
    case 8: return NB_SPACE;
    case 10: return NB_RIGHT;
    case 1: return NB_DEL;
    case 3: return NB_MODE;
    default: return NB_NONE;
    }
}
static const char *nm_group(uint32_t p)
{
    static char one[2];
    if (!nm.num)
        return NM_ABC[p & 15u];
    one[0] = NM_NUM[p & 15u];
    one[1] = 0;
    return one;
}

static void name_open(uint32_t kind, uint32_t slot)
{
    char b[NAME_LEN + 1u];
    nm.kind = (uint8_t)kind;
    nm.slot = (uint8_t)(slot & 3u);
    nm.num = nm.key = nm.rep = 0;
    nm.sig = 0;
    if (kind == NK_RENAME)
        project_name(slot, b);
    else
        name_set(b, chain.name);
    if (!b[0]) {
        name_set(b, "PROJECT A");
        b[8] = (char)('A' + (slot & 3u));
    }
    name_set(nm.s, b);
    nm.len = nm.cur = (uint8_t)str_len(nm.s);
    song.seq_mode = 1;                             /* the keys' presses are NAME's (seq.c: no hits, no MIDI) */
    ui.force = 1;
}
static void name_rename(void)                      /* PROJECT, the NAME knob */
{
    uint32_t k = (uint32_t)song.g[G_SLOT] - 1u;
    if (!project_used(k))
        ui_message("EMPTY SLOT");
    else if (transport_busy())
        ui_message("STOP TO SAVE");
    else
        name_open(NK_RENAME, k);
}

static void nm_commit(void)                        /* the letter being cycled is kept: the cursor past it */
{
    if (nm.key) {
        nm.key = 0;
        nm.cur++;
    }
}
static int nm_insert(char c)                       /* at the cursor (it stays on it); 0 = full */
{
    uint32_t i;
    if (nm.len >= NAME_LEN) {
        ui_message("NAME FULL");
        return 0;
    }
    for (i = nm.len; i > nm.cur; i--)
        nm.s[i] = nm.s[i - 1u];
    nm.s[nm.cur] = c;
    nm.s[++nm.len] = 0;
    return 1;
}
static void nm_white(uint32_t p, uint32_t now)
{
    const char *g = nm_group(p);
    uint32_t n = str_len(g);
    if (nm.key == p + 1u && now - nm.t < NM_T(NM_TAP_MS)) {   /* the same key again: its next character */
        nm.tap = (uint8_t)((nm.tap + 1u) % n);
        nm.s[nm.cur] = g[nm.tap];
        nm.t = now;
        return;
    }
    nm_commit();
    if (!nm_insert(g[0]))
        return;
    if (n > 1u) {
        nm.key = (uint8_t)(p + 1u);
        nm.tap = 0;
        nm.t = now;
    } else {
        nm.cur++;
    }
}
static void nm_do(uint32_t f)                      /* a black key's function */
{
    uint32_t i;
    nm_commit();
    switch (f) {
    case NB_LEFT:
        if (nm.cur)
            nm.cur--;
        break;
    case NB_RIGHT:
        if (nm.cur < nm.len)
            nm.cur++;
        break;
    case NB_SPACE:
        if (nm_insert(' '))
            nm.cur++;
        break;
    case NB_DEL:
        if (!nm.cur)
            break;
        for (i = --nm.cur; i < nm.len; i++)
            nm.s[i] = nm.s[i + 1u];
        nm.len--;
        break;
    case NB_MODE:
        nm.num ^= 1u;
        break;
    default:
        break;
    }
}
static void nm_knob(uint32_t k, int32_t s)         /* KNOB 1 the cursor, KNOB 2 the character there */
{
    int32_t i, n = (int32_t)sizeof NAME_SET - 1;
    nm_commit();
    if (k == 0u) {
        nm.cur = (uint8_t)clamp((int32_t)nm.cur + s, 0, nm.len);
        return;
    }
    if (nm.cur == nm.len && !nm_insert(' '))
        return;
    for (i = 0; i < n && NAME_SET[i] != nm.s[nm.cur]; i++)
        ;
    i = i < n ? i : 0;
    nm.s[nm.cur] = NAME_SET[((i + s) % n + n) % n];
}
static void name_ok(void)                          /* OCT+: written (ends trimmed); refused while playing */
{
    char b[NAME_LEN + 1u];
    uint32_t a = 0, z;
    nm_commit();
    for (z = nm.len; z && nm.s[z - 1u] == ' '; z--)
        ;
    while (a < z && nm.s[a] == ' ')
        a++;
    name_set(b, "");
    for (z -= a; z--;)
        b[z] = nm.s[a + z];
    if (transport_busy()) {
        ui_message("STOP TO SAVE");
        return;
    }
    if (nm.kind == NK_SAVE) {
        name_set(chain.name, b);
        project_save(nm.slot);
    } else {
        project_rename(nm.slot, b);
    }
    name_close();
}
/* one UI frame of NAME (ui_input.c): button edges, note edges, the HOME button's tap / hold */
static void name_input(uint32_t pressed, uint32_t notes, uint32_t now, uint32_t home)
{
    uint32_t k;
    int32_t s;
    song.seq_mode = 1;
    if (home) {                                    /* HOME: cancel */
        name_close();
        return;
    }
    if ((pressed >> panel.btn[B_PLAY]) & 1u)
        transport_req = song.playing || chain_busy() ? 2 : 1;
    if (nm.key && now - nm.t >= NM_T(NM_TAP_MS))
        nm_commit();
    for (k = 0; k < 27u; k++) {
        if (!((notes >> k) & 1u))
            continue;
        if (!key_black(k)) {
            if (key_place(k) < 16u)
                nm_white(key_place(k), now);
            continue;
        }
        nm_do(nm_black(k));
        if (nm_black(k) == NB_LEFT || nm_black(k) == NB_RIGHT || nm_black(k) == NB_DEL) {
            nm.rep = (uint8_t)(k + 1u);
            nm.rep_t = now + NM_T(NM_REP_MS);
        }
    }
    if (nm.rep && !((fm1_in.notes >> (nm.rep - 1u)) & 1u))
        nm.rep = 0;
    else if (nm.rep && (int32_t)(now - nm.rep_t) >= 0) {
        nm_do(nm_black(nm.rep - 1u));
        nm.rep_t = now + NM_T(NM_RATE_MS);
    }
    for (k = 0; k < 2u; k++)
        if ((s = panel_enc(EN_K1 + k)) != 0)
            nm_knob(k, s);
    enc_drop();
    if ((pressed >> panel.btn[B_OCTUP]) & 1u)
        name_ok();
    else if ((pressed >> panel.btn[B_OCTDN]) & 1u)
        name_close();
}
static uint32_t name_leds(uint32_t now)            /* the keys lit: every one that acts; the one cycling blinks */
{
    uint32_t k, m = 0, blink = ((now / NM_T(250u)) & 1u) == 0u;
    for (k = 0; k < 27u; k++) {
        uint32_t on = key_black(k) ? nm_black(k) != NB_NONE : key_place(k) < 16u && (nm.key != key_place(k) + 1u || blink);
        m |= on << k;
    }
    return m;
}

/* the screen: what is written, the name in large cells with the cursor, the cycling key's letters, the white keys'
 * groups, the black keys, OCT+ / OCT- */
static void draw_name(void)
{
    char b[24];
    uint32_t i, sig = 2166136261u;
    sig = str_hash(sig, nm.s) + nm.cur * 7u + nm.key * 131u + nm.tap * 977u + nm.num * 3u + nm.kind * 11u +
          (uint32_t)transport_busy() * 29u + (ui.msg_t ? str_hash(5u, ui.msg) : 0u);
    if (!ui.force && sig == nm.sig)
        return;
    nm.sig = sig;
    ui.force = 0;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    str_cpy(b, nm.kind == NK_SAVE ? "SAVE PROJECT A" : "RENAME PROJECT A", sizeof b);
    b[str_len(b) - 1u] = (char)('A' + nm.slot);
    draw_text_box(4, 6, 180, &FONT_S, ui.msg_t ? ui.msg : b, ui.msg_t ? C_HI : C_GRAY, 0);
    fmt_int(b, nm.len);
    str_cpy(b + str_len(b), "/12", 4);
    draw_text_box(184, 6, 52, &FONT_S, b, nm.len >= NAME_LEN ? C_WHITE : C_DIM, 0);
    cv_begin(240, 40, C_BLACK);
    for (i = 0; i <= NAME_LEN; i++) {
        int32_t x = 12 + 18 * (int32_t)i;
        char c[2] = {i < nm.len ? nm.s[i] : 0, 0};
        if (i < NAME_LEN)
            cv_rect(x + 2, 36, 14, 1, C_LINE);
        if (i == nm.cur)
            cv_rect(x, 37, 18, 3, nm.key ? C_AMB : C_WHITE);
        if (c[0] && c[0] != ' ')
            cv_text(x + 2, 4, &FONT_L, c, i == nm.cur && nm.key ? C_AMB : C_WHITE);
    }
    cv_blit(0, 34);
    if (nm.key) {
        const char *g = nm_group(nm.key - 1u);
        str_cpy(b, g, sizeof b);
        str_cpy(b + str_len(b), "  TAP AGAIN: NEXT", 20);
        draw_text_box(4, 84, 232, &FONT_S, b, C_AMB, 0);
    } else {
        draw_text_box(4, 84, 232, &FONT_S, nm.num ? "123: ONE TAP, ONE CHARACTER" : "ABC: TAP AGAIN, NEXT LETTER", C_GRAY, 0);
    }
    for (i = 0; i < 2u; i++) {                     /* the white keys: the left octave, then the right one */
        uint32_t p;
        b[0] = 0;
        for (p = i * 8u; p < i * 8u + 8u; p++) {
            str_cpy(b + str_len(b), nm_group(p), 6);
            if (p < i * 8u + 7u)
                str_cpy(b + str_len(b), " ", 2);
        }
        draw_text_box(0, 110 + 20 * (int32_t)i, 240, &FONT_S, b, C_HI, 1);
    }
    draw_text_box(0, 156, 240, &FONT_S, "F# LEFT   G# SPACE   A# RIGHT", C_GRAY, 1);
    draw_text_box(0, 174, 240, &FONT_S, nm.num ? "C# DELETE   D# ABC" : "C# DELETE   D# 123", C_GRAY, 1);
    draw_text_box(0, 200, 240, &FONT_S, "KNOB 1 MOVE   KNOB 2 CHARACTER", C_DIM, 1);
    draw_text_box(0, 218, 240, &FONT_S, nm.kind == NK_SAVE ? "OCT- CANCEL   OCT+ SAVE" : "OCT- CANCEL   OCT+ RENAME",
                  C_WHITE, 1);
}
```

- [ ] **Step 4: Hook it in**

`ui.c` forward declarations: `static int name_on(void);` and `static void draw_name(void);`.
`ui_input.c`: after the `led_put` helpers' file header, `#include "ui_name.c"   /* NAME: naming a project */` placed
after `cur_fam`/`octdn_held` (before `ui_leds`). `ui_leds`: before `if (ui.layer) {`:
```c
    if (name_on()) {                                  /* NAME: the keys that type */
        uint32_t m = name_leds(fm1_ticks());
        for (k = 0; k < 27u; k++)
            led_put(nl, 14u + k, (int)((m >> k) & 1u));
    } else if (ui.layer) {
```
`fx_allowed`: `return !ui.menu && !ui.confirm && !name_on();`.
`ui_input`: after `notes &= ~kb_layer;`:
```c
    if (name_on()) {                                  /* NAME owns the panel (OCT+ / OCT- / HOME / PLAY, keys, K1 K2) */
        name_input(pressed, notes, now, home);
        return;
    }
```
The GO switch: `case G_SAVE: *vp = 0; if (transport_busy()) ui_message("STOP TO SAVE"); else name_open(NK_SAVE, (uint32_t)song.g[G_SLOT] - 1u); break;`.
`edit_param`: before `d = page_desc(...)`: `if (pg->graph == GR_SLOTS && id == G_NAME) { if (steps > 0) name_rename(); return; }`.
`ui_draw.c` `ui_draw`: after the `ui.confirm` block: `if (name_on()) { draw_name(); return; }`.
`graph_slots`:
```c
static void graph_slots(void)
{
    uint32_t i;
    char nb[NAME_LEN + 1u], f[NAME_LEN + 1u];
    for (i = 0; i < 4u; i++) {
        int32_t y = 4 + (int32_t)i * 24;
        char b[2] = {(char)('A' + i), 0};
        int sel = (int32_t)i + 1 == song.g[G_SLOT], used = project_name(i, nb);
        if (sel)
            cv_rect(4, y + 6, 3, 3, C_WHITE);
        cv_text(14, y, &FONT_S, b, sel ? C_WHITE : C_GRAY);
        fit(f, used ? (nb[0] ? nb : "USED") : "EMPTY", &FONT_S, 190);
        cv_text(40, y, &FONT_S, f, used ? (sel ? C_WHITE : C_HI) : C_DIM);
    }
    if (chain.name[0]) {
        cv_text(14, 102, &FONT_S, "NOW", C_DIM);
        cv_text(52, 102, &FONT_S, chain.name, C_GRAY);
    }
}
```
`graph_signature` GR_SLOTS: also `h = str_hash(h, nb)` per slot name and `h = str_hash(h, chain.name)`:
```c
    if (pg->graph == GR_SLOTS) {
        char nb[NAME_LEN + 1u];
        for (i = 0; i < 4u; i++) {
            h ^= (uint32_t)project_name(i, nb) << (20u + i);
            h = str_hash(h, nb);
        }
        h = str_hash(h, chain.name);
    }
```
`draw_columns` generic loop, after the `G_MIDI` case: `if (pg->id[c] == G_NAME && pg->scope == SC_GLOBAL) { draw_column(c, "NAME", "EDIT", "", C_HI, -1, ICON_AUTO); continue; }`.
`params.c`: `static const char *const N_SLOT[] = {"-", "A", "B", "C", "D"};   /* G_SLOT 1..4: the slots A..D */` and
`[G_SLOT] = {"SLOT", F_ENUM, 1, 4, 1, N_SLOT, 0},`.

- [ ] **Step 5: Run** — `sh tests/run_drum_tests.sh`: NAME checks ok, ALL DRUM HOST TESTS PASSED; look at
`build/ui_shots/name/*.png`.

- [ ] **Step 6: Commit** — `git commit -am "NAME: naming projects on the device (keys + knobs), names on PROJECT and SONG, slots A..D"`

---

### Task 5: demo, docs, full build

**Files:** `tests/drumsim.c`, `CHANGELOG.md`, `docs/DEVICE_INSTALL.md`, `docs/UPSTREAM_1.0.2.md`

- [ ] **Step 1: drumsim WAV** — before `main`:
```c
/* SONG: A (four on the floor + hats) x2, B (a broken kick + snare) x1, LOOP ON: 6 bars, the rows change on the bar */
static step_t song_st[2][NTRK][NSTEP];
static void song_bar(uint32_t b) { (void)b; }
static void write_song(const char *dir)
{
    static const char *const PAT[2][3] = {{"X...x...x...x...", "................", "x.x.x.x.x.x.x.x."},
                                          {"X.....x...x.....", "....X.......X..x", "x.xxx.x.x.xxx.x."}};
    uint32_t s, i, k;
    sp_kit();
    memset(song_st, 0, sizeof song_st);
    for (s = 0; s < 2u; s++) {
        chain.src[s].m = &MOTION_NONE;
        for (k = 0; k < NTRK; k++) {
            chain.src[s].step[k] = song_st[s][k];
            chain.src[s].timing[k][0] = 16;
            chain.src[s].timing[k][1] = 2;
            chain.src[s].timing[k][2] = 0;
            chain.src[s].timing[k][3] = 0;
            chain.src[s].model[k] = (uint8_t)trk[k].p[P_MODEL];
        }
        for (i = 0; i < 3u; i++)
            for (k = 0; k < 16u; k++) {
                song_st[s][i][k].on = PAT[s][i][k] != '.';
                song_st[s][i][k].acc = PAT[s][i][k] == 'X';
            }
    }
    chain.cfg.count = 2;
    chain.cfg.loop = 1;
    chain.cfg.row[0] = (chain_row_t){0, 2};
    chain.cfg.row[1] = (chain_row_t){1, 1};
    chain.armed = 1;
    write_demo(dir, "song_a2_b1_loop.wav", 6, song_bar);
    transport_req = 2;
    render_mix(L, R, CTL);
    memset(&chain, 0, sizeof chain);
}
```
and `write_song(dir);` after `write_motion(dir);`.

- [ ] **Step 2: Docs** — CHANGELOG under "## Unreleased":
```
- SONG + NAME (upstream Felucca 1.0's song chain and naming): SEQ > SONG plays up to 16 rows of {project A..D,
  repeat x1..x16} with the sounds loaded now (each row: that project's steps, LEN / DIV / SWING / SRC and motion; a
  row lasts its longest track; LOOP ON / OFF; STOP brings the current pattern back). Projects get names (SAVE opens
  the NAME screen: keys type phone-style, KNOB 1 / 2 cursor and character; the PROJECT page's NAME renames a slot);
  slots shown as A..D. Saved with the project (format FDR9; older projects load with no song and no name).
```
`DEVICE_INSTALL.md`: a "SONG / NAME" section with spec §7's checks. `UPSTREAM_1.0.2.md` item 6: "Done in 0.12.0
(16 rows, LOOP, a row as long as its longest track; names on projects)" — written at the merge, not before.

- [ ] **Step 3: Full build and suites**

Run: build (`DRUM_PACKAGE=1 ./build.sh`, retry on "exec format error"), `sh tests/run_tests.sh`, `sh tests/run_drum_tests.sh`.
Expected: ALL HOST TESTS PASSED, ALL DRUM HOST TESTS PASSED, compare_upstream 0 different, stack within 75 %,
loader pinned, package in build/site.

- [ ] **Step 4: Commit** — `git commit -am "SONG + NAME: demo, device checks, changelog"`
