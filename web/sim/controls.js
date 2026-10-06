// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The FM-1 panel's controls as data (no DOM): label ids are panel.c's enum order (tests/sim_glue.mjs checks it),
// the 27 keys, the computer keyboard map, and the held-state bookkeeping the page uses.
export const BTN = [
  { c: 'B_FX', label: 'FX' }, { c: 'B_SCL', label: 'SCL' }, { c: 'B_ENV', label: 'ENV' }, { c: 'B_LFO', label: 'LFO' },
  { c: 'B_EDIT', label: 'EDIT' }, { c: 'B_GLO', label: 'GLO' }, { c: 'B_HOME', label: 'HOME' },
  { c: 'B_SAVE', label: 'SAVE' }, { c: 'B_ARP', label: 'ARP' }, { c: 'B_SEQ', label: 'SEQ' },
  { c: 'B_PLAY', label: 'PLAY' }, { c: 'B_REC', label: 'REC' }, { c: 'B_OCTDN', label: 'OCT-' }, { c: 'B_OCTUP', label: 'OCT+' },
];
export const B = Object.fromEntries(BTN.map((b, i) => [b.label, i]));

export const ENC = [
  { c: 'EN_SELECT', label: 'SELECT' }, { c: 'EN_ALGO', label: 'ALGORITHM' }, { c: 'EN_PRESET', label: 'PRESETS' },
  { c: 'EN_K1', label: 'KNOB1' }, { c: 'EN_K2', label: 'KNOB2' }, { c: 'EN_K3', label: 'KNOB3' }, { c: 'EN_K4', label: 'KNOB4' },
];

const NAMES = ['F', 'F#', 'G', 'G#', 'A', 'A#', 'B', 'C', 'C#', 'D', 'D#', 'E'];
export const KEYS = Array.from({ length: 27 }, (_, n) => {
  const name = NAMES[n % 12], octave = 3 + Math.floor((n + 5) / 12);   // F3 .. G5 (C starts an octave)
  return { n, name: name + octave, black: name.includes('#') };
});

// KeyboardEvent.code (the physical key, any layout) -> control. Two piano rows: Z.. / S.. from F3, Q.. / 3.. to G5.
const PIANO = {
  KeyZ: 0, KeyS: 1, KeyX: 2, KeyD: 3, KeyC: 4, KeyF: 5, KeyV: 6, KeyB: 7, KeyH: 8, KeyN: 9, KeyJ: 10, KeyM: 11,
  Comma: 12, KeyL: 13, Period: 14, Semicolon: 15, Slash: 16, Quote: 17,
  KeyQ: 18, KeyW: 19, Digit3: 20, KeyE: 21, Digit4: 22, KeyR: 23, KeyT: 24, Digit6: 25, KeyY: 26,
};
export const KEYBOARD = {
  ...Object.fromEntries(Object.entries(PIANO).map(([code, n]) => [code, { kind: 'key', id: n }])),
  Space: { kind: 'btn', id: B.PLAY }, Enter: { kind: 'btn', id: B.REC }, Escape: { kind: 'btn', id: B.HOME },
  ArrowLeft: { kind: 'btn', id: B['OCT-'] }, ArrowRight: { kind: 'btn', id: B['OCT+'] },
};
export const keyHint = Object.fromEntries(Object.entries(KEYBOARD).map(([code, c]) => [`${c.kind}:${c.id}`,
  code.replace(/^Key|^Digit/, '').replace('Comma', ',').replace('Period', '.').replace('Slash', '/')
    .replace('Semicolon', ';').replace('Quote', "'").replace('Space', 'Space').replace('Escape', 'Esc')
    .replace('ArrowLeft', '←').replace('ArrowRight', '→')]));

// a keyboard event -> { ctl: 'key:n' | 'btn:id', down } or null (unmapped, autorepeat, a shortcut with a modifier)
export function keyEvent(e) {
  const c = KEYBOARD[e.code];
  const down = e.type === 'keydown';
  if (!c || e.repeat || (down && (e.metaKey || e.ctrlKey || e.altKey))) return null;
  return { ctl: `${c.kind}:${c.id}`, down };
}

// a control is held while any source (a pointer, the keyboard, a latch) holds it
export class HeldSet {
  constructor() { this.m = new Map(); }
  press(ctl, src) {                                // -> true when the control just went down
    const s = this.m.get(ctl) ?? new Set();
    const first = s.size === 0;
    s.add(src);
    this.m.set(ctl, s);
    return first;
  }
  release(ctl, src) {                              // -> true when the control just went up
    const s = this.m.get(ctl);
    if (!s || !s.delete(src)) return false;
    if (s.size) return false;
    this.m.delete(ctl);
    return true;
  }
  holds(ctl, src) { return this.m.get(ctl)?.has(src) ?? false; }
  releaseAll() {                                   // -> the controls that were held
    const all = [...this.m.keys()];
    this.m.clear();
    return all;
  }
}
