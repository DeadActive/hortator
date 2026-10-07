# MIDI clock in and exact step timing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Step timing without drift (each track carries its step-length remainder), then the FM-1 follows a MIDI
clock from USB or TRS (GLOBAL CLK INT / USB / TRS), ported from upstream Felucca 1.0's `midi_clock.c`.

**Architecture:** `seq.c` gets `div_period(div, rem)` / `div_rem_next(div, rem)` and a per-track `seq_rem` (and
`gclk.rem` for Grids), so den steps of a division last exactly num beats. `fx.c` gets `beat_samples()` (the
measured tempo when following, else BPM) used by every tempo user. A new `midi_clock.c` (upstream's, adapted)
turns the chosen source's Clock / Start / Continue / Stop into the sequencer's advance; `events_block` drives the
steps and Grids by that advance in external mode.

**Tech Stack:** C (one translation unit `firmware/src/felucca.c`; host tests with `cc`), the pi32v2 toolchain in
Docker via `./build.sh`.

**Spec:** `docs/superpowers/specs/2026-10-07-midi-clock-design.md`

## Global Constraints

- Repo `/Users/evech/Desktop/dev/fm1-drummachine/felucca`, branch `midi-clock` (from main 0166de1). Never push. Never
  install anything on the FM-1; never run `tools/fm1_install.py`, the web installer or the M-VAVE updater; never
  install mido / python-rtmidi.
- Environment: `export PYTHON=/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin/python PATH="/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin:$PATH"`;
  Docker running; retry a build up to 5× on "exec format error".
- No frozen file changes (check_untouched stays ok). New state goes into existing structs (`track_t`, `gclk`, the
  new `midi_clock` struct is upstream's own; `midi_beat_samples` is upstream's own global).
- The kit hash re-pin in `tests/sound_pack_test.c` is user-approved (spec §1); other thresholds unchanged.
- Credit: the new `midi_clock.c` keeps upstream's line "Adapted from MIDI clock contributions by ChanceTheMaker and
  keremimo (2026)", upstream's copyright, and "Drum machine fork: 2026 DEADACTIVE". Do not wrap test runs in `sh -c`.
- After the firmware changes: `DRUM_PACKAGE=1 ./build.sh` on a committed tree; CHANGELOG line under `## Unreleased`
  (0.9.0 at the merge).
- Host compile line: `cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src`.

## Review Focus

1. DIV changed while playing (rem from another den): the next step a few samples long at most, then exact (Task 1:
   `test_exact_timing` changes DIV mid-run).
2. A DAW that sends Clock while stopped and Start later: tempo measured meanwhile, step 0 on the first pulse after
   Start (Task 2: `clock_start_on_first_pulse`).
3. Clock jitter / a late USB packet burst (several pulses in one block): no step skipped, the advance capped (Task 2:
   the jitter run and `clock_burst`).
4. CLK switched (or a project with another CLK loaded) while playing: the sequencer stops, the clock state cleared,
   INT runs again on PLAY (Task 2: `clock_mode_switch`).
5. Swing on an exact-timed track: swing pairs keep their total (no drift with swing) (Task 1: the swing case).

---

### Task 1: Exact step timing

**Files:**
- Modify: `firmware/src/core.h` (`track_t.seq_rem`), `firmware/src/seq.c` (`div_period`, `div_rem_next`,
  `step_samples`, `seq_tick`, `rec_hit`, `seq_start`, `gclk`, `grids_l16`, `grids_tick`)
- Modify: `tests/drum_test.c` (new test; existing expected positions), `tests/sound_pack_test.c` (re-pin; exact 4BAR)

**Interfaces:**
- Produces: `static uint32_t div_period(uint32_t div, uint32_t rem)`, `static uint32_t div_rem_next(uint32_t div,
  uint32_t rem)` (seq.c, after `div_samples` is visible), `track_t.seq_rem` (uint8_t), `gclk.rem`.

- [ ] **Step 1: The test**

In `tests/drum_test.c`, before `int main`, add:

```c
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
```

and in `main` after `test_seq_timing();` add `test_exact_timing();`.

Run: `export PYTHON=/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin/python PATH="/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin:$PATH"; python3 tools/build.py --gen-only >/dev/null && cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/drum_test tests/drum_test.c -lm`
Expected: compile errors — `div_period`, `div_rem_next`, `seq_rem` undeclared.

- [ ] **Step 2: The remainder**

In `firmware/src/core.h`, after `    uint32_t seq_cnt;            /* steps played since PLAY: ... */` add:

```c
    uint8_t seq_rem;             /* the step length's remainder carried to the next step (seq.c div_period) */
```

In `firmware/src/seq.c`, before `/* the length of the step played as number cnt since PLAY: ...` add:

```c
/* a division (N_DIV id) as num / den beats: ids 0..5 are 1 / DEN (fx.c), the slow ids 6..9 are 2, 4, 8, 16 beats.
 * A step's period carries the remainder of the one before (rem < den), so den steps last exactly num beats: no
 * division drifts against another, or against an external clock (each 1/16 at 120 BPM: 5512, 5513, 5512, 5513) */
static uint32_t div_num_den(uint32_t div, uint32_t *den)
{
    if (div >= 6u && div < 10u) {
        *den = 1u;
        return 1u << (div - 5u);
    }
    *den = DIV_DEN[div % 6u];
    return 1u;
}
static uint32_t div_period(uint32_t div, uint32_t rem)
{
    uint32_t den, num = div_num_den(div, &den);
    return (beat_samples() * num + rem) / den;
}
static uint32_t div_rem_next(uint32_t div, uint32_t rem)
{
    uint32_t den, num = div_num_den(div, &den);
    return (beat_samples() * num + rem) % den;
}
```

`beat_samples()` comes in Task 2; for this task add it to `fx.c` now, right before `/* length of one division
(N_DIV id)`:

```c
static uint32_t beat_samples(void) { return (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM]; }   /* a quarter note */
```

and make `div_samples` use it: `uint32_t quarter = (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM];` →
`uint32_t quarter = beat_samples();`.

Replace `step_samples` with (the swing amount from the division's plain period, so a pair keeps its exact total):

```c
static uint32_t step_samples(const track_t *t, uint32_t period, uint32_t cnt)
{
    int32_t sw = track_swing(t) * (int32_t)div_samples((uint32_t)t->p[P_SDIV]) / 250;
    return period + (uint32_t)((cnt & 1u) ? -sw : sw);
}
```

In `rec_hit`: `uint32_t period = div_samples((uint32_t)t->p[P_SDIV]);` →
`uint32_t period = div_period((uint32_t)t->p[P_SDIV], t->seq_rem);`.

In `seq_start`'s track loop, after `t->seq_cnt = 0xFFFFFFFFu;` add `        t->seq_rem = 0;`; after
`gclk.cnt = 0xFFFFFFFFu;` add `    gclk.rem = 0;`.

Replace the whole `seq_tick` with:

```c
static void seq_tick(track_t *t, uint32_t n)
{
    uint32_t div = (uint32_t)t->p[P_SDIV], len = (uint32_t)t->p[P_SLEN];
    if (!song.playing)
        return;
    t->seq_pos += n;
    for (;;) {
        uint32_t cur_len = step_samples(t, div_period(div, t->seq_rem), t->seq_cnt);
        rat_due(t, cur_len);
        if (t->seq_pos < cur_len && t->seq_pos != 0x7FFFFFFFu + n)
            break;
        if (t->seq_pos >= 0x7FFFFFFFu) {
            t->seq_pos = 0;                         /* PLAY: step 0 (its remainder 0) */
        } else {
            t->seq_pos -= cur_len;
            t->seq_rem = (uint8_t)div_rem_next(div, t->seq_rem);
        }
        t->seq_cnt++;                               /* the step: steps since PLAY mod LEN, so a LEN change keeps
                                                     * the track on the shared clock (and LEN back = in sync) */
        t->seq_idx = (uint16_t)(t->seq_cnt % (len ? len : 1u));
        if (t->rskip && t->rskip_idx == t->seq_idx) {
            t->rskip = 0;
            t->rat_n = 0;
        } else {
            step_fire(t, step_samples(t, div_period(div, t->seq_rem), t->seq_cnt));
        }
    }
}
```

The Grids clock: `static struct { uint32_t pos, cnt; uint8_t half; } gclk;` →
`static struct { uint32_t pos, cnt; uint8_t half, rem; } gclk;   /* ..., the 1/16's remainder (div_period) */`;
`grids_l16` becomes:

```c
static uint32_t grids_l16(uint32_t cnt)
{
    uint32_t p16 = div_period(2u, gclk.rem);
    int32_t sw = song.g[G_SWING] * (int32_t)div_samples(2u) / 250;
    return p16 + (uint32_t)((cnt & 1u) ? -sw : sw);
}
```

(it moves after `div_period`; place the new block before the Grids comment), and in `grids_tick`
`gclk.pos = start ? 0 : gclk.pos - l16;` →

```c
        if (!start)
            gclk.rem = (uint8_t)div_rem_next(2u, gclk.rem);
        gclk.pos = start ? 0 : gclk.pos - l16;
```

- [ ] **Step 3: Green, the old expectations, the re-pin**

Run: `cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/drum_test tests/drum_test.c -lm && build/host/drum_test | grep -E "exact timing|FAIL|drum_test:"`
Expected: the four `exact timing` lines `ok`. Existing checks whose expected positions use the rounded period
(`p = FS * 60 / 120 / 4` = 5512 times a count, e.g. `test_seq_timing`'s `CEILB(4 * p)`, the PROB 2/2 and PROB
position checks) may FAIL: correct each to the exact position (`k` steps of 1/16 at 120 BPM start at
`k * 5512 + k / 2`, i.e. `(k * 11025) / 2`), keep the test's meaning, and record a Ruling naming each changed check.
Any other FAIL is a code bug: debug it.

Then re-pin the kit hashes (user-approved) and make the sound pack's 4BAR test exact:

```bash
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/sound_pack_test tests/sound_pack_test.c -lm && SOUND_PACK_RECORD=1 build/host/sound_pack_test
```

Put the two printed values into `HASH_FLAT` / `HASH_LOWCUT` in `tests/sound_pack_test.c`, and change that file's
header comment "the kit below rendered by the build before the sound pack (trs-midi 08866e0 .. bf98902)" to
"the kit below rendered with exact step timing (midi-clock, re-pinned with the user's approval)". In
`tests/drum_test.c`'s `test_slow_divisions`, replace the `NEAR` tolerance check with the exact one:

```c
    check("divisions: a 4BAR track fires once every 4 bars, on the 1/16 track's bar hits",
          n0 >= 9u && n1 == 3u && at1[0] == at0[0] && at1[1] == at0[4] && at1[2] == at0[8]);
```

(delete the comment above it, the `#define NEAR` and `#undef NEAR` lines).

Run: `cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/drum_test tests/drum_test.c -lm && build/host/drum_test | grep -E "FAIL|drum_test:"; cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/sound_pack_test tests/sound_pack_test.c -lm && build/host/sound_pack_test | tail -1`
Expected: `drum_test: all passed`; `sound_pack_test: all passed`.

- [ ] **Step 4: Drum suite, commit**

Run: `sh tests/run_drum_tests.sh > /Users/evech/.claude/jobs/d6b478c9/tmp/drum.log 2>&1; tail -2 /Users/evech/.claude/jobs/d6b478c9/tmp/drum.log; grep FAIL /Users/evech/.claude/jobs/d6b478c9/tmp/drum.log | head`
Expected: `ALL DRUM HOST TESTS PASSED`. A failure in ui_test / boot_test from a moved step position: correct it as in
Step 3 with a Ruling.

```bash
git add firmware/src/core.h firmware/src/seq.c firmware/src/fx.c tests/drum_test.c tests/sound_pack_test.c tests/ui_test.c tests/boot_test.c
git commit -m "exact step timing: each track carries its step-length remainder (den steps = num beats), Grids too: no drift between divisions; kit hashes re-pinned (user approved)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01BGWb4f19EttazmDKtgYxeD"
```

---

### Task 2: MIDI clock in

**Files:**
- Create: `firmware/src/midi_clock.c`, `tests/midi_clock_test.c`
- Modify: `firmware/src/fx.c` (`midi_beat_samples`, `beat_samples`), `firmware/src/lfo.c`, `firmware/src/slicer.c`,
  `firmware/src/seq.c` (`midi_block`, `events_block`, include), `firmware/src/params.c` (`N_CLOCK`),
  `firmware/src/ui_input.c` (BPM knob), `tests/run_drum_tests.sh`

**Interfaces:**
- Consumes: Task 1's `div_period`, `seq_rem`, `gclk.rem`, `beat_samples()`.
- Produces: `midi_clock` (struct), `midi_clock_transport(uint32_t status, uint32_t ms)`,
  `midi_clock_pulse(uint32_t ms)`, `midi_clock_advance(uint32_t now)`; CLK values 0 INT, 1 USB, 2 TRS.

- [ ] **Step 1: The test**

Create `tests/midi_clock_test.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* MIDI clock in (seq.c, midi_clock.c): a simulated source sends Clock / Start / Continue / Stop into the input ring
 * (midi_enqueue, USB or TRS) with timestamps from a simulated fm1_ms, block by block; the steps and Grids follow it
 * without drift. */
#include "drum_host.h"
#include <math.h>
#include <stdlib.h>

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static uint64_t now;                                 /* samples since the test began */
static double next_pulse;                            /* the source's next Clock, in samples */
static uint32_t pulses, rng = 7;
static uint32_t pulse_at[40000];                     /* the block each pulse was sent in */

static void send(uint32_t status, uint32_t src, double at)
{
    int32_t j;
    rng = rng * 1664525u + 1013904223u;
    j = (int32_t)(rng >> 29) % 5 - 2;               /* -2 .. +2 ms of timestamp jitter */
    fm1_ms = (uint32_t)((int64_t)(at * 1000.0 / FS) + j);
    midi_enqueue(0x0Fu | status << 8, src);
}

/* render frames, the source sending Clock at bpm from src while running; hits of tracks 0 / 1 into h0 / h1 */
static uint32_t *H0, *H1, N0, N1, A0, A1, CAP;
static void run(uint64_t frames, double bpm, uint32_t src, int clock_on)
{
    uint64_t end = now + frames;
    while (now < end) {
        while (clock_on && next_pulse < (double)(now + CTL)) {
            send(0xF8u, src, next_pulse);
            if (pulses < 40000u)
                pulse_at[pulses] = (uint32_t)(now / CTL);
            pulses++;
            next_pulse += FS * 60.0 / bpm / 24.0;
        }
        fm1_ms = (uint32_t)(now * 1000u / FS);
        render_mix(0, 0, CTL);
        if (hit_age(&trk[0]) != A0) {
            A0 = hit_age(&trk[0]);
            if (N0 < CAP)
                H0[N0] = (uint32_t)(now / CTL);
            N0++;
        }
        if (hit_age(&trk[1]) != A1) {
            A1 = hit_age(&trk[1]);
            if (N1 < CAP)
                H1[N1] = (uint32_t)(now / CTL);
            N1++;
        }
        now += CTL;
    }
}

static void setup(uint32_t clk)
{
    uint32_t i;
    host_init();
    song.g[G_CLOCK] = (int16_t)clk;
    for (i = 0; i < 16u; i++)
        trk[0].step[i].on = trk[1].step[i].on = 1;
    trk[0].p[P_SDIV] = 2;                            /* 1/16 */
    trk[1].p[P_SDIV] = 9;                            /* 4BAR */
    render_mix(0, 0, CTL);                           /* the mode change settles */
    A0 = hit_age(&trk[0]);
    A1 = hit_age(&trk[1]);
    N0 = N1 = 0;
    pulses = 0;
    next_pulse = (double)now + 100.0;
}

static uint32_t h0[6000], h1[64];

/* a source at bpm on src: Start, then 5 000 1/16 steps of Clock with jitter: every 1/16 on its 6th pulse */
static int locked(double bpm, uint32_t src)
{
    uint32_t k, bad = 0;
    H0 = h0;
    H1 = h1;
    CAP = 6000u;
    setup(src);
    send(0xFAu, src, (double)now);
    run((uint64_t)(5000.0 * 6.0 * FS * 60.0 / bpm / 24.0), bpm, src, 1);
    for (k = 0; k < 5000u && k < N0; k++)
        bad += h0[k] > pulse_at[6u * k] + 2u || h0[k] < pulse_at[6u * k];
    for (k = 0; k < N1 && k < 13u; k++)
        bad += h1[k] > pulse_at[384u * k] + 2u || h1[k] < pulse_at[384u * k];
    if (bad)
        printf("     %.0f BPM src %u: %u hits off their pulse (1/16 %u, 4BAR %u)\n", bpm, src, bad, N0, N1);
    return bad == 0u && N0 >= 4999u && N1 >= 13u;
}

int main(void)
{
    uint32_t k, n0;
    check("CLK USB, 120 BPM, jittered timestamps: 1/16 on every 6th pulse, 4BAR on every 384th, 5 000 steps", locked(120.0, 1));
    check("CLK TRS, 140 BPM, jittered timestamps: the same", locked(140.0, 2));
    check("following: BPM shows the measured tempo (140, +-3 with the ms jitter)", abs(song.g[G_BPM] - 140) <= 3);

    /* review focus 2: Clock while stopped (tempo measured), Start later: step 0 on the first pulse after Start */
    H0 = h0;
    H1 = h1;
    CAP = 6000u;
    setup(1);
    run(FS, 100.0, 1, 1);                            /* one second of Clock, stopped */
    check("stopped: no steps; the tempo measured (100 BPM, +-3)", N0 == 0u && abs(song.g[G_BPM] - 100) <= 3);
    send(0xFAu, 1, (double)now);
    n0 = pulses;
    run(FS / 2, 100.0, 1, 1);
    check("Start: step 0 on the first pulse after it", N0 >= 1u && h0[0] >= pulse_at[n0] && h0[0] <= pulse_at[n0] + 2u);

    /* a tempo change: 100 -> 150 BPM while playing: the steps follow the new pulses */
    n0 = N0;
    run(2u * FS, 150.0, 1, 1);
    {
        uint32_t p = pulses - 1u;
        check("tempo change 100 -> 150 BPM: still one 1/16 per 6 pulses", N0 - n0 >= 35u && h0[N0 - 1u] <= pulse_at[p] + 2u &&
              abs(song.g[G_BPM] - 150) <= 3);
    }

    /* Stop, Continue: resumes the step it stopped at */
    send(0xFCu, 1, (double)now);
    run(FS / 4, 150.0, 1, 1);
    k = trk[0].seq_idx;
    n0 = N0;
    run(FS / 4, 150.0, 1, 1);
    check("Stop: no steps (Clock still coming)", N0 == n0 && !song.playing);
    send(0xFBu, 1, (double)now);
    run(FS / 10, 150.0, 1, 1);
    check("Continue: plays on from the step it stopped at", song.playing && N0 > n0 && trk[0].seq_idx == (k + N0 - n0) % 16u);

    /* the other source is ignored */
    n0 = N0;
    {
        uint32_t i;
        for (i = 0; i < 48u; i++)
            midi_enqueue(0x0Fu | 0xF8u << 8, 2u);
        midi_enqueue(0x0Fu | 0xFCu << 8, 2u);
    }
    run(CTL, 150.0, 1, 1);
    check("CLK USB: TRS Clock / Stop ignored", song.playing);

    /* the clock lost: no pulse for 500 ms -> stop */
    run(FS * 6 / 10, 150.0, 1, 0);
    check("no Clock for 500 ms while playing: stopped", !song.playing);

    /* review focus 3: a burst (10 pulses in one block, a late USB packet): no step skipped, the advance capped */
    setup(1);
    send(0xFAu, 1, (double)now);
    run(FS / 2, 120.0, 1, 1);
    n0 = N0;
    {
        uint32_t i;
        for (i = 0; i < 12u; i++) {
            send(0xF8u, 1, next_pulse);
            next_pulse += FS * 60.0 / 120.0 / 24.0;
            pulses++;
        }
    }
    run(FS / 2, 120.0, 1, 1);
    check("a burst of 12 pulses in one block: the two 1/16s it carries play, none skipped (>= 2 + 3 in 0.5 s)", N0 - n0 >= 5u);

    /* review focus 4: CLK switched while playing: stopped, the clock state cleared; INT plays on PLAY */
    song.g[G_CLOCK] = 0;
    render_mix(0, 0, CTL);
    check("CLK switched to INT while playing: stopped", !song.playing);
    transport_req = 1;
    n0 = N0;
    run(FS / 2, 120.0, 1, 0);
    check("CLK INT: PLAY runs on the internal tempo", song.playing && N0 > n0);

    /* PLAY on the FM-1 while following: armed, the steps wait for the pulses */
    setup(1);
    transport_req = 1;
    run(FS / 10, 120.0, 1, 0);
    check("CLK USB, PLAY on the FM-1, no Clock yet: armed, no step", N0 == 0u);

    printf(fails ? "midi_clock_test: %d FAILED\n" : "midi_clock_test: all passed\n", fails);
    return fails ? 1 : 0;
}
```

In `tests/run_drum_tests.sh`, after the two `usb_audio_test` lines, add:

```sh
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/midi_clock_test" tests/midi_clock_test.c -lm
"$OUT/midi_clock_test"
```

Run: `cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/midi_clock_test tests/midi_clock_test.c -lm && build/host/midi_clock_test`
Expected: it compiles (it uses only existing names) and FAILs: no clock following (CLK has one value; the realtime
packets are ignored). `midi_clock_test: N FAILED`.

- [ ] **Step 2: The tempo**

In `firmware/src/fx.c`, replace Task 1's one-line `beat_samples` with (upstream's):

```c
static uint32_t midi_beat_samples;                    /* zero until an external clock has a measured tempo */
static uint32_t beat_samples(void)                    /* a quarter note: the clock's tempo when following, else BPM */
{
    return song.g[G_CLOCK] && midi_beat_samples ? midi_beat_samples : (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM];
}
```

`firmware/src/lfo.c`: `uint32_t spq = (uint32_t)FS * 60u / (uint32_t)clamp(song.g[G_BPM], 40, 240);   /* samples a quarter note */`
→ `uint32_t spq = beat_samples();                    /* samples a quarter note (the clock's when following) */`.
Check `lfo.c` is included after `fx.c` in `felucca.c` (`grep -n '#include "lfo.c"\|#include "fx.c"' firmware/src/felucca.c`);
if `lfo.c` comes first, add a forward declaration `static uint32_t beat_samples(void);` at the top of `lfo.c`.

`firmware/src/slicer.c`: `s->base = (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM] / SL_DEN[...]` →
`s->base = beat_samples() / SL_DEN[...]` (same check; add the forward declaration if needed).

`firmware/src/params.c`: `static const char *const N_CLOCK[] = {"INT"};` →
`static const char *const N_CLOCK[] = {"INT", "USB", "TRS"};   /* G_CLOCK: = the input ring's source (1 USB, 2 TRS) */`.

- [ ] **Step 3: The clock**

Create `firmware/src/midi_clock.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from MIDI clock contributions by ChanceTheMaker and keremimo (2026).
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* MIDI clock in (upstream 1.0's midi_clock.c, without its arpeggiator / motion / song chain parts): the chosen
 * source's Clock moves the sequencer, a beat / 24 a pulse (the remainder kept: 24 pulses = one beat), interpolated
 * between pulses but never past the next one; the tempo is measured over 6 pulses from the input ring's timestamps
 * (USB / TRS jitter stays outside the render). Audio ISR state. */
static struct {
    uint32_t pos, rendered, last_ms, start_ms, rem, interval_ms;
    uint32_t pulse_samples, interp_q8, tempo_ms;
    uint8_t mode, have_pulse, tempo_valid, tempo_n;
} midi_clock;

static __attribute__((noinline)) void midi_clock_transport(uint32_t status, uint32_t ms)
{
    if (status == 0xFAu) {                         /* Start: step 0, on the first pulse after it */
        midi_clock.pos = midi_clock.rendered = midi_clock.rem = 0;
        midi_clock.have_pulse = midi_clock.tempo_valid = 0;
        midi_clock.interval_ms = 0;
        midi_clock.start_ms = ms;
        seq_start();
    } else if (status == 0xFBu) {                  /* Continue: the step kept, on the first pulse after it */
        midi_clock.pos = midi_clock.rendered;
        midi_clock.rem = 0;
        midi_clock.have_pulse = midi_clock.tempo_valid = 0;
        midi_clock.interval_ms = 0;
        midi_clock.start_ms = ms;
        song.playing = 1;
    } else if (status == 0xFCu) {
        seq_stop();
    }
}

static __attribute__((noinline)) void midi_clock_pulse(uint32_t ms)
{
    uint32_t q;
    if (!midi_clock.tempo_valid) {
        midi_clock.tempo_valid = 1;
        midi_clock.tempo_ms = ms;
        midi_clock.tempo_n = 0;
    } else if (++midi_clock.tempo_n == 6u) {
        uint32_t dt = ms - midi_clock.tempo_ms;
        midi_clock.tempo_ms = ms;
        midi_clock.tempo_n = 0;
        /* Six clocks are a quarter of a beat. Reject gaps and corrupt bursts. */
        if (dt >= 62u && dt <= 375u) {
            uint32_t old_beat = beat_samples(), new_beat = (uint32_t)FS * dt / 250u;
            if (song.playing && new_beat != old_beat) {
                uint32_t i, ratio = (new_beat << 12) / old_beat;
                /* each track keeps its place in its step when the tempo changes (and Grids its 1/16) */
                for (i = 0; i < NTRK; i++)
                    if (trk[i].seq_pos < 0x7FFFFFFFu)
                        trk[i].seq_pos = (uint32_t)(((uint64_t)trk[i].seq_pos * ratio + 2048u) >> 12);
                if (gclk.pos < 0x7FFFFFFFu)
                    gclk.pos = (uint32_t)(((uint64_t)gclk.pos * ratio + 2048u) >> 12);
            }
            midi_beat_samples = new_beat;
            song.g[G_BPM] = (int16_t)clamp((int32_t)((15000u + dt / 2u) / dt), 40, 240);
        }
    }
    if (song.playing) {
        if (midi_clock.have_pulse) {
            uint32_t interval = ms - midi_clock.last_ms;
            if (interval >= 8u && interval <= 80u)
                midi_clock.interval_ms = interval;
            q = beat_samples() + midi_clock.rem;
            midi_clock.pos += q / 24u;
            midi_clock.rem = q % 24u;
        }
        midi_clock.have_pulse = 1;
    }
    midi_clock.pulse_samples = beat_samples() / 24u;
    if (!midi_clock.interval_ms)
        midi_clock.interval_ms = beat_samples() * 1000u / ((uint32_t)FS * 24u);
    midi_clock.interp_q8 = midi_clock.pulse_samples * 256u / midi_clock.interval_ms;
    midi_clock.last_ms = ms;
}

static uint32_t midi_clock_advance(uint32_t now)
{
    uint32_t target, elapsed, offset, n;
    if (!midi_clock.have_pulse)
        return 0;
    elapsed = now - midi_clock.last_ms;
    /* Interpolate to the next pulse, never across it before it arrives. */
    if (elapsed > midi_clock.interval_ms)
        elapsed = midi_clock.interval_ms;
    offset = elapsed * midi_clock.interp_q8 >> 8;
    if (offset >= midi_clock.pulse_samples)
        offset = midi_clock.pulse_samples - 1u;
    target = midi_clock.pos + offset;
    n = (int32_t)(target - midi_clock.rendered) > 0 ? target - midi_clock.rendered : 0u;
    if (n > (uint32_t)FS / 8u)
        n = (uint32_t)FS / 8u;
    midi_clock.rendered += n;
    return n;
}
```

In `firmware/src/seq.c`, add `#include "midi_clock.c"` right before `/* everything that happens between two rendered
blocks */` (after `seq_start` / `seq_stop` / `midi_block`).

In `midi_block`'s loop, the realtime packets of the chosen source go to the clock. Replace

```c
        uint32_t pkt = midi_in_q[mi_r % MQ], st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 0x0Fu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, i;
        mi_r++;
```

with

```c
        uint32_t at = mi_r % MQ, pkt = midi_in_q[at], st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 0x0Fu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, i, status = (pkt >> 8) & 0xFFu;
        mi_r++;
        if (status >= 0xF8u) {                      /* realtime: the chosen clock source's (CLK USB / TRS) */
            uint32_t src = midi_in_source[at] ? midi_in_source[at] : 1u;
            if (song.g[G_CLOCK] && (uint32_t)song.g[G_CLOCK] == src) {
                if (status == 0xF8u)
                    midi_clock_pulse(midi_in_ms[at]);
                else
                    midi_clock_transport(status, midi_in_ms[at]);
            }
            continue;
        }
```

`midi_block` is defined before `#include "midi_clock.c"`: add forward declarations before `midi_block`:
`static void midi_clock_pulse(uint32_t ms);` and `static void midi_clock_transport(uint32_t status, uint32_t ms);`
(and remove `__attribute__((noinline))` from those two definitions only if the compiler rejects the mismatch; keep it
otherwise).

Replace `events_block`'s start, from `    uint32_t i, pr;` through the `transport_req` block, with:

```c
    uint32_t i, pr, seq_n = n, run, clock_mode = (uint32_t)song.g[G_CLOCK];
    if (midi_clock.mode != clock_mode) {             /* CLK changed (knob or project): stop, a clean clock state */
        seq_stop();
        memset(&midi_clock, 0, sizeof midi_clock);
        midi_clock.mode = (uint8_t)clock_mode;
        midi_beat_samples = 0;
    }
    if (transport_req == 1u) {
        if (clock_mode)
            midi_clock_transport(0xFAu, fm1_ms);     /* PLAY while following: a Start, the steps wait for pulses */
        else
            seq_start();
        transport_req = 0;
    } else if (transport_req == 2u) {
        seq_stop();
        transport_req = 0;
    }
    if (clock_mode) {                                /* following: the steps move with the clock */
        seq_n = 0;
        if (song.playing) {
            uint32_t last = midi_clock.have_pulse ? midi_clock.last_ms : midi_clock.start_ms;
            if (fm1_ms - last > 500u) {
                seq_stop();                          /* the clock lost (a cable out): stop, nothing left running */
                midi_clock.tempo_valid = 0;
            } else {
                seq_n = midi_clock_advance(fm1_ms);
            }
        }
    }
```

In the MUTE NEXT BAR block, replace both `gclk.pos + n` with `gclk.pos + seq_n`. Replace the end of `events_block`

```c
    for (i = 0; i < NTRK; i++)
        seq_tick(&trk[i], n);
    grids_tick(n);
```

with

```c
    run = !clock_mode || midi_clock.have_pulse;     /* following: step 0 / the resumed step on the first pulse */
    if (run) {
        for (i = 0; i < NTRK; i++)
            seq_tick(&trk[i], seq_n);
        grids_tick(seq_n);
    }
```

In `firmware/src/ui_input.c`, the BPM knob:

```c
    if ((s = panel_enc(EN_SELECT)) != 0) {
        song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
        ui.bpm_t = 40;
    }
```

→

```c
    if ((s = panel_enc(EN_SELECT)) != 0) {
        if (song.g[G_CLOCK])                         /* following a clock: its tempo, not the knob */
            ui_message(song.g[G_CLOCK] == 1 ? "CLK USB" : "CLK TRS");
        else {
            song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
            ui.bpm_t = 40;
        }
    }
```

and in `edit_param` (the knob path for page parameters), right after `d = page_desc(pg, slot, &vp);` and its
`return` check, add:

```c
    if (pg->scope == SC_GLOBAL && id == G_BPM && song.g[G_CLOCK]) {
        ui_message(song.g[G_CLOCK] == 1 ? "CLK USB" : "CLK TRS");
        return;
    }
```

- [ ] **Step 4: Green**

Run: `cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/midi_clock_test tests/midi_clock_test.c -lm && build/host/midi_clock_test`
Expected: every line `ok`, `midi_clock_test: all passed`. A failure is a code or test bug: debug it (the test's
timing tolerance is 2 blocks after the pulse's block; change it only with a Ruling that says why).

- [ ] **Step 5: Drum suite, build, the frozen checks, commit**

Run: `export PYTHON=/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin/python PATH="/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin:$PATH"; sh tests/run_drum_tests.sh > /Users/evech/.claude/jobs/d6b478c9/tmp/drum.log 2>&1; tail -2 /Users/evech/.claude/jobs/d6b478c9/tmp/drum.log; grep FAIL /Users/evech/.claude/jobs/d6b478c9/tmp/drum.log | head; DRUM_PACKAGE=1 ./build.sh > /Users/evech/.claude/jobs/d6b478c9/tmp/build.log 2>&1; grep -E "FAIL|ok    image" /Users/evech/.claude/jobs/d6b478c9/tmp/build.log; python3 tools/compare_upstream.py build build/upstream/build | tail -1; python3 tools/stack_depth.py build --compare build/upstream/build | tail -1; python3 tools/check_loader.py build`
Expected: `ALL DRUM HOST TESTS PASSED`; the image line; `0 different`; the stack within 75 %; the pinned loader.

```bash
git add firmware/src/midi_clock.c firmware/src/fx.c firmware/src/lfo.c firmware/src/slicer.c firmware/src/seq.c firmware/src/params.c firmware/src/ui_input.c tests/midi_clock_test.c tests/run_drum_tests.sh
git commit -m "MIDI clock in (upstream 1.0 midi_clock.c): GLOBAL CLK INT / USB / TRS; steps and Grids follow the chosen source's Clock / Start / Continue / Stop, step 0 on the first pulse, stop after 500 ms without Clock; one beat length for the slicer, LFO SYNC and the delay

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01BGWb4f19EttazmDKtgYxeD"
```

---

### Task 3: Screens, records, package

**Files:**
- Modify: `tests/ui_test.c` (screens), `CHANGELOG.md`, `docs/DEVICE_INSTALL.md`, `docs/UPSTREAM_1.0.2.md`

- [ ] **Step 1: The screens**

In `tests/ui_test.c`, before `int main`, add:

```c
/* MIDI clock: GLOBAL with CLK USB, and the BPM knob while following (a message, the tempo kept) */
static void test_clock_screens(void)
{
    int16_t bpm;
    ui_host_init();
    while (!str_eq(cur_page()->title, "GLOBAL") || ui.home) {
        press(B_GLO);
        ui_frame();
        release_all();
        ui_frame();
    }
    song.g[G_CLOCK] = 1;
    ui.force = 1;
    snap_page("global_clk_usb");
    bpm = song.g[G_BPM];
    turn(EN_SELECT, 3);
    ui_frame();
    check("CLK USB: the BPM knob keeps the tempo (the clock's) and says so", song.g[G_BPM] == bpm && ui.msg_t);
    song.g[G_CLOCK] = 0;
    render_mix(0, 0, CTL);
}
```

and call `test_clock_screens();` before `main`'s final `printf`.

Run: `cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/ui_test tests/ui_test.c -lm && build/host/ui_test | grep -E "CLK USB|FAIL|ui_test:"`
Expected: the CLK line `ok`, `ui_test: all passed` (if `ui.msg_t` is not the message timer's name, use the one
`ui_message` sets: `grep -n "static void ui_message" -A4 firmware/src/*.c`). Look at the screenshot after the drum
suite converts it: CLK shows USB.

- [ ] **Step 2: Records**

`CHANGELOG.md`, under `## Unreleased`:

```markdown
- MIDI clock in (upstream Felucca 1.0, adapted from contributions by ChanceTheMaker and keremimo): GLOBAL CLK INT /
  USB / TRS; the steps (every division), Grids, the slicer, synced LFOs and the delay follow the source's tempo,
  its Start / Continue / Stop drive the transport (step 0 on the first pulse; stops if the clock stops for 0.5 s).
- Exact step timing: each track carries its step-length remainder, so divisions never drift against each other
  (e.g. a 1/16 and a 1/4 track at 120 BPM) or against an external clock.
```

`docs/DEVICE_INSTALL.md`, after the `### USB audio (check on the FM-1)` section (before `### TOOLS`), add:

```markdown
### MIDI clock (check on the FM-1)

- GLOBAL > CLK USB, a DAW sending MIDI clock to the FM-1: the DAW's Start / Stop / Continue start, stop and resume
  the FM-1 (step 0 on the downbeat); a 1/16 hat stays tight against the DAW's metronome for 5+ minutes; a tempo
  change in the DAW is followed within a beat; the header shows the DAW's BPM; the BPM knob says CLK USB.
- Unplug the cable while it plays: the FM-1 stops within half a second.
- CLK TRS with a hardware sequencer on the TRS input: the same.
- CLK INT: as before; a 1/16 track and a 1/4 track never drift apart.
- PLAY on the FM-1 while CLK USB: it waits for the DAW's clock (stops again after 0.5 s if none comes).
```

`docs/UPSTREAM_1.0.2.md` §3 item 3 (`**MIDI clock in**`): append ` Done in 0.9.0, with exact step timing.`.

- [ ] **Step 3: Commit, package, full suite**

```bash
git add tests/ui_test.c CHANGELOG.md docs/DEVICE_INSTALL.md docs/UPSTREAM_1.0.2.md
git commit -m "MIDI clock: screens, device checks, changelog

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01BGWb4f19EttazmDKtgYxeD"
```

Run: `export PYTHON=/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin/python PATH="/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin:$PATH"; DRUM_PACKAGE=1 ./build.sh > /Users/evech/.claude/jobs/d6b478c9/tmp/build.log 2>&1; tail -1 /Users/evech/.claude/jobs/d6b478c9/tmp/build.log; tests/run_tests.sh > /Users/evech/.claude/jobs/d6b478c9/tmp/suite.log 2>&1; tail -1 /Users/evech/.claude/jobs/d6b478c9/tmp/suite.log; grep "0 different\|within 75\|pinned loader\|FAIL" /Users/evech/.claude/jobs/d6b478c9/tmp/suite.log`
Expected: the site line (no `-dirty`); `ALL HOST TESTS PASSED`; H2 0 different, stack within 75 %, the pinned loader.
