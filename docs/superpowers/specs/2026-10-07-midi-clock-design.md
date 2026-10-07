# MIDI clock in (roadmap §3.3) and exact step timing — design

Date: 2026-10-07. Parent: `docs/UPSTREAM_1.0.2.md` §3.3. Upstream reference: Felucca 1.0 `727f272`
`firmware/src/midi_clock.c` (adapted from MIDI clock contributions by ChanceTheMaker and keremimo), its use in
`seq.c` `events_block`, `fx.c` `beat_samples`, `params.c` `N_CLOCK`. Branch `midi-clock` from main `0166de1`
(`drum-v0.8.0`). No frozen file changes.

## 1. Goal and user decisions

The FM-1 follows a MIDI clock from USB or TRS: its steps (every division, 4BAR included), Grids, the slicer, synced
LFOs and the delay lock to the source's tempo, and the source's Start / Continue / Stop drive the transport. It
stays in time over long runs and tempo changes, stops cleanly when the clock disappears, and with the internal
clock behaves as today.

User decisions (2026-10-07):
- The clock source is GLOBAL **CLK** = **INT / USB / TRS**, kept **per project** (as upstream; `G_CLOCK` is already
  a stored global, so no format change).
- **Exact step timing** is part of this work (the pre-existing drift between divisions, reported 2026-10-07):
  step lengths keep their remainder from step to step. Step timing in INT moves by less than a sample a step; the
  pinned kit hashes (`tests/sound_pack_test.c`) are re-pinned (approved).
- Version after the merge: **0.9.0**.

## 2. One beat length for everything tempo-based

- `fx.c`: `static uint32_t midi_beat_samples;` (0 until an external clock has a measured tempo) and
  `static uint32_t beat_samples(void)` = `midi_beat_samples` when `song.g[G_CLOCK]` and it is set, else
  `FS * 60 / BPM` (upstream's). In INT mode the same integer as today.
- Users of the tempo take it from `beat_samples()`: `div_samples` (steps, delay TIME, Grids), `slicer.c`'s
  `s->base`, `lfo.c`'s samples-a-quarter for SYNC rates. INT: unchanged arithmetic. The slicer and synced LFOs run
  in audio time: with an external clock they take its tempo but are not locked to its pulses (the steps and Grids
  are).

## 3. Exact step timing (INT and external)

- A division is a fraction of a beat: ids 0..5 = 1/DEN (1, 2, 4, 8, 3, 6), ids 6..9 = 2, 4, 8, 16 beats.
- Each track keeps `seq_rem` (a new `uint8_t` in `track_t`, < DEN): the step that starts gets
  `period = (beat × num + seq_rem) / den`, and the next step starts with `seq_rem = (beat × num + seq_rem) % den`.
  So den steps last exactly num beats (e.g. four 1/16s = one beat, 5512 + 5513 + 5512 + 5513 at 120 BPM), and every
  division lands on the beat, against each other and against an external clock. Swing is applied to that period as
  today. `seq_start` sets `seq_rem = 0`.
- The Grids clock (`gclk`, its 1/16) gets the same remainder (`gclk.rem`).
- Live recording, ratchets and PROB use the step's period as today.

## 4. Following a clock (upstream's `midi_clock.c`, ported)

- `N_CLOCK` = {"INT", "USB", "TRS"}; the value matches the input ring's source (1 USB, 2 TRS).
- `midi_block` passes the realtime packets of the chosen source to the clock: `F8` → `midi_clock_pulse(ms)`,
  `FA` / `FB` / `FC` → `midi_clock_transport(status, ms)`; the other source's realtime packets and everything in
  INT are read and ignored as today. Notes play as today.
- **Pulses:** each advances the sequencer's target by `beat / 24` samples (remainder kept: 24 pulses = one beat
  exactly); between pulses the sequencer moves at the measured rate (interpolated from the pulse interval) but
  never past the next pulse; at most 1/8 s in one block.
- **Tempo:** measured every 6 pulses from their timestamps (`midi_in_ms`); a gap outside 62..375 ms (40..240 BPM)
  is ignored. A new tempo rescales each track's position in its step (`seq_pos`, and Grids' `gclk.pos`) by the
  ratio, so every track keeps its place; `song.g[G_BPM]` shows the measured tempo (rounded, 40..240).
- **Start (FA):** the sequencer from step 0; step 0 plays on the first pulse after it (the MIDI downbeat; upstream
  played it on the Start byte itself, up to a pulse early): until a pulse has come, `seq_tick` / `grids_tick` do not
  run. **Continue (FB):** resume where it stopped, on its first pulse. **Stop (FC):** stop.
- **Lost clock:** playing and no pulse for 500 ms → stop.
- **The FM-1's buttons:** PLAY acts as Start (armed, waiting for pulses); STOP stops.
- **BPM knob while following:** changes nothing; a short message "CLK USB" / "CLK TRS".
- **Switching CLK** (by the knob or a project load) stops the sequencer and clears the clock state.
- In external mode `events_block` gives `seq_tick`, `grids_tick` and the MUTE NEXT BAR logic the clock's advance
  (`seq_n`) instead of the block length; the audio (voices, FX, free LFOs) runs as always.

## 5. Testing (host, failing first)

New `tests/midi_clock_test.c` (drum suite): a simulated clock source writes `F8` / `FA` / `FB` / `FC` into the input
ring (`midi_enqueue`, source USB or TRS) with timestamps from a simulated `fm1_ms`, block by block:
- 120 and 140 BPM, timestamps with ±2 ms jitter: a 1/16 track fires every 6 pulses and a 4BAR track every 384, the
  first on Start's first pulse, with no drift after 5 000 1/16 steps (each hit within a block of its pulse);
- a tempo change (120 → 140 in the middle of a step): each track keeps its place, the next steps on the new pulses;
- Start resets to step 0; Stop stops; Continue resumes at the step it stopped; TRS pulses ignored while CLK = USB
  (and the reverse); no pulse for 500 ms stops; PLAY on the FM-1 waits for the pulses;
- Grids stays on the 1/16 track's hits; BPM shows the measured tempo; the BPM knob changes nothing.

Exact timing (drum_test): in INT, a 1/16 and a 1/4 track at 120 BPM stay together (every 4th 1/16 hit on a 1/4 hit)
over 1 000 beats; four 1/16 periods sum to one beat at several BPMs; 8T / 16T sum to their beats. The sound pack's
4BAR-vs-1/16 test goes back to exact. `sound_pack_test`'s kit hashes are re-pinned (the INT timing moved by less
than a sample a step); existing timing tests whose expected positions came from the rounded period are corrected to
the exact positions.

Screens: GLOBAL with CLK USB; the header while following.

## 6. On the device (the user)

- A DAW sending MIDI clock over USB (CLK USB): Start / Stop / Continue from the DAW; a 1/16 hat tight against the
  DAW's metronome over 5+ minutes; a DAW tempo change followed within a beat; unplugging the cable stops the FM-1.
- A hardware box on TRS (CLK TRS): the same.
- CLK INT: as before; a 1/16 and a 1/4 track never drift apart.

## 7. Done when

§5 passes, the firmware builds, the frozen / loader / stack checks are green, CHANGELOG has the line, the user has
checked §6; merged as 0.9.0 (tag `drum-v0.9.0`).

## 8. Out of scope

Sending MIDI clock out, Song Position Pointer, nudging the tempo by hand while following, upstream's arpeggiator /
motion / song-chain parts of the clock code.
