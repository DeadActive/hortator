# GRIND pages (ARP, then ARP again)

Five pages in the GRIDS family, `SC_GRIND` scope (firmware/src/grind/ui.c, pages.c). Values live in `gm_v[]` and are not
all stored: only STYLE and WHEN are (the settings word).

| # | Knobs 1..4 | Notes |
|---|---|---|
| 1 | STYLE, AGGR, DENS, CHAOS | a style turn loads that style's factory controls |
| 2 | HUMAN, MUT, WHEN, KIT | |
| 3 | KEEP, UNDO, FREE, VARY | VARY 0..100: velocity swing per generated hit, up to +-8 at 100 (seq.c `hv_vel`, grind/hit_variation.h). Deterministic per PLAY (track, step count, roll ordinal). 0 = the exact old sound. It is a playback feel: it varies every ordinary drum step and its ratchets, hand-programmed and KEEP steps included (the firmware does not record which steps GRIND wrote). ACID/gated tracks (and their rolls), manual hits and MIDI are not varied. Not stored. |
| 4 | SINK, SWELL, CUT | LIVE macros, see below |
| 5 | STYLE (browser), BANK, APPLY, | looks at a style with a live preview; nothing changes until APPLY; leaving the page cancels |

## STYLE browser (page 5)
STYLE (browse) picks one of the 23 entries, BANK scrolls the 16-step view of longer patterns. The preview is made by the
same path as a real request (`gm_runtime_make`: live pattern as base, KEEP respected) and shown as eight roles with
accents, ratchets and KEEP marks. It shows BUSY while a request is staged and is regenerated when the pattern is edited.
APPLY is exactly a STYLE turn (stopped: now; playing: staged for the WHEN boundary).

## 7/4 (entry 23, MATH)
Technical on a 3+2+2 seven-quarter bar; see CHANGELOG. Style ids, saved bytes and golden patterns are unchanged.

## LIVE macros (page 4)
- **SINK** 0..100: the master sinks toward a lowpass. It drives perform.c's existing FILTER macro (LPF end), the stronger of the
  knob and SINK wins. 0: the normal sound.
- **SWELL** 0..100: more of the mix into the delay and reverb sends (the THROW macro), smoothed. No gain jump.
- **CUT**: a performance stop. Stops the transport (a queued cue is dropped), cuts every track's voices, cancels a staged
  or waiting GRIND change. The delay and reverb tails ring out. A later PLAY is an ordinary PLAY. It is not a recovery.
  Limits: on an FM-Link follower the leader's transport keeps driving the pattern, so CUT only silences the current voices; a REPEAT STEP button held (MIDI) keeps repeating after CUT, as after an ordinary STOP.
  With SINK and SWELL at 0 the output is bit-identical to a firmware without them. The levels are not stored and reset on power-up.

## Not built (RAM)
At 95,160 of 98,304 B app RAM (CI, PR #15) there are 3,144 B free. ANCHOR/RETURN and COMPARE (about 2.2 KB), Scene Morph
(depends on ANCHOR) and Tape (an 8 KB ring) were not built; AM and Phone and the per-style preferences (A2) were not built either.
