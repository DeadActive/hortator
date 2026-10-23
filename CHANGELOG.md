# Changelog

The drum firmware's versions (docs/VERSIONING.md). Newest first; notes go under "Unreleased" as work is merged to
`main`, and `python3 tools/version.py bump patch|minor|major` dates them with the new version.

## Unreleased

- Knobs crank faster: MENU > ACCEL ON (the default) now speeds a turn up from about 12 clicks a second, in fractions
  of a step (x2 at 20 clicks/s, x4 at 40, up to x8 on a wide value: upstream Felucca 1.1.5's caps, 276f72a), so a
  0..127 value takes ~65 clicks at 20/s and ~24 at 60/s (before, and upstream Felucca: 127 and 64). A slow turn and
  single clicks stay one step a click; a reversal's first click is one step. Lists of names (MODEL, TYPE ..) never
  speed up (as upstream), the PRESETS list at most x4; ACCEL OFF stays one step a click (tests/accel_test.c).
- USB name: the FM-1 now shows up as **Hortator** (USB MIDI and audio: in a DAW, AUM, the MIDI port list) instead of
  Felucca: the app's USB product string (firmware/src/usb_app.c STR2, 18 B "Hortator"; user decision 2026-10-23,
  frozen baseline moved to `frozen-base-9` for that one string). While an update is written it still shows up as
  **Felucca Update** (the update loader, unchanged); the manufacturer string, VID/PID and the loader are unchanged.
  The web installer, the editor, `tools/fm1_install.py` and `tools/fm1_sample_upload.py` find a port named Hortator
  or Felucca (older firmware), so updating from any earlier build works as before.
- MENU > COLOR: every colour now comes in both polarities, BLUE INV (blue on white) and GREEN INV (green on black)
  join RED INV (red on black); the row shows the theme's name as its value (RED, RED INV, BLUE, BLUE INV, GREEN,
  GREEN INV, in that order) instead of the boxes. A stored colour loads as the same theme as before (the new
  inverses take new indexes; an older Hortator shows them as RED).
- Step keys: holding a step no longer turns it off. A step toggles only on a short tap (let go within 0.3 s, no knob
  edit meanwhile), on the release. Held 0.3 s an OFF step turns on there and then (lit, ready to edit); an ON step
  stays exactly as it was. While a step is held on STEP, KNOB 1 / 2 / 3 set its PROB / RATCH / ACCENT (KNOB 3: a click
  right ACC on, left off), shown in the knob boxes and the `STEP n PROB RATCH n ACC` line (once held 0.3 s or edited).
  OCT+ held + a step key still flips the accent at the press. Every page whose white keys are the steps (STEP,
  PATTERN, MOTION, SONG, TRACKS).
- RESON KEYF (KEYFOLLOW, RESON 2/2 KNOB 4, 0..100 %, 0 % = off, the default): the resonator's pitch follows the note of
  each hit from C4 (C4 = TUNE): a step's NOTE, a KEYS note, a ratchet's; at 100 % a step at C5 rings an octave above
  TUNE, at 50 % half an octave (MIDI notes play their track at C4: TUNE). Every model (STRNG, PIPE, CHORD, MODAL). A
  new note while the ring sounds glides the line there (a semitone a block at most, the line read per sample between
  lengths: no click). An amount rather than ON / OFF: partial tracking keeps a ring in a narrower range under a wide
  melody. KEYF 0 renders bit for bit as before. MIDI LEARN (`R.KEYF`) and MOTION take it. Projects stay "FDRE" at
  3840 B (the sector is full): KEYF rides in each track's first step's spare byte, which every earlier firmware wrote
  as 0 and ignores, so older projects load with KEYF off and a project with KEYF loads on 0.23.2 without it.
- MUTE: NEXT BAR works while following a clock (CLK USB, TRS or BLE): the waiting mutes now flip on the bar line
  instead of never. The bar is found from the clock's real advance in the block (read after the block's Clock), not
  the block's length; a muted COMP source (GHOST KEEP / HIDE) starts its 5 ms fade on the bar's last Clock pulse, so
  the fade ends before the bar's first hit. CLK INT is unchanged.
- Knobs: a quick turn no longer skips clicks. The frozen input HAL (upstream Felucca 1.0) took a contact state only
  when two scan frames in a row saw it, so a knob snapping through its clicks lost them from ~40 clicks/s (a flick
  counted next to nothing). The knobs are now decoded by upstream Felucca 1.1.5's decoder (276f72a, issue #126: every
  frame counts, a jump carries on the way of the last click) in the drum firmware's own code (firmware/src/enc.c, run
  by the scan's hook once a frame; the frozen files are untouched): every click counted up to ~300 clicks/s, still one
  step a click, bounce and a still knob's chatter ignored (tests/enc_test.c).

## 0.23.2 - 2026-10-23

- REC: on TRACKS a REC tap arms / disarms the selected track, saying so (`T3 REC ARMED` / `T3 REC OFF`). On every
  other page a REC tap is REC ALL: every track armed (`REC ALL`, the REC LED and the SMC-Mixer's REC 1-8 lit), so each
  white track key plays and records its own track wherever the keys play tracks (HOME, SOUND, FX, TRACKS ...):
  finger-drum a whole kit live. KEYS, step record and knob motion stay the selected track's. A REC tap off TRACKS ends
  it and arms again the tracks armed before (`T3 REC ARMED` / `REC OFF`); on TRACKS it ends it with the selected track
  armed. Step record on STEP, REC held and `STOP TO EDIT` while a song plays are unchanged; a learned REC and the
  SMC-Mixer's REC act as the button on the page shown; the SMC-Mixer's REC n during REC ALL ends it and disarms track n
  (the others stay armed). Not saved in projects, as the arm state.
- REC no longer starts play: a REC tap (and REC ALL, a learned REC, the SMC-Mixer's REC and REC 1-8) only arms /
  disarms; PLAY starts the transport, and an armed track records once playing. Step record is unchanged.

## 0.23.1 - 2026-10-22

- Web site only (the firmware is the same as 0.23.0): the landing page and the installer count visits and installs
  anonymously with GoatCounter (deadactive.goatcounter.com; no cookies, no personal data); the hero shows "Installed N
  times" under the install button. The install is counted once, after the installer says Done, and never holds it up.

## 0.23.0 - 2026-10-22

The first published release since 0.16.0: it also brings everything in 0.17.0 .. 0.22.0 (CHANGELOG.md): the MD
bank's 21 voices (13 TRX, 8 EFM), 151 factory presets and 128 user preset slots, the default TRX kit, the 4-slot LFO
routing, accents on OCT+ and a step, the Machinedrum-style screen and its faster redraws. Projects saved by 0.23.0
don't load in older versions.

- SOUND pages: every engine as FM, ACID and BDTRX: the name on the left, its number "#NN/45" under it, VOICES, CHOKE
  and "+ LAYER" under that, a graph on the right. The engines with no graph of their own show their rendered hit: the
  voice's real sound (one hit at velocity 100, played silently in the background, never touching what you hear) as a
  waveform, its length labelled ("HIT / 300 MS", 100 .. 1000 ms, following the sound). It draws again when MODEL or a
  SOUND knob changes, over a few frames (the old picture stays until the new one is done); SAMPL shows its sample.
- Engines renamed (the names shown only: projects, presets and MIDI unchanged). MD bank: KTRX -> BDTRX, XTTRX -> TMTRX,
  XCTRX -> CGTRX, MATRX -> SHTRX, XTEFM -> TMEFM. MODERN bank: KBOOM -> BOOM, KPUNC -> PUNCH, SSNAP -> SNAP,
  SCRAK -> CRACK, HMETL -> METAL, HNOIS -> HISS, MEMB -> SKIN, SMPL -> SAMPL. User preset slots saved from them are
  named after the new names ("BOOM 07").
- The power-on kit (also TOOLS INIT ALL and a new project) is the TRX classic kit: KTRX, SDTRX, CPTRX, CHTRX, OHTRX,
  XTTRX, RSTRX, CYTRX, each on its default preset, with the same roles and MIDI notes as before (kick 36, snare 38,
  clap 39, closed hat 42, open hat 46, tom 45, rim 37, cymbal 49) and no choke. Saved projects load unchanged.
- HOME: the 4 boxes above PROJ show the bar of a 4-bar cycle: the current bar solid, moving with the playhead, back to
  the first at every PLAY (stopped: the first). They used to show the selected track's banks.
- The MODEL knob's list runs MD, MODERN, CLASSIC (KTRX .. CYEFM, KBOOM .. SMPL, K808 .. CLAVE), and the preset banks
  MACHINEDRUM, MODERN, CLASSIC, USER. Models keep their numbers and presets their uids, so older projects load with
  the same voices and presets.
- The SOUND pages show the engine's place in that list under its name, "#01/45" (KTRX) .. "#45/45" (CLAVE).
- LFO: the OCT- switch between LFO 1 and LFO 2 is gone. The LFO pages show both LFOs side by side (LFO 1 on KNOB 1-2,
  LFO 2 on KNOB 3-4): WAVE RATE, MORPH PHASE, TRIG MODE (the RATE MODE now has a knob; OCT+ still switches the one of
  the LFO turned last), and a last page, DEST: 4 slots per track, each a source (LFO 1 / LFO 2), a destination (any
  knob an LFO could target) and its own depth (KNOB 1 the slot, 2 SRC, 3 DEST, 4 DEPTH). Older projects load with
  their LFO 1 / LFO 2 routing as slots 1 / 2 (the sound, motion and MIDI LEARN unchanged); projects are saved in a new
  format (FDRE) that 0.22.0 cannot load.
- Accent: hold OCT+ and tap a step key (STEP, PATTERN, MOTION, SONG, TRACKS): the step accented (an off step turns on
  accented), or plain again. Holding a step key no longer sets the accent, so a held step is only PROB (KNOB 1) and
  RATCH (KNOB 2). OCT+ on its own changes the bank when it is let go; while it is held the keys show the bank's
  accents. The STEP hint reads `[OCT+] +TAP ACCENT`.
- The selected track on the keys: on HOME and every EDIT (SOUND, LAYER), FX and LFO page its white key (F3..F4) is
  lit, as the keys select tracks there (held keys lit too). KEYS, COMP, the step pages and GLOBAL / SAVE / GRIDS keep
  their own.
- OCT- / OCT+ breathe (upstream Felucca's breathing LED, #119): fading in and out, ~1.1 s, where pressing them would
  do something (the next / previous bank on the steps, LAYER from SOUND, the LFO pages' RATE MODE (OCT+), YES in a
  dialog, OK in the menu, NAME's RENAME, MIDI LEARN's DONE, the PRESETS list's LOAD and DELETE); a way back (NO,
  CANCEL, BACK) is lit.
- PERFORM keeps its knobs: while its screen is up (FX held, the PAGE, a layer key held) KNOB 1-4 are its macros only,
  also for detents that arrive while the firmware is between its two knob reads (they used to reach the page under
  it), and SELECT turns no page under it (a page button, HOME or an FX tap still close the PAGE).
- MENU BACKLIGHT has upstream Felucca's LED levels: OFF, DIM LO (the old ON: unlit keys and now the idle buttons at
  ~5 %, the step beats ~25 %), DIM HI (~10 %, beats ~30 %), INV (idle lit, active dark), INV LO / INV HI (idle at
  ~10 % / ~25 %). KNOB 1 steps, OCT+ steps and wraps. Saved in the settings record's palette word (bits 9..11); an
  older Hortator reads any level but OFF as ON, and an older record's ON loads as DIM LO.
- MENU > ABOUT credits everyone whose work is in the firmware, its research and its pages (Felucca, Mutable
  Instruments, DaisySP, the samples, the recovery tool, the web fonts and icons, the emulator the MD voices were
  measured from, with a disclaimer). The credits scroll under the logo with SELECT, a row a detent, with a scroll
  bar at the right; ABOUT opens at the top. LICENSING.md lists DaisySP (MIT, LICENSES/MIT-DaisySP.txt) and the
  research; README and the site's footer carry the same credits.

## 0.22.0 - 2026-10-21

- Faster screen: only what changed is sent to the display (the same look, pixel for pixel). A message popup (an
  SMC-Mixer knob's value, LOADED, ...) and the TEMPO popup no longer repaint the whole screen when they appear,
  change or end: only the popup and what lay under it. The breadcrumb sends only its text, the trig strip only the
  playhead's step, TRACKS only a row's level, meter mark or playhead cell, HOME's strip only the knob that moved
  (its LFO's dot alone). HOME's scope waits while a knob turns. A full repaint sends the screen once (it sent ~1.6
  screens). E.g. an SMC knob click on TRACKS: ~6k pixels (~8 ms at the display's 12 MHz) instead of a full repaint
  (~99k, ~130 ms); TRACKS playing: ~0.3k pixels a frame instead of ~13-29k.

- A new screen look in the style of a classic digital drum machine's LCD, on every screen: two colours only (ink on a
  backlight; dither, dotted rules, inverted bars and title tabs instead of grey tones), a square pixel font drawn at
  2x with no smoothing, round knob dials with their values under them, shadowed popups. Layout: an inverted top bar,
  a compact knob row, the graph area, a trig strip with gaps between steps, and a breadcrumb (`T1>K909>SOUND`).
  HOME: a main-screen strip under the top bar, four small knobs with values below and a large scope; TRACKS keeps
  everything it shows on dithered level boxes. MENU > COLOR now cycles four themes: MD RED (the default), RED ON
  BLACK, WHITE ON BLUE and BLACK ON GREEN (old colour settings map to the nearest). A new 1-bit logo on BOOT and
  ABOUT. The PRESETS popup and list, MENU, NAME, the Bluetooth page and every popup in the same style. Nothing that
  was on a screen before is lost.

## 0.21.0 - 2026-10-19

- Presets: a preset is a voice (MODEL and the 8 SOUND knobs), in banks MACHINEDRUM, CLASSIC, MODERN (151 factory
  presets, below) and USER (128 slots in flash). The PRESETS encoder loads the next / previous preset onto the
  selected track at each detent, through every bank (empty USER slots and a capped ACID skipped), with a popup over
  the graph (the bank, the preset, two before and after; ~1.5 s). EDIT held ~0.4 s opens the PRESETS list (an EDIT tap
  and step record's EDIT as before; seven presets on screen): KNOB 1 / PRESETS move, KNOB 2 / SELECT the bank, and
  every move plays the preset under the cursor once on the selected track, which keeps that sound while the list is
  open (its key and the pattern play it). OCT+ LOAD keeps it and closes the list; EDIT or HOME closes it with the
  track's own sound back (preset and `*` too). SAVE stores the track's own sound (not the auditioned one) into the
  first free USER slot or the chosen one (a used one: SAVE twice), OCT- twice DELETE. USER presets are
  named from the voice and the slot ("SDTRX 03"); saved while playing they are written to flash at the next STOP (as
  MIDI LEARN's map; its own flash object). The SOUND pages show the track's preset ("PRESET HOUSEBD") with `*` once a
  SOUND knob moved (one already at its end turned further: no `*`), TRACKS' footer too; a track whose USER slot now
  holds another voice (or none) shows `-`. Projects keep each track's preset and mark ("FDRD": a factory preset by
  its own fixed number, never its place in the banks, so later banks can grow; a USER one by its slot; older projects
  load with no preset, their sound unchanged). SAVE while MOTION plays stores the knobs as set, not motion's values.
  While MIDI LEARN is on, EDIT held does nothing (the list would end LEARN).
- The factory banks: MACHINEDRUM 55 presets read from the Machinedrum's factory kits (1 to 4 per voice; none for
  MATRX, CLTRX), CLASSIC 56 and MODERN 40 (4 per voice, ACID and SMPL included). Every voice's default sound is its
  first preset: unchanged for CLASSIC and MODERN, new for the 19 Machinedrum voices with presets (KTRX SUBTHUMP, SDTRX
  HARDSNAP, CPEFM BUZZCLAP, ...; the list in docs/DEVICE_INSTALL.md). Generated from tools/presets.json by
  tools/gen_presets.py.
- No voice has a default choke group any more: HATC, HATO, HMETL, HNOIS, CHTRX, OHTRX and HHEFM used to start in group
  1. CHOKE is now only the track's own setting: a MODEL change or a preset load never changes it, so the power-on
  kit's HATC / HATO no longer cut each other until CHOKE is set on both. Older projects keep their stored CHOKE.

## 0.20.0 - 2026-10-19

- BDEFM, XTEFM: an FM bass drum and an FM tom measured from a classic digital drum machine's (an emulator's renders,
  knob by knob; only the measured laws, no code or data of it). One voice, two measured sets: two sine operators in
  true FM (the carrier's frequency is modulated), one pitch envelope on both (RAMP: the start pitch x RAMP / 16, back
  to the pitch over RDEC; RAMP 16 flat, below it the pitch rises, from 0 Hz at RAMP 0), the deviation MOD / 16 of the
  carrier falling over MDEC, the modulator at MFRQ / 16 of the carrier, then the machine's 21 Hz high-pass. BDEFM TUNE
  DECAY RAMP RDEC, MOD MFRQ MDEC MFB (MFB the modulator's feedback: grit from ~40, noise at 127). XTEFM TUNE DECAY RAMP
  RDEC, MOD MFRQ MDEC CLIC: the tom's pitch, its 121 Hz high-pass, CLIC the start phase (a click at 127). XTEFM's FM
  is an approximation (the machine's own FM is not understood; a chaotic modulator gives its kind of grit, not its
  exact tone); with MOD 0 XTEFM is exact. TUNE 0 is the machine's PTCH 64 (102.6 Hz, 86.1 Hz); the defaults are the
  measured neutral sounds (RAMP 0: the pitch rises from 0 Hz). DECAY 107..127 is its end (0.58 s, 4 s to -60 dB, as
  every voice; the machine's goes on to ~1.6 s). Levels as K909, TOM. 1 voice each. After B2TRX in the MODEL list.
- SDEFM, CPEFM, RSEFM: a snare, a clap and a rim shot measured from a classic digital drum machine's FM family (an
  emulator's renders, knob by knob; only the measured laws, no code or data of it). Each is a sine whose frequency
  is modulated by noise; that FM noise is the same on every hit, as the machine's (SDEFM's NOIS layer is random per
  hit). SDEFM TUNE DECAY NOIS NDEC, MOD MFRQ MDEC HPF: a 290 Hz carrier, MOD the noise FM's depth (plus a weak sine
  at MFRQ), MDEC its decay; NOIS / NDEC a separate noise burst; HPF a one-pole high-pass. CPEFM TUNE DECAY CLPS
  CDEC, MOD MFRQ MDEC HPF: the same FM on an 820 Hz carrier restarted CLPS / 8 times (none below 8) at CDEC's
  measured period (1 .. 41 ms), then DECAY. RSEFM TUNE DECAY MOD HPF, SNAR SPTC SDEC SMOD: a 976 Hz rim and a
  second "snare" voice (SNAR its level, 0 = off; SPTC its pitch), each noise-modulated (MOD, SMOD); HPF at the
  measured corners. Every DECAY follows the machine's law to its 1.6 s end; past 5.5 s every voice fades out (the
  6 s lifetime). TUNE 0 is the machine's PTCH 64; TUNE and a step's note move every pitch by semitones (RSEFM's
  second voice too: the machine's PTCH moves only the rim). 1 voice each. After B2TRX in the MODEL list.
- CBEFM, HHEFM, CYEFM: an FM cowbell, hat and cymbal measured from a classic digital drum machine's FM voices (an
  emulator's renders, knob by knob; only the measured laws, no code or data of it). CBEFM TUNE DECAY SNAP FB, MOD MFRQ
  MDEC: two tones 1.4805 apart, each a true-FM pair (the deviation MOD / 32.65 falling over MDEC, the modulator at MFRQ
  / 16 of its tone), phase feedback on the carriers (FB), SNAP a fast extra decay (4.5 ms) that lowers the rest of the
  hit; fixed start phases, as the machine's. HHEFM TUNE DECAY TREM TFRQ, MOD MFRQ MDEC FB: one true-FM pair whose
  carrier jumps to a random phase every 32 samples, then a near-differentiator (the hat's sizzle), FB fed into the
  carrier's phase accumulator (it stalls the tone, then turns chaotic), a tremolo restarting at each hit; choke group 1
  (as HATC). CYEFM TUNE DECAY FB HPF, MOD MFRQ MDEC: four PM pairs at 1 / 1.41 / 1.80 / 2.70 x the base, the index (MOD
  + 7) / 13.5 falling over MDEC (0.28 .. 11 s), phase feedback on the carriers, HPF two one-pole high-passes (431 ..
  3216 Hz, off at 0), the sidebands past Nyquist aliased as the machine's are; 2 voices. TUNE 0 is each machine's PTCH
  64 (409.7, 577, 580 Hz); DECAY the family's law (tau 64.6 ms x 2 ^ ((DECAY - 64) / 13.5); 127 fades out from 5.5 s).
  Levels with COWB, HATC, CYMB. After B2TRX in the MODEL list.
- The EFM voices' DECAY: BDEFM and XTEFM end at 0.58 s (Hortator's longest decay, as KTRX: a faded sub-bass tail
  sounds worse than a short one); SDEFM CPEFM RSEFM CBEFM HHEFM CYEFM follow the machine's whole 1.6 s law and fade out
  from 5.5 s at DECAY's top (the voices' 6 s life).

## 0.19.0 - 2026-10-18

- KTRX: a sine kick measured from a classic digital drum machine's (an emulator's renders, knob by knob; only the
  measured laws, no code or data of it). TUNE DECAY SWEEP SDEC, PUNCH NOISE SUB CLIP: the pitch with its built-in
  drop, SWEEP's cubic rise over SDEC, a short transient at the hit; PUNCH a square first cycle; NOISE a burst; SUB
  half-harmonics (alternate cycles long and short, fading on their own); CLIP a hard clip at a falling threshold.
  DECAY and SDEC follow the measured laws, floors and ends included: DECAY 0..27 is its 7 ms floor and 109..127
  its end (4 s to -60 dB, as every voice); SDEC 0..14 its 5 ms floor, 107..127 the same end. 1 voice; the SOUND graph shows
  the pitch and the level over 300 ms. After ACID in the MODEL list.
- SDTRX: a snare measured from the same machine's (only the measured laws). TUNE DECAY SNAP TONE, BUMP BENV CLIP:
  two sines frozen for the machine's first 32-sample block, A at 342 Hz with BUMP's alpha-shaped pitch blip (its time
  BENV's), B a block-rate comb whose F1 / F2 BENV sets from the measured table (erratic, as the machine's); TONE
  crossfades them; CLIP a peak-preserving tanh whose drive falls on its own (~28 ms), whatever DECAY is; SNAP the same
  noise on every hit (1.6..5 kHz, tau 20 ms; above 64 a 1.44 kHz ring). TUNE moves both sines by semitones (the
  machine's own PTCH, +-0.8 %, and TUNE, a few Hz on B, are left out). 1 voice.
- B2TRX: the same machine's second bass drum (only the measured laws). TUNE DECAY RAMP HOLD, TICK NOISE DIRT DIST: a
  50 Hz sine (TUNE 0; it starts at 0 on every hit, the machine's runs free), RAMP's added pitch in Hz (the same on
  every note), HOLD at full level before DECAY's fall (up to seconds; shortened when a long DECAY would outlast the
  voice's 6 s), TICK a click into the output's saturation, NOISE a hiss, DIRT a 3..257-level quantiser and DIST a hard
  clip before the envelope; the output's high-pass and smoothing as measured. 1 voice.
- XTTRX, XCTRX, CLTRX, RSTRX: a tom, a conga, claves and a rim shot measured from the same machine's (only the
  measured laws, no code or data of it); 1 voice each. XTTRX / XCTRX (one voice, two
  measured sets) TUNE DECAY RAMP RDEC, DAMP DIST DTYP: a fixed click, then a sine whose RAMP adds Hz falling over
  RDEC; DAMP moves level from the body to the click; DIST a hard clip after the decay, DTYP making the clipped part
  sag; the tail cut to zero as measured (XTTRX's DECAY 114..127 is its 0.85 s end: the voice ends by 5.4 s).
  CLTRX TUNE DECAY DUAL ENH, TUNE2 CLICK: two pure sines (TUNE2 the second's pitch, TUNE moving both) at their
  measured levels; DUAL delays the first; ENH raises it; CLICK a short click. RSTRX TUNE DECAY DIST: a sine and a
  fixed 18 ms click under one decay; DIST warps the sine's half-cycles (stronger at low pitch). TUNE 0 is each
  machine's PTCH 64 (145, 173, 2045, 258 Hz); the other knobs are the machine's own.
- CPTRX and MATRX: a clap and maracas measured from the same machine (averaged renders: the voices are noise). Their
  own noise per hit (a hit renders the same every time). CPTRX: TUNE DECAY CLPY HARD, RATE ROOM RSIZ RTUN: noise
  through a 4-pole high-pass (TUNE: the machine's TONE in semitones) and a resonant low-pass (HARD), four bursts on
  one noise (RATE their spacing, DECAY their decay, CLPY the first three's level), a soft clip the clap never passes,
  and ROOM a resonant noise tail after the clip (RSIZ its length, RTUN its colour; 0: off). MATRX: TUNE DECAY ATT
  REV, DAMP RATL TONE HARD: noise in 32-sample grains (DAMP: more of them silent) through a 4-pole high-pass (TONE,
  HARD; TUNE moves it by semitones), a rattle that grows through the hit (RATL), a hold (DECAY: 14 ms + 1.45 ms a
  step) after an attack (ATT), REV's dip and second hold, then a cut. 1 voice each.
- CBTRX, CHTRX, OHTRX, CYTRX: a cowbell, closed and open hats and a cymbal measured from a classic digital drum
  machine's (an emulator's renders, knob by knob; only the measured laws, no code or data of it). The hats and the
  cymbal share one metal bank: seven oscillators with whole-sample periods (other phases at every hit). CHTRX / OHTRX:
  TUNE DECAY GAP HPF LPF MTAL (MTAL from noise to metal; DECAY a linear hold, GAP the decay after it; OHTRX's long
  hold dips to silence and comes back, as the machine's); choke group 1, as HATC / HATO. CYTRX: TUNE DECAY RICH TOP
  TTUN SIZE PEAK (RICH the oscillators' balance, PEAK a 5.2 kHz bell, SIZE the body's corners, TOP / TTUN a brighter
  top that fades first; 2 voices). CBTRX: TUNE DECAY ENH DAMP TONE BUMP PTCH (two tones, ENH's resonator and grit,
  DAMP a narrow band-pass steady from the first sample, a hold floor). Levels as HATC, HATO, CYMB, COWB. After KTRX in
  the MODEL list.
- The new voices' place in the MODEL list, after ACID: KTRX SDTRX XTTRX XCTRX CPTRX RSTRX CBTRX CHTRX OHTRX CYTRX
  MATRX CLTRX B2TRX.

## 0.18.0 - 2026-10-14

- MIDI LEARN: up to 16 CCs from any controller, each set to one knob of one track. Hold GLO (the MUTE screen) and
  press G4 to turn it on; turn a knob of the selected track (HOME, EDIT, LAYER, FILTER, FX, SLICER, RESON, LFO,
  PATTERN, TRACKS' LEVEL / LEN / PAN), then move the controller's knob: `CC74 -> T3 DECAY`. The footer shows what is
  picked and its CC. The CC moves that track's knob whichever track is selected, from USB, TRS or Bluetooth in MIDI
  mode (not SMC DAW mode), on any channel, over the knob's whole range (centred knobs 0 at 64, lists by step); an
  LFO on the knob keeps modulating around it. One CC per knob, one knob per CC; CC 0, 32 and 120..127 are never
  learned. While on, OCT- clears the picked knob's CC (nothing picked: `CLEAR ALL LEARNED?`), OCT+ ends it. Saved in
  the FM-1's flash (its own record, not in projects), after each change once stopped; off at power-on.
- MIDI LEARN: actions and 32 controls. While LEARN is on, PLAY, REC, ALGORITHM right / left (NEXT / PREV TRACK),
  FX + a black key (a PERFORM effect) and FX + a white key F3..F4 (that track's PERFORM mute) pick their action
  instead of doing their job; a PERFORM knob (FILTER CRUSH THROW DEPTH) picks its macro. A CC or a note is bound to
  an action (`NOTE36 -> PLAY`, `CC22 -> FX R1/16`), a CC to a macro (`CC20 -> FX FILTER`, 0 at 64). A press (a CC
  rising to 64 or more, a note on) taps PLAY / REC or steps the track; a PERFORM effect or mute is held while the
  controller's button is (as its key, FX not held) and ends on release or when the menu, a confirm, NAME or the
  MUTE screen comes up. A learned note plays no drum. 32 bindings (was 16); the saved 16 load as they were.

## 0.17.0 - 2026-10-13

- SELECT turns the pages: SELECT does what PRESETS did (a section's pages, HOME 1/3 to COMP 2/3 and 3/3, the page
  under PERFORM, the menu's cursor, the Bluetooth page's rows), except on HOME's TRACKS mode, where it sets the
  tempo. PRESETS does nothing; ALGORITHM still picks the track.
- HOME has two modes: TRACKS (the mixer: TRACK LEVEL LEN PAN, the steps on the keys) and HOME (the scope, the
  selected track's knobs, the compressor after it). A HOME tap on HOME switches between them, from elsewhere it
  opens the one last shown; power-on shows TRACKS. REC no longer opens TRACKS.
- REC arms on every screen: a REC tap arms / disarms the selected track (arming while stopped starts play; `STOP
  TO EDIT` during a song). Kept: step record on STEP with KEYS on and stopped, and REC held for CLEAR TRACK on SEQ,
  DELETE ROW / CLEAR SONG on SONG. REC is no longer the mute modifier.
- The MUTE screen (GLO held): the eight tracks as boxes, filled = heard, outline = muted, a waiting mute blinking,
  the key under each; the white keys F3..F4 toggle them, the top C# / D# set MUTE: NOW / NEXT BAR. A GLO tap still
  opens GLOBAL. Replaces REC held + a key.
- The last project loads at power-on: every SAVE to flash remembers its slot; at power-on (not in safe start) it
  loads, stopped. If it can't, `LOAD FAILED: SLOT n` / `START NEW PROJECT?`: YES forgets the slot, NO asks again at
  the next power-on; both carry on with the power-on kit.
- PERFORM knob effects stay on: FILTER, CRUSH, THROW and DEPTH keep working after FX is let go or the PERFORM page
  closes, on every page, until turned back to 0 on the PERFORM screen (or TOOLS `INIT*`). An amber `FX` in the top
  bar shows while any is on. The black keys' effects still stop when let go.
- Metronome: GLOBAL 3/3 METRO OFF / LOW / MID / HIGH (a device setting, not in projects). While playing, a click on
  every quarter note, higher on the bar's first, after the PERFORM effects and at the MASTER volume, on the speaker,
  the headphones and USB audio.
- CPU: voices are shed at 95 % of an audio block (was 85 %). PRIO per track (EDIT, after CHOKE): HIGH / LOW, a LOW
  track's voices go first at a shed and at the voice cap (a held ACID note still last). A red CPU icon shows in the
  top bar for 2 s after a shed.
- Project format FDRC (for PRIO): older projects load as before, every track PRIO HIGH; projects saved by this
  build can't be read by 0.16.0. Project data only in RAM (a slot whose flash save failed) is not carried over.
- Key backlight: MENU BACKLIGHT OFF / ON (a device setting, default OFF). ON: every unlit key glows at about 5 %,
  the unlit steps 1, 5, 9 and 13 of the bank shown at about 25 %, marking the beats on every screen where the keys
  are the selected track's steps (STEP, PATTERN, MOTION, SONG; HOME's TRACKS mode while no track is armed); lit keys
  stay full, the buttons unchanged. The LEDs are dimmed by the 10 kHz key scan: PWM over 20 scan frames (22 ms),
  25 % lit every 4th frame (~230 Hz), 5 % in 1 frame of 20 (~45 Hz).
- Device settings: the last project, METRO and BACKLIGHT are kept in spare bits. Going back to an older Hortator with
  any of them set: its SPEAKER EQ reads FLAT.
- Bluetooth version: the Bluetooth page gains **AUTOSTART** OFF / ON (after BLUETOOTH; KNOB 1 right ON / left OFF,
  OCT+ toggles; saved when the menu closes; default OFF). ON: 5 s after power-on, Bluetooth starts by itself as if
  BLUETOOTH ON were chosen, and reconnects to the remembered device. Not in a safe start (SEQ held). If the last
  Bluetooth start before this power-on did not complete or ended in a fatal error, it is skipped and the top bar says
  `BT AUTOSTART SKIPPED`; BLUETOOTH ON by hand still works. Kept in the palette word's bit 8 of the settings, by both
  versions: going back to an older Hortator with AUTOSTART ON resets its display settings (COLOR, SPEAKER EQ, ZOOM,
  KNOB ACCEL, USB LEVEL, PERFORM) to their defaults once (its COLOR check).
- Frozen baseline moved to `frozen-base-8` (user decision 2026-10-12): the frozen `main.c` gains one line, the key
  backlight's hook in the TIMER5 scan ISR right after `fm1_input_tick()` (`led_dim_tick()`, `firmware/src/led_dim.c`),
  the only way to dim LEDs while the audio ISR renders. `frozen-base-8` = `frozen-base-7` + that line + the file it
  calls; gate 2a accepts `main.c` differing from `drum-v0.16.0` by exactly that line (nothing else).

## 0.16.0 - 2026-10-11

- ACID: every step holds a note, SLIDE and TIE (format FDRB; older projects load and play as before, but projects
  saved by this build can't be read by 0.15.0); drum tracks play their steps transposed; ACID, a 303-style bass voice
  (TUNE DECAY CUTOF RESO ENVMD ACCNT WAVE GATE), plays them with accent, slide and tie on up to two tracks ("ACID: 2
  TRACKS MAX"). KEYS (an SCL tap): the keys play the selected track chromatically, OCT- / OCT+ the octave (-2 to +2;
  LAYER from EDIT and the LFO pages' OCT buttons need KEYS off, the STEP bank stays on KNOB 1, TRACKS keeps OCT as its bank); KEYS notes
  send no MIDI out; an ACID note (or a tied chain) over 5.5 s is cut. Live record (a REC tap on TRACKS arms the track,
  and starts the transport when stopped): a key writes the nearest step, a held key TIEs the later steps, a legato
  overlap SLIDEs (TIE / SLIDE on ACID tracks only). Step
  record: KEYS on, STEP page, stopped, a REC tap (KNOB 1..4 STEP NOTE TIME FLAG, EDIT a rest; REC, PLAY or leaving
  the page ends it). A loaded project keeps ACID on its first two ACID tracks, the others return as their power-on
  kit drum. A step the white keys turn on takes the last KEYS note on an ACID track, C4 on a drum (the last note
  resets on LOAD and on clearing the pattern). An ACID track's STEP page is a piano roll, the note shows in the footer; ACID's SOUND page shows its filter
  and the note's shape.
- A new start screen, CRT COLLAPSE: the HortatoR logo squashed through five dithered frames into one white scan line
  that bursts sideways, in the chosen COLOR (`assets/boot.png`, made by `tools/boot_variants.py --ship`; the study's
  other screens: `tools/boot_variants.py OUTDIR`). The logo, there and on ABOUT, is now UnifrakturMaguntia (SIL OFL).
- Releases can carry two versions: the plain firmware and the same build with Bluetooth MIDI. The web installer
  offers a choice (the plain one by default), the release a second file `hortator-X.Y.Z-bluetooth.fwsc`
  (`BLE=1 tools/publish.sh`; first used for the Bluetooth version with parts 2, 3 and 5).
- Bluetooth MIDI (the Bluetooth version only): MENU (HOME held) > BLUETOOTH turns Bluetooth on (off at every
  power-on), scans, lists the devices found (BLE-MIDI ones first) and connects to the one picked. It remembers it
  (saved) and reconnects whenever it is in range, also after a power-on once BLUETOOTH is ON; KNOB 1 left on DEVICE
  forgets it. MODE (saved): AUTO (SMC DAW for a device named `SMC…`, else MIDI), SMC DAW, MIDI. `BT CONNECTED` /
  `BT LOST` in the top bar. The console's `ble` stays for testing.
- The M-VAVE SMC-Mixer in its DAW mode: faders 1–8 = the track levels (pickup: a fader acts once it reaches the
  level; the mixer blinks it until then), knobs 1–4 = the selected track's first four knobs, MUTE / SOLO / REC /
  SELECT per track, PLAY / STOP / REC, BANK / CHANNEL ◀ ▶ the track; its LEDs show the mutes, solos, armed tracks,
  the selected track and the transport. Fader and knob moves record as motion on the selected track while armed.
- MIDI mode: the device's notes on the drum channel play the tracks, as USB and TRS; GLO CLK BLE follows its clock (a
  project saved with CLK BLE loads with CLK INT in the plain version).
- SOLO (from the SMC-Mixer): while any track is soloed only the soloed ones are heard; not saved, cleared by INIT,
  and cleared (nothing cut) when the SMC-Mixer disconnects or MODE leaves SMC DAW, as the FM-1 has no unsolo. Leaving
  SMC DAW while connected turns the mixer's LEDs off.
- MODE and the remembered device are kept in spare bits of the device settings, in both versions. Going back to an
  older Hortator after picking a device: its SPEAKER EQ reads FLAT.

## 0.15.0 - 2026-10-09

- Bluetooth MIDI, part 1 (a separate build, `FELUCCA_BLE=1`; the normal firmware does not contain it, gate 5): the
  JieLi BLE stack fits the app slot (424 KB, 157 KB free), runs in the main loop in 5 ms slices, and is driven from
  the USB console: `ble start / scan / list / connect N / stop`, `ble` for its status. Off at every power-on. Tested
  on the FM-1 with the M-VAVE SMC-Mixer: connected and receiving, no audio glitches, an update and UBOOT with
  Bluetooth on. Received MIDI is counted, not played yet (part 2). Docs: `docs/ble/FIT.md`, `docs/ble/DEVICE_STEPS.md`.
- Build: every build stops unless the update loader is the pinned one (`tools/frozen_base.txt`); a Bluetooth build
  also stops on its update-path and no-BT-at-boot gates.

## 0.14.0 - 2026-10-08

- FM: a 2-operator FM percussion model (TUNE DECAY INDEX RATIO, MDEC SWEEP FBK VEL): bells, metallic toms,
  woodblocks, zaps, FM kicks; 2 voices; the SOUND graph shows the ratio's sidebands.
- SEQ: the white keys are the selected track's steps on every SEQ page (STEP, PATTERN, MOTION, SONG) and on TRACKS
  (tap on / off, hold accent, OCT+ / OCT- the bank); ALGO or TRACKS' KNOB 1 picks the track. TRACKS mutes: REC held + a
  key (+ top C# / D#: now / next bar), was OCT- held.
- PERFORM: the knobs are the macros whenever the PERFORM screen shows (a held layer key no longer let them edit the
  page under it).
- FILTER: a filter on every track (FX > FILTER: TYPE OFF LP BP HP NOT, CUT, RESO, ENV, DECAY), after RESON, before
  DIST; each hit sweeps it by ENV (accents further); LFO DEST F.CUT / F.RES. Projects: format FDRA (older ones load
  with the filter OFF). BENCH: filter, compressor and sidechain cases.
- COWB: TUNE now moves its pitch (its own 540 / 800 Hz oscillators, the band-pass follows); before, it only moved
  the filter over fixed partials. At TUNE 0 it sounds as before.

## 0.13.1 - 2026-10-08

- Recovery from a Mac with no extra hardware: when the FM-1 stops answering both the web installer and M-UPGRADE,
  `tools/rescue/fm1_uboot.py` backs up its flash and puts M-VAVE's stock V15 back through the chip's boot mode
  ([docs/RECOVERY.md](docs/RECOVERY.md)). The installer page and the README link to the guide. No firmware change.

## 0.13.0 - 2026-10-08

- PHYS (upstream Felucca 1.0's physical models, MIT DaisySP): MEMB, a struck drum head as a drum model (TUNE DECAY
  TONE HEAD on HOME; POS, BEND, STICK; toms, timpani, tabla), and MODAL, a bell / bar / plate body as a RESON model
  (STRCT from harmonic to bell, DECAY its ring time as STRNG's; 2 tracks at most). The RESON MODEL knob now skips a
  model that is full. No project format change.
- BENCH: the console's `bench yes` plays the performance cases on the FM-1; `tools/fm1_bench.py` compares them with
  the host's (render time, CPU %, per case: every model, RESON model, the heavy kits and their FX).
- The start screen and ABOUT show the HortatoR logo (UnifrakturCook Bold, SIL OFL); the "untested" labels are gone.

## 0.12.0 - 2026-10-07

- SONG + NAME (upstream Felucca 1.0's song chain and naming): SEQ > SONG plays up to 16 rows of {project A..D,
  repeat x1..x16} with the sounds loaded now (each row: that project's steps, LEN / DIV / SWING / SRC and motion; a
  row lasts its longest track; LOOP ON / OFF; STOP brings the current pattern back). Projects get names (the PROJECT page's
  NAME knob opens the NAME screen: keys type phone-style, KNOB 1 / 2 cursor and character; SAVE saves as before);
  slots shown as A..D. Saved with the project (format FDR9; older projects load with no song and no name).

## 0.11.0 - 2026-10-07

- MOTION (upstream Felucca 1.0's motion recording): with a track armed (REC on TRACKS) and playing, its sound knobs
  turned are recorded per step and played back with the pattern (a value holds until the knob's next event; the
  patch comes back at the loop start and on STOP; the LFOs keep modulating on top). SEQ > MOTION: PLAY ON / OFF, the
  event count, CLEAR. 128 events for the 8 tracks, saved with the project (format FDR8; older projects load without
  motion).

## 0.10.0 - 2026-10-07

- PERFORM (upstream Felucca 1.0's FX hold layer): hold FX, then the black keys play master effects while held
  (REPEAT 1/8 1/16 1/32, REVERSE, TAPE STOP, LPF, HPF, FREEZE, OCT UP, OCT DN), the white track keys mute their
  track while held, KNOB 1..4 are FILTER / CRUSH / THROW / DEPTH (SHIMMER with OCT); a tap on FX still opens the FX
  pages (now on the release). MENU > PERFORM PAGE: holding FX opens a PERFORM screen that stays (keys and knobs
  work without FX) until another screen is chosen. REPEAT / REVERSE start on the 1/16 (the MIDI clock's when
  following); the buffer is the slicer's (743 ms). Nothing changes when the layer is idle.

## 0.9.0 - 2026-10-07

- MIDI clock in (upstream Felucca 1.0, adapted from contributions by ChanceTheMaker and keremimo): GLOBAL CLK INT /
  USB / TRS; the steps (every division), Grids, the slicer, synced LFOs and the delay follow the source's tempo,
  its Start / Continue / Stop drive the transport (step 0 on the first pulse; stops if the clock stops for 0.5 s).
- Exact step timing: each track carries its step-length remainder, so divisions never drift against each other
  (e.g. a 1/16 and a 1/4 track at 120 BPM) or against an external clock.

## 0.8.0 - 2026-10-07

- USB audio input (upstream Felucca 1.0, UAC1): the FM-1 records into a DAW over its USB cable (16-bit stereo,
  44.1 kHz; the headphone signal, SPEAKER EQ included). MENU USB LEVEL: MASTER (the recording follows the volume
  knob) or FIXED (always full level; the knob sets only the speaker / headphones, upstream 1.0.2 #42). Frozen
  baseline `frozen-base-6`.

## 0.7.0 - 2026-10-07

- Sound pack (upstream Felucca 1.0.2): MENU SPEAKER EQ FLAT / LOWCUT / BASS+ (the bass heard through its harmonics
  on the small speaker); SPRING reverb (shown SPRNG) beside ROOM on the new REVERB page (TYPE SIZE DAMP; chorus on
  its own CHORUS page); slow divisions 1/2, 1/1, 2BAR, 4BAR on PATTERN DIV and delay TIME, division knobs ordered
  by length. Projects are FDR7 (older ones load with ROOM).

## 0.6.0 - 2026-10-07

- TRS MIDI input on by default: notes from the jack play like USB's; clock bytes are queued for the coming MIDI
  clock feature (upstream Felucca 1.0, stage 2 step 4, frozen baseline `frozen-base-5`).

## 0.5.0 - 2026-10-06

The first numbered version: everything on `main` so far.

- Drum machine on Felucca: 8 tracks, drum models (808 / 909 kicks and snares, samples, FM and more), step
  sequencer with per-track length, swing, live recording, quick mutes, MUTE NEXT BAR.
- M2: step PROB / RATCH, Grids (Mutable Instruments) pattern generator as a track source.
- M3: Streams compressor with sidechain ducking keyed by a track, COMP GHOST (MUTE / KEEP / HIDE).
- Two LFOs per track (10 waves with morph, SYNC / Hz / TIME rates, one destination each).
- RESON: per-track resonator insert (STRNG / PIPE / CHORD).
- PRESET knob turns the current section's pages; the regrouped EDIT / COMP / FX pages.
- From upstream Felucca 1.0-1.0.2: safe fixes (calm knob acceleration, swing cap, STOP TO SAVE, installer
  resume); key debounce and encoder fix (#23); TIMER5 nesting; USB-MIDI driver; storage hardening (sequence wrap,
  slot check, no truncated loads, full header read-back) and the erase order that keeps a save silent.
- Project format FDR6; older projects are converted on load.
