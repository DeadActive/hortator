/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from MIDI clock contributions by ChanceTheMaker and keremimo (2026).
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* MIDI clock in (upstream 1.0's midi_clock.c, without its arpeggiator / motion / song chain parts): the chosen
 * source's Clock moves the sequencer, a beat / 24 a pulse (the remainder kept: 24 pulses = one beat), interpolated
 * between pulses, stopping 1/8 pulse short of the next one; the tempo is measured over 24 pulses (a beat) and
 * averaged over 4 beats, from the input ring's timestamps
 * (USB / TRS jitter stays outside the render). Audio ISR state. */
static struct {
    uint32_t pos, rendered, last_ms, start_ms, rem, interval_ms;
    uint32_t pulse_samples, interp_q8, tempo_ms;
    uint8_t mode, have_pulse, tempo_valid, tempo_n;
    uint16_t dts[4];                                 /* the last beats' lengths (ms, 24 pulses each) */
    uint8_t dt_n, dt_i;                              /* .. how many, the next slot */
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
        motion_begin();                             /* motion.c: the base, as PLAY takes it */
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
    } else if (++midi_clock.tempo_n == 24u) {
        uint32_t dt = ms - midi_clock.tempo_ms;
        midi_clock.tempo_ms = ms;
        midi_clock.tempo_n = 0;
        /* 24 clocks are a beat (upstream measured 6, a quarter of one: a few ms of USB / TRS arrival jitter then
         * moved the tempo +-3 % at every reading). Reject gaps and corrupt bursts (40 .. 240 BPM). */
        if (dt >= 250u && dt <= 1500u) {
            /* the tempo: the last 4 beats averaged, changed only when that moves more than 0.5 % (a clock timed
             * in whole ms reads e.g. 428 / 429 ms a beat at 140 BPM: taken raw, the tempo flipped every beat,
             * moving the delay's read point (a tick) and rescaling every track). A beat more than 2 % off is a
             * real change: the average starts again from it, so it is followed within the beat. */
            uint32_t n, sum = 0, old_beat = beat_samples(), one = (uint32_t)FS * dt / 1000u, new_beat;
            if (midi_clock.dt_n && (one * 50u > old_beat * 51u || one * 51u < old_beat * 50u))
                midi_clock.dt_n = 0;
            midi_clock.dts[midi_clock.dt_i] = (uint16_t)dt;
            midi_clock.dt_i = (uint8_t)((midi_clock.dt_i + 1u) & 3u);
            if (midi_clock.dt_n < 4u)
                midi_clock.dt_n++;
            for (n = 0; n < midi_clock.dt_n; n++)
                sum += midi_clock.dts[(midi_clock.dt_i + 3u - n) & 3u];
            new_beat = (uint32_t)FS * sum / (1000u * midi_clock.dt_n);
            if (midi_beat_samples && new_beat * 200u <= old_beat * 201u && new_beat * 201u >= old_beat * 200u)
                new_beat = old_beat;                 /* within 0.5 %: kept */
            if (song.playing && new_beat != old_beat) {
                /* each track keeps its place in its step when the tempo changes (and Grids its 1/16), the
                 * in-progress advance too. The ratio is Q15 and rounded to nearest: upstream's Q12, rounded down,
                 * lost a little of each step at every reading, so the steps fell behind the pulses (~10 ms after
                 * 1 000 jittered readings). (No 64-bit division on the target: a 32-bit one for the ratio, then
                 * multiply and shift.) */
                uint32_t i, r = ((new_beat << 15) + old_beat / 2u) / old_beat;   /* new_beat < 2^17 */
                int64_t d = (int32_t)(midi_clock.rendered - midi_clock.pos);
                for (i = 0; i < NTRK; i++)
                    if (trk[i].seq_pos < 0x7FFFFFFFu)
                        trk[i].seq_pos = (uint32_t)(((uint64_t)trk[i].seq_pos * r + 16384u) >> 15);
                if (gclk.pos < 0x7FFFFFFFu)
                    gclk.pos = (uint32_t)(((uint64_t)gclk.pos * r + 16384u) >> 15);
                /* the advance already rendered into this pulse, in the new units too (else it stays counted at
                 * the old size and every track runs that difference late from then on) */
                midi_clock.rendered = midi_clock.pos + (uint32_t)(int32_t)((d * (int64_t)r + 16384) >> 15);
            }
            midi_beat_samples = new_beat;
            song.g[G_BPM] = (int16_t)clamp((int32_t)(((uint32_t)FS * 60u + new_beat / 2u) / new_beat), 40, 240);
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
    /* Interpolate towards the next pulse, stopping 1/8 of a pulse short of it: every step boundary sits on a pulse,
     * so a step due there fires when its pulse arrives, never early by a rounding sample or two (upstream ran to
     * one sample short: such a step fired up to an early interpolation's stall ahead, ~6 ms) */
    if (elapsed > midi_clock.interval_ms)
        elapsed = midi_clock.interval_ms;
    offset = elapsed * midi_clock.interp_q8 >> 8;
    if (offset > midi_clock.pulse_samples - midi_clock.pulse_samples / 8u)
        offset = midi_clock.pulse_samples - midi_clock.pulse_samples / 8u;
    target = midi_clock.pos + offset;
    n = (int32_t)(target - midi_clock.rendered) > 0 ? target - midi_clock.rendered : 0u;
    if (n > (uint32_t)FS / 8u)
        n = (uint32_t)FS / 8u;
    midi_clock.rendered += n;
    return n;
}
