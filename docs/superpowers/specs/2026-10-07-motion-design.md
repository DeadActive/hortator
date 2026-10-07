# Motion recording (roadmap §3.5) — design

Date: 2026-10-07. Parent: `docs/UPSTREAM_1.0.2.md` §3.5. Upstream reference: Felucca 1.0 `727f272`
`firmware/src/motion.c` (202 lines), its calls in `seq.c` (`motion_begin` / `motion_end` / `motion_step`),
`ui_input.c` (`motion_capture` after a knob edit), `ui_draw.c` / `ui_graph.c` (the MOTION page) and `project.c`
(the store in the project). Branch `motion` from main `6b7a46a` (`drum-v0.10.0`). No frozen file changes.

## 1. Goal and user decisions

Knob moves played into the running sequencer are recorded per step and played back with the pattern: a DECAY
swept over a bar comes back every bar.

User decisions (2026-10-07):
- **Upstream's live motion recording only** (REC armed, knobs turned while playing). Elektron-style parameter locks
  (step entry) are parked in `docs/IDEAS.md`.
- **Upstream's playback rules:** a recorded value holds until that knob's next event; the patch value comes back at
  the track's loop start and on STOP.
- **128 events** shared by the 8 tracks (upstream: 64 for 4), project format **FDR8**; song chain + names (§3.6)
  become FDR9.
- Version after the merge: **0.11.0**.

## 2. Recording

- A knob turn records when the selected track is armed (`song.rec` bit, REC on TRACKS, as for hits) and the
  sequencer is playing. It records the knob's new value at the nearest step of that track: the step playing now,
  or the next one when more than half of it has passed (upstream's `motion_capture`).
- The same knob on the same step again: the value is replaced. A sweep over several steps writes an event at each
  step passed (one per knob per step).
- Recorded wherever the selected track's knob is turned: SOUND / HOME (the model's knobs, LEVEL, PAN), LAYER, FX,
  SLICER, RESON, LFO, TRACKS (LEVEL, PAN).
- **Recordable (sound only):** `P_E0..P_E7`, `P_LEVEL`, `P_PAN`, `P_DIST`, `P_CHOR`, `P_DLY`, `P_REV`, `P_SLPAT`,
  `P_SLRATE`, `P_SLDEPTH`, `P_LLEVEL`, `P_LTUNE`, `P_LDEC`, each LFO's `LF_RATE`, `LF_MORPH`, `LF_DEPTH`, and
  `P_RTUNE .. P_RPOS`. **Not recorded:** `P_MODEL`, `P_MUTE`, `P_CHOKE`, `P_NOTE`, `P_SLCR`, `P_SLEN`, `P_SDIV`,
  `P_SSWING`, `P_LSET`, `P_LKEY`, `P_SRC`, `P_DUCK`, the LFOs' WAVE / MODE / DEST / TRIG / PHASE, `P_RMODEL`, and
  every GLOBAL (`song.g`).
- Not recording (not armed, stopped, another track's knob): the turn changes the patch value; while motion plays,
  it becomes the new base the loop start and STOP come back to (upstream's `motion_base`).
- **Full:** all 128 places used: "MOTION FULL" (once a turn), the turn is not recorded (the knob still moves).

## 3. Playback

- Per track a PLAY bit (on when the track gets its first event; KNOB 1 on the MOTION page).
- When a track enters step s (`seq_tick`), its events at s set their knobs (clamped to the knob's range now).
  A value holds until that knob's next event; at the track's step 0 every knob it changed returns to the base.
- **Start / STOP:** PLAY takes the base (every knob as set); STOP and switching PLAY OFF put the base back, so a
  project saved after STOP (saves wait for STOP) holds the patch, not a recorded value.
- **LFOs:** the LFOs modulate around the value motion set. In a block the LFOs' modulated copy is in `t->p` and the
  value they return to in `t->lsave`; motion's write during the block goes to `lsave` for a knob an LFO is on
  (else to `t->p`), so neither overwrites the other.
- Motion follows each track's own steps: LEN, DIV, exact timing, MIDI clock, Grids tracks (their step clock still
  runs).
- A model change (`P_MODEL`) keeps the events; values are clamped to the new model's ranges when played.

## 4. The MOTION page (SEQ family, after PATTERN)

- KNOB 1 **PLAY** ON / OFF (the selected track); KNOB 2 **EVENTS** (the track's count, shown only); KNOB 3 empty;
  KNOB 4 **CLEAR**: a turn asks "CLEAR MOTION T<n>?" (OCT+ yes, OCT- no), as "CLEAR TRACK n?".
- The graph: the track's LEN steps as a row of cells, the cells holding events marked, the playhead.
- Clearing a track (REC held, "CLEAR TRACK n?") and TOOLS CLR SEQ / CLR ALL / INIT ALL clear the motion they
  cover (that track / every track).

## 5. Storage (FDR8)

- `motion_event_t {uint8_t trk, step, param; int8_t value}` (4 B: every recordable range is -64..127),
  `motion_store_t {uint8_t count, on, rsv[2]; motion_event_t ev[128]}` = 516 B, one per project, after the
  tracks. The project grows from 3028 B to 3544 B (flash payload 3840 B).
- FDR7 (and older) records load as before with no motion. A record whose motion is invalid (count > 128, a track
  or step out of range, a param not recordable, a duplicate place) loads with the motion dropped.
- RAM: the store (516 B), the base copy `int16_t [8][P_COUNT]` (880 B) and per-track active bits.

## 6. Testing (host, failing first)

New `tests/motion_test.c` (drum suite):
- recording: armed + playing, a knob value set mid-step lands on that step, past half on the next; not armed /
  stopped / another track: no event, the base changes; the same place replaces; a non-recordable knob is not
  recorded; 128 full: the next is refused and flagged;
- playback: an event at step 4 sets the knob there, it holds through 5..LEN-1, step 0 restores the base; PLAY OFF
  restores and plays none; STOP restores; a turn while motion plays becomes the base;
- LFO + motion: an LFO on DECAY while motion moves DECAY: after the block the knob's base is motion's value;
- clamping after a model change; Grids track; MIDI clock (events on the clock's steps);
- FDR8 save / load round trip with motion; an FDR7 record loads with none; an invalid motion block is dropped;
- the kit hashes (`sound_pack_test`) unchanged with no motion.
- `ui_test`: recording through edit_param / HOME / TRACKS; the MOTION page: PLAY, EVENTS, CLEAR with its dialog;
  REC-hold clear; screens of the MOTION page (empty, with events while playing).

For the user's ears: a drumsim WAV of a kick with its DECAY recorded as a sweep over a bar, 4 bars.

## 7. On the device (the user)

- TRACKS: arm a track (REC), play, go to SOUND and sweep DECAY over a bar: the sweep repeats every bar; STOP: the
  knob back where it was.
- MOTION page: PLAY OFF / ON, EVENTS count, CLEAR asks and clears.
- Save, power off, load: the motion is there.

## 8. Done when

§6 passes, the firmware builds, the frozen / loader / stack checks are green, CHANGELOG has the line,
DEVICE_INSTALL.md has a MOTION section, the user has checked §7; merged as 0.11.0 (tag `drum-v0.11.0`).

## 9. Out of scope

Parameter locks / step entry (IDEAS.md), recording GLOBAL knobs, per-event editing, motion in the song chain
(§3.6 decides), upstream's undo copy of motion.
