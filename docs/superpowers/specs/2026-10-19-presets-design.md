# Presets and sound banks — design (2026-10-19)

## Intent

On the FM-1 you reach for a sound, not eight knobs. A track's voice (its MODEL and the eight SOUND knobs) can be
loaded from named, ready-made sounds organised in themed banks, browsed live while the pattern plays, and the
owner's own sounds can be saved and found again. The same work replaces every voice's default knob values with the
first sound of its bank, so a fresh track already sounds like a good example of that voice. For the 21 voices measured
from the Machinedrum (KTRX, the 12 TRX voices, the 8 EFM voices) the ready-made sounds and the defaults come from the
Machinedrum's own factory kits, read from the emulator.

## Goals

- **A preset = one voice:** MODEL + P_E0..P_E7 (the eight SOUND knobs). Nothing else (not the FILTER, LFOs, sends,
  level, pan, note, choke, steps).
- **Themed banks, in this order:** Machinedrum, Classic 808 / 909, Modern / Plaits, User.
  - Machinedrum: KTRX, SDTRX ... B2TRX, BDEFM ... CYEFM (21 voices), up to 4 presets each (a voice with fewer
    distinct factory sounds simply gets fewer).
  - Classic 808 / 909: K808 K909 S808 S909 C808 C909 HATC HATO CYMB COWB TOM CONGA RIM CLAVE x 4 = 56.
  - Modern / Plaits: KBOOM KPUNC SSNAP SCRAK HMETL HNOIS MEMB FM ACID SMPL x 4 = 40 (SMPL and ACID: see builder's
    calls).
  - User: 128 slots in flash.
- **No default choke:** every voice's default choke group becomes none (today HATC, HATO, CHTRX, OHTRX, HHEFM start
  in group 1). CHOKE is purely the track's own setting: a MODEL change or a preset load never changes it. Existing
  projects keep their stored CHOKE.
- **Browse with the PRESETS encoder** (today free, `panel_enc(EN_PRESET)` does nothing): each detent loads the
  next / previous preset onto the selected track **instantly** (MODEL and knobs; the voice may change), stepping
  through every bank in one run (Machinedrum → Classic → Modern → User; empty User slots skipped; wraps). A popup
  over the current screen shows the bank, the selected preset's name and a few neighbours above and below; it closes
  by itself after a short pause.
- **The full list on a long press of EDIT:** a screen listing the banks and their presets (name, voice), the track's
  current one marked; choose and load from it; **SAVE** writes the selected track's voice into a User slot: the next
  free one by default, or any User slot you move to (saving over it); **DELETE** clears a User slot.
- **User presets** are auto-named from the voice and a number ("SDTRX 03"); 128 slots; saved in flash (survive power
  off and firmware updates). Saved while the pattern plays: the preset appears in the list at once and is written to
  flash at the next STOP (as MIDI LEARN's map; lost only if power is cut before stopping); saved while stopped: written
  at once.
- **The track remembers its preset:** the project stores which preset each track was loaded from and shows its name;
  turning any of that track's SOUND knobs (or MODEL) marks it edited (shown with a mark).
- **New defaults:** every voice's default knob values (the `def` of its `PD()` entries in `DM_*_DEF`) become the
  first preset of its bank. For Machinedrum voices: the most typical of its 4 factory sounds (chosen as the one
  closest to the median of the voice's factory-kit settings) is first.
- **Ready-made sounds for Classic and Modern** are designed by Claude, rendered as demos, and approved by the owner by
  ear before they are fixed in the firmware.

## Non-goals

- Whole-kit presets (8 tracks at once); projects already do that.
- Renaming user presets, or naming them on the device (auto names only, for now).
- Sharing / backing up presets through the web editor, USB or SysEx.
- Presets for the FILTER, effects, LFOs, sequencer or any other part than the voice.
- Changing how existing voices sound (DSP); only their default knob values change.

## Anti-goals (a failure even if it works)

- Browsing glitches the audio (a click or a dropped block when a preset loads while playing beyond what a MODEL
  change already does today), or the popup stalls the UI.
- An existing project changes its sound on load: projects store their knob values; new defaults must only affect
  fresh tracks / a MODEL change, never a loaded project.
- Anything from the emulator in the firmware other than knob values: no samples, code, ROM data or the Machinedrum's
  preset/kit names (the presets get Hortator's own names). Knob values are taken from the factory kits only as
  settings, mapped through each port's measured laws.
- User presets lost or corrupted by a firmware update, a power cut during save, or MIDI LEARN's map (same flash area).

## Constraints

- Frozen files stay byte-identical (`python3 tools/check_untouched.py`; storage.c is frozen): user presets go in the
  existing, unused flash object `OBJ_UPRESET0 + 1` (storage.c's enum already defines `OBJ_COUNT = OBJ_UPRESET0 + 2`;
  MIDI LEARN uses `OBJ_UPRESET0`). One 4 KB sector: 128 x (MODEL 1 B + 8 knobs as int8 1 B each + a used flag) fits.
- Fixed point, no 64-bit divide, no new work in the audio ISR beyond what a MODEL change does.
- The project format grows by the per-track preset reference and edited flag: a new format letter after "FDRC"
  ("FDRD"), with older records converted on load (no preset, not edited), as every earlier format change.
- ACID's 2-track cap (`ACID_MAXTRK`): a preset whose voice is ACID is skipped by the popup (and refused in the list
  with the existing "ACID: 2 TRACKS MAX" message) when the cap is reached.
- Every changed default changes that model's golden render hash (`tests/drum_golden.txt`): re-pinned in the same
  change, and only the models whose defaults changed may differ.
- **Fidelity tests keep measuring exactly what they measure today:** every test that renders "at the default" (the
  `-99` knobs in the per-voice tests, `tests/drum_fidelity.c`'s `REF_DEF` check and its knobs 5..8, the Machinedrum
  references captured at the old neutral values) gets the old values written into the test, so no fidelity metric,
  reference or threshold changes (owner's decision).
- Never install or run the FM-1 installer / updater; the owner checks on the device and merges.

## Decided (by the owner)

- Browse and load in both places: PRESETS encoder popup (instant load) and a full list on a long press of EDIT.
- Themed banks: Machinedrum, Classic 808 / 909, Modern / Plaits, User. The popup steps through everything.
- A preset is the voice only. 128 user slots, auto names, saved from the list screen.
- The track shows its preset's name and an edited mark.
- Machinedrum factory kits give both the defaults and the presets, 4 per voice. The other banks also 4 per voice,
  designed by Claude, approved by ear. Each voice's default = its bank's first preset.
- Preset loads are instant even when the voice changes.
- No voice has a default choke group; preset loads and MODEL changes never touch CHOKE.
- User slots: SAVE to the next free slot or over any chosen User slot; DELETE clears one.
- Machinedrum voices with fewer than 4 distinct factory sounds get fewer presets.
- The fidelity tests are pinned to the old knob values (no metric changes).
- SAVE while playing is written to flash at the next STOP.

## Claude's calls (check these)

- With no current preset (a fresh track, a converted FDRC project), the first PRESETS detent lands on the first preset
  of the track's current voice. After a MODEL change the track's preset reference moves to the new voice's first
  preset (its knobs now equal it), not edited.
- The Machinedrum A/B demos render the emulator side as the voice alone: the kit track's own amp, EQ, filter, effects
  and LFO set neutral (as all md-re renders), so the owner judges only what a preset holds.
- Claude picks the 4 Machinedrum presets per voice (distinct, characteristic) and the owner approves the whole
  Machinedrum bank by ear, as the Classic and Modern banks.

## The Machinedrum sounds: how they are obtained (first step of the plan: a spike)

The emulator keeps its memory in `~/Documents/Gearmulator Preview/Machinedrum/nvram/md-uw-1.63-factory-v2.cache`,
named as the factory state, so its kits should be the factory kits. Extend md-re's host (`host/mdrender.c`) to capture
the AU's MIDI output (kAudioUnitProperty_MIDIOutputCallback) and request each kit's dump over SysEx (the Machinedrum's
kit request), decode the kits' per-track machine and 8 parameter values, and list for each of the 21 machines every
distinct setting found. If the factory kits turn out not to be there, stop and tell the owner before choosing another
source. From each machine's list pick 4 distinct, characteristic settings (the knob spread, by ear on A/B renders),
convert them to the Hortator voice's knobs (PTCH -> TUNE semitones and the other per-port mappings recorded in
md-re/docs/TRX.md, EFM.md and the ports' DEVICE_INSTALL sections), name them (Hortator's own names), and render
emulator-then-firmware demos for the owner.

## Left to the builder

- The popup's exact look (how many neighbours, the close delay ~1.5 s), the list screen's layout and controls (how
  to move between banks, how to load, where SAVE sits), the edited mark's glyph, and where the preset name shows on
  other screens — following the existing UI's style (ui_draw.c, the MUTE / NAME / LEARN screens) with screenshots in
  ui_test for the owner.
- What happens to a project's reference when its User slot is overwritten or deleted (show the new name / drop the
  reference and keep the knobs).
- The long-press time for EDIT (match the existing hold conventions) and that it does not break EDIT's tap or step
  record.
- How a preset is referenced in the project (bank + index, a 1-byte id, ...).
- The factory banks' storage (const tables in flash) and the preset name format (<= the popup's width).
- SMPL (sample choice knobs) and ACID in the Modern bank: include them if their 4 presets make sense; otherwise leave
  them out and say so.
- Tests: preset load/save round trips, the popup's order and skipping, SAVE / DELETE / overwrite, deferred save on
  STOP and a power cut, FDRC -> FDRD conversion, the ACID cap, CHOKE untouched by loads and MODEL changes, defaults =
  first preset for every model, MIDI LEARN's map untouched by preset saves; the hats' existing choke tests set their
  group explicitly.
