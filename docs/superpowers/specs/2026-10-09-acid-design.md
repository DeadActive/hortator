# ACID: notes on the steps, keyboard mode and a 303-style voice — design

Date: 2026-10-09. Branch `acid` (worktree `felucca-acid`) from `main` = `drum-v0.14.0`. The user's third part
(filter, FM percussion, then the melodic part), its first release. No frozen file changes.

## 1. Goal and user decisions

A 303-style acid bassline in Hortator: each step holds a note, and a new voice plays it with accent, slide and tie.
The keyboard can play and record any track chromatically. User decisions (2026-10-09):
- **Scope:** this release is notes on the steps + keyboard mode + the ACID voice. The 808 BASS voice is part 2 (its
  own spec), reusing the same note steps.
- **Any of the 8 tracks** can hold notes and use ACID (the CPU, not the format, limits how many sound at once).
- **Entry:** step record from the keyboard, and live record. Upstream Felucca's note sequencer is the model for both.
- **Keyboard mode** (KEYS): an SCL tap turns it on and off; the top bar shows it; it applies to drum tracks too, where
  the keys transpose the drum.
- **Drum pitch reference:** C4 plays a drum exactly as its TUNE sets it.
- **Ties** are in this release (added in review of the design).
- **Approach A:** ACID is one more drum model with two optional model hooks (glide, gate end), not a separate synth
  voice layer.
- **Not in this release:** the 808 BASS; ACID from MIDI notes; scales / quantise; chords / poly; per-step velocity;
  saving KEYS mode.

## 2. Steps and the project format

`step_t` stays 4 bytes, so `project_t` stays 3672 B (the sector holds 3840 B):

| Byte | Contents |
|---|---|
| 0 | flags: bit 0 ON, bit 1 ACC, bit 2 SLIDE, bit 3 TIE, bits 4..5 RATCH (hits − 1) |
| 1 | COND (PROB), unchanged |
| 2 | NOTE − 60, signed (−60 .. +67; 0 = C4), so an all-zero step is still a plain step at C4 |
| 3 | spare (0) |

- A new format **FDRB**. FDR1 .. FDRA projects convert on load: ON, ACC and RATCH move into the flags, NOTE = C4
  (byte 2 = 0), SLIDE = TIE = 0, COND as it was. Such a project plays exactly as before (the golden kit hashes do not
  change). FDRA records are the same size as FDRB ones: they are told apart by their marker, in flash and in the RAM
  slots (.noinit) left by the previous firmware.
- A plain step (ON only) written by the step keys gets NOTE = the track's last played or recorded note (60 at first).
- Every place that reads or writes `on`, `acc` or `rat` (sequencer, step keys, UI, conversion, tests) moves to the
  flag accessors; nothing else changes meaning.

## 3. Playback

Per track, at each step the sequencer fires today:
- **Drum models:** an ON step hits the model transposed by NOTE − 60 semitones, clamped to ±24, added to TUNE (the
  models already turn TUNE into a pitch at the hit: the transpose joins it there). SLIDE is ignored; a TIE step plays
  nothing.
- **ACID** (mono, one voice per track):
  - an ON step starts the note NOTE at velocity 127 with ACC, else 96: envelopes restart;
  - the gate closes GATE of the step's length after the start (§4), unless the step has SLIDE or the next step is a
    TIE;
  - **SLIDE:** the next ON step does not restart the envelopes; its pitch glides from the sounding note in about
    60 ms (in the log-pitch domain, as a 303); its own ACC still applies;
  - **TIE:** no new note; the sounding note keeps its pitch and its gate is held through this step (several TIEs in a
    row: one long note); PROB, RATCH and ACC are ignored on a TIE step. A TIE after an empty or rest step plays
    nothing;
  - a step that is off (a rest) after a held gate closes the gate at its start;
  - PROB and RATCH work as on drums (a ratchet restarts the note within the step).
- MIDI in is unchanged: notes on the drum channel fire tracks by NOTE (P_NOTE), at the step pitch C4 (no transpose).

## 4. The ACID voice

`ACID` (`DM_ACID`), appended after FM (stored model numbers unchanged), in `firmware/src/dm_acid.c`. One voice per
track (mono), weight 1. The model table gains two optional hooks, `glide(track, voice, note)` and
`gate(track, voice, on)`; every other model leaves them empty.

| Knob | Range | Default | Notes |
|---|---|---|---|
| TUNE | ±24 semitones | 0 | on top of the step's NOTE |
| DECAY | 0..127 = 30 ms .. 3 s | 50 | the filter envelope's decay |
| CUTOF | 0..127 = about 20 Hz .. 8 kHz (exponential) | 40 | |
| RESO | 0..127 | 64 | 127: self-oscillation |
| ENVMD | 0..127 | 64 | filter envelope amount |
| ACCNT | 0..127 | 64 | how much ACC adds (level, filter envelope, shorter decay) |
| WAVE | SAW / SQR | SAW | |
| GATE | 0..127 = 10 .. 100 % of the step (linear) | 56 (50 %) | the note's length |

- **Oscillator:** a band-limited saw and square (integer phase accumulator, polyBLEP-style correction), the pitch from
  NOTE + TUNE through the existing note-to-increment helpers.
- **Filter:** a 24 dB/oct 4-pole ladder, per sample, fixed point (Q24 style, as `dm_dsp.c`'s helpers), with a soft
  saturation in the feedback path; stable up to self-oscillation at RESO 127 at every CUTOF; cutoff from CUTOF +
  ENVMD × the filter envelope (+ accent), updated per 32-sample block and smoothed. The main risk: most tests go here.
- **Envelopes, the 303 way:** amp: fast attack (about 3 ms), held while the gate is open, a short release (about
  8 ms) after it; filter envelope: decays over DECAY from each restart. **Accent:** more level and filter envelope
  (ACCNT), the filter decay shortened; consecutive accents charge a slow smoother so the filter "wows" as on a 303.
- **Level:** at the defaults within ±3 dB of a TOM's peak, checked by a test; A/B WAVs to the user.
- **Cost:** a throwaway prototype is measured first (host instructions per sample, BENCH's yardstick) and the numbers
  go to the user before the budget lines are set (estimate: 100–200 per sample while it sounds; 1–2 ACID tracks fit
  the realistic budget). A BENCH case "heavy kit + ACID" joins the BENCH list.
- **Memory:** the voice's state lives in `dvoice_t`; no new `.pool` buffers (the `ble` branch's pool has about 670 B
  to spare).
- The LFOs can target its knobs like any model's; FILTER, RESON, DIST, the sends and the mixer follow as for any model.

## 5. Keyboard mode (KEYS)

- **SCL tap** toggles it. On: the SCL LED is lit and the top bar shows `KEYS` and the lowest key's note (`KEYS F3`).
  Off at power-on; not saved.
- **The keys** (27, F3..G5 at octave 0) are a chromatic keyboard for the selected track, on HOME, EDIT, the SEQ pages
  and the other per-track pages:
  - **ACID:** mono, last key wins; a key's release closes the gate; a new key pressed while one is held slides to it
    (no restart);
  - **a drum:** hit transposed by the key's note − 60 (C4 = as tuned).
- **OCT- / OCT+** shift the keyboard by an octave, −2 .. +2 (holding both 5 s still gives UBOOT, main.c).
- **Other layers keep the keys:** FX held (PERFORM), the TRACKS and COMP pages (mute / duck keys), NAME, the menu.
- KEYS off: the keys do what they do today.

## 6. Recording

- **Live record** (armed as today: a REC tap on TRACKS, then PLAY), in KEYS mode: a key writes the selected track's
  nearest step (the next one when pressed past mid-step): ON + NOTE. A key held into later steps writes TIE on them;
  a legato overlap onto a different key writes SLIDE on the earlier step.
- **Step record:** in KEYS mode on the STEP page with the transport stopped, a REC tap starts it (top bar
  `STEP REC 05`; the cursor shown on the grid):
  - a key writes its note to the cursor step (ON, NOTE; TIME = NOTE) and the cursor advances when it is released;
    the next key pressed before the previous one is released sets SLIDE on the previous step;
  - knobs: K1 the cursor, K2 NOTE, K3 TIME (NOTE / TIE / REST), K4 FLAG (– / ACC / SLD / A+S);
  - an EDIT tap writes a rest (step off) and advances;
  - a REC tap, PLAY, or leaving the page ends step record.

## 7. Display

- Top bar: `KEYS F3` / `STEP REC 05` as above.
- **An ACID track's STEP page** shows a piano roll (adapted from upstream's `graph_roll`): about 2 octaves of rows ×
  16 steps, C rows labelled, the view following the notes; ACC a full-height mark, SLIDE a line into the next note,
  a TIE continuing the bar, RATCH splitting it; the playhead and the step-record cursor.
- **Drum tracks** keep the step grid; a transposed step (NOTE ≠ 60) gets a small mark.
- **The footer** shows the held or cursor step's note: `C#3`, plus `ACC` `SLD` `TIE`.
- **ACID's EDIT graph:** the filter curve at CUTOF / RESO and the envelope shape.
- Screenshots of each new screen go to the user.

## 8. Tests and checks

- Steps: the flag accessors; FDR1..FDRA → FDRB conversion (NOTE 60, no SLIDE / TIE); the golden kit hashes unchanged.
- Drum transpose: ±semitones, the ±24 clamp, C4 = as before.
- ACID: filter stability over RESO × CUTOF (no overflow, no blow-up), the self-oscillation pitch, saw / square
  aliasing, slide time, gate length, accent, tie (no restart, the gate held), the level against TOM.
- Sequencer: SLIDE / TIE / rest / RATCH / PROB on ACID as in §3.
- Keyboard mode: the key map, the octave range, mono last-key, slide on overlap, drum transpose.
- Recording: step record (cursor, slide on overlap, TIME / FLAG knobs, the EDIT rest, ending it); live record (nearest
  step, TIE on held keys, SLIDE on overlap).
- The budgets (`tests/target_budget.py`, `drum_cost_ref.txt`) and the BENCH case with the user's approved numbers;
  H2 / H3 / check_untouched as for every release; no frozen file changes.
- By ear and eye: A/B WAVs of a 303-style line (plain, accents, slides, ties, a RESO sweep) and screenshots.

## 9. Merging with `ble`

`ble` (BLE MIDI, waiting for its device test) also changes `felucca.c`, `console.c`, `tools/build.py`,
`tests/run_tests.sh`. Whichever merges second rebases on the other. When ACID ships as a release, the `ble` branch's
gate 5 base (`tools/check_default_build.sh`) moves to that release's tag.
