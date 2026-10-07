# Performance: the FM-1 baseline and the optimization plan (for later)

Measured 2026-10-08 on the FM-1 with BENCH (branch `bench`, 243c58e: PHYS + the HortatoR logo):
`tools/fm1_bench.py --yes` plays the 63 cases of `firmware/src/bench.c` on the device (console `bench yes`, the
audio ISR's own render time, voices never shed) and on the host, and writes `build/bench/report.md` (this run: `docs/bench/2026-10-08-baseline.md`, its raw log next to it). Re-run it
after every optimization below: the table is the before / after.

## 1. What the FM-1 measured

The host predicts the device: a median **6.86 ns on the FM-1 per host instruction** (heavy DSP within ±8 %). So
**100 % SYSTEM CPU ≈ 3300 host instructions / sample**; the stock Felucca reference (1566, `tests/drum_cost_ref.txt`)
is ≈ 47 %. An audio half is 5804 µs (256 samples); a half above 85 % sheds a voice, above 100 % is late (a click).

| Kit | CPU average | worst half | late halves |
|---|---|---|---|
| idle (nothing playing) | 12 % | 12 % | 0 |
| realistic heavy kit (demo, layer / DIST / SLICER / sends on 8, CHORD + STRNG) | 41 % | 63 % | 0 |
| PHYS-heavy (2 MEMB, RESON 2 CHORD + 2 MODAL) | **84 %** | **99 %** | **48 / 344** |
| PHYS-heavy without RESON | 45 % | 58 % | 0 |
| 8 × MEMB (4 sound at once) | 57 % | 65 % | 0 |
| 8 × S909 / SMPL (heaviest classic models), no FX | 47 / 50 % | 50 / 54 % | 0 |

Cost of the parts on the FM-1:

| Part | CPU |
|---|---|
| Always on (chorus, delay, reverb buses, master, LFOs): even idle | 12 % |
| RESON MODAL | ~9.5 % per track |
| RESON CHORD | ~7 % per track |
| RESON STRNG / PIPE | ~2.5 % per track |
| sample layers on 8 tracks | 8 % |
| SLICER on 8 tracks | 5 % |
| DIST on 8 tracks | 4 % |
| a send turned off | ~0 (the bus keeps running) |

Findings:
- The PHYS-heavy kit is the "80 % CPU" seen on the device; RESON is ~39 of its 84 points.
- The worst half sits 15–20 points above the average in the heavy kits: voices are shed by the peaks, not the
  average (per-hit work landing in one half).
- Light cases run 20–40 % slower than the host predicts (idle, hats, HMETL, HNOIS, KBOOM): the fixed per-half
  work (buses, master, ISR) is memory-bound on the FM-1. Heavy DSP is not: running code from RAM probably gains
  little (an inference, not measured).

## 2. The plan, in order

Sound identical (proved by host tests: identical output hashes), each measured with BENCH:

1. **RESON: coefficients only on change.** STRNG / PIPE / CHORD setup and MODAL's `px_modal_block_q` (12 modes,
   two 64 ÷ 32 divides each) run every 32-sample block even when no knob, LFO or R.TUN moves. Cache the inputs,
   skip when equal. MEMB's `px_memb_block` likewise once its BEND has settled. Expected: ~10 % of RESON / MEMB on
   the host, likely more on the FM-1 (divides).
2. **Idle buses stop.** Chorus, delay, reverb run every half (12 % idle). A bus with no send from any track whose
   tail has fully died (its buffer and states exactly zero: tracked by the largest |value| written over one buffer
   length) is skipped, exact. Gains only when a bus is unused.
3. **Peaks.** Find what lands in the worst half (per-hit setups: voice triggers, RESON / MODAL coefficient
   changes, sample starts) with BENCH's max_us per case; spread it over halves. Lowers the shedding, not the
   average.

Sound changed slightly (WAV A/B for the user, nothing without approval):

4. **MODAL / MEMB: fewer modes.** Drop modes above ~12 kHz or with tiny gain (est. 20–40 % of their cost);
   or 8 instead of 12 modes for MODAL, 6–8 for MEMB.
5. **Envelopes per few samples** (sample layer, models) instead of per sample.
6. **Reverb at half rate.**
7. **Lower-precision mode filters** (must keep the DaisySP check, 5 cents; any threshold change: the user).

Policy (the user decides):

8. **Overload: degrade before shedding.** Above 85 % a half first lightens RESON / PHYS (fewer modes for a moment);
   voices are shed only as the last resort.
9. **The shedding threshold** 85 % → ~95 %: fewer voices shed, more risk of a late half (a click).

Not planned now: the audio code in fast internal RAM, `-O2` for the audio code (a split compile unit): the host
matches the FM-1 on heavy DSP, so the gain looks small; BENCH would show it if tried.

## 3. Using BENCH

- FM-1 on USB with a build of the same commit: `tools/fm1_bench.py --yes` (~4 min, the speaker silent; the pattern
  in memory is lost, power-on state after; flash untouched). A saved run: `tools/fm1_bench.py --log
  build/bench/device.log`.
- Case list: `firmware/src/bench.c` (shared by both sides; `tests/bench_host.c` proves the FM-1's reset equals the
  host tests' `host_init`). Adding a case changes the firmware: install before comparing.
