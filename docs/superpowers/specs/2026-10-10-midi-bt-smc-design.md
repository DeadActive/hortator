# Bluetooth MIDI on the FM-1: scan, connect, MIDI in / out and the SMC-Mixer DAW mode — design

Date: 2026-10-10. Branch `midi` (worktree `felucca-midi`) from `acid` (b731c68: drum 0.15.0 = BLE part 1, plus ACID).
BLE MIDI work: 2026 DEADACTIVE. This is BLE parts 2, 3 and 5 of `2026-10-08-ble-midi-design.md` in one release; part
4 (MIDI learn) is not in it. Everything here is in the Bluetooth build only (`FELUCCA_BLE=1`, the package
`felucca-ble-*.fwsc`); the default build gains only the solo mask (§7) and the settings round-trip (§8).

## 1. Goal and decisions

The user (2026-10-10): "work on midi support, implement midi scan and midi connection on hortator firmware, also bring
smc-mixer daw mode (can be selectable in settings) from ~/Desktop/dev/smc-seq project, i want faders to control tracks
volume, knobs controls paramaters of voice (first 4 for now), mute, solo, record must works, as well as play, stop,
record, change track buttons, auto approve youself, i want i to make me a working firmware as result".

"Auto approve yourself": the design choices below were made by Claude, not asked; they are listed for the user in the
final report. The hard rules of the BLE design stay: nothing Bluetooth at boot (gate 3), the update path untouched
(gates 1, 2a, 2b), no flash writes from BT code, integer only, the default build byte-identical without the BT files
(gate 5).

## 2. The Bluetooth page

MENU (HOME held) gains a row **BLUETOOTH** (BLE build only) above ABOUT; OCT+ opens the page, OCT- goes back to the
menu. PRESETS moves the cursor; KNOB 1 changes the row's value; OCT+ acts on the row.

```
BLUETOOTH                        <- head
STATE   CONNECTED  SMC-Mixer     <- the state line (not a row)
BLUETOOTH        ON              row 0: KNOB 1 right / OCT+: ON; KNOB 1 left / OCT+ again: OFF
MODE             AUTO            row 1: AUTO / SMC DAW / MIDI
DEVICE           SMC-Mixer       row 2: the remembered device; KNOB 1 left: FORGET
SCAN                             row 3: OCT+: a 10 s scan
 1 SMC-Mixer        MIDI  -59    rows 4..: the devices found (MIDI ones first), OCT+ connects and remembers
 2 C4:12:..:9A            -80
PRESETS MOVE   OCT+ OK   OCT- BACK
```

- **BLUETOOTH ON** starts the stack (`ble_user_start`, the only trigger, gate 3). It stays started until power-off
  (the stack cannot stop). **OFF** disconnects and stops reconnecting. Off at every power-on; not saved.
- **State line:** OFF, STARTING, IDLE, SCANNING n s, CONNECTING, CONNECTED + the device's name, FAILED + the reason
  (`NO RF TRIM IN FLASH`, `BT START TIMEOUT`); the last error after a failed connect (`CONNECT TIMEOUT`,
  `NO BLE-MIDI SERVICE`, `DISCONNECTED 0x08`).
- **MODE** says what received BLE MIDI does: **SMC DAW** (§5), **MIDI** (§4), **AUTO** (default) = SMC DAW when the
  connected device's advertised name starts with `SMC`, else MIDI. Saved with the settings.
- **DEVICE:** the remembered device: its name when it was seen in a scan this power-on, else its address. `NONE` when
  there is none. KNOB 1 left forgets it (and disconnects). Saved with the settings (§8).
- **SCAN:** disconnects first if connected, then scans 10 s. Up to 8 devices are listed (the scan keeps 16), sorted
  as `ble list`: BLE-MIDI devices first, then by signal.
- **A device row, OCT+:** remembers it (saved) and connects to it; a connected device is disconnected first.
- Top-bar messages anywhere: `BT CONNECTED` on a connection, `BT LOST` when a connection drops.

## 3. Connecting and reconnecting

All in the BLE unit, one pure decision function `ble_auto_step` (host-tested) called each `ble_service` pass:
- **want** = BLUETOOTH ON and a remembered device. Without want, nothing connects by itself.
- **IDLE + want:** if the remembered address was seen in the current scan list → connect to it; otherwise start an
  **auto scan** (10 s), at most one every 2 s after the last scan ended or the last connection dropped.
- **Scanning (auto) + the remembered address reported** → the scan stops and the connection starts at once.
- **A manual scan (SCAN row)** pauses reconnecting while the Bluetooth page stays open, so the list can be read and
  another device picked; picking a device or leaving the page ends the pause.
- **Connection lost** (supervision timeout, the device switched off): the state goes back to IDLE and the cycle above
  reconnects when the device is seen again.

## 4. BLE MIDI in: MIDI mode (source 3)

- Received BLE-MIDI messages (already parsed to USB-MIDI packets by `ble_midi.c`) go into Hortator's MIDI input ring
  as **source 3**, with the same filter as USB (`usb_app.c midi_in_event`: channel voice messages, and the realtime
  F8 FA FB FC). The ring is filled by the TIMER5 ISR too, so the main loop enqueues with interrupts off.
- Notes on the drum channel (G_DRCH) fire the tracks whose NOTE matches, as USB and TRS do.
- **CLK** (G_CLOCK) gains **BLE** (value 3) in the BLE build: the realtime messages of source 3 then drive the clock.
  The tempo messages and the BPM knob's `CLK …` message say `CLK BLE`. A project saved with CLK BLE and loaded by the
  default build gets CLK INT (a G_CLOCK beyond the build's range loads as INT, not clamped to TRS).

## 5. The SMC-Mixer DAW mode

The M-VAVE SMC-Mixer in its DAW (Mackie Control) mode, with `smc-seq`'s checked mapping (`core/src/mapping.rs`):
faders = 14-bit pitch bend on channels 1–8; knobs = relative CCs 16–23 on channel 1, sign-magnitude (bit 6 = counter-
clockwise, bits 0..5 = clicks); buttons = notes on channel 1 (velocity > 0 pressed, 0 / note-off released); LEDs =
the same notes sent back, velocity 127 lit / 0 dark; fader pickup position = pitch bend sent back (`E0+ch, v, v`).

| Control | MIDI | Hortator |
|---|---|---|
| Fader n (1–8) | pitch bend ch n | track n LEVEL (0..127 = the 14-bit value >> 7), with pickup (below) |
| Master fader | pitch bend ch 9 | ignored |
| Knob 1–4 | CC 16–19 | the selected track's first four model knobs (P_E0..P_E3: TUNE DECAY TONE CHAR, ACID: TUNE DECAY CUTOF RESO), one value step per click, clamped; the top bar shows `T3 DECAY 64`-style text |
| Knob 5–8 | CC 20–23 | nothing (for now) |
| MUTE n | note 0x10+n−1 | mute / unmute track n, exactly as the TRACKS key (the MUTE: NEXT BAR setting applies) |
| SOLO n | note 0x08+n−1 | solo / unsolo track n (§7) |
| REC n | note 0x00+n−1 | arm / disarm track n for recording; arming while stopped starts play (as REC on TRACKS) |
| SELECT n | note 0x18+n−1 | select track n |
| PLAY | 0x5E | stopped: play (as the FM-1's PLAY); playing: nothing |
| STOP | 0x5D | playing (or a song armed): stop (as the FM-1's PLAY while playing) |
| REC | 0x5F | arm / disarm the selected track (as REC on TRACKS) |
| BANK ◀ / CHANNEL ◀ | 0x2E / 0x30 | the previous track |
| BANK ▶ / CHANNEL ▶ | 0x2F / 0x31 | the next track |
| REW, FF, arrows | 0x5B 0x5C 0x60–0x63 | nothing |

- Only presses act (releases are ignored).
- **Recording:** an armed, playing track records the FM-1's keys as live record does now; knob 1–4 turns and the
  selected track's fader are recorded as motion (`motion_capture`), as the FM-1's own knobs are.
- **LEDs:** MUTE n lit = track n muted (a mute waiting for the bar: lit too), SOLO n = soloed, REC n = armed, SELECT n =
  the selected track, PLAY = playing, STOP = stopped, REC = any track armed. Hortator keeps the mixer's LED state it
  last sent and sends only the changes; on every new connection it sends all of them.
- **Fader pickup** (smc-seq's soft takeover): after a connection, every fader is "not picked up"; Hortator sends each
  fader's position (its track's LEVEL), and the mixer blinks that fader's LED until the physical fader reaches it. An
  unpicked fader changes nothing until it comes within 2 of the LEVEL or crosses it; then it follows. When a track's
  LEVEL changes by any other means (the FM-1's knob, LOAD, INIT, motion), its fader is unpicked again and the new
  position is sent.

## 6. BLE MIDI out (part 3)

- A queue of up to 64 three-byte MIDI messages in the BLE unit; full: the message is dropped and counted (`ble`
  shows `out` and `out_drops`). Nothing is queued while not connected.
- `ble_service` sends as BLE-MIDI packets (MMA BLE-MIDI 1.0): a header byte `0x80 | ts[12:7]`, then per message a
  timestamp byte `0x80 | ts[6:0]` and the full message (no running status), at most 20 bytes per packet (the default
  ATT MTU), so at most 4 messages each; ts = milliseconds, 13 bits.
- Each packet is a write without response (`ATT_OP_WRITE_WITHOUT_RESPOND`, 6) to the BLE-MIDI characteristic's value
  handle; a packet is sent only when the stack's send buffer has room for it (`ble_op_att_get_remain`), else it waits
  for the next pass.

## 7. Solo

- `song.solo`: a bitmask of soloed tracks, runtime only (not in projects; cleared by INIT* and at power-on).
- A track is **silent** when `P_MUTE` is set, or when any track is soloed and it is not. Everywhere the engine reads
  P_MUTE to silence a track (`drum_core.c` hits, `fx.c` the mix and the COMP ghost) it reads `trk_silent()`; so do the
  mixer displays (HOME, TRACKS) that dim a muted track. P_MUTE itself, the TRACKS key LEDs and saved projects are
  unchanged.
- Solo is reachable only from the SMC (BLE build), but the mask is in both builds (always 0 in the default build); the
  golden kit hashes do not change.

## 8. Settings

The remembered device and MODE live in spare bits of the existing settings object (`persist_t`, unchanged size and
magic, so the flash format stays readable by older Hortator and by Felucca):
- `zoom` bits 5..6: MODE (0 AUTO, 1 SMC DAW, 2 MIDI); bit 7: the address type (0 public, 1 random);
- `zoom` bits 8..31: address bytes 0..2; `lowcut` bits 8..31: address bytes 3..5; all zero = no device.
- `settings` (.noinit) gains `btmode`, `bt_type`, `bt_lo`, `bt_hi` in both builds, loaded and saved every time, so the
  default build keeps a remembered device it never uses.
- An older Hortator reading a record with a device in it reads SPEAKER EQ as FLAT (its range check); nothing else
  changes.

## 9. Code layout

- **BLE unit** (`firmware/src/ble/`): `ble_central.c` gains the auto-connect step, the out queue and packer, the
  page's entry points; `ble_midi.c` the packer; `ble_core.h` the new entry points both ways.
- **Core unit:** `firmware/src/bt_page.c` (the page: draw, input, the state messages) and `firmware/src/smc.c` (the
  SMC layout: decode, actions, LEDs, pickup), both included by `felucca.c` under `#if FELUCCA_BLE` after the UI;
  `core_ble_api.c` gains `core_ble_midi_in(pkt)` (MIDI mode → the ring; SMC mode → `smc_in`).
- New BLE entry points the core calls: the state and error text, the scan list (name, address, type, RSSI, MIDI),
  scan, pick (target address), forget, want on / off, the pause, the connected device's name, `ble_midi_send`.

## 10. Tests and checks

- Host (tests/ble): the auto-connect step (want, target seen / not, cooldown, manual pause, lost link); the BLE-MIDI
  packer (header, timestamps, 4-message cut, 20-byte limit) and the queue (full, drop count, not connected); the SDK
  ABI gains `ATT_OP_WRITE_WITHOUT_RESPOND` and `BLE_CMD_ATT_VAILD_LEN`.
- Host (core): the SMC decode (pitch bend → 0..127, sign-magnitude knobs, note rows); each action (level, the four
  knobs per model, mute incl. NEXT BAR, solo, REC arm incl. start, select, prev / next, PLAY, STOP, REC); LED diffing
  (only changes, full refresh on connect); pickup (near, crossing, unpicked by an FM-1 change, re-sent position);
  MIDI mode → source 3 in the ring with the USB filter; CLK BLE; solo silences in the engine (`trk_silent`), the golden
  kit hashes unchanged; the settings bits round-trip and an old record (no bits) loads as before; G_CLOCK clamped on
  load.
- The page: screenshots of each state (off, scanning with a list, connected, failed) to the user.
- Every gate as for part 1 on the BLE build (1, 2a, 2b, 3, 4, `.ram_text`); the default build: suites, H2, H3, the
  budgets, gate 5.

## 11. Device test (the user)

`docs/ble/DEVICE_STEPS_MIDI.md`: install the BLE package; MENU → BLUETOOTH → ON; SCAN; pick the SMC-Mixer; faders,
knobs 1–4, MUTE / SOLO / REC / SELECT, PLAY / STOP / REC, ◀ ▶; the LEDs and the fader pickup; record (REC n + the FM-1's
keys, a knob turn as motion); switch the SMC off and on (it reconnects); power-cycle the FM-1, BLUETOOTH ON (it
reconnects to the remembered device); OCT- + OCT+ 5 s → UBOOT with BT on; an update with BT on; `ble` shows
`audio_late 0` while playing with the faders moving.
