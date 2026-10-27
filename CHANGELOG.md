# Changelog

What changed in each version of Hortator, the drum firmware for the M-VAVE FM-1, newest first. Changes that are not
released yet are listed under Unreleased.

## Unreleased

- **Live cue and variations**: while a pattern plays, queue the next project slot (A–D) or another variation and it
  comes in exactly where the pattern ends (its longest track's), every track from its first step, in time. On HOME
  and the SEQ pages the first 8 black keys (F#3 to A#4) queue variations 1–8 of the playing slot: the playing one's
  key lit, a queued one blinking (not with KEYS on; elsewhere the black keys work as before). Hold SAVE (0.4 s, or
  press a key with it) for the cue map: white keys 1–4 queue slots A–D, the black keys variations 1–8 (lit: used,
  winking: playing, blinking: queued, breathing: unsaved edits); a slot key then a variation key queues both
  (`CUE>B3` on top). The same key again switches at the next step; the playing slot's key
  cancels; stopped, a cue switches at once; STOP drops a queued cue. The keys pressed with SAVE play no note, send no
  MIDI and record nothing. A SAVE tap still opens the projects page, now when it is let go.
  - A slot switch brings the slot's sounds, steps, timing and locks; the tempo, swing, clock, MIDI, the master
    effects, Grids, the mutes and the selected track stay. The old sounds fade out over the last 5 ms before the
    switch (no click). A variation switch changes the steps and the timing only; the sounds ring on.
  - Each slot has 8 variations: variation 1 is its own steps; 2–8 hold their own steps and timing and share the
    slot's sounds and locks. A variation never used before starts as a copy of the one playing when it is first
    cued. When a slot's variations are full the cue says `VARIATIONS FULL` and the playing one plays on. A slot comes
    back on the variation it played last. A SONG row plays its slot's variation 1; while SONG plays a cue says
    `STOP SONG TO CUE`. Following an external clock (USB or TRS) the switch comes on the pulse that ends the pattern.
- **Edits stay in RAM until SAVE**: a reset or a power cut brings every slot back as it was last saved. Leaving a
  slot by LOAD or by a cue keeps its edits in RAM, so going back finds them; a slot with unsaved edits shows `*` in the
  PROJECTS list. LOAD of a slot with unsaved edits (a tempo change counts) asks `DISCARD EDITS?`; SAVE onto a slot
  that is not loaded but has unsaved edits asks `OVERWRITE B*?`. SAVE to another slot moves the edits there (the slot
  you came from is its saved version again). RENAME of a slot with unsaved edits renames it in RAM only
  (`RENAMED (SAVE TO KEEP)`). Unsaved variations of one slot can wait in RAM while another plays: until they are saved, a cue
  of a third slot (or a LOAD, when the slot you leave has unsaved variations too) says `SAVE C FIRST`.
- **FILL**: a step's PROB goes on past 8/8 with `FILL` (plays only during a fill) and `!FILL` (only outside one).
  Hold REC (0.4 s) on any page but SEQ's to play a fill while you hold it (`FILL` on top). A shorter REC press arms
  as before, when it is let go; REC held on STEP, PATTERN, MOTION and SONG does what it did. A learned REC and the
  SMC-Mixer's REC are always taps. (A long REC hold off the SEQ pages used to toggle REC ALL.)
- **COPY / PASTE a slot**: SAVE's third page, COPY: KNOB 2 COPY remembers SLOT's slot (`COPIED A`, `#` in the list),
  KNOB 3 PASTE (turn twice) makes SLOT's slot a copy of it: kit, steps, locks, variations and song, named
  `... COPY`, unsaved until SAVE. PASTE again on the same slot before saving it undoes it (`PASTE UNDONE`). Not while
  playing (`STOP TO PASTE`).
- **User sample slot 2 (USR2) now holds up to 48 KiB**, as USR3 does: the rest of its flash holds the projects'
  variations. A longer USR2 (uploaded under upstream Felucca) is shortened to its first 48 KiB once, at the next
  power-on (`USR2 SHORTENED TO 48K`). If that ever fails (`USR2 CLAMP FAILED` at power-on), SAVE says
  `USR2 ERROR: NO SAVE` until the next power-on.
- Projects: a project saved now loads on 0.25.0 and 0.24.0 without its variations. If a project's variations can't
  be read, it loads without them and the FM-1 says `VARIATIONS LOST` once. A power cut during SAVE keeps the previous
  save whole.

## 0.25.0 - 2026-10-27

### USB audio

- **USB audio, 2 or 10 channels**: the FM-1 records into a computer over its USB cable (16-bit, 44.1 kHz), and MENU
  (hold HOME) > USB AUDIO now picks what it sends. **2 CH**, the default, is the stereo master, exactly as before.
  **10 CH MIX** sends the master on channels 1-2 and the eight tracks T1..T8 on channels 3-10, one mono channel each
  (each track before its PAN, the effect sends and the master effects). **10 CH FX** sends the same eight tracks, with
  the chorus, delay and reverb returns on channels 1-2 instead of the master: pan the tracks in your DAW and add the
  effects pair to get the whole mix back, each part on its own fader.
- **Switching**: 2 CH <-> 10 CH takes effect when the menu closes: the FM-1 leaves USB for about a second and comes
  back as the other device, "Hortator 8ch" in 10 CH (its channels named Main L, Main R, T1..T8). The sequencer and the
  sound play on; when following CLK USB the steps wait up to 2 s for the clock to come back. MIX <-> FX switches at
  once with a short cross-fade, no reconnect.
- **Saved**: USB AUDIO is a device setting, saved and not part of projects; older settings start in 2 CH. A safe
  start (SEQ held at power-on) is always 2 CH.
- **Computers**: the 10 channels work on macOS and iPadOS (class-compliant USB audio) and on Linux. Windows may not
  accept 10 CH: use 2 CH there.
- **Bluetooth version**: 10 CH needs the standard build. The Bluetooth build is unchanged from 0.24.0: it has no USB
  AUDIO row, is always 2 CH and ignores a 10 CH setting saved by the standard build.

### Web site

- **Manual**: the landing page has a manual before the engines: a map of the panel (point at a control on a small
  FM-1, or at its line, to see what it does) and ten short guides (find your way, make a beat, shape a sound, play
  and record live, mute, perform, lock a step, save and chain, MIDI and LEARN, settings). Manual is in the top bar and
  the footer, and "More below" leads to it.

## 0.24.0 - 2026-10-25

### Parameter locks

- **Parameter locks**, Elektron style: any step can have its own value for a knob.
- **ENV: STEPS / TRACKS**: ENV (unused before) switches the white keys between the selected track's steps (STEPS)
  and the tracks (TRACKS), on its release. In STEPS its LED is lit and a padlock shows in the top bar (left of the
  battery) on every page. The mode is not saved: TRACKS at power-on.
- **STEPS mode**: the keys tap and hold steps as on SEQ, on HOME, TRACKS (armed too), SOUND, LAYER, FX, LFO and COMP,
  and play no drum (REC ALL records no hits then). ENV held + OCT+ / OCT- changes the bank; OCT+ held + a step sets
  its accent; OCT+ alone and OCT- keep their page's job. ENV turns KEYS and SCL STEPS off. In TRACKS the keys work
  exactly as before.
- **Locking a step**: in STEPS, hold a step and turn any knob that MOTION can record (on HOME, TRACKS' LEVEL and PAN,
  SOUND, LAYER, FX, SLICER, RESON, FILTER, LFO and LFO DEST): that step gets its own value for that knob. The top line
  says `LOCK STEP 5` (`LOCK 5` on HOME), the knob boxes show the step's values with a locked one inverted, and the
  knob's own value stays as it was. A turn right after the press works; the release then doesn't toggle the step.
  Knobs that MOTION can't record do nothing while a step is held.
- **Clearing locks**: step held + an OCT- tap clears that step's locks on the knobs of the page shown
  (`STEP 5 LOCKS CLEARED`); OCT- held 0.6 s clears all its locks (`STEP 5 ALL CLEARED`). OCT- with no step held keeps
  its job. `LOCKS FULL` at 1024 events. SEQ STEP still sets PROB / RATCH / ACC on a held step; a song's
  rows are not edited.
- **Playback**: a lock sets its knob for its own step only, and the knob goes back on the next step. An LFO on that
  knob modulates around the locked value. A step that is off keeps its locks without playing them. Locks play whatever
  MOTION's PLAY says; MOTION CLEAR leaves them; CLEAR TRACK, TOOLS CLR* and INIT* remove them. A SONG row plays its
  own project's locks and none carry into the next row. STOP, a model change, a MIDI LEARN CC and a preset SAVE use
  the knob's own value, never a lock's.
- **On screen**: in STEPS the footer's step strip underlines each locked step of the bank shown (in a song, the
  row's); SEQ > MOTION shows the selected track's MOTION (recorded events) and LOCKS counts on KNOB 2 / 3.

### Projects and samples

- **1024 events**: MOTION and the locks share 1024 events per project (MOTION had 128). A save cut by a power loss
  keeps the last complete project.
- **Compatibility**: older projects load with their motion as before. 0.23.x sees a project saved by 0.24.0 as an
  empty slot (`LOAD FAILED` at power-on): going back is safe but not supported. If a project's locks can't be read,
  it loads without them and the FM-1 says `LOCKS LOST`.
- **USR3 now holds up to 48 KiB** (USR1 and USR2 stay at 80 KiB): the locks are stored in the last 32 KiB of its
  space. A longer USR3 sample is shortened once at power-on (`USR3 SHORTENED TO 48K`: zones past 48 KiB are dropped,
  one that crosses it is cut); a power cut during that one rewrite empties USR3 and nothing else. If the rewrite
  fails, the FM-1 says `USR3 CLAMP FAILED` and SAVE and RENAME keep the project in memory only, saying so, so an
  older Hortator never plays the locks as USR3's sound.

## 0.23.3 - 2026-10-23

### Controls

- **Faster knobs**: with MENU > ACCEL ON (the default) a turn now speeds up from about 12 clicks a second, in
  fractions of a step: x2 at 20 clicks a second, x4 at 40, up to x8 on a wide value (the same limits as Felucca
  1.1.5). A 0..127 value takes about 65 clicks at 20 a second and about 24 at 60 (before: 127 and 64). A slow turn and
  single clicks stay one step a click, and the first click after changing direction is one step. Lists of names
  (MODEL, TYPE ...) never speed up, the PRESETS list at most x4. ACCEL OFF stays one step a click.
- **Quick turns no longer skip clicks**: before, a knob turned fast lost clicks from about 40 a second (a flick
  counted next to nothing). The knobs now use Felucca 1.1.5's knob decoder: every click counts up to about 300 a
  second, still one step a click, and bounce and a still knob's chatter are ignored.
- **Step keys**: holding a step no longer turns it off. A step toggles only on a short tap (released within 0.3 s
  with no knob turned), on the release. Held 0.3 s, an OFF step turns on at once, ready to edit; an ON step stays as
  it was. While a step is held on STEP, KNOB 1 / 2 / 3 set its PROB / RATCH / ACCENT (KNOB 3: right on, left off),
  shown in the knob boxes and the `STEP n PROB RATCH n ACC` line. OCT+ held + a step key still flips the accent at the
  press. This works on every page whose white keys are the steps (STEP, PATTERN, MOTION, SONG, TRACKS).

### Sounds and sequencer

- **RESON KEYF** (KEYFOLLOW, RESON 2/2 KNOB 4, 0..100 %, 0 % = off, the default): the resonator's pitch follows the
  note of each hit from C4 (C4 = TUNE): a step's NOTE, a KEYS note, a ratchet's. At 100 % a step at C5 rings an octave
  above TUNE, at 50 % half an octave; MIDI notes play their track at C4. Works on every model (STRNG, PIPE, CHORD,
  MODAL). A new note while the ring sounds glides there without a click. An amount rather than on / off: partial
  tracking keeps a ring in a narrower range under a wide melody. KEYF 0 sounds exactly as before. MIDI LEARN
  (`R.KEYF`) and MOTION take it. Older projects load with KEYF off, and a project with KEYF loads on 0.23.2 without
  it.
- **MUTE NEXT BAR with an external clock**: NEXT BAR now works while following a clock (CLK USB, TRS or BLE): the
  waiting mutes flip on the bar line instead of never. A muted COMP source (GHOST KEEP / HIDE) fades out just before
  the bar's first hit. CLK INT is unchanged.

### Look and USB

- **USB name**: the FM-1 now shows up as **Hortator** (USB MIDI and audio: in a DAW, AUM, the MIDI port list) instead
  of Felucca. While an update is written it still shows up as **Felucca Update**. The web installer, the editor and
  the install and sample upload tools find it under either name, so updating from any earlier build works as before.
- **COLOR**: every colour now comes in both polarities: BLUE INV (blue on white) and GREEN INV (green on black) join
  RED INV (red on black). The row shows the theme's name (RED, RED INV, BLUE, BLUE INV, GREEN, GREEN INV, in that
  order) instead of boxes. A stored colour loads as the same theme as before; an older Hortator shows the new
  inverses as RED.

## 0.23.2 - 2026-10-23

- **REC ALL**: on TRACKS a REC tap arms / disarms the selected track, saying so (`T3 REC ARMED` / `T3 REC OFF`). On
  every other page a REC tap is REC ALL: every track armed (`REC ALL`, the REC LED and the SMC-Mixer's REC 1-8 lit),
  so each white track key plays and records its own track wherever the keys play tracks (HOME, SOUND, FX, TRACKS ...):
  finger-drum a whole kit live. KEYS, step record and knob motion stay the selected track's.
- **Ending REC ALL**: a REC tap off TRACKS ends it and arms again the tracks that were armed before (`T3 REC ARMED` /
  `REC OFF`); on TRACKS it ends it with the selected track armed. The SMC-Mixer's REC n during REC ALL ends it and
  disarms track n (the others stay armed). A learned REC and the SMC-Mixer's REC act as the button on the page shown.
  Step record on STEP, REC held and `STOP TO EDIT` while a song plays are unchanged. Not saved in projects, like the
  arm state.
- **REC no longer starts play**: a REC tap (and REC ALL, a learned REC, the SMC-Mixer's REC and REC 1-8) only arms /
  disarms; PLAY starts the transport, and an armed track records once playing. Step record is unchanged.

## 0.23.1 - 2026-10-22

- **Web site only** (the firmware is the same as 0.23.0): the landing page and the installer count visits and
  installs anonymously with GoatCounter (no cookies, no personal data); the hero shows "Installed N times" under the
  install button. An install is counted once, after the installer says Done, and never holds it up.

## 0.23.0 - 2026-10-22

The first published release since 0.16.0: it also brings everything in 0.17.0 .. 0.22.0: the MD bank's 21 voices (13
TRX, 8 EFM), 151 factory presets and 128 user preset slots, the default TRX kit, the 4-slot LFO routing, accents on
OCT+ and a step, the Machinedrum-style screen and its faster redraws. Projects saved by 0.23.0 don't load in older
versions.

### Sounds

- **SOUND pages**: every engine as FM, ACID and BDTRX: the name on the left, its number "#NN/45" under it, VOICES,
  CHOKE and "+ LAYER" under that, a graph on the right. The engines with no graph of their own show their hit: the
  voice's real sound (one hit at velocity 100, played silently in the background, never touching what you hear) as a
  waveform, its length labelled ("HIT / 300 MS", 100 .. 1000 ms, following the sound). It redraws when MODEL or a
  SOUND knob changes, over a few frames; SAMPL shows its sample.
- **Engines renamed** (only the names shown: projects, presets and MIDI unchanged). MD bank: KTRX -> BDTRX, XTTRX ->
  TMTRX, XCTRX -> CGTRX, MATRX -> SHTRX, XTEFM -> TMEFM. MODERN bank: KBOOM -> BOOM, KPUNC -> PUNCH, SSNAP -> SNAP,
  SCRAK -> CRACK, HMETL -> METAL, HNOIS -> HISS, MEMB -> SKIN, SMPL -> SAMPL. User preset slots saved from them are
  named after the new names ("BOOM 07").
- **The power-on kit** (also TOOLS INIT ALL and a new project) is the TRX classic kit: KTRX, SDTRX, CPTRX, CHTRX,
  OHTRX, XTTRX, RSTRX, CYTRX, each on its default preset, with the same roles and MIDI notes as before (kick 36, snare
  38, clap 39, closed hat 42, open hat 46, tom 45, rim 37, cymbal 49) and no choke. Saved projects load unchanged.
- **MODEL list**: MD, MODERN, CLASSIC (KTRX .. CYEFM, KBOOM .. SMPL, K808 .. CLAVE), and the preset banks
  MACHINEDRUM, MODERN, CLASSIC, USER. Older projects load with the same voices and presets. The SOUND pages show the
  engine's place in that list under its name, "#01/45" (KTRX) .. "#45/45" (CLAVE).
- **LFO**: the OCT- switch between LFO 1 and LFO 2 is gone. The LFO pages show both LFOs side by side (LFO 1 on KNOB
  1-2, LFO 2 on KNOB 3-4): WAVE RATE, MORPH PHASE, TRIG MODE (the RATE MODE now has a knob; OCT+ still switches the
  one of the LFO turned last), and a last page, DEST: 4 slots per track, each a source (LFO 1 / LFO 2), a destination
  (any knob an LFO can target) and its own depth (KNOB 1 the slot, 2 SRC, 3 DEST, 4 DEPTH). Older projects load with
  their LFO 1 / LFO 2 routing as slots 1 / 2 (the sound, motion and MIDI LEARN unchanged); projects saved now can't be
  loaded by 0.22.0.

### Sequencer and keys

- **Accent**: hold OCT+ and tap a step key (STEP, PATTERN, MOTION, SONG, TRACKS): the step is accented (an off step
  turns on accented), or plain again. Holding a step key no longer sets the accent, so a held step is only PROB (KNOB
  1) and RATCH (KNOB 2). OCT+ on its own changes the bank when it is let go; while it is held the keys show the bank's
  accents. The STEP hint reads `[OCT+] +TAP ACCENT`.
- **HOME**: the 4 boxes above PROJ show the bar of a 4-bar cycle: the current bar solid, moving with the playhead,
  back to the first at every PLAY (stopped: the first). They used to show the selected track's banks.
- **The selected track on the keys**: on HOME and every EDIT (SOUND, LAYER), FX and LFO page its white key (F3..F4)
  is lit, as the keys select tracks there (held keys lit too). KEYS, COMP, the step pages and GLOBAL / SAVE / GRIDS
  keep their own.
- **Breathing OCT- / OCT+** (after Felucca's breathing LED): they fade in and out, about 1.1 s, where pressing them
  would do something (the next / previous bank on the steps, LAYER from SOUND, the LFO pages' RATE MODE (OCT+), YES in
  a dialog, OK in the menu, NAME's RENAME, MIDI LEARN's DONE, the PRESETS list's LOAD and DELETE); a way back (NO,
  CANCEL, BACK) is lit.
- **PERFORM keeps its knobs**: while its screen is up (FX held, the PAGE, a layer key held) KNOB 1-4 are its macros
  only, for every knob click (some used to reach the page under it), and SELECT turns no page under it (a page button,
  HOME or an FX tap still close the PAGE).
- **MENU BACKLIGHT** has Felucca's LED levels: OFF, DIM LO (the old ON: unlit keys and now the idle buttons at about
  5 %, the step beats about 25 %), DIM HI (about 10 %, beats about 30 %), INV (idle lit, active dark), INV LO / INV HI
  (idle at about 10 % / 25 %). KNOB 1 steps, OCT+ steps and wraps. An older Hortator reads any level but OFF as ON,
  and an older ON loads as DIM LO.

### Credits

- **MENU > ABOUT** credits everyone whose work is in the firmware, its research and its pages (Felucca, Mutable
  Instruments, DaisySP, the samples, the recovery tool, the web fonts and icons, the emulator the MD voices were
  measured from, with a disclaimer). The credits scroll under the logo with SELECT, a row a detent, with a scroll bar
  at the right; ABOUT opens at the top. The licensing notes list DaisySP (MIT) and the research; the project page and
  the site's footer carry the same credits.

## 0.22.0 - 2026-10-21

- **Faster screen**: only what changed is sent to the display (the same look, pixel for pixel). A popup (an
  SMC-Mixer knob's value, LOADED, ...) and the TEMPO popup no longer repaint the whole screen when they appear, change
  or end. HOME's scope waits while a knob turns. E.g. an SMC-Mixer knob click on TRACKS now redraws in about 8 ms
  instead of about 130 ms, and TRACKS playing redraws a small fraction of the screen each frame.
- **A new screen look** in the style of a classic digital drum machine's LCD, on every screen: two colours only (ink
  on a backlight; dither, dotted rules, inverted bars and title tabs instead of grey tones), a square pixel font with
  no smoothing, round knob dials with their values under them, shadowed popups. Layout: an inverted top bar, a
  compact knob row, the graph area, a trig strip with gaps between steps, and a breadcrumb (`T1>K909>SOUND`). HOME: a
  main-screen strip under the top bar, four small knobs with values below and a large scope; TRACKS keeps everything
  it shows on dithered level boxes. MENU > COLOR now cycles four themes: MD RED (the default), RED ON BLACK, WHITE ON
  BLUE and BLACK ON GREEN (old colour settings map to the nearest). A new 1-bit logo on BOOT and ABOUT. The PRESETS
  popup and list, MENU, NAME, the Bluetooth page and every popup in the same style. Nothing that was on a screen
  before is lost.

## 0.21.0 - 2026-10-19

- **Presets**: a preset is a voice (MODEL and the 8 SOUND knobs), in banks MACHINEDRUM, CLASSIC, MODERN (151 factory
  presets) and USER (128 slots). The PRESETS encoder loads the next / previous preset onto the selected track at each
  detent, through every bank (empty USER slots and a capped ACID skipped), with a popup over the graph (the bank, the
  preset, two before and after; about 1.5 s).
- **The PRESETS list**: EDIT held about 0.4 s opens it (an EDIT tap as before; seven presets on screen). KNOB 1 /
  PRESETS move, KNOB 2 / SELECT pick the bank, and every move plays the preset under the cursor once on the selected
  track, which keeps that sound while the list is open (its key and the pattern play it). OCT+ LOAD keeps it and
  closes the list; EDIT or HOME closes it with the track's own sound back. SAVE stores the track's own sound (not the
  auditioned one) into the first free USER slot or the chosen one (a used one: SAVE twice), OCT- twice DELETEs. USER
  presets are named from the voice and the slot ("SDTRX 03"); saved while playing, they are written at the next STOP.
  While MIDI LEARN is on, EDIT held does nothing.
- **Shown**: the SOUND pages show the track's preset ("PRESET HOUSEBD") with `*` once a SOUND knob moved, TRACKS'
  footer too; a track whose USER slot now holds another voice (or none) shows `-`. Projects keep each track's preset
  and its mark; older projects load with no preset, their sound unchanged. SAVE while MOTION plays stores the knobs as
  set, not motion's values.
- **The factory banks**: MACHINEDRUM 55 presets read from the Machinedrum's factory kits (1 to 4 per voice; none for
  MATRX, CLTRX), CLASSIC 56 and MODERN 40 (4 per voice, ACID and SMPL included). Every voice's default sound is its
  first preset: unchanged for CLASSIC and MODERN, new for the 19 Machinedrum voices with presets (KTRX SUBTHUMP, SDTRX
  HARDSNAP, CPEFM BUZZCLAP, ...).
- **No default choke groups**: HATC, HATO, HMETL, HNOIS, CHTRX, OHTRX and HHEFM used to start in group 1. CHOKE is now
  only the track's own setting: a MODEL change or a preset load never changes it, so the power-on kit's HATC / HATO no
  longer cut each other until CHOKE is set on both. Older projects keep their stored CHOKE.

## 0.20.0 - 2026-10-19

Eight FM voices measured from a classic digital drum machine's FM family: from an emulator's renders, knob by knob;
only the measured behaviour is used, no code or data of it. TUNE 0 is the machine's PTCH 64. All come after B2TRX
in the MODEL list.

- **BDEFM, XTEFM**: an FM bass drum and an FM tom (1 voice each). BDEFM TUNE DECAY RAMP RDEC, MOD MFRQ MDEC MFB;
  XTEFM TUNE DECAY RAMP RDEC, MOD MFRQ MDEC CLIC. RAMP sets where the pitch starts (16 flat, below it the pitch rises,
  from 0 Hz at RAMP 0) and RDEC how fast it gets back; MOD the FM depth, falling over MDEC; MFRQ the modulator's
  pitch; MFB grit from about 40, noise at 127; CLIC a click at the start. XTEFM's FM is an approximation (its kind of
  grit, not its exact tone); with MOD 0 it is exact. Both end at 0.58 s at the top of DECAY, as KTRX.
- **SDEFM, CPEFM, RSEFM**: a snare, a clap and a rim shot (1 voice each), each a tone modulated by noise, the same on
  every hit as on the machine. SDEFM TUNE DECAY NOIS NDEC, MOD MFRQ MDEC HPF: MOD the noise FM's depth, MDEC its
  decay, NOIS / NDEC a separate noise burst (random on every hit), HPF a high-pass. CPEFM TUNE DECAY CLPS CDEC, MOD
  MFRQ MDEC HPF: the same FM restarted CLPS / 8 times at CDEC's spacing (1 .. 41 ms), then DECAY. RSEFM TUNE DECAY MOD
  HPF, SNAR SPTC SDEC SMOD: a rim and a second "snare" voice (SNAR its level, 0 = off; SPTC its pitch).
- **CBEFM, HHEFM, CYEFM**: an FM cowbell, hat and cymbal. CBEFM TUNE DECAY SNAP FB, MOD MFRQ MDEC: two FM tones, FB
  feedback, SNAP a fast extra decay (4.5 ms) that lowers the rest of the hit. HHEFM TUNE DECAY TREM TFRQ, MOD MFRQ
  MDEC FB: a sizzling FM hat, FB from a stalled tone to chaos, a tremolo restarting at each hit; choke group 1, as
  HATC. CYEFM TUNE DECAY FB HPF, MOD MFRQ MDEC: four FM pairs, MDEC from 0.28 to 11 s, HPF two high-passes (off at 0);
  2 voices.
- **DECAY**: SDEFM, CPEFM, RSEFM, CBEFM, HHEFM and CYEFM follow the machine's whole 1.6 s range and fade out from 5.5
  s at the top of DECAY. BDEFM and XTEFM stop at 0.58 s: a faded sub-bass tail sounds worse than a short one.

## 0.19.0 - 2026-10-18

Thirteen voices measured from a classic digital drum machine: from an emulator's renders, knob by knob; only the
measured behaviour is used, no code or data of it.

- **KTRX**: a sine kick. TUNE DECAY SWEEP SDEC, PUNCH NOISE SUB CLIP: the pitch with its built-in drop; SWEEP a rise
  over SDEC; a short transient at the hit; PUNCH a square first cycle; NOISE a burst; SUB half-harmonics that fade on
  their own; CLIP a hard clip. DECAY runs from a 7 ms floor to 4 s (to -60 dB). 1 voice; the SOUND graph shows the
  pitch and the level over 300 ms.
- **SDTRX**: a snare. TUNE DECAY SNAP TONE, BUMP BENV CLIP: two tones, one at 342 Hz with BUMP's pitch blip (its time
  set by BENV), the other shaped by BENV; TONE crossfades them; CLIP a drive that eases off on its own (about 28 ms),
  whatever DECAY is; SNAP the same noise on every hit (above 64, a 1.44 kHz ring). TUNE moves both tones by semitones.
  1 voice.
- **B2TRX**: the machine's second bass drum. TUNE DECAY RAMP HOLD, TICK NOISE DIRT DIST: a 50 Hz sine at TUNE 0,
  starting at 0 on every hit; RAMP added pitch; HOLD full level before DECAY's fall (up to seconds); TICK a click;
  NOISE a hiss; DIRT a coarse quantiser; DIST a hard clip. 1 voice.
- **XTTRX, XCTRX, CLTRX, RSTRX**: a tom, a conga, claves and a rim shot, 1 voice each. XTTRX / XCTRX TUNE DECAY RAMP
  RDEC, DAMP DIST DTYP: a click, then a sine whose RAMP adds pitch falling over RDEC; DAMP moves level from the body
  to the click; DIST a hard clip, DTYP making the clipped part sag. CLTRX TUNE DECAY DUAL ENH, TUNE2 CLICK: two pure
  tones (TUNE2 the second's pitch, TUNE moving both); DUAL delays the first, ENH raises it, CLICK a short click. RSTRX
  TUNE DECAY DIST: a sine and a click under one decay; DIST warps the sine (stronger at low pitch). TUNE 0 is each
  machine's PTCH 64; the other knobs are the machine's own.
- **CPTRX, MATRX**: a clap and maracas, 1 voice each; a hit sounds the same every time. CPTRX TUNE DECAY CLPY HARD,
  RATE ROOM RSIZ RTUN: four noise bursts (RATE their spacing, DECAY their decay, CLPY the first three's level), HARD a
  resonant low-pass, and ROOM a noise tail (RSIZ its length, RTUN its colour; 0: off). MATRX TUNE DECAY ATT REV, DAMP
  RATL TONE HARD: grainy noise (DAMP: sparser), a rattle that grows through the hit (RATL), an attack (ATT), a hold
  (DECAY) and REV's dip and second hold.
- **CBTRX, CHTRX, OHTRX, CYTRX**: a cowbell, closed and open hats and a cymbal; the hats and the cymbal share one bank
  of metallic oscillators. CHTRX / OHTRX TUNE DECAY GAP HPF LPF MTAL (MTAL from noise to metal; DECAY a hold, GAP the
  decay after it; OHTRX's long hold dips to silence and comes back, as the machine's); choke group 1, as HATC / HATO.
  CYTRX TUNE DECAY RICH TOP TTUN SIZE PEAK (RICH the oscillators' balance, PEAK a 5.2 kHz bell, SIZE the body, TOP /
  TTUN a brighter top that fades first; 2 voices). CBTRX TUNE DECAY ENH DAMP TONE BUMP PTCH (two tones, ENH a
  resonator and grit, DAMP a narrow band-pass).
- **MODEL list**: the new voices come after ACID: KTRX SDTRX XTTRX XCTRX CPTRX RSTRX CBTRX CHTRX OHTRX CYTRX MATRX
  CLTRX B2TRX.

## 0.18.0 - 2026-10-14

- **MIDI LEARN**: up to 16 CCs from any controller, each set to one knob of one track. Hold GLO (the MUTE screen) and
  press G4 to turn it on; turn a knob of the selected track (HOME, EDIT, LAYER, FILTER, FX, SLICER, RESON, LFO,
  PATTERN, TRACKS' LEVEL / LEN / PAN), then move the controller's knob: `CC74 -> T3 DECAY`. The footer shows what is
  picked and its CC. The CC moves that track's knob whichever track is selected, from USB, TRS or Bluetooth in MIDI
  mode (not SMC DAW mode), on any channel, over the knob's whole range (centred knobs 0 at 64, lists by step); an LFO
  on the knob keeps modulating around it. One CC per knob, one knob per CC; CC 0, 32 and 120..127 are never learned.
  While on, OCT- clears the picked knob's CC (nothing picked: `CLEAR ALL LEARNED?`), OCT+ ends it. Saved in the FM-1
  (not in projects) after each change once stopped; off at power-on.
- **MIDI LEARN actions, 32 controls**: while LEARN is on, PLAY, REC, ALGORITHM right / left (NEXT / PREV TRACK), FX +
  a black key (a PERFORM effect) and FX + a white key F3..F4 (that track's PERFORM mute) pick their action instead of
  doing their job; a PERFORM knob (FILTER CRUSH THROW DEPTH) picks its macro. A CC or a note is bound to an action
  (`NOTE36 -> PLAY`, `CC22 -> FX R1/16`), a CC to a macro (`CC20 -> FX FILTER`, 0 at 64). A press (a CC rising to 64
  or more, a note on) taps PLAY / REC or steps the track; a PERFORM effect or mute is held while the controller's
  button is, and ends on release or when the menu, a confirm, NAME or the MUTE screen comes up. A learned note plays
  no drum. 32 bindings (was 16); the 16 saved before load as they were.

## 0.17.0 - 2026-10-13

### Controls

- **SELECT turns the pages**: SELECT does what PRESETS did (a section's pages, HOME 1/3 to COMP 2/3 and 3/3, the page
  under PERFORM, the menu's cursor, the Bluetooth page's rows), except on HOME's TRACKS mode, where it sets the
  tempo. PRESETS does nothing; ALGORITHM still picks the track.
- **HOME has two modes**: TRACKS (the mixer: TRACK LEVEL LEN PAN, the steps on the keys) and HOME (the scope, the
  selected track's knobs, the compressor after it). A HOME tap on HOME switches between them; from elsewhere it opens
  the one last shown; power-on shows TRACKS. REC no longer opens TRACKS.
- **REC arms on every screen**: a REC tap arms / disarms the selected track (arming while stopped starts play; `STOP
  TO EDIT` during a song). Kept: step record on STEP with KEYS on and stopped, and REC held for CLEAR TRACK on SEQ,
  DELETE ROW / CLEAR SONG on SONG. REC is no longer the mute modifier.
- **The MUTE screen** (GLO held): the eight tracks as boxes, filled = heard, outline = muted, a waiting mute blinking,
  the key under each; the white keys F3..F4 toggle them, the top C# / D# set MUTE: NOW / NEXT BAR. A GLO tap still
  opens GLOBAL. Replaces REC held + a key.
- **PERFORM knob effects stay on**: FILTER, CRUSH, THROW and DEPTH keep working after FX is let go or the PERFORM page
  closes, on every page, until turned back to 0 on the PERFORM screen (or TOOLS `INIT*`). An amber `FX` in the top
  bar shows while any is on. The black keys' effects still stop when let go.

### Projects, sound and settings

- **The last project loads at power-on**: every SAVE remembers its slot, and at power-on (not in a safe start) it
  loads, stopped. If it can't, `LOAD FAILED: SLOT n` / `START NEW PROJECT?`: YES forgets the slot, NO asks again at
  the next power-on; both carry on with the power-on kit.
- **Metronome**: GLOBAL 3/3 METRO OFF / LOW / MID / HIGH (a device setting, not in projects). While playing, a click
  on every quarter note, higher on the bar's first, after the PERFORM effects and at the MASTER volume, on the
  speaker, the headphones and USB audio.
- **CPU and PRIO**: voices are shed at 95 % of an audio block (was 85 %). PRIO per track (EDIT, after CHOKE): HIGH /
  LOW; a LOW track's voices go first at a shed and at the voice cap (a held ACID note still last). A red CPU icon
  shows in the top bar for 2 s after a shed.
- **Projects**: older projects load as before, every track PRIO HIGH; projects saved by this version can't be read
  by 0.16.0. A slot whose save to flash failed (its project only in memory) is not carried over.
- **Key backlight**: MENU BACKLIGHT OFF / ON (a device setting, default OFF). ON: every unlit key glows at about 5 %,
  and the unlit steps 1, 5, 9 and 13 of the bank shown at about 25 %, marking the beats on every screen where the keys
  are the selected track's steps (STEP, PATTERN, MOTION, SONG; HOME's TRACKS mode while no track is armed); lit keys
  stay full, the buttons unchanged.
- **Going back**: going back to an older Hortator with the last project, METRO or BACKLIGHT set: its SPEAKER EQ reads
  FLAT.
- **Bluetooth version: AUTOSTART** OFF / ON on the Bluetooth page (after BLUETOOTH; KNOB 1 right ON / left OFF, OCT+
  toggles; saved when the menu closes; default OFF). ON: 5 s after power-on, Bluetooth starts by itself as if
  BLUETOOTH ON were chosen, and reconnects to the remembered device. Not in a safe start (SEQ held). If the last
  Bluetooth start before this power-on did not complete or ended in an error, it is skipped and the top bar says
  `BT AUTOSTART SKIPPED`; BLUETOOTH ON by hand still works. Going back to an older Hortator with AUTOSTART ON resets
  its display settings (COLOR, SPEAKER EQ, ZOOM, KNOB ACCEL, USB LEVEL, PERFORM) to their defaults once.

## 0.16.0 - 2026-10-11

### ACID and notes

- **ACID**: every step holds a note, SLIDE and TIE; drum tracks play their steps transposed. ACID, a 303-style bass
  voice (TUNE DECAY CUTOF RESO ENVMD ACCNT WAVE GATE), plays them with accent, slide and tie on up to two tracks
  ("ACID: 2 TRACKS MAX"). Older projects load and play as before, but projects saved by this version can't be read by
  0.15.0.
- **KEYS** (an SCL tap): the keys play the selected track chromatically, OCT- / OCT+ the octave (-2 to +2; LAYER
  from EDIT and the LFO pages' OCT buttons need KEYS off, the STEP bank stays on KNOB 1, TRACKS keeps OCT as its
  bank). KEYS notes send no MIDI out; an ACID note (or a tied chain) over 5.5 s is cut.
- **Live record** (a REC tap on TRACKS arms the track, and starts the transport when stopped): a key writes the
  nearest step, a held key TIEs the later steps, a legato overlap SLIDEs (TIE / SLIDE on ACID tracks only).
- **Step record**: KEYS on, STEP page, stopped, a REC tap (KNOB 1..4 STEP NOTE TIME FLAG, EDIT a rest; REC, PLAY or
  leaving the page ends it).
- **Notes on steps**: a loaded project keeps ACID on its first two ACID tracks; the others return as their power-on
  kit drum. A step the white keys turn on takes the last KEYS note on an ACID track, C4 on a drum (the last note
  resets on LOAD and on clearing the pattern). An ACID track's STEP page is a piano roll, the note shows in the
  footer; ACID's SOUND page shows its filter and the note's shape.

### Start screen and releases

- **A new start screen, CRT COLLAPSE**: the HortatoR logo squashed through five dithered frames into one white scan
  line that bursts sideways, in the chosen COLOR. The logo, there and on ABOUT, is now UnifrakturMaguntia (SIL OFL).
- **Two versions per release**: the plain firmware and the same build with Bluetooth MIDI. The web installer offers
  a choice (the plain one by default), and the release has a second file, `hortator-X.Y.Z-bluetooth.fwsc`.

### Bluetooth MIDI (the Bluetooth version only)

- **BLUETOOTH**: MENU (HOME held) > BLUETOOTH turns Bluetooth on (off at every power-on), scans, lists the devices
  found (BLE-MIDI ones first) and connects to the one picked. It remembers it and reconnects whenever it is in range,
  also after a power-on once BLUETOOTH is ON; KNOB 1 left on DEVICE forgets it. MODE (saved): AUTO (SMC DAW for a
  device named `SMC…`, else MIDI), SMC DAW, MIDI. `BT CONNECTED` / `BT LOST` in the top bar.
- **The M-VAVE SMC-Mixer** in its DAW mode: faders 1-8 = the track levels (pickup: a fader acts once it reaches the
  level; the mixer blinks it until then), knobs 1-4 = the selected track's first four knobs, MUTE / SOLO / REC /
  SELECT per track, PLAY / STOP / REC, BANK / CHANNEL ◀ ▶ the track; its LEDs show the mutes, solos, armed tracks,
  the selected track and the transport. Fader and knob moves record as motion on the selected track while armed.
- **MIDI mode**: the device's notes on the drum channel play the tracks, as USB and TRS; GLO CLK BLE follows its
  clock (a project saved with CLK BLE loads with CLK INT in the plain version).
- **SOLO** (from the SMC-Mixer): while any track is soloed only the soloed ones are heard; not saved, cleared by
  INIT, and cleared (nothing cut) when the SMC-Mixer disconnects or MODE leaves SMC DAW, as the FM-1 has no unsolo.
  Leaving SMC DAW while connected turns the mixer's LEDs off.
- **Going back**: going back to an older Hortator after picking a device: its SPEAKER EQ reads FLAT.

## 0.15.0 - 2026-10-09

- **Bluetooth MIDI, first step**: a separate Bluetooth build (the normal firmware does not contain it) that can scan
  for and connect to Bluetooth MIDI devices, started from a computer over USB for now; off at every power-on. Tested
  on the FM-1 with the M-VAVE SMC-Mixer: connected and receiving, no audio glitches, and updates still work with
  Bluetooth on. Received MIDI doesn't play anything yet.
- **Safer builds**: every build checks that the FM-1's update loader stays the original one.

## 0.14.0 - 2026-10-08

- **FM**: a 2-operator FM percussion model (TUNE DECAY INDEX RATIO, MDEC SWEEP FBK VEL): bells, metallic toms,
  woodblocks, zaps, FM kicks; 2 voices; the SOUND graph shows the ratio's sidebands.
- **SEQ**: the white keys are the selected track's steps on every SEQ page (STEP, PATTERN, MOTION, SONG) and on TRACKS
  (tap on / off, hold accent, OCT+ / OCT- the bank); ALGO or TRACKS' KNOB 1 picks the track. TRACKS mutes: REC held +
  a key (+ top C# / D#: now / next bar), was OCT- held.
- **PERFORM**: the knobs are the macros whenever the PERFORM screen shows (a held layer key no longer let them edit
  the page under it).
- **FILTER**: a filter on every track (FX > FILTER: TYPE OFF LP BP HP NOT, CUT, RESO, ENV, DECAY), after RESON, before
  DIST; each hit sweeps it by ENV (accents further); LFO DEST F.CUT / F.RES. Older projects load with the filter OFF.
- **COWB**: TUNE now moves its pitch; before, it only moved the filter over fixed partials. At TUNE 0 it sounds as
  before.

## 0.13.1 - 2026-10-08

- **Recovery from a Mac with no extra hardware**: when the FM-1 stops answering both the web installer and
  M-UPGRADE, a rescue tool backs up its flash and puts M-VAVE's stock V15 firmware back through the chip's boot mode.
  The installer page and the project page link to the guide. No firmware change.

## 0.13.0 - 2026-10-08

- **PHYS** (Felucca 1.0's physical models, built on DaisySP): MEMB, a struck drum head as a drum model (TUNE DECAY
  TONE HEAD on HOME; POS, BEND, STICK; toms, timpani, tabla), and MODAL, a bell / bar / plate body as a RESON model
  (STRCT from harmonic to bell, DECAY its ring time as STRNG's; 2 tracks at most). The RESON MODEL knob now skips a
  model that is full. Projects load as before.
- **Logo**: the start screen and ABOUT show the HortatoR logo (UnifrakturCook Bold, SIL OFL); the "untested" labels
  are gone.

## 0.12.0 - 2026-10-07

- **SONG** (Felucca 1.0's song chain): SEQ > SONG plays up to 16 rows of {project A..D, repeat x1..x16} with the
  sounds loaded now (each row: that project's steps, LEN / DIV / SWING / SRC and motion; a row lasts its longest
  track; LOOP ON / OFF; STOP brings the current pattern back).
- **NAME** (Felucca 1.0's naming): projects get names (the PROJECT page's NAME knob opens the NAME screen: keys type
  phone-style, KNOB 1 / 2 cursor and character; SAVE saves as before); slots shown as A..D. Saved with the project;
  older projects load with no song and no name.

## 0.11.0 - 2026-10-07

- **MOTION** (Felucca 1.0's motion recording): with a track armed (REC on TRACKS) and playing, its sound knobs turned
  are recorded per step and played back with the pattern (a value holds until the knob's next event; the patch comes
  back at the loop start and on STOP; the LFOs keep modulating on top). SEQ > MOTION: PLAY ON / OFF, the event count,
  CLEAR. 128 events for the 8 tracks, saved with the project; older projects load without motion.

## 0.10.0 - 2026-10-07

- **PERFORM** (Felucca 1.0's FX hold layer): hold FX, then the black keys play master effects while held (REPEAT 1/8
  1/16 1/32, REVERSE, TAPE STOP, LPF, HPF, FREEZE, OCT UP, OCT DN), the white track keys mute their track while held,
  KNOB 1..4 are FILTER / CRUSH / THROW / DEPTH (SHIMMER with OCT); a tap on FX still opens the FX pages (now on the
  release). MENU > PERFORM PAGE: holding FX opens a PERFORM screen that stays (keys and knobs work without FX) until
  another screen is chosen. REPEAT / REVERSE start on the 1/16 (the MIDI clock's when following); the buffer is the
  slicer's (743 ms). Nothing changes when the layer is idle.

## 0.9.0 - 2026-10-07

- **MIDI clock in** (from Felucca 1.0, adapted from contributions by ChanceTheMaker and keremimo): GLOBAL CLK INT /
  USB / TRS; the steps (every division), Grids, the slicer, synced LFOs and the delay follow the source's tempo, and
  its Start / Continue / Stop drive the transport (step 0 on the first pulse; stops if the clock stops for 0.5 s).
- **Exact step timing**: divisions never drift against each other (e.g. a 1/16 and a 1/4 track at 120 BPM) or
  against an external clock.

## 0.8.0 - 2026-10-07

- **USB audio input** (from Felucca 1.0): the FM-1 records into a DAW over its USB cable (16-bit stereo, 44.1 kHz;
  the headphone signal, SPEAKER EQ included). MENU USB LEVEL: MASTER (the recording follows the volume knob) or FIXED
  (always full level; the knob sets only the speaker / headphones).

## 0.7.0 - 2026-10-07

- **Sound pack** (from Felucca 1.0.2): MENU SPEAKER EQ FLAT / LOWCUT / BASS+ (the bass heard through its harmonics
  on the small speaker); SPRING reverb (shown SPRNG) beside ROOM on the new REVERB page (TYPE SIZE DAMP; chorus on its
  own CHORUS page); slow divisions 1/2, 1/1, 2BAR, 4BAR on PATTERN DIV and delay TIME, division knobs ordered by
  length. Older projects load with ROOM.

## 0.6.0 - 2026-10-07

- **TRS MIDI input** on by default (from Felucca 1.0): notes from the jack play like USB's.

## 0.5.0 - 2026-10-06

The first numbered version.

- **Drum machine on Felucca**: 8 tracks, drum models (808 / 909 kicks and snares, samples, FM and more), step
  sequencer with per-track length, swing, live recording, quick mutes, MUTE NEXT BAR.
- **Steps and patterns**: step PROB / RATCH, and Grids (Mutable Instruments) as a pattern generator for a track.
- **Compressor**: Streams compressor with sidechain ducking keyed by a track, COMP GHOST (MUTE / KEEP / HIDE).
- **LFOs**: two per track (10 waves with morph, SYNC / Hz / TIME rates, one destination each).
- **RESON**: a resonator insert per track (STRNG / PIPE / CHORD).
- **Pages**: the PRESET knob turns the current section's pages; the EDIT / COMP / FX pages regrouped.
- **From Felucca 1.0-1.0.2**: calmer knob acceleration, a swing cap, STOP TO SAVE, installer resume; key debounce and
  a knob fix; the USB MIDI driver; safer saving (no truncated loads) and saves that keep the sound going.
- **Projects**: older projects are converted on load.
