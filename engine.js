// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The FM-1 firmware as wasm (tests/sim_core.c, built by tools/build_sim.sh) behind a small JS API. Runs in the
// AudioWorklet (worklet.js) and in node (tests/sim_glue.mjs). One instance = one power-on.
export const FS = 44100;
export const FLASH_SIZE = 0x100000;
export const SECTOR = 4096;
const W = 240, MAX_FRAMES = 1024;

export class Fm1Sim {
  static async create(bytes) {
    const module = await WebAssembly.compile(bytes);
    const imports = {};
    for (const imp of WebAssembly.Module.imports(module))       // none today; a stub keeps a new one from failing
      if (imp.kind === 'function') (imports[imp.module] ??= {})[imp.name] = () => 0;
    return new Fm1Sim(await WebAssembly.instantiate(module, imports));
  }

  constructor(instance) {
    this.x = instance.exports;
    this.x._initialize?.();
    this.left = new Float32Array(MAX_FRAMES);
    this.right = new Float32Array(MAX_FRAMES);
  }

  view(Type, ptr, n) { return new Type(this.x.memory.buffer, ptr, n); }   // fresh: memory may be replaced

  // sectors: Map(offset -> Uint8Array(4096)) from storage, or null / empty for a first visit (the demos)
  boot(sectors) {
    if (!sectors || sectors.size === 0) {
      this.x.sim_init(1);
      return true;
    }
    this.x.sim_flash_reset();
    const flash = this.view(Uint8Array, this.x.sim_flash(), FLASH_SIZE);
    for (const [off, bytes] of sectors)
      if (Number.isInteger(off) && off >= 0 && off % SECTOR === 0 && off + SECTOR <= FLASH_SIZE && bytes?.length === SECTOR)
        flash.set(bytes, off);
    this.x.sim_init(0);
    return false;
  }

  render(frames) {                                 // -> { left, right, uiFrames } (views valid until the next call)
    frames = Math.min(frames, MAX_FRAMES);
    const uiFrames = this.x.sim_render(frames);
    const a = this.view(Float32Array, this.x.sim_audio(), 2 * frames);
    for (let i = 0; i < frames; i++) {
      this.left[i] = a[2 * i];
      this.right[i] = a[2 * i + 1];
    }
    return { left: this.left.subarray(0, frames), right: this.right.subarray(0, frames), uiFrames };
  }

  btn(label, down) { this.x.sim_btn(label, down ? 1 : 0); }   // label: panel.c B_FX .. B_OCTUP
  key(n, down) { this.x.sim_key(n, down ? 1 : 0); }           // note key 0 (F3) .. 26 (G5)
  enc(role, steps) { this.x.sim_enc(role, steps); }           // role: EN_SELECT .. EN_K4, + = clockwise
  master(v) { this.x.sim_master(Math.max(0, Math.min(1023, Math.round(v)))); }
  releaseAll() {
    for (let b = 0; b < 14; b++) this.btn(b, false);
    for (let n = 0; n < 27; n++) this.key(n, false);
  }

  fb() { return this.view(Uint16Array, this.x.sim_fb(), W * W).slice(); }   // RGB565, row-major
  leds() { return this.x.sim_leds() >>> 0; }      // bit b = button label b
  keyLeds() { return this.x.sim_key_leds() >>> 0; }   // bit n = note key n
  playing() { return this.x.sim_playing() !== 0; }

  takeDirty() {                                    // -> [{ off, bytes }] written since the last call
    const bits = this.view(Uint32Array, this.x.sim_flash_dirty(), FLASH_SIZE / SECTOR / 32);
    const flash = this.view(Uint8Array, this.x.sim_flash(), FLASH_SIZE);
    const out = [];
    for (let s = 0; s < FLASH_SIZE / SECTOR; s++)
      if ((bits[s >> 5] >>> (s & 31)) & 1) out.push({ off: s * SECTOR, bytes: flash.slice(s * SECTOR, (s + 1) * SECTOR) });
    this.x.sim_flash_clean();
    return out;
  }
}
