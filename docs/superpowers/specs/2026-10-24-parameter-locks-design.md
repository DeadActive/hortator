# Parameter locks — design

Date: 2026-10-24. Status: draft for the owner's approval. Base: main 2d7a0b1 (drum 0.23.3, published).

## Intent

Elektron-style parameter locks in Hortator: hold a step and turn a knob, and that step plays with its own value for
that knob. It must work on every page where MOTION can record a knob today, without leaving the page. Locks and
recorded MOTION are the same data (a per-step knob value). The room for them grows from 128 events shared by 8 tracks
to 1024 per project.

## Goals

- **ENV toggle (owner).** The ENV button (unused in Hortator today, panel.c `B_ENV`) toggles the white step keys on
  every page:
  - TRACKS mode, ENV LED off: the keys do what they do today on that page.
  - STEPS mode, ENV LED lit: the keys are the selected track's steps, as on the SEQ pages.
- **Locks (owner).** In STEPS mode, on any page with MOTION-recordable knobs (motion.c `motion_param`: SOUND E0..E7,
  LEVEL, PAN, the FX sends DIST..REV, SLICER, LAYER, RESON incl. KEYF, FILTER except TYPE, LFO RATE / MORPH / DEPTH,
  DEST slot depths), hold a step and turn a knob to write a lock on that step for that knob.
- **Snap back (owner).** A lock applies only to its own step. On the next step the knob plays the track's own value
  again (the base, or the LFO-modulated base). Recorded MOTION keeps its hold-until-next behaviour; the two kinds
  coexist and are flagged per event.
- **Clear (owner).** Hold a step and press OCT- to clear that step's locks on the current page's knobs. Hold OCT- for
  0.6 s with the step held to clear all of the step's locks on every page.
- **Capacity (owner, option D).** 1024 lock/motion events per project slot, stored in a new per-slot flash bank in the
  last 32 KiB of user sample slot 3. Slot 3 shrinks from 80 KiB to 48 KiB.

## Non-goals

- Locks on the model, mute, choke, note, routing (LFO SRC / DEST), FILTER TYPE or RESON MODEL: these are not
  MOTION-recordable today, so they are not lockable (owner: "where motion sequencing is possible").
- Trig-condition or "lock-only trig" features beyond what PROB / RATCH / ACC already do.
- Locks over MIDI CC (CC moves are not recorded as motion today; unchanged).

## Anti-goals (a failure even if it works)

- Bricking or corrupting anything: the update loader, OTA staging, stock config / BT calibration, key_mac or another
  object's flash must never be touched. Every new flash address is outside every update and erase path (below).
- Losing a user's data silently: projects, presets, MIDI LEARN map, samples.
- Audio glitches or UI lag: the per-step lookup in the audio ISR stays tiny; redraws stay partial (the UI-lag work).
- Changing how the FM-1 plays when ENV is off: TRACKS mode is byte-for-byte today's key behaviour.

## Constraints

- Frozen files stay byte-identical, except an approved, minimal storage.c change (the object table and addresses). It
  moves the frozen baseline (frozen-base-10) the way the USB rename did (frozen-base-9, usb_app.c).
- No 64-bit divide. CPU and stack budgets unchanged (report, never raise, a budget).
- Tests first (TDD); all suites, the web sim and the web tests pass.

## Storage (option D)

### Flash map facts (read-only check, 2026-10-24)

- **Free today:** only 0x9F000 and 0xFE000. These stay unused (reserve).
- **User samples:** 0xA0000–0xDBFFF, 3 slots of 80 KiB (eng_sample.c:36-42, SMP_USER_SIZE 0x14000).
  - USR3 = 0xC8000–0xDBFFF.
- **Loader erase scan:** the update loader erases any sector in 0x93000–0xFBFFF whose bytes at +0xF00 look like an
  update record (ldr_core.c:129-134). Every new record therefore keeps its payload ≤ 3584 B, so that offset stays
  erased (0xFF) and can never match.

### New layout

- **USR3 shrinks to 48 KiB:** 0xC8000–0xD3FFF (0xC000). The freed range 0xD4000–0xDBFFF is 8 sectors.
- **Lock banks:** new storage objects OBJ_LOCKS0..3, one per project slot, two copies each:
  - addresses 0xD4000 + slot × 2 × ST_SECTOR + copy × ST_SECTOR
  - written and read like the project objects (header, CRC, A/B copies)
  - OBJ_COUNT grows; the existing objects keep their ids and addresses
- **Record format:**
  - an inner payload magic ("LKB1") plus the owning slot, so a stale or foreign record is never misread
  - a uint16 event count, then up to 1024 packed events of 3 B each:
    - a 16-bit key: track 3 bits, step 6 bits, param 7 bits
    - an int8 value
  - one bit of the key space, or a parallel bitmap, marks a lock (snap back) versus motion (hold): the builder picks
    whichever packs cleanly
  - payload ≤ 3584 B; 3 + 2 + 1024 × 3 = 3077 B plus the magic fits
- **The project record:**
  - stays in its own sector; its motion block (516 B) is no longer the source of truth
  - new projects save the lock bank alongside, and the project record's magic moves to a new layout ("FDRF")
  - an old project (FDRE or earlier) loads its ≤128 motion events into the bank as motion (hold) events
  - the freed 516 B stay reserved, zeroed

### User sample slot 3

- Max length 48 KiB.
- On boot, if USR3's stored sample is longer than 48 KiB:
  - its stored length is rewritten to 48 KiB in flash, once (owner: downgrade safe), so an older Hortator never plays
    lock data as sound
  - "USR3 SHORTENED TO 48K" shows on that boot
  - the lock-bank sectors are only written after that check
  - a sample overlapping the new range is never played past 0xD3FFF
- Hortator itself has no sample-upload protocol: USR3 samples come only from an earlier upstream Felucca install. If
  someone later runs upstream Felucca and uploads an 80 KiB USR3, it overwrites the lock banks. Each bank's header
  CRC and inner magic detect that. The bank then loads as empty, and "LOCKS LOST" shows once. A damaged bank is never
  misread. The web editor and `tools/fm1_sample_upload.py` talk only to upstream Felucca and are not changed.
- **Downgrade (owner): safe, not supported.**
  - Older Hortator (0.23.x) shows LOAD FAILED for new-layout projects and nothing breaks.
  - The old project block's motion is not kept in sync.

**Frozen change approved:** the storage.c object-table change is approved by the owner, who picked option D, which
includes it.

### RAM

- All 4 slots stay in RAM, as today (`proj_slot[4]`; SONG rows read their slot's motion in place, song.c:90).
- 4 banks of about 3.1 KB, plus the working copy, is about 15.4 KB, placed in .bss (BLE build: about 29.7 KB free;
  NOINIT and the pool are too tight).
- The bank is loaded from flash at boot and on project load, and saved with the project.

### Lookup

- `motion_step` currently scans every event (motion.c:113). The bank becomes sorted by key, and a step's events are
  found by binary search over the track/step prefix (about 10 probes, no extra RAM).
- `motion_valid`'s O(n²) check becomes O(n).
- `count` becomes uint16.

## UI and behaviour

- **ENV toggle:**
  - ENV tap = toggle STEPS / TRACKS mode; ENV LED lit in STEPS mode.
  - The mode is global (not per page) and not saved (off at power-on). That is my call: the owner can make it saved.
- **In STEPS mode on any page:**
  - The footer strip shows the selected track's steps for the current bank of 16.
  - A locked step is marked with a small dot or underline (my call: an underline under the step box).
- **OCT+ / OCT- in STEPS mode (owner):**
  - OCT+ and OCT- keep their page jobs everywhere: SOUND to LAYER, LAYER back, LFO MODE, and so on.
  - The bank changes with ENV held + OCT+ (next bank) or ENV held + OCT- (previous bank). ENV released without
    OCT± in between toggles the mode, so the toggle acts on ENV's release.
  - **OCT+ keeps its accent role (owner):** OCT+ held + a step tap toggles that step's accent, as on the SEQ pages,
    in STEPS mode on every page. OCT+ released alone does the page's own job, the way OCT+ released alone changes the
    bank on SEQ.
  - Hold a step + OCT- = clear (a held step is its own context, so OCT-'s page job doesn't happen then).
- **Tap and hold:** the same as the SEQ pages (stephold):
  - a short tap toggles the step on release
  - a hold of 0.3 s turns an off step on at once
  - an on step stays on
- **While a step is held on a page with recordable knobs:**
  - a knob turn writes a lock for that step and knob (LOCKS FULL if the bank is full)
  - the knob cells show the step's locked value, or the base if not locked, and locked cells are marked (inverted
    value)
  - the top line says "LOCK STEP 5"
  - letting go returns the cells to the base values
- **On the SEQ STEP page itself:** a held step keeps KNOB 1/2/3 = PROB / RATCH / ACC (unchanged); STEP's knobs are not
  sound knobs.
- **Clearing:**
  - hold a step + OCT- tap = clear that step's locks on this page's knobs ("STEP 5 LOCKS CLEARED")
  - hold a step + OCT- held 0.6 s = clear all its locks
  - OCT- without a held step keeps its bank / octave role
- **KEYS (SCL) and ENV:** pressing ENV while KEYS is on turns KEYS off and enters STEPS mode, and vice versa (my call:
  one key role at a time).
- **REC ALL and live recording:** in STEPS mode the keys are steps, so finger-drumming records nothing; REC ALL still
  arms tracks for MOTION knob recording. TRACKS mode is unchanged.
- **Playback:**
  - Entering step s, the track's lock events at s set their knobs for that step only, applied on top of the base,
    with the LFO modulating around the locked value as it does around the base.
  - At the next step without a lock for that knob, the knob returns to its base (or to the motion-held value if a
    motion event set one).
  - Motion events behave exactly as today.
  - **Locks are independent of MOTION (owner):**
    - locks always play, like PROB / RATCH, as part of the steps
    - MOTION's per-track PLAY switch and MOTION CLEAR affect only recorded motion events
    - the MOTION page shows both counts, e.g. "MOTION 40 · LOCKS 212"
  - **A turned-off step keeps its locks dormant (owner):** they play nothing while the step is off, come back when it
    is on again, and are removed only by hold + OCT- or a track or pattern clear.
- **Copy, clear, init:** CLEAR TRACK, INIT and pattern clears remove that track's locks as well as its motion. The
  builder lists every path that edits steps or motion and keeps the bank consistent.
- **SMC-Mixer and MIDI LEARN:** no lock entry from them in this version (they act as today).

## What is left to the builder

- The exact lock / motion flag encoding in the 3-byte event.
- The locked-step mark's pixels.
- The LOCKS FULL wording.
- The internal API split between motion.c and a new locks module.

The builder keeps:
- ISR safety (the main loop writes with the audio IRQ off, as motion does)
- the converter for old projects
- tests for:
  - every page's lock entry, snap back, LFO on a locked knob, motion plus lock on the same knob and step
  - clear on page and clear all
  - LOCKS FULL at 1024
  - song rows from other slots
  - save and load round trip, and old-project migration
  - USR3 clamp, with no write ever to an address outside the new lock range
  - the loader-scan offset staying 0xFF

Device checklist in DEVICE_INSTALL; README; CHANGELOG.
