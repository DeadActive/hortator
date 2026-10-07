# FILTER: a per-track filter with an envelope — design

Date: 2026-10-08. Branch `filter` from `cowb-tune` (main `drum-v0.13.0` + the COWB TUNE fix, which ships with this).
First of three parts the user ordered (filter, then FM percussion, then the melodic part: note sequencer, 303, 808
bass). No frozen file changes.

## 1. Goal and user decisions

A filter on every track: LP, BP, HP or NOTCH with resonance, swept by each hit (an envelope). User decisions
(2026-10-08):
- **Per track** (one filter on the track's sound), not per voice; **with an envelope** whose decay has **its own
  knob** (5 parameters, 80 B of the project's 248 B free).
- Order of the work: this filter, then FM percussion, then the melodic part.
- BENCH gains: the heavy kit with the filter on 8 tracks; the heavy kit with the compressor and with the sidechain,
  each with and without the kit's FX.

## 2. The filter

- **Core:** the models' SVF (`dm_dsp.c dsvf_coef` / `dsvf_tick`: Simper's trapezoidal SVF, 12 dB, the same as
  upstream's TRIO LP / BP / HP / NOTCH). LP = its low-pass output, BP its band-pass, HP its high-pass, NOTCH = LP +
  HP (in − k·bp). Coefficients once per 32-sample block (CTL); per sample only the filter.
- **Place in the track's chain** (`fx.c mix_part`): voice → RESON → **FILTER** → DIST → SLICER → level / pan / sends.
- **TYPE OFF:** bypassed, no code runs; the render is bit-identical to 0.13.0 (the golden hashes unchanged).
- **Headroom:** a track's block reaches ±2^19 (8 loud voices); the filter takes it scaled into its state range and
  gives it back at the same level, so a track at full level with RESO 127 does not clip the filter's states.
- **Cutoff per block:** CUT (0..127 → the SVF's 0..127 << 8, 30 Hz .. 16 kHz) + ENV × the envelope × the hit's
  velocity / 127 + the LFOs (F.CUT), clamped to 0..127 << 8.
- **The envelope:** each hit of the track (steps, keys, MIDI; a choke or a model's retrigger included) sets it to
  full; it falls exponentially, DECAY 0..127 = a time constant of 5 ms .. 2 s (log scale), evaluated per block.
  ENV −64..+63 = the cutoff moves by up to ±64 units (half the range, ~4.5 octaves) at the hit, returning to CUT.
- **Tail:** after the track's last voice, the filter runs on until its output has been silent (|y| < 1) for a whole
  block and its states are small, then the track goes idle as now (a resonant ring is not cut). DIST's tail keeps
  running after it as today.

## 3. Parameters, pages, LFO

| Knob | Range | Default | Shown |
|---|---|---|---|
| TYPE (`P_FTYPE`) | OFF LP BP HP NOT | OFF | name |
| CUT (`P_FCUT`) | 0..127 | 127 | Hz / kHz |
| RESO (`P_FRESO`) | 0..127 | 0 | 0..127 |
| ENV (`P_FENV`) | −64..+63 | 0 | signed |
| DECAY (`P_FDEC`) | 0..127 | 40 | ms / s |

- Appended after RESON's parameters (`P_COUNT` 55 → 60). INIT SOUND, a model change's defaults, the power-on kit:
  TYPE OFF.
- Pages (FX family, first): **FILTER 1/2** TYPE CUT RESO ENV, **FILTER 2/2** DECAY. Each draws the filter's
  response (the curve of TYPE at CUT / RESO, log frequency) with the reach of ENV marked.
- **LFO DEST** gains **F.CUT (17)** and **F.RES (18)** after R.POS (16); stored values 0..16 keep their meaning.
- Motion recording records the five like any knob. The web editor (upstream's synth editor; the drum firmware does
  not serve it) is unchanged.

## 4. Storage

Format **FDR10** = FDR9 with 5 more parameters per track: 3592 → 3672 B (≤ ST_PAYLOAD_MAX 3840; 168 B stay free).
FDR1..FDR9 records load converted, the filter OFF (they sound as before). An older firmware does not load FDR10 (as
every earlier format step). `storage.c` unchanged (frozen).

## 5. BENCH

Five cases appended to `firmware/src/bench.c` (the host / FM-1 comparison of `tools/fm1_bench.py`). COMP here is
always keyed by a source track (SRC) and compresses only the tracks with DUCK on (the source is never ducked), so
"compressor" = the detector alone and "sidechain" = the detector plus the ducking:
- `heavy+filter`: the heavy kit, the filter on all 8 tracks (LP, CUT 70, RESO 60, ENV +30, DECAY 40).
- `heavy+comp`: the heavy kit as it is (DIST, SLICER, sends), COMP SRC T1 (the kick), no track ducked.
- `heavy-fx+comp`: the heavy kit without DIST, SLICER and sends, the same COMP.
- `heavy+sidechain`: the heavy kit as it is, COMP SRC T1, DUCK on tracks 2..8.
- `heavy-fx+sidechain`: the heavy kit without DIST, SLICER and sends, the same sidechain.

## 6. Testing (host, failing first)

- **Response per TYPE** (steady tones, CUT at ~1 kHz): LP passes 250 Hz and is ≥ 18 dB down at 4 kHz; HP the
  reverse; BP peaks near the cutoff; NOTCH ≥ 20 dB down at the cutoff, 250 Hz and 4 kHz passing. One octave of CUT
  moves the response by one octave (± a semitone).
- **RESO** raises the peak at the cutoff; every TYPE × CUT × RESO extreme (0 / 64 / 127) is bounded; a track at
  full level with RESO 127 does not clip the filter's states.
- **ENV / DECAY:** ENV +40 starts brighter and returns to CUT within DECAY's time; −40 darker; an accent sweeps
  further than a plain hit; DECAY 0 vs 127 ~400× apart.
- **TYPE OFF** bit-identical to 0.13.0 (golden hashes unchanged).
- **Tail:** a resonant filter rings out after the hit, then the track is silent (all zero) and idle.
- **LFO:** F.CUT moves the cutoff, F.RES the peak.
- **Projects:** an FDR9 record loads with the filter OFF; FDR10 save → load round-trips the five; the size check.
- **BENCH:** the five cases exist, reset and render as the host's (bench_host).
- **Screens:** FILTER 1/2 per TYPE, 2/2, the LFO page with F.CUT. **WAVs:** an LP sweep on S909, a resonant HP on
  hats, a NOTCH on a cymbal, K808 with ENV −40, an LFO F.CUT wobble on a clap.
- **Firmware:** the build; H2 0 different; stack; loader pinned; no `divdi3`; `check_untouched`; a target budget
  line for the filter's function with the number the user approves; the host cost suite (`drum_cost_ref.txt`)
  within its limits with the filter on (asking before any limit change).

## 7. On the device (the user)

FILTER pages and graph; each TYPE heard; RESO high with a sweep; ENV on a kick; the LFO on CUT; an old project
loads unchanged; `tools/fm1_bench.py --yes` (the five new cases on the FM-1).

## 8. Done when

§6 passes, the firmware builds, the frozen / loader / stack checks are green, CHANGELOG has the line,
DEVICE_INSTALL.md has a FILTER section, the user has checked §7; merged (with COWB) as 0.14.0, published.

## 9. Out of scope

Per-voice filters; 24 dB / ladder / diode filters (the 303 part decides its own); filter key tracking; a filter on
the sample layer alone; MIDI CC control.
