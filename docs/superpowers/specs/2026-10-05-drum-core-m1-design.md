# FM-1 Drum Machine — Milestone 1: Drum Sound Core

Date: 2026-10-05 · Branch: `drum-m1` (fork of hugelton/Felucca @ 1e838e1, remote `upstream`)
License: GPL-3.0-only (inherited). Credits to Felucca kept.

## 1. Goal

A drum-only firmware for the M-VAVE FM-1, built on Felucca. Milestone 1 (M1) delivers the
**sound core**: 8 drum tracks with swappable 808/909-style models, an optional sample layer
per track, Felucca's full FX chain, MIDI/key triggering, a minimal device UI and the existing
sequencer adapted to 8 drum tracks. Everything is verified **on the host (simulator) only**;
the real firmware is built but **never installed** in M1.

### Decisions (from the brainstorm)

| Topic | Decision |
|---|---|
| Device access in M1 | Simulator only. No install, no device writes. |
| Real firmware build | Yes — build `felucca.bin` / `ota.bin` to verify toolchain and flash size. |
| Repo | Fork with history: `./felucca`, branch `drum-m1`, `upstream` = hugelton/Felucca. |
| Approach | A: drum-centric core; synth engines and `voice.c` removed; FX, seq timing, USB, OTA, HAL untouched. |
| Models | Own integer 808/909 models (always) + Plaits models ported to C (float) behind `DRUM_PLAITS=1`. |
| Model set in M1 | Kick/snare 808+909, clap 808+909, 808 hats, 808 cymbal, tom/conga, rimshot, claves, cowbell, sample. |
| Voices | Per-model: 1 voice (kick, snare, clap, hats, rim, claves, cowbell) or 2 (cymbal, tom/conga, sample). |
| Layering | Every track: synth model + optional sample layer. |
| FX | All 8 tracks get DIST + SLICER + chorus/delay/reverb sends; master low-cut + limiter as today. |
| Triggering | MIDI ch 10 (global setting), per-track note (GM defaults); 8 white keys from F3 = tracks 1–8. |
| Device UI | Minimal pages: TRACK, SOUND, LAYER, FX, GLOBAL. |

Out of scope for M1 (own specs later): probability / ratchets / Grids mode (M2), Streams
compressor + sidechain (M3), hardware phase with flash backup and first install (M4), web
editor + user presets (M5).

## 2. Safety and build pipeline

### 2.1 No device writes in M1
- The build produces `build/felucca.bin` and `build/loader/ota.bin`.
- Packaging (`.fwsc`) runs only with `DRUM_PACKAGE=1` and is named `*-UNTESTED.fwsc`; it is
  used only to check that the package builds and fits.
- `tools/fm1_install.py` and the web installer are never run. `mido` / `python-rtmidi` are not installed.

### 2.2 Code frozen to upstream
These files must stay byte-identical to `upstream` @ 1e838e1:
`firmware/hal/*`, `firmware/loader/*`, `firmware/src/ota.c`, `firmware/src/usb.c`,
`firmware/crt0.S`, `firmware/app.ld`, and `firmware/src/storage.c` except one line: the
settings record magic `ST_MAGIC` (storage.c line 14) changes value; `check_untouched.sh`
allows exactly that one-line difference in that file and nothing else.
`tools/check_untouched.sh` compares them with `git diff --exit-code 1e838e1 -- <paths>`;
`tests/run_tests.sh` fails if it fails. Rationale: USB-MIDI update entry is the primary
recovery path; it must never be broken by our changes.

### 2.3 Flash layout
Unchanged: app area, user sample slots (0xA0000..0xDBFFF), user preset banks, project slots,
settings and update records keep their addresses. No new flash regions. The settings/project
record magic changes (new value, e.g. `"FDRM"`) so leftover Felucca records are ignored, not misread.

### 2.4 Device identity
Package family stays `FM-1_9xx` so M-VAVE's updater and the Felucca web installer still
recognise the device for going back. Version string `DRUM-0.1`.

### 2.5 Recovery plan (executed in M4, documented now)
1. Before the first install: XIAO RP2040 + FM-1-transporter, `fm1t.py dump backup.bin`
   (full 1 MiB). Store the dump in two places.
2. Going back: M-VAVE official updater (official firmware) or the Felucca web installer.
3. If the FM-1 does not boot: FM-1-transporter writes the backup (sectors 0x4000–0x93000;
   it never touches the boot area), entering the mask-ROM bootloader via USB_KEY.

Facts relied on (from the code/README; not yet verified on hardware): the update loader only
writes 0x4000..0xFC000 (`FL_RANGE_OK`); an update commits only after the host's "success";
an interrupted write is finished by re-running the installer; the mask-ROM bootloader cannot be erased.

### 2.6 Build environment (macOS arm64)
1. Python venv `.venv` with Pillow.
2. `tools/get_toolchain.sh` → `~/.jieli/toolchain`.
3. `git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK`.
4. Docker `linux/amd64` (Rosetta) as `build.sh` does.
5. **Baseline first:** unmodified Felucca must build and pass `tests/run_tests.sh` before any change.
   If the baseline fails, M1 stops and the cause is reported.

## 3. Architecture

Felucca is a single compilation unit (`felucca.c` includes the sources). Kept.

### 3.1 Files
New:
- `firmware/src/drum_core.c` — 8 tracks: trigger, voice allocation, choke groups, sample layer, per-track render.
- `firmware/src/dmodels.c` — model table; includes:
  - `dm_dsp.c` (helpers), `dm_sample.c` (sample playback), `dm_kick.c` (kicks + pitched body),
    `dm_snare.c` (snares, claps), `dm_perc.c` (tom, conga, rimshot, claves) — grouped by instrument,
    not by 808/909, because the two versions of an instrument share code,
  - `dm_metal.c` (shared 6-square source: hats, cymbal, cowbell),
  - `dm_plaits.c` (Plaits models in C, float; compiled only with `DRUM_PLAITS=1`).

Kept, changed: `core.h`, `seq.c`, `fx.c`, `slicer.c` (NTRK = 8), `params.c`, `ui.c`,
`ui_draw.c`, `ui_input.c`, `ui_menu.c`, `panel.c`, `project.c`, `felucca.c`, `main.c` (if needed),
`tools/gen_samples.py` (drop synth sets), `build.sh` (packaging guard), `tests/*`.
Kept as is: `eng_sample.c` (decoder + user slots), `dsp.c`, `audio.c` (except the voice-shed
hook), `lcd.c`, `gfx.c`, `icons.c`, `midi_uart.c`, `console.c`, `libc.c`, and all frozen files.

Removed: `eng_analog.c`, `eng_digital.c`, `eng_phase.c`, `eng_lofi.c`, `eng_formant.c`,
`eng_trio.c`, `eng_drawbar.c`, `eng_grain.c`, `eng_slice.c`, `engines.c`, `voice.c`, `drums.c`;
arpeggiator, scales/quantize, glide and voice modes in `seq.c` / `params.c` / `ui*`;
PIANO, TRANH, FLUTE, SAX sample sets (KIT and PERC stay). `upreset.c` and `editor.c` are
compiled out in M1 (`DRUM_EDITOR=0`); user sample upload stays only if it works without the
full editor protocol (checked during planning; otherwise deferred to M5).

### 3.2 Data

```c
typedef struct {                  /* one drum model */
    const char *name;             /* "808 KICK" */
    uint8_t voices;               /* 1 or 2 */
    uint8_t group;                /* default choke group, 0 = none */
    param_desc_t edit[8];         /* TUNE DECAY TONE CHAR + 4 extras */
    void (*trigger)(dtrack_t *t, dvoice_t *v, uint32_t vel);
    void (*render)(dtrack_t *t, dvoice_t *v, int32_t *out, uint32_t n);
} dmodel_t;

typedef struct dtrack {           /* one of 8 */
    int16_t p[DP_COUNT];          /* model, 8 model params, level, pan, sends, dist,
                                     slicer params, choke, midi note,
                                     layer set/zone, layer level, layer tune, layer decay */
    dvoice_t v[2];                /* model voices */
    dvoice_t lv[2];               /* sample-layer voices */
    /* sequencer fields (trimmed), dist + slicer state as today */
} dtrack_t;
```
`dvoice_t` holds per-voice state (phases, filter states, envelopes, age, active, declick tail).

### 3.3 Signal flow per audio block
seq + MIDI events → hits → per track: model voices + layer voices → track buffer → DIST →
SLICER → level/pan + sends → chorus / delay / reverb buses → master (low-cut, limiter).
Everything after the track buffer is Felucca's existing code looped over 8 tracks.

## 4. Models, voices, sample layer

Common macro knobs for every model: **TUNE / DECAY / TONE / CHAR**; 4 extras on page 2.

| Model | Method | Voices | CHAR | Choke |
|---|---|---|---|---|
| 808 KICK | pinged resonator, pitch dip, soft clip | 1 | pitch dip | – |
| 909 KICK | sine + pitch env, click/noise transient, drive | 1 | click | – |
| 808 SNARE | 2 resonators (~173/336 Hz) + noise BP 3–5 kHz | 1 | snappy | – |
| 909 SNARE | 2 sines, separate decays + LP/HP split noise | 1 | snappy | – |
| 808 CLAP | noise BP ~1 kHz, 3 sawtooth bursts + tail | 1 | tail | – |
| 909 CLAP | tighter, randomized bursts | 1 | spread | – |
| 808 HAT C / HAT O | metal source → BP 3.4/7.1 kHz → decay | 1 | brightness | 1 |
| 808 CYMBAL | metal source, two bands, separate decays | 2 | band balance | – |
| TOM / CONGA | resonator + subtle pitch drop (+ noise for tom) | 2 | pitch drop | – |
| RIMSHOT | 2 resonators (1,667/455 Hz) → drive → HP | 1 | drive | – |
| CLAVES | resonator ~2.5 kHz | 1 | pitch | – |
| COWBELL | squares 540/800 Hz → BP ~880 Hz, 2-stage decay | 1 | tail | – |
| SAMPLE | Felucca ADPCM player (built-in kit or user slot) | 2 | drive (a start offset is not possible: IMA ADPCM must be decoded from the start) | – |
| Plaits (flag) | analog/synth kick, analog/synth snare, hi-hat ×2 | 1 | per model | – |

- **Voices:** retrigger takes a free voice, else the oldest; a stolen voice fades over ~1 ms
  (declick tail, as `drums.c` does today).
- **Choke:** per-track group 0–4; a hit cuts other tracks in the same group with the same fade.
  Hat C / Hat O default to group 1.
- **Sample layer:** SET/zone, LEVEL (0 = off), TUNE, DECAY. Triggers with the model, same DIST /
  SLICER / sends; layer voices = model voices.
- **Velocity:** from MIDI or step accent; scales level, and TONE where the model supports it.
- **Fixed point:** 32-bit filter states for low-frequency resonators; one noise state per voice.
- **CPU target:** typical 150–300 cycles/sample, worst case ≤ ~600 (estimates; verified in sim).

## 5. MIDI, keys, UI, sequencer, storage

- **MIDI:** channel = global setting (default 10). Per-track note, defaults 36, 38, 39, 42, 46,
  45, 37, 49 for tracks 1–8. Note-on triggers all tracks with that note at its velocity;
  note-off ignored. Clock/sync and USB-MIDI in/out unchanged.
- **Keys:** the 8 white keys from F3 trigger tracks 1–8 at velocity 100; SHIFT (or the existing
  modifier) + white key selects the track. Black keys unused in M1.
- **Pages:** TRACK (MODEL / LEVEL / PAN / CHOKE; ALGORITHM cycles tracks), SOUND (macros + page 2),
  LAYER (SET / LEVEL / TUNE / DECAY), FX (existing DIST / sends / SLICER pages for the selected
  track), GLOBAL (BPM, swing, MIDI ch, sync, FX bus params, info). Existing drawing/input code reused.
- **Sequencer (adapted only):** 8 tracks × 64 steps; a step = on/off + accent (velocity);
  per-track length / rate / swing (`P_SLEN`, `P_SDIV`, `P_SSWING`); play/stop; live recording of hits.
  Chords, ties, slide and the arpeggiator are removed.
- **Storage:** new record magic; one kit + patterns per existing project slot.

## 6. Testing and success criteria

### 6.1 Host simulator (extends `tests/hostsim.c`)
- `drumsim MODEL out.wav` — each model at several velocities/settings.
- `drumsim KIT out.wav` — 8-track demo pattern through the full FX chain + limiter.
- `BENCH=1` — cost per model and for the kit.

### 6.2 Automated checks (`tests/run_tests.sh`)
- `check_untouched.sh` passes (§2.2).
- Golden hashes per model render (`tests/golden.txt` mechanism).
- Levels: nothing exceeds full scale after the limiter; each model decays to exact silence;
  an idle render (no hits) is digital silence.
- Choke: open hat silent within ~2 ms (≤ 100 samples at 44.1 kHz) of a closed-hat hit.
- Voices: a 2-voice model's tail survives one retrigger; a 1-voice model's steal has no step
  larger than a set threshold (declick).
- Cost: worst-case kit (8 tracks, 2-voice models + layers, every FX) ≤ 1,566 host instructions per
  sample (`proc_pid_rusage`, as Felucca's `regress.c`) — the cost of stock Felucca's heaviest mix
  (`cpu/mix/3parts_full_drums`), which runs on the FM-1. If it is exceeded, the user decides
  (optimise, cap voices, or rely on overload shedding). Target-side loop budgets
  (`tests/target_budget.txt`) are recorded in M1-B with the real build.
- Felucca's existing host tests that remain applicable (storage, MIDI parser, OTA entry,
  loader, installer, package format) still pass unchanged.

### 6.3 Real build
`./build.sh` produces `felucca.bin` + `ota.bin`; the app fits `APP_SLOT` (0x8DFBC bytes); the
build prints the remaining space. Package only with `DRUM_PACKAGE=1` (`*-UNTESTED.fwsc`).

### 6.4 Listening review
WAV renders of every model and the kit demo are sent to the user; the user approves the sound.

### 6.5 Done when
1. Unmodified Felucca baseline builds and passes its tests.
2. The drum firmware builds and fits in flash.
3. All checks in §6.2 pass.
4. The user approves the renders.
5. Nothing has been installed on the FM-1.

## 6.6 Plans
M1 is implemented in three plans: **M1-A** host drum core (models, layer, mix, sequencer, tests, WAVs);
**M1-B** device UI, firmware integration (`felucca.c`, `main.c`, `audio.c`, `project.c`), real drum build,
`tests/run_tests.sh`; **M1-C** Plaits models behind `DRUM_PLAITS=1` (approach set by the C++ probe in M1-A Task 1).

## 7. Risks

| Risk | Mitigation |
|---|---|
| JieLi toolchain/SDK download fails (gitee/JieLi servers) | Report and stop; simulator work can proceed without it. |
| Docker/Rosetta issues on macOS 26 | Report; try native Linux container image options only with user approval. |
| Plaits is C++; JieLi clang 4 C++ support unknown | Port to C; keep behind `DRUM_PLAITS`; host-measure only. |
| FM-1 float speed unknown | Plaits flag stays off by default until measured on hardware (M4). |
| UI pages touch much shared code | Keep drawing/input code; only change page tables and parameter bindings. |
| Sample upload depends on `editor.c` | Checked in planning; defer to M5 if coupled. |
