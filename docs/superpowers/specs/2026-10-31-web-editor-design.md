# Web editor for Hortator — design

Date: 2026-10-31. Status: approved by the owner 2026-10-31 (questions answered below). Credit: DEADACTIVE.
Base: branch `editor` 95b9899 = drum 0.26.0 (4e40f27, released) + fx027 (SRR and INSERT on every track, DLY MODE,
USB 10 CH order; unreleased). Prior art: upstream Felucca's editor (hugelton/Felucca 5d9694d: `firmware/src/editor.c`,
`upreset.c`, `web/editor.html`, `web/EDITOR_PROTOCOL.md`; GPL-3.0-only, © Leo Kuroshita / Hügelton Instruments).
We reuse its protocol shape and parts of its code with credit; the data model (8 drum tracks, variations, lock banks) is
Hortator's own.

## Intent

A web page that talks to a Hortator FM-1 over USB MIDI: back up and restore presets and projects, edit the sound live,
browse factory presets, and see and edit patterns — without ever putting a firmware update at risk.

## Owner's decisions (binding)

The editor does all of these:
1. **User presets backup / restore**: all 128 user presets to one file and back; rename, delete, reorder in the page;
   restore onto another FM-1.
2. **Project backup / restore**: projects A–D with their lock banks and variations, to files and back.
3. **Live sound editing**, both ways: the selected track's model and every knob on every page (SOUND, LAYER, FX,
   FILTER, RESON, SLICER, LFO / DEST, SRR, INSERT, DLY MODE).
4. **Factory preset browser**: the 151 presets (MACHINEDRUM 55, MODERN 40, CLASSIC 56); load one onto a track.
5. **Step grid per variation**: every track's steps in variations 1–8 of a slot (on/off, note, PROB, RATCH, ACC,
   FILL / !FILL), editable.
6. **Locks view / edit** per step and track; clear.
7. **Remote cue**: click a slot or variation to queue it, exactly like the FM-1's cue keys (black keys = variations,
   SAVE + white 1–4 = slots, as of 0.26.0).
8. **Copy** variations and slots, between slots or from a backup file.

0.26.0 shipped without the editor. The editor ships as 0.28.0, after fx027 as 0.27.0 (decision 1 below).

## Non-goals

Bluetooth as the editor's link (USB only, see Transport); user sample upload; settings / MIDI LEARN map backup; song
chain editing; reading upstream Felucca's backup files; editing anything during an update.

## What the user sees

- **Connect**: one button. The browser asks for MIDI (SysEx) permission. The page lists only ports whose name contains
  "Hortator" (the USB product string since frozen-base-9) and asks the device who it is. A Hortator with the editor:
  connected, tabs appear. A Hortator without it (0.26.0 and older): "Update this FM-1 to use the editor" + installer
  link. A port named "Felucca": "This FM-1 runs Felucca — use Felucca's editor". Anything else: nothing is sent.
- **Header bar** (always): PLAY / STOP state, loaded slot and variation, unsaved marks (A*), the selected track 1–8.
  Choosing a track in the page selects it on the FM-1 and the other way round: there is one selection.
- **Sound tab**: the selected track's pages as on the device (SOUND by model, LAYER, FILTER, FX, INSERT, SRR, RESON,
  SLICER, LFO 1/2, DEST, and the globals page with DLY MODE). Each knob shows the device's own text ("1/16.",
  "1.4k"). Turning a knob on the FM-1 moves it in the page within a frame.
- **Presets tab**: left, the factory banks (MD / MODERN / CLASSIC) with a "Load onto track N" button. Right, the 128
  user slots: drag to reorder, rename, delete, "Save track N here". "Back up" downloads `hortator-presets-<date>.json`;
  "Open file" loads one into the list for editing; "Write to FM-1" replaces the device's 128 slots with the list.
- **Projects tab**: four cards A–D (name, saved / unsaved, variations used, locks count). Per card: "Back up" →
  `hortator-project-<A..D>-<date>.json`; "Restore here…" (pick a file); "Copy to…" (another slot). "Back up all"
  → one `hortator-backup-<date>.json` (presets + 4 projects). Restores and copies ask for confirmation and say what
  is replaced, including unsaved changes on the FM-1.
- **Patterns tab**: slot picker A–D and variation strip 1–8 (playing one lit, queued one blinking, as the keys).
  Clicking a slot / variation **cues** it (same rules and refusals as the keys: shown as the device's message).
  Below, an 8 × 64 grid (tracks × steps, LEN shown per track); a step cell shows on / accent / ratchet count /
  PROB % / FILL mark; click toggles, a side panel edits NOTE, PROB, RATCH, ACC, FILL / !FILL (SLIDE / TIE on ACID).
  A lock dot marks steps with locks; the **Locks** panel lists them per track (step, knob, value) with edit and
  clear (step / track). Locks belong to the slot and apply in every variation, as on the device. "Copy variation…"
  copies a variation (this slot, another slot, or a file) into a variation of the loaded slot. "Save on FM-1" is the
  device's SAVE.

Editing rules (same as the device): edits change the loaded slot in RAM (A* until saved). Other slots and their
variations are shown read-only from flash. Editing a variation that is not playing: owner question 3.

## Safety rules

1. **Updates first, always.** Measured facts the design relies on:
   - The update's frames start `F0 00` (ota.c packs `00 59 …` as a bit stream, first byte 0); the stock keys are
     `F0 22 24 35 7D/7F F7`. Editor frames start `F0 7D 48 54` ("HT"; upstream Felucca uses `7D 46 4C`). The three
     never overlap at the first data byte.
   - usb_app.c (frozen) holds ONE received frame (`sx_frame`, 640 B); frames arriving while it is full are dropped and
     the update host retries. main.c (frozen) calls `ed_service()` then `ota_service()` each pass, and `ed_service()`
     again in its 15 ms wait. `ed_service` takes only `7D 48 54` frames and frees them in the same pass; anything else
     is left for `ota_take()`. During `ota_session()` the editor never runs and `ota_take()` discards its frames.
   - Any update traffic (a frame not starting `7D`, or the upgrade command `usb.ota_req`) makes the editor go silent
     for 10 s: no replies, no pushes, a restore not yet committed is dropped. The page sees silence and shows
     "FM-1 busy (update?)".
   - The page keeps at most one request in flight (300 ms timeout, 3 tries), so it can hold the frame slot for one
     main-loop pass at most.
   - Today any SysEx pauses MIDI clock / THRU out for 3 s (midi_out.c `mout_paused`). Change: only update traffic
     pauses it; editor frames do not (midi_out.c is not frozen).
2. **Frozen files untouched**: no change to ota.c, usb.c, usb_app.c, main.c, storage.c, hal/, loader/; no new flash
   object (the restore writes the existing objects through `st_save`). `check_untouched.py`, `check_update_path.py`
   and the loader hash pass unchanged.
3. **Flash writes only while stopped, never during an update**, through the device's own save order and A/B commit:
   variations, then lock bank, then the project record (which links both by length + link hash, `proj_fetch` /
   `proj_var_flash` look in both copies), so a power cut anywhere leaves the old or the new project whole. A
   project's three writes run in one uninterruptible step (~0.2 s, like SAVE); the update check happens right before
   it. Playing → the device answers "STOP FIRST" and writes nothing.
4. **Flash is the truth**: a backup reads the saved flash copies, never RAM; unsaved changes are flagged ("A has
   unsaved changes — SAVE on the FM-1 to include them"). After a restore the device reloads that slot from flash.
5. **Audio ISR untouched**: editor code runs only in the main loop; knob writes use the same setter as the device's
   knobs, lock edits the same guarded lockbank calls as motion.c, cue the same `cue_slot` / `cue_var`.
   `tests/target_budget.txt` does not change.
6. **Validation on the device**: every frame's length is checked per command (upstream `ed_args_ok`); restored
   objects pass the same checks as at boot (`proj_ok` or a known older project magic, `lk_valid`, `var_valid`, UPR1 +
   model range + knob clamps) and their CRC-32 before anything is written. Requests name objects, never addresses.

## Protocol (Hortator editor protocol 1)

Frame `F0 7D 48 54 <cmd> <args> F7`. Values: 14 bit, two 7-bit bytes LSB first, offset 8192 (as upstream). Bulk
data: 7-bit groups (upstream `ed_unpack7`), 256 data bytes per piece (≤ 306 B on the wire, under the 640 B slot).
Every request gets one reply with the same cmd and an rc (0 ok, 1 bad request, 2 validation, 3 stop first, 4 flash
error, 5 stale / changed, 6 busy). Full byte layout goes to `web/EDITOR_PROTOCOL.md` (task 1), replacing upstream's.

| cmd | name | does |
| --- | --- | --- |
| 1 | INFO | "HORTATOR", firmware version, protocol 1.minor, capability bits, NTRK / NSTEP / P_COUNT / G_COUNT / models / 151 / 128, state |
| 2 | PAGES | the device's page table (titles, which knobs) |
| 3 | DESC | a knob: name, range, default, format, value names; SOUND knobs per model |
| 4–5 | GET / SET | a track or global value; reply = value as clamped + device text |
| 6 | DUMP | a track's model and all values |
| 7 | SELECT | select track 1–8 |
| 8 | WATCH | on / off (3 s keep-alive). Pushes: CHANGED (knob), STATE (play, slot, var, selection, unsaved), STEPS_CHANGED, LOCKS_CHANGED |
| 9–10 | NAMES / PRESET_LOAD | preset names by bank; load preset id onto a track |
| 11–12 | STEPS_GET / STEPS_SET | 64 steps of a track (any slot / variation to read; write = loaded slot's playing variation) |
| 13–14 | LOCKS_GET / LOCK_EDIT | a track's locks; set / clear one, a step, a track |
| 15 | CUE | slot or variation, as the keys; rc carries the device's refusal (SAVE C FIRST, VARIATIONS FULL…) |
| 16 | SAVE | the device's SAVE of the loaded slot (stopped only) |
| 17–18 | BACKUP_LIST / BACKUP_GET | objects with size, CRC-32, sequence; read a piece |
| 19 | RESTORE | begin (object, target slot, size, CRC) / data / commit / abort |

Objects: 0 user presets (1,284 B), 1–4 project record A–D (3,840 B), 5–8 lock bank A–D (3,208 B), 9–12 variations
A–D (8 + used, ≤ 3,584 B). A project restore is one begin… commit of three objects, re-sealed on the device for the
target slot (slot fields, links, sum), so a project file restores into any slot.

**Versioning.** INFO carries protocol major.minor and capability bits. The page refuses a different major ("use the
editor from your firmware's release", link), hides features whose bit is missing, ignores unknown push types.

## Staging (no new buffers)

Restore data goes straight into the RAM homes the device already has, then into flash through the existing save:
records → `proj_slot[s]`; lock bank → `lk_slot[s]`; variations → the park buffer `var_buf[var_play ^ 1]` (cue
cancelled, stopped); user presets → `upr`. During a restore (begin to commit, 3 s idle timeout) the FM-1 shows
"RESTORING" and refuses PLAY, cue, LOAD and SAVE. Abort or timeout: the homes are reloaded from flash (`proj_fetch`,
`preset_user_boot`). Rejected alternative: staging in `st_buf` (other main-loop code uses it between frames).
Backups read the linked flash copies directly through XIP; if a copy changes mid-read (its sequence moves), GET
answers rc 5 and the page re-reads that object.

## Files

JSON, UTF-8, one envelope: `{"format": "hortator-presets" | "hortator-project" | "hortator-backup", "version": 1,
"firmware": "0.28.0", "protocol": 1, "created": …, …}`. Presets are readable (`slots[128]`: `{name, model,
modelName, knobs[8]}` or null), so rename / reorder / delete is plain list editing; on write the page builds the
UPR1 record and its CRC. Projects keep the device's objects byte-exact (base64, size, CRC-32 each) plus a readable
summary (name, tracks' models, variations used, lock count). Readers ignore unknown keys; a higher `version` is
refused with "made by a newer editor". A project record newer than the FM-1 knows is refused by the device at begin
(rc 2, "update the FM-1 first"); older formats the firmware converts at load are accepted. Projects remember user
presets by slot number only, so restoring presets together with projects keeps names matching.

## Numbers

- **RAM** (BLE build at 95b9899, measured today with `FELUCCA_BLE=1 ./build.sh`): .data + .bss 93,136 B of 98,304
  (5,168 B free); pool 334,232 of 344,064 (9,832 free); image 510,064 B. Editor budget: ≤ 640 B .bss (state, 64 B
  request copy, push shadows: selected track 76 + globals 42 values = 236 B, per-track step / lock hashes 32 B) and
  ≤ 400 B pool (the reply buffer). Large requests are read in place from `sx_frame`. build.py prints the editor's
  share; a gate fails the BLE build above 94,160 B.
- **Full backup**, worst case: presets 1,284 + 4 × (3,840 + 3,208 + 3,584) = 43,812 B = 174 pieces ≈ 53 KB on the wire
  (7 USB packets a piece). One request at a time, ~10–25 ms a piece (USB 1 ms frames, `ed_service` also polled in the
  15 ms wait) → **about 2–4 s**; typical projects (variations mostly empty: 22 B) are smaller. Restore adds 13 sector
  erases + writes (erase ~45 ms datasheet-typical, IRQs off as every save) ≈ 1 s → **about 3–6 s**. Budget asserted
  on hardware: ≤ 10 s each.
- **Live edit latency**: a SET is one USB packet, handled within ~1 ms (≤ ~20 ms if it lands during a screen draw);
  the sound changes at the next audio block. FM-1 → page: changes are compared each pass and pushed (≤ 4 frames a
  pass, only into a half-empty SysEx ring, so they never wait) → ≤ ~30 ms.
- Big transfers while playing: a 306 B reply holds MIDI clock out for ~7 ms (SysEx is never split by notes). The page
  suggests stopping first when SYNC out is on.

## Transport

USB MIDI SysEx only. Bluetooth: not needed (the update path is USB-only anyway) and not feasible cheaply — bt_midi.c
carries no SysEx, BLE MIDI packets are ~20 B, and the BLE build has 5 KB of RAM left. Both builds get the editor over
USB.

## Publishing, browsers, security

- Page sources under `web/editor/` (split for parallel work), inlined by `web/make_site.py` into one self-contained
  `webapp/editor/index.html` (no CDN, the font embedded); `tools/publish.sh` publishes it with the release. Offline:
  "save page" and open the file locally (to verify on Chrome in task 9); no service worker.
- Chrome / Edge / Opera on desktop and Android; Firefox with its site-permission prompt; Safari has no Web MIDI and
  gets a notice.
- Nothing is sent before the user clicks Connect; only INFO to "Hortator" ports; nothing else until INFO answered.
  Every outgoing frame comes from one table and starts `F0 7D 48 54` — the page has no way to send `F0 00` (update),
  `7D 7F` / `7D 7D` keys or another manufacturer's header (a web test enforces it).
- Today's site still publishes upstream's 4-track Felucca editor at `webapp/editor`, which cannot talk to Hortator
  (see question 4).

## Port vs rewrite

- **Port with credit**: firmware framing helpers (`ed_begin`, `ed_b`, `ed_v`, `ed_str`, `ed_send`), `ed_unpack7`,
  the `ed_service` frame peek, per-command length checks, the live-sync scheme (shadows, `ED_PUSH_MAX`, half-empty
  ring rule, WATCH keep-alive), the backup LIST / GET / PUT shape and rc table. Page: Web MIDI connect and port
  handling, the request queue, 7-bit packing, the mock-device test harness, file save / open.
- **Rewrite**: the command table and object ids (Hortator's data model), restore staging and commit (Hortator's
  storage), patterns / locks / cue, all page views, file formats. `upreset.c` is not ported (Hortator's user presets
  live in presets.c / project.c).

## Tests (behaviour → test)

- Firmware host test `tests/editor_test.c` (flash_host.h): every command's reply and refusals; an update frame
  arriving while an editor frame is pending is not lost on retry, and the editor then stays silent 10 s; `ota_req`
  during a restore → nothing committed; power cut at every program step of a project restore → old or new project
  loads whole (A/B); abort / timeout reloads RAM from flash; restore while playing → rc 3, flash untouched; backup bytes
  = flash bytes, CRC matches; a project restored into another slot loads and plays; editor frames do not pause clock
  out, update frames still do.
- Gates: `check_untouched.py`, `check_update_path.py`, loader hash, `target_budget.py` unchanged, BLE RAM gate.
- `web/test_web.mjs` against a mock device: connect / refuse (Felucca, stock, old Hortator, newer major); outgoing
  frame whitelist; file round trip byte-exact; rename / reorder / delete → correct UPR1 record; unknown keys ignored,
  newer version refused; cue refusals shown.
- Hardware (DEVICE_INSTALL.md checklist): back up FM-1 #1, restore onto FM-1 #2, projects play identically; run the web
  installer with the editor open and watching → update succeeds; live edit both ways; timings within budget.

## Tasks

1. Protocol document `web/EDITOR_PROTOCOL.md` (Hortator 1) and the mock device. *First; everything uses it.*
2. Firmware: editor core (`editor.c`: INFO, PAGES, DESC, GET / SET, DUMP, SELECT, WATCH pushes), the update-silence
   rule, midi_out.c pause change, RAM gate.
3. Firmware: backup / restore (`editor_backup.c`: LIST, GET, RESTORE, staging, "RESTORING" lock, commit).
4. Firmware: patterns (`editor_pattern.c`: STEPS, LOCKS, CUE, SAVE, NAMES / PRESET_LOAD).
5. Page shell: connect / detect / refuse, request queue, header bar, Sound tab.
6. Page: Presets tab (factory browser, user librarian, presets file).
7. Page: Projects tab (backup / restore / copy, backup-all file).
8. Page: Patterns tab (grid, locks, cue, copy variation).
9. Site: make_site.py inlining, landing link, offline check, test wiring.
10. Hardware verification, timings, CHANGELOG / README / DEVICE_INSTALL.

Parallel worktrees: after 1, tasks 2 / 3 / 4 (separate included files) and 5 can run at once; 6 / 7 / 8 after 5 (separate
source files); 9 any time after 5; 10 last.

## Risks

- RAM: 5,168 B free in the BLE build; the editor's ~1 KB must stay within its gate as other features land.
- The single frame slot: a chatty page could delay an update's handshake by retries. Mitigated by one request in
  flight, the 10 s silence and the "installer with editor open" hardware test.
- Restore of projects from other firmware versions: covered by the device's own load checks; newer ones refused.
- Clock jitter during large transfers while playing (documented, page warns).
- Web MIDI availability (no Safari); the published old editor confusing users until replaced.

## Owner decisions (2026-10-31)

1. **Release:** fx027 ships first as 0.27.0; the editor ships on its own as 0.28.0, with its own hardware round.
2. **User preset names:** live only in the page and the files. The FM-1 keeps showing "MODEL NN"; no flash-format
   change in 0.28.0. Storing names on the device can follow later.
3. **Editing a variation that is not playing:** the FM-1 switches to it (at once when stopped, queued like the cue key
   when playing), so the page always edits what plays. No extra RAM.
4. **Upstream Felucca's old editor page:** removed from the site in 0.27.0 (it cannot talk to Hortator).
