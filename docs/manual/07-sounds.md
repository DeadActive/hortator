# Sounds
<!--blurb: The 45 sound models, their knobs, presets, samples and layers.-->

## How a track's sound works

Press [[EDIT]] to open the selected track's sound. [[KNOB 1]] picks the **MODEL**; the SOUND page then shows the model's knobs, four per page, followed by LEVEL, PAN, NOTE, CHOKE and PRIO. Every model has **TUNE** (-24 to +24 semitones), **DECAY**, and then its own knobs (all 0 to 127 unless stated). The first four knobs are the ones on HOME.

The SOUND page shows the model's name, its place in the list (`#NN/45`), VOICES, CHOKE and `+ LAYER`, and on the right a graph. Models without a graph of their own show *the hit*: the voice's real sound (one hit at velocity 100, played silently in the background) as a waveform, labelled e.g. `HIT / 300 MS`; it redraws a few frames after MODEL or a knob changes. SAMPL shows its sample. The page also shows the track's preset, with `*` once a SOUND knob has moved.

Per-track extras on the SOUND pages:

| Parameter | Meaning |
| --- | --- |
| **LEVEL / PAN** | the track's mix level and position (also on TRACKS). |
| **NOTE** | the MIDI note that plays this track (see ROUT NOTE). |
| **CHOKE** | choke group: a hit in a group cuts the others in it (closed hat cuts open hat). Never changed by a MODEL change or a preset; no model starts with a choke group. |
| **PRIO** | HIGH (default) or LOW: a LOW track loses its voices first when the CPU is overloaded. |
| **VOICES** | the model's own polyphony (1 or 2). |

## Models

The MODEL list has three banks. Display names changed in 0.23.0 (projects, presets and MIDI are unchanged); the old names are in parentheses.

### MD bank (21): a classic digital drum machine, measured

Written from the measured behaviour of a classic digital drum machine's emulator, knob by knob; no code or data of it is used. TRX models are digital, EFM models are FM. **TUNE 0 is the original machine's PTCH 64.**

| # | Model | What it is | Knobs (page 1 / page 2) | Voices |
| --- | --- | --- | --- | --- |
| 1 | BDTRX | Sine kick with a pitch drop, transient, half-harmonic SUB, hard CLIP. DECAY runs 7 ms to 4 s. | TUNE DECAY SWEEP SDEC / PUNCH NOISE SUB CLIP | 1 |
| 2 | SDTRX | Snare: two tones crossfaded by TONE, a fixed noise burst; SNAP above 64 adds a 1.44 kHz ring. | TUNE DECAY SNAP TONE / BUMP BENV CLIP | 1 |
| 3 | TMTRX | Tom: a click, then a sine whose RAMP pitch falls over RDEC. | TUNE DECAY RAMP RDEC / DAMP DIST DTYP | 1 |
| 4 | CGTRX | Conga: the TMTRX engine with conga settings. | TUNE DECAY RAMP RDEC / DAMP DIST DTYP | 1 |
| 5 | CPTRX | Clap: four noise bursts (RATE spacing, CLPY level), a resonant low-pass HARD, and a ROOM noise tail (RSIZ length, RTUN colour). | TUNE DECAY CLPY HARD / RATE ROOM RSIZ RTUN | 1 |
| 6 | RSTRX | Rim shot: a sine and a click; DIST warps the sine. | TUNE DECAY DIST | 1 |
| 7 | CBTRX | Cowbell: two tones, ENH a resonator and grit, DAMP a narrow band-pass. | TUNE DECAY ENH DAMP / TONE BUMP PTCH | 1 |
| 8 | CHTRX | Closed hat: MTAL goes from noise to metal; DECAY is a hold, GAP the decay after it. | TUNE DECAY GAP HPF / LPF MTAL | 1 |
| 9 | OHTRX | Open hat: the same engine with a long hold that dips to silence and comes back. | TUNE DECAY GAP HPF / LPF MTAL | 1 |
| 10 | CYTRX | Cymbal: RICH oscillator balance, PEAK a 5.2 kHz bell, SIZE the body, TOP / TTUN a brighter top that fades first. | TUNE DECAY RICH TOP / TTUN SIZE PEAK | 2 |
| 11 | SHTRX | Maracas / shaker: grainy noise, RATL a rattle that grows through the hit, ATT attack, REV a dip and second hold. | TUNE DECAY ATT REV / DAMP RATL TONE HARD | 1 |
| 12 | CLTRX | Claves: two pure tones, DUAL delays the first, ENH raises it, CLICK a short click. | TUNE DECAY DUAL ENH / TUNE2 CLICK | 1 |
| 13 | B2TRX | Second bass drum: about 50 Hz sine from phase 0, RAMP added pitch, HOLD full level before DECAY, TICK click, NOISE hiss, DIRT coarse quantiser, DIST hard clip. | TUNE DECAY RAMP HOLD / TICK NOISE DIRT DIST | 1 |
| 14 | BDEFM | FM bass drum. RAMP sets where the pitch starts, RDEC how fast it returns; MOD FM depth falling over MDEC; MFRQ modulator pitch; MFB grit from about 40, noise at 127. Ends at 0.58 s. | TUNE DECAY RAMP RDEC / MOD MFRQ MDEC MFB | 1 |
| 15 | SDEFM | FM snare: a tone modulated by noise; NOIS / NDEC a separate random noise burst; HPF a high-pass. | TUNE DECAY NOIS NDEC / MOD MFRQ MDEC HPF | 1 |
| 16 | TMEFM | FM tom (an approximation of its kind of grit; exact at MOD 0). Ends at 0.58 s. | TUNE DECAY RAMP RDEC / MOD MFRQ MDEC CLIC | 1 |
| 17 | CPEFM | FM clap: the same FM restarted CLPS / 8 times at CDEC's spacing (1 to 41 ms), then DECAY. | TUNE DECAY CLPS CDEC / MOD MFRQ MDEC HPF | 1 |
| 18 | RSEFM | FM rim shot plus a second "snare" voice: SNAR its level (0 = off), SPTC pitch. | TUNE DECAY MOD HPF / SNAR SPTC SDEC SMOD | 1 |
| 19 | CBEFM | FM cowbell: two FM tones, FB feedback, SNAP a fast 4.5 ms extra decay. | TUNE DECAY SNAP FB / MOD MFRQ MDEC | 1 |
| 20 | HHEFM | FM hat: a sizzling tone, FB from a stalled tone to chaos, a tremolo restarting at each hit (TREM, TFRQ). | TUNE DECAY TREM TFRQ / MOD MFRQ MDEC FB | 1 |
| 21 | CYEFM | FM cymbal: four FM pairs, MDEC from 0.28 to 11 s, HPF two high-passes (off at 0). | TUNE DECAY FB HPF / MOD MFRQ MDEC | 2 |

Former names: BDTRX = KTRX, TMTRX = XTTRX, CGTRX = XCTRX, SHTRX = MATRX, TMEFM = XTEFM. A hit of the CPTRX, SHTRX and the EFM snare/clap/rim models sounds the same every time except where noted (random noise bursts). DECAY on the EFM models other than BDEFM and TMEFM follows the machine's whole 1.6 s range and fades out from 5.5 s at the top.

### MODERN bank (10)

Integer ports of Mutable Instruments Plaits drum algorithms (MIT), DaisySP physical models, an FM voice, ACID and a sample player. Models marked *heavy* count double toward the voice cap.

| # | Model | What it is | Knobs | Voices |
| --- | --- | --- | --- | --- |
| 22 | BOOM | Analog bass drum: pulse into resonator with overdrive (heavy). Was KBOOM. | TUNE DECAY TONE PUNCH | 1 |
| 23 | PUNCH | Synthetic bass drum: a distorted sine with FM and pitch envelopes (heavy). Was KPUNC. | TUNE DECAY TONE FM | 1 |
| 24 | SNAP | Analog snare: five shell modes, pulse exciter, band-passed noise (heavy). Was SSNAP. | TUNE DECAY TONE SNAP | 1 |
| 25 | CRACK | Synthetic snare: two distorted FM sines plus noise (heavy). Was SCRAK. | TUNE DECAY FM SNAP | 1 |
| 26 | METAL | 808-like metallic hat: six square oscillators and a resonant band-pass. Was HMETL. | TUNE DECAY TONE NOISE | 1 |
| 27 | HISS | Noisy trashy hat: ring-modulated square and saw pairs. Was HNOIS. | TUNE DECAY TONE NOISE | 1 |
| 28 | SKIN | A struck drum head (modal physical model): HEAD from ideal to loaded (toms and timpani to tabla), POS centre to rim, BEND, STICK (heavy). Was MEMB. | TUNE DECAY TONE HEAD / POS BEND STICK | 1 |
| 29 | FM | Two-operator FM percussion: a carrier phase-modulated by a sine at RATIO x pitch. MDEC the FM envelope decay, SWEEP a pitch rise, FBK modulator feedback, VEL velocity into INDEX. Bells, metallic toms, woodblocks, zaps, FM kicks. The graph shows the ratio's sidebands. | TUNE DECAY INDEX RATIO / MDEC SWEEP FBK VEL | 2 |
| 30 | ACID | 303-style mono bass: saw or square into a 24 dB ladder filter, with accent, slide and tie from the steps. Max two ACID tracks. | TUNE DECAY CUTOF RESO / ENVMD ACCNT WAVE GATE | 1 |
| 31 | SAMPL | One-shot sample player (see Samples). Was SMPL. | TUNE DECAY TONE DRIVE / SET KEY | 2 |

Lists: FM **RATIO** is one of 0.5, 1, 1.41, 1.5, 2, 2.76, 3, 3.5, 4, 5.4, 6, 7, 8, 9.2, 11, 14. ACID **WAVE** is SAW or SQR. ACID **GATE** is how long a hit that no step drives stays open, as a fraction of the step. ACID is skipped in the MODEL list and PRESETS once two ACID tracks exist (`ACID: 2 TRACKS MAX`).

### CLASSIC bank (14): 808 and 909 style

| # | Model | What it is | Knobs | Voices |
| --- | --- | --- | --- | --- |
| 32 | K808 | 808 kick: a sine with a pitch dip. | TUNE DECAY TONE DIP / DRIVE | 1 |
| 33 | K909 | 909 kick. | TUNE DECAY SWEEP CLICK / SWPT DRIVE | 1 |
| 34 | S808 | 808 snare: two tones plus band-passed noise. | TUNE DECAY TONE SNAP / NTONE | 1 |
| 35 | S909 | 909 snare. | TUNE DECAY BODY SNAP / NTONE | 1 |
| 36 | C808 | 808 clap. | TUNE DECAY TONE TAIL | 1 |
| 37 | C909 | 909 clap. | TUNE DECAY TONE SPRD | 1 |
| 38 | HATC | Closed 808 metal hat. | TUNE DECAY TONE BRITE | 1 |
| 39 | HATO | Open hat. | TUNE DECAY TONE BRITE | 1 |
| 40 | CYMB | Cymbal. | TUNE DECAY TONE BAL | 2 |
| 41 | COWB | Cowbell; TUNE moves its pitch (since 0.14). | TUNE DECAY TONE TAIL | 1 |
| 42 | TOM | Tom. | TUNE DECAY NOISE DROP | 2 |
| 43 | CONGA | Conga. | TUNE DECAY SLAP DROP | 2 |
| 44 | RIM | Rim shot: 1667 and 455 Hz, driven, high-passed. | TUNE DECAY TONE DRIVE | 1 |
| 45 | CLAVE | Claves. | TUNE DECAY CLICK DROP | 1 |

DRIVE on K808, K909, BOOM, PUNCH and SAMPL is shown as a percentage.

## The power-on kit

At power-on, on TOOLS **INIT ALL** and in a new project the eight tracks load the TRX classic kit, each on its default preset: **BDTRX, SDTRX, CPTRX, CHTRX, OHTRX, TMTRX, RSTRX, CYTRX**, with MIDI notes kick 36, snare 38, clap 39, closed hat 42, open hat 46, tom 45, rim 37, cymbal 49, and no choke.

## Presets

A **preset** is one voice: MODEL plus the eight SOUND knobs and a name. It does not store the FILTER, LFOs, sends, LEVEL, PAN, NOTE, CHOKE or steps.

| Bank | Presets |
| --- | --- |
| MACHINEDRUM | 55, from the original machine's factory kits (1 to 4 per voice; none for SHTRX and CLTRX) |
| MODERN | 40 (4 per voice, ACID and SAMPL included) |
| CLASSIC | 56 (4 per voice) |
| USER | 128 empty slots for yours, stored in flash |

151 factory presets in all. Every voice's **first preset is its default sound**: a MODEL change loads it.

- **PRESETS encoder.** Each detent loads the next / previous preset onto the selected track, through every bank with wrap-around. A popup shows the bank, the preset, and two before and after (about 1.5 s). Empty USER slots, and ACID beyond two tracks, are skipped.
- **The PRESETS list.** Hold [[EDIT]] about 0.4 s (a tap opens EDIT as before). Seven presets on screen. [[KNOB 1]] or PRESETS move the cursor; [[KNOB 2]] or [[SELECT]] pick the bank (MD, MODERN, CLASSIC, USER); every move auditions the preset on the selected track. [[OCT+]] **LOAD** keeps it and closes the list; [[EDIT]] or [[HOME]] closes it and restores the track's own sound. **SAVE** stores the track's own sound (not the auditioned one) in the first free USER slot or the chosen one (a used slot: SAVE twice); [[OCT-]] twice **DELETE**s a USER preset. USER presets are named from the voice and slot (`SDTRX 03`). Saved while playing, they are written at the next STOP. While MIDI LEARN is on, EDIT held does nothing.
- **Display.** The SOUND pages and the TRACKS footer show `PRESET <name>` with `*` once a SOUND knob moved. A track whose USER slot now holds another voice (or none) shows `-`. Projects keep each track's preset and its mark; older projects load with no preset and their sound unchanged. SAVE while MOTION plays stores the knobs as set, not MOTION's values.

## Samples

SAMPL plays a sample set. **SET** chooses the set: **PERC** (built in) or **USR1**, **USR2**, **USR3**. **KEY** (0 to 127, default 36) selects the zone whose key range contains it; the sample is pitched by KEY and the step's note relative to the zone's root, plus TUNE (limited to three octaves up). DECAY, TONE (a one-pole low-pass) and DRIVE (a soft clip) shape it. If no zone matches, or a slot is empty, the hit is silent.

- **PERC** is a General MIDI-style drum map on the keys: kick, rim, snare, clap, hats, toms, crash and ride, tambourine, cowbell, conga, shaker, claves and woodblock. It is Hortator's generated drum samples (Hügelton Sample Pack) and CC0 hand percussion (Versilian Studios VSCO-2 CE and VCSL), stored as 22,050 Hz IMA ADPCM.
- **User slots** USR1 (80 KiB), USR2 and USR3 (48 KiB each; the rest of their flash holds the project variations and the lock banks). Each holds up to 16 zones, mono 22,050 Hz IMA ADPCM; 80 KiB is about 7 s of audio, 48 KiB about 4 s.
- **Hortator has no sample upload.** Its firmware has no editor protocol, so the Felucca web editor at `/webapp/editor/` and `tools/fm1_sample_upload.py` do not load samples into it. USR1 to USR3 are empty unless filled earlier under upstream Felucca.
- A longer USR2 or USR3 (uploaded under upstream Felucca) is shortened once at the next power-on (`USR2 SHORTENED TO 48K`, `USR3 SHORTENED TO 48K`). If that fails (`USR2 CLAMP FAILED`, `USR3 CLAMP FAILED`), SAVE says `USR2 ERROR: NO SAVE` until the next power-on. Do not upload 80 KiB to USR2 or USR3 under upstream Felucca afterwards: it overwrites Hortator's variations and lock banks.

> **Web editor.** The site carries the Felucca editor from the upstream project at `/webapp/editor/`. It speaks Felucca's SysEx protocol, which Hortator does not implement, so it will find the FM-1's USB port but get no replies. Do not rely on it with Hortator.

## LAYER: a sample under any model

Press [[EDIT]], then [[OCT+]] (with KEYS off) for the **LAYER** page: a sample layer under any model, with **LSET** (set), **LKEY** (key), **LLVL** (level, 0 = off), **LTUNE** (plus or minus 24) and **LDEC** (decay). [[OCT-]] returns to SOUND.
