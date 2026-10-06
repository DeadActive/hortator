# Ideas — parked for later

Things we talked about and decided to come back to. Nothing here is designed or built yet; each one starts with
the usual design questions before any code.

## 303-style mono bass voice (parked 2026-10-06)

### What it is

A new sound engine, one of the engines a track can use (EDIT → MODEL). It is not a new kind of track: there are
still 8 tracks, and a track switched to this engine plays a monophonic acid bassline.

- Oscillator: saw or square, band-limited.
- Amp envelope and a filter envelope.
- Its own resonant low-pass filter (a 303-like ladder).
- Accent per step: the step's existing ACC flag, used the 303 way (louder, more filter envelope, snappier
  decay).
- Slide per step: the next note glides and the envelopes do not retrigger.
- A note per step: each step plays its own pitch, set in the sequencer.

### Can we afford it? (answered 2026-10-06, from the measured numbers)

Yes for one or two such tracks. The limit to know about: a bassline sounds most of the time, while a drum hit is
a short burst, so its cost is paid continuously.

- **CPU.** The tests' yardstick is host instructions per sample. A realistic heavy kit with every FX measures
  1207; the limit is 1566 (stock Felucca's heaviest mix, known to run on the FM-1), so about 360 is spare.
  The voice is estimated at 100–200 per sample while it sounds (oscillator, 4-pole filter with soft saturation
  in integer maths, envelopes, accent, slide), about the cost of one of the heavier drum engines. One or two
  303 tracks fit inside the realistic budget; three or more pass it; eight at once would reach the overload
  protection. These are estimates: measure a throwaway prototype first.
- **Project size.** A project must fit one flash sector: 3840 B usable, 2912 B used (FDR4). A note and a slide
  flag per step fit in one byte (7 bits of note, 1 bit of slide): 64 steps × 8 tracks = +512 B, about 3424 B,
  leaving about 400 B for later features. That means a new project format (FDR5) that converts FDR4 and older
  records with every step at the track's NOTE and no slide.
- **Knobs.** No new knob parameters: the engine uses the 8 engine knobs every engine already has, for example
  `WAVE` (SAW / SQR), `CUTOF`, `RESO`, `ENVMD`, `DECAY`, `ACCNT`, `SLIDE` (glide time), `TUNE` or `DRIVE`.
- **RAM and code space.** RAM is 43 KB of 96 KB used (plus about 2 KB more for the four project copies); code
  space is no concern.

### Sequencer changes

- Step data grows by one byte (note + slide). Accent already exists.
- Slide changes how the sequencer triggers this engine: a slid step changes the pitch of the sounding note
  instead of starting a new hit.
- The LFOs can target its knobs like any engine's (CUTOF with an LFO is the obvious use).

### Main risk

A resonant ladder filter in integer-only maths (no float, no 64-bit division on the target) that stays stable up
to self-oscillation. Solvable, but that is where most of the testing goes. Open303 (MIT licence) is a good
reference for the structure; it uses floating point, so this would be a port to fixed point, not a copy.

### Open questions for when we return

1. How notes are entered on the steps: hold a step and turn a knob (as PROB / RATCH today), record from the
   keys, or both. How slide is set per step.
2. The final list of the 8 knobs.
3. Whether to measure a throwaway prototype's cost first (recommended).
4. How the STEP grid and the footer show notes and slides.

## Also parked

- **FM drum engine:** Mutable Instruments Peaks' FM drum (integer maths, MIT) was the best fit found.
- **"Make the compressor optional":** asked once, the meaning was never settled.
- **M5:** the web editor and presets.
- **LFO polish (deferred minors from the LFO review):**
  - when both LFOs target the same knob, the EDIT marker shows only LFO 1;
  - SYNC rates drift slowly against the sequencer (integer rounding);
  - untested: a tempo change while playing, corrupt saved LFO fields, white-key select on the LFO pages.
- **Target cost budget:** the audio ISR measures about 8800 against its 15188 budget (−42 %); lowering the
  budget to the measured value would tighten the check; not decided yet.
