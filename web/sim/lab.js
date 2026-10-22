// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The landing page's lab (docs/superpowers/specs/2026-10-09-websim-features-lab-design.md): the firmware's own DSP
// (tests/sim_lab.c in fm1sim.wasm) driven directly for the feature pieces. One class, two uses: the analyser in the
// page (silent: a voice's waveform, the Grids lanes, the sequencer's dice) and the player in an AudioWorklet
// (worklet.js 'fm1lab': the same calls, heard live). Ids come from lab_ids.json, made from the firmware's enums.
import { Fm1Sim } from './engine.js';

const PROBE = { gr: 0, lfo: 4, fired: 68, idx: 72, muteQ: 88, muted: 89, watch: 90 };   // sim_lab.c lab_probe_t (92 B)

export class Lab {
  static async create(bytes, ids) { return new Lab(await Fm1Sim.create(bytes), ids); }

  constructor(sim, ids) {
    this.sim = sim;
    this.x = sim.x;
    this.ids = ids;
  }

  reset() { this.x.lab_reset(); }
  model(t, m) { this.x.lab_model(t, m); }
  param(t, p, v) { this.x.lab_param(t, p, Math.round(v)); }
  global(g, v) { this.x.lab_global(g, Math.round(v)); }
  step(t, s, on, acc = false, cond = 0, rat = 0) { this.x.lab_step(t, s, on ? 1 : 0, acc ? 1 : 0, cond, rat); }
  note(t, s, nt, slide = false, tie = false) { this.x.lab_note(t, s, nt, slide ? 1 : 0, tie ? 1 : 0); }   // 0 = C4
  play(on) { this.x.lab_play(on ? 1 : 0); }
  hit(t, vel = 127) { this.x.lab_hit(t, vel); }
  perf(e, down) { this.x.lab_perf(e, down ? 1 : 0); }
  watch(t, id) { this.x.lab_watch(t, id); }           // the probes' watch: track t's knob id
  mute(t, atBar) { this.x.lab_mute(t, atBar ? 1 : 0); }
  motion(t, rec) { this.x.lab_motion(t, rec ? 1 : 0); }
  knob(t, id, v) { this.x.lab_knob(t, id, Math.round(v)); }   // a turn on the panel (MOTION records it when armed)
  motionClear(t) { this.x.lab_motion_clear(t); }

  // track 1's hit alone from silence (inserts: through its RESON and FILTER), mono, -> a Float32Array copy
  voice(frames, inserts) {
    this.x.lab_voice(frames, inserts ? 1 : 0);
    return this.sim.view(Float32Array, this.x.lab_buf(), frames).slice();
  }

  // the whole mix (events, sequencer, FX), (L + R) / 2 -> a copy
  renderMono(frames) {
    this.x.lab_render_mono(frames);
    return this.sim.view(Float32Array, this.x.lab_buf(), frames).slice();
  }

  // the Grids lanes for the globals now: 3 x 32 bytes, bit 0 a hit, bit 1 an accent
  grids() { return this.sim.view(Uint8Array, this.x.lab_grids(), 96).slice(); }

  // the transport running: what track t played on each of 16 steps for the next `bars` bars (hits per step; a ratchet
  // counts each), from the probes
  dice(t, bars) {
    const out = Array.from({ length: bars }, () => new Array(16).fill(0)), fs = this.ids.fs;
    let bar = 0, last = -1;
    for (let chunk = 0; bar < bars && chunk < bars; chunk++) {   // 2 bars at 120 a chunk; a stalled transport ends it
      this.probeClear();
      this.renderMono(fs * 4);
      for (const p of this.probes()) {
        const i = p.idx[t] % 16;
        if (i < last && ++bar >= bars) break;
        last = i;
        if ((p.fired >> t) & 1) out[bar][i]++;
      }
    }
    return out;
  }

  // the FM-1's own screen for the state now, after the panel moves that open a page: ops [{ sel: track }, { tap:
  // button }, { hold: button }, { enc: role, steps }, { perf: PF id }] (button ids: panel.c's labels; role: EN_SELECT
  // .. EN_K4). -> RGB565, 240 x 240 (a copy)
  screen(ops) {
    const run = ms => this.renderMono(Math.round(this.ids.fs * ms / 1000));
    for (const op of ops) {
      if ('sel' in op) this.x.lab_select(op.sel);
      if ('perf' in op) this.perf(op.perf, true);
      if ('tap' in op) {                               // short: under the firmware's 0.4 s hold
        this.sim.btn(op.tap, true);
        run(40);
        this.sim.btn(op.tap, false);
        run(60);
      }
      if ('hold' in op) {
        this.sim.btn(op.hold, true);
        run(600);
      }
      if ('enc' in op) {                               // a turn, taken at the next UI frame
        this.sim.enc(op.enc, op.steps);
        run(40);
      }
    }
    run(40);
    const fb = this.sim.fb();
    this.sim.releaseAll();
    return fb;
  }

  probeClear() { this.x.lab_probe_clear(); }
  // per audio half (256 frames): { gr, lfo[track][2], fired (bit per track), idx[track], muteQ, muted (bits), watch }
  probes() {
    const n = this.x.lab_probe_count(), size = this.x.lab_probe_size(), base = this.x.lab_probes();
    const v = new DataView(this.x.memory.buffer, base, n * size), out = [];
    for (let i = 0; i < n; i++) {
      const o = i * size, lfo = [], idx = [];
      for (let k = 0; k < 8; k++) {
        lfo.push([v.getInt32(o + PROBE.lfo + 8 * k, true), v.getInt32(o + PROBE.lfo + 8 * k + 4, true)]);
        idx.push(v.getUint16(o + PROBE.idx + 2 * k, true));
      }
      out.push({ gr: v.getInt32(o + PROBE.gr, true), lfo, fired: v.getUint32(o + PROBE.fired, true), idx,
        muteQ: v.getUint8(o + PROBE.muteQ), muted: v.getUint8(o + PROBE.muted), watch: v.getInt16(o + PROBE.watch, true) });
    }
    return out;
  }
}

// ---- the player, page side: one AudioContext, started on the first click on a piece; one piece plays at a time
export class LabPlayer {
  static async start(wasm, ids) {
    const ctx = new AudioContext({ sampleRate: 44100, latencyHint: 'interactive' });
    const resumed = ctx.resume();                     // inside the click: the browser allows sound from here
    await ctx.audioWorklet.addModule('worklet.js');
    const node = new AudioWorkletNode(ctx, 'fm1lab', {
      numberOfInputs: 0, outputChannelCount: [2], processorOptions: { wasm: wasm.slice(0), ids },
    });
    const gain = ctx.createGain(), analyser = ctx.createAnalyser();
    analyser.fftSize = 4096;                          // the output as heard, for a piece's live tape
    node.connect(gain).connect(ctx.destination);
    gain.connect(analyser);
    await new Promise((resolve, reject) => {
      node.port.onmessage = e => (e.data.t === 'ready' ? resolve() : e.data.t === 'error' && reject(new Error(e.data.message)));
    });
    await resumed;
    return new LabPlayer(ctx, node, gain, analyser);
  }

  constructor(ctx, node, gain, analyser) {
    this.ctx = ctx;
    this.node = node;
    this.gain = gain;
    this.analyser = analyser;
    this.listener = null;
    this.live = false;
    this.sleep = 0;
    node.port.onmessage = e => { if (e.data.t === 'probes' && this.listener) this.listener(e.data.p); };
  }

  call(fn, ...args) { this.node.port.postMessage({ fn, args }); }
  onProbes(fn) { this.listener = fn; }
  async on() {
    this.live = true;
    clearTimeout(this.sleep);
    this.gain.gain.setTargetAtTime(1, this.ctx.currentTime, 0.01);
    await this.ctx.resume();
  }
  off() {                                             // a short fade, the transport stopped, then the context asleep:
    this.live = false;                                // the worklet renders nothing until the next piece plays
    this.gain.gain.setTargetAtTime(0, this.ctx.currentTime, 0.02);
    this.call('play', false);
    this.listener = null;
    clearTimeout(this.sleep);
    this.sleep = setTimeout(() => { if (!this.live) this.ctx.suspend().catch(() => {}); }, 200);
  }
}
