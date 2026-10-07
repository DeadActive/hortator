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
static double next_pulse, next_arrival = -1.0;      /* the source's next Clock (samples), when it arrives */
static uint32_t pulses, clk_rng = 7;
static uint32_t pulse_at[40000];                     /* the block each pulse was sent in */
static uint32_t pring[4096], hits_early, hits_late;   /* the last 4096 pulses' blocks; 1/16 hits off their pulse */
static int32_t grid_err;                             /* the largest |step position - the clock's since its pulse| */

static void clk_send(uint32_t status, uint32_t src, double at)   /* arrives at `at` (samples): stamped then */
{
    fm1_ms = (uint32_t)(at * 1000.0 / FS);
    midi_enqueue(0x0Fu | status << 8, src);
}
static int clk_jitter = 1;                           /* 0: every pulse on time */
static double late(void)                             /* a pulse arrives 0 .. 4 ms late (USB / TRS jitter) */
{
    clk_rng = clk_rng * 1664525u + 1013904223u;
    return clk_jitter ? (double)((clk_rng >> 29) % 5u) * FS / 1000.0 : 0.0;
}

/* render frames, the source sending Clock at bpm from src while running; hits of tracks 0 / 1 into h0 / h1 */
static uint32_t *H0, *H1, N0, N1, A0, A1, CAP;
static void run(uint64_t frames, double bpm, uint32_t src, int clock_on)
{
    uint64_t end = now + frames;
    while (now < end) {
        while (clock_on) {                           /* each pulse arrives once, 0 .. 4 ms late, in its block */
            if (next_arrival < 0.0)
                next_arrival = next_pulse + late();
            if (next_arrival >= (double)(now + CTL))
                break;
            clk_send(0xF8u, src, next_arrival);
            if (pulses < 40000u)
                pulse_at[pulses] = (uint32_t)(now / CTL);
            pring[pulses & 4095u] = (uint32_t)(now / CTL);
            pulses++;
            next_pulse += FS * 60.0 / bpm / 24.0;
            next_arrival = -1.0;
        }
        fm1_ms = (uint32_t)((now + CTL) * 1000u / FS);   /* the block is computed after what arrived in it */
        render_mix(0, 0, CTL);
        if (hit_age(&trk[0]) != A0) {
            uint32_t b = (uint32_t)(now / CTL), j = 6u * N0;   /* a 1/16 on track 0: its pulse is the 6 N0-th */
            A0 = hit_age(&trk[0]);
            if (N0 < CAP)
                H0[N0] = b;
            if (j < pulses && pulses - j < 4096u) {
                hits_early += b < pring[j & 4095u];
                hits_late += b > pring[j & 4095u] + 2u;
            }
            if (song.g[G_CLOCK] && midi_clock.have_pulse) {  /* on the clock's grid: the step starts where its pulse is */
                int32_t e = (int32_t)trk[0].seq_pos - (int32_t)(midi_clock.rendered - midi_clock.pos);
                e = e < 0 ? -e : e;
                grid_err = e > grid_err ? e : grid_err;
            }
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
    next_arrival = -1.0;
}

static uint32_t h0[6000], h1[6000];

/* a source at bpm on src: Start, then 5 000 1/16 steps of Clock with jitter: every 1/16 on its 6th pulse */
static int locked(double bpm, uint32_t src)
{
    uint32_t k, bad = 0;
    H0 = h0;
    H1 = h1;
    CAP = 6000u;
    setup(src);
    clk_send(0xFAu, src, (double)now);
    run((uint64_t)(5000.0 * 6.0 * FS * 60.0 / bpm / 24.0), bpm, src, 1);
    for (k = 0; k < 5000u && k < N0; k++)
        bad += h0[k] > pulse_at[6u * k] + 2u || h0[k] < pulse_at[6u * k];
    for (k = 0; k < N1 && k < 13u; k++)
        bad += h1[k] > pulse_at[384u * k] + 2u || h1[k] < pulse_at[384u * k];
    if (bad)
        printf("     %.0f BPM src %u: %u hits off their pulse (1/16 %u, 4BAR %u)\n", bpm, src, bad, N0, N1);
    return bad == 0u && N0 >= 4999u && N1 >= 13u;
}

/* final review: a steady clock (140 BPM: 428 / 429 ms a beat in ms timestamps) must not flip the tempo every beat
 * (each flip moved the delay's read point: a tick) */
static int steady_tempo(int jitter)
{
    uint32_t changes = 0, last, i;
    H0 = h0;
    H1 = h1;
    CAP = 6000u;
    setup(1);
    clk_jitter = jitter;
    clk_send(0xFAu, 1, (double)now);
    run(3u * FS, 140.0, 1, 1);                       /* the first readings settle */
    last = beat_samples();
    for (i = 0; i < 20u * FS / (CTL * 64u); i++) {
        run(CTL * 64u, 140.0, 1, 1);
        changes += beat_samples() != last;
        last = beat_samples();
    }
    clk_jitter = 1;
    if (changes > 1u)
        printf("     140 BPM, jitter %d: the tempo changed %u times in 20 s\n", jitter, changes);
    return changes <= 1u;
}

/* final review: an hour of a jittered clock: every 1/16 on its pulse (none early, none late), at bpm */
static int long_run(double bpm)
{
    H0 = h0;
    H1 = h1;
    CAP = 0u;                                        /* checked as they come (run): an hour is too many to keep */
    setup(1);
    hits_early = hits_late = 0;
    grid_err = 0;
    clk_send(0xFAu, 1, (double)now);
    run(3600u * (uint64_t)FS, bpm, 1, 1);
    printf("     %.0f BPM: %u early, %u late of %u 1/16s; the step off the clock's grid by %d samples at most\n", bpm,
           hits_early, hits_late, N0, grid_err);
    return hits_early == 0u && hits_late == 0u && N0 > 10000u && grid_err <= (int32_t)CTL;
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
    clk_send(0xFAu, 1, (double)now);
    n0 = pulses;
    run(FS / 2, 100.0, 1, 1);
    check("Start: step 0 on the first pulse after it", N0 >= 1u && h0[0] >= pulse_at[n0] && h0[0] <= pulse_at[n0] + 2u);

    /* a tempo change: 100 -> 150 BPM while playing: the steps follow the new pulses */
    n0 = N0;
    run(2u * FS, 150.0, 1, 1);
    {
        uint32_t p = pulses - 1u;
        check("tempo change 100 -> 150 BPM: still one 1/16 per 6 pulses (2 s = 20 1/16s)", N0 - n0 >= 18u && h0[N0 - 1u] <= pulse_at[p] + 2u &&
              abs(song.g[G_BPM] - 150) <= 3);
    }

    /* Stop, Continue: resumes the step it stopped at */
    clk_send(0xFCu, 1, (double)now);
    run(FS / 4, 150.0, 1, 1);
    k = trk[0].seq_idx;
    n0 = N0;
    run(FS / 4, 150.0, 1, 1);
    check("Stop: no steps (Clock still coming)", N0 == n0 && !song.playing);
    clk_send(0xFBu, 1, (double)now);
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

    /* review focus 3: a burst (12 pulses in one block, late USB packets): no step skipped, the advance capped */
    setup(1);
    clk_send(0xFAu, 1, (double)now);
    run(FS / 2, 120.0, 1, 1);
    n0 = N0;
    {
        uint32_t i;
        for (i = 0; i < 12u; i++) {
            clk_send(0xF8u, 1, (double)now);       /* late packets: they all arrive now */
            next_pulse += FS * 60.0 / 120.0 / 24.0;
            pulses++;
        }
        next_arrival = -1.0;
    }
    run(FS / 2, 120.0, 1, 1);
    check("a burst of 12 pulses in one block (the next 12, late together): 24 pulses in all, exactly 4 1/16s: none skipped",
          N0 - n0 == 4u && song.playing);

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

    check("a steady clock (140 BPM, on time): the tempo holds (no per-beat flip)", steady_tempo(0));
    check("a steady clock (140 BPM, 0 .. 4 ms late): the tempo holds", steady_tempo(1));
    check("an hour at 128 BPM, 0 .. 4 ms late: every 1/16 on its pulse, on the clock's grid within a block", long_run(128.0));
    check("an hour at 174 BPM, 0 .. 4 ms late: the same", long_run(174.0));
    check("an hour at 240 BPM, 0 .. 4 ms late: the same", long_run(240.0));
    printf(fails ? "midi_clock_test: %d FAILED\n" : "midi_clock_test: all passed\n", fails);
    return fails ? 1 : 0;
}
