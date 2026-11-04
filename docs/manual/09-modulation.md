# Modulation, Grids and compressor
<!--blurb: Two LFOs per track with four destination slots, the Grids pattern generator and the ducking compressor.-->

## LFOs

Every track has two LFOs. Press [[LFO]] for **LFO 1/4**, **LFO 2/4**, **LFO 3/4** and **DEST 4/4**. LFO 1 is always on [[KNOB 1]] and [[KNOB 2]], LFO 2 on [[KNOB 3]] and [[KNOB 4]].

| Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
| --- | --- | --- | --- | --- |
| LFO 1/4 | LFO 1 WAVE | LFO 1 RATE | LFO 2 WAVE | LFO 2 RATE |
| LFO 2/4 | LFO 1 MORPH | LFO 1 PHASE | LFO 2 MORPH | LFO 2 PHASE |
| LFO 3/4 | LFO 1 TRIG | LFO 1 MODE | LFO 2 TRIG | LFO 2 MODE |
| DEST 4/4 | SLOT (1 to 4) | SRC (LFO 1, LFO 2) | DEST | DEPTH |

[[OCT+]] on an LFO page cycles the RATE MODE (SYNC, HZ, TIME) of the LFO turned last (on DEST, of the selected slot's source LFO); it needs KEYS off.

### Parameters

- **WAVE** (ten): SQUAR, SAW, RSAW, SINE (default), TRI, S&H, WANDR, EXP+, EXP-, RWALK.
- **MORPH** 0 to 127 (0): bends the wave: squareness for SQUAR and SINE, the curve for SAW, RSAW and EXP, the peak position for TRI. No effect on S&H, WANDR or RWALK.
- **PHASE** 0 to 355 degrees.
- **MODE:** SYNC (default), HZ, TIME.
- **RATE** (default 23): in SYNC, 8BAR 4BAR 2BAR 1BAR 1/2 1/4 and then 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, 1/64 (17 values over the knob); in HZ about 0.02 to 40 Hz; in TIME 25 ms to 60 s.
- **TRIG:** FREE (free-running), HIT (restarts on each hit of the track), PLAY (restarts on PLAY; default).

### DEST: four destination slots per track

Each slot has a **SRC** (LFO 1 or LFO 2), a **DEST** and its own **DEPTH** (-100 to +100 %, default 0). Slots 1 and 2 default to LFO 1 and LFO 2. KNOB 1 picks the slot. Destinations:

| Destinations |
| --- |
| OFF |
| the model's eight knobs E0 to E7 (shown by name; `--` if the model has none) |
| LVL, PAN |
| R.TUN, R.DCY, R.MIX, R.TON, R.STR, R.POS (RESON) |
| F.CUT, F.RES (FILTER) |
| SRR, DST, CHO, DLY, REV (the SRR and the sends) |
| MIX, then A, B, C (the INSERT's mix and its three knobs, shown by the TYPE's names) |

The last two groups were added in 1.0.0 after F.RES, so projects, presets, step locks and MIDI LEARN keep their meaning; nothing changes until an LFO is routed to one.

An LFO on a knob moves it around its set value. A locked step or a MIDI LEARN CC sets the centre value; the LFO keeps modulating around it. Older projects load with their LFO 1 and LFO 2 routing as slots 1 and 2. MOTION records an LFO's RATE, MORPH and DEPTH (not SRC or DEST); MODEL, WAVE, DEST and SRC cannot be locked.

## Grids

Press [[ARP]] for **GRIDS 1/2** and **2/2**. Grids is Mutable Instruments' pattern generator: a map of drum patterns, with chaos, or Euclidean rhythms, on three channels: kick (K), snare (S) and hats (H). Any track follows a channel through PATTERN **SRC** (G-KCK, G-SNR, G-HAT); the track then plays the generated pattern and not its own steps.

| Page | Knob | MAP mode | EUCL mode |
| --- | --- | --- | --- |
| 1/2 | **KNOB 1** MODE | MAP (default) | EUCL |
| 1/2 | **KNOB 2** | X (0 to 127, 64) | LEN K (1 to 32, 16) |
| 1/2 | **KNOB 3** | Y (0 to 127, 64) | LEN S (1 to 32, 12) |
| 1/2 | **KNOB 4** | CHAOS (0 to 100 %, 0) | LEN H (1 to 32, 8) |
| 2/2 | **KNOB 1 to 3** | FIL K, FIL S, FIL H: fill density per channel, 0 to 100 % (50 %) | the same |

Move X and Y through the map and the lanes rewrite; CHAOS adds randomness. The density fill knobs change how busy each lane is.

## Compressor (COMP)

The Streams compressor, keyed by any track, for ducking and pumping. Reach it from HOME: on the HOME screen [[SELECT]] goes on to **COMP 2/3** and **3/3**. On the COMP pages a white key toggles DUCK on its track: the tracks marked are ducked by the source.

| Page | Knob | Label | Values (default) |
| --- | --- | --- | --- |
| 2/3 | **KNOB 1** | SRC | OFF (default), T1 to T8 |
| 2/3 | **KNOB 2** | THRSH | about -30 to 0 dB (26, about -24 dB) |
| 2/3 | **KNOB 3** | RATIO | 1.0:1 to 25.6:1 (45, about 4:1) |
| 2/3 | **KNOB 4** | REL | 59 ms to 5.8 s (26, about 131 ms) |
| 3/3 | **KNOB 1** | ATK | 0.1 to 583 ms (2, about 1.1 ms) |
| 3/3 | **KNOB 2** | KNEE | HARD, SOFT (default) |
| 3/3 | **KNOB 3** | MKUP | makeup gain, 0 dB up; 127 shows LIMIT (brickwall) (0) |
| 3/3 | **KNOB 4** | GHOST | MUTE, KEEP (default), HIDE |

**GHOST** decides what happens when the source track is muted: **MUTE**: the muted source is silent and does not key; **KEEP**: the muted source still keys the ducking (keep the pump with the kick muted); **HIDE**: the source is never heard but keys. The ducking is a gain on each ducked track, so the USB T channels and their sends are already ducked. A muted COMP source fades out just before the bar's first hit when MUTE is NEXT BAR.
