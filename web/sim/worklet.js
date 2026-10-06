// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The FM-1 firmware in the audio thread: every quantum renders 128 frames (the firmware's audio ISR runs per
// half buffer, its UI frame every 15 ms of audio, inside sim_render). Input arrives as messages; frames (the
// screen + LEDs) and written flash sectors go back to the page.
import { Fm1Sim } from './engine.js';

class Fm1Processor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.sim = null;
    this.queue = [];
    const { wasm, sectors } = options.processorOptions;
    Fm1Sim.create(wasm).then(sim => {
      const fresh = sim.boot(new Map(sectors));
      this.sim = sim;
      this.port.postMessage({ t: 'ready', fresh });
    }, err => this.port.postMessage({ t: 'error', message: String(err) }));
    this.port.onmessage = e => this.queue.push(e.data);
  }

  apply(m) {
    const s = this.sim;
    if (m.t === 'btn') s.btn(m.id, m.down);
    else if (m.t === 'key') s.key(m.id, m.down);
    else if (m.t === 'enc') s.enc(m.id, m.steps);
    else if (m.t === 'master') s.master(m.value);
    else if (m.t === 'release') s.releaseAll();
  }

  process(inputs, outputs) {
    const out = outputs[0];
    if (!this.sim) return true;
    for (const m of this.queue) this.apply(m);
    this.queue.length = 0;
    const { left, right, uiFrames } = this.sim.render(out[0].length);
    out[0].set(left);
    if (out[1]) out[1].set(right);
    if (uiFrames) {
      const fb = this.sim.fb();
      this.port.postMessage({ t: 'frame', fb, leds: this.sim.leds(), keyLeds: this.sim.keyLeds() }, [fb.buffer]);
      const dirty = this.sim.takeDirty();
      if (dirty.length) this.port.postMessage({ t: 'flash', sectors: dirty }, dirty.map(d => d.bytes.buffer));
    }
    return true;
  }
}

registerProcessor('fm1', Fm1Processor);
