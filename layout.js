// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The panel's geometry (no DOM): the FM-1's own landscape layout and the stacked portrait one phones play on,
// the morph between them, and the hero's choreography (scroll progress -> pose). app.js applies them; node tests
// them (tests/sim_site.mjs).
import { B, KEYS } from './controls.js?v=f8e51fdd5f';

// ---- layouts: design-size boxes [x, y, w, h]; the device is scaled to fit the window
const WHITE = KEYS.filter(k => !k.black), BLACK = KEYS.filter(k => k.black);
function keybed(x0, y0, width, wW, wH, bW, bH, gapY) {    // whites in a row, the raised keys over their gaps
  const step = width / WHITE.length, out = {};
  WHITE.forEach((k, i) => { out[`key:${k.n}`] = [x0 + i * step + (step - wW) / 2, y0 + bH + gapY, wW, wH]; });
  BLACK.forEach(k => {
    const left = WHITE.findIndex(w => w.n === k.n - 1);                  // the white key below it
    out[`key:${k.n}`] = [x0 + (left + 1) * step - bW / 2, y0, bW, bH];
  });
  return out;
}
const ROWS = [['FX', 'SEL', 'ENV', 'LFO', 'EDIT', 'GLO'], ['HOME', 'SAVE', 'ARP', 'SEQ', 'PLAY', 'REC']];
function buttonRows(rows, x0, y0, size, step, rowStep) {
  const out = {};
  rows.forEach((r, j) => r.forEach((l, i) => { out[`btn:${B[l]}`] = [x0 + i * step, y0 + j * rowStep, size, size]; }));
  return out;
}
const knob = (cx, cy, d = 46) => [cx - d / 2, cy - d / 2, d, d];

export const DECO_CLASS = { bezel: 'bezel', oct: 'tray', buttons: 'tray', keys: 'tray keybed' };
export const LAYOUTS = {
  landscape: {                                     // the FM-1 as it is
    W: 1000, H: 620,
    deco: { bezel: [270, 36, 268, 268], oct: [52, 262, 196, 64], buttons: [572, 158, 394, 156], keys: [40, 336, 920, 262] },
    lcd: [284, 50, 240, 240],
    power: [478, -12, 56, 24],                      // the power switch: on the top edge, above the screen's right corner
    enc: { master: knob(95, 108), 0: knob(195, 108), 2: knob(95, 214), 1: knob(195, 214),
           3: knob(632, 108), 4: knob(731, 108), 5: knob(830, 108), 6: knob(929, 108) },
    ctl: { ...buttonRows(ROWS, 588, 174, 52, 62, 68),
           [`btn:${B['OCT-']}`]: [66, 275, 76, 38], [`btn:${B['OCT+']}`]: [158, 275, 76, 38],
           ...keybed(60, 350, 880, 47, 122, 44, 100, 14) },
  },
  portrait: {                                      // phones: the screen on top, OCT- / OCT+ end the button rows
    W: 420, H: 772,
    deco: { bezel: [76, 20, 268, 268], buttons: [14, 450, 392, 128], keys: [10, 590, 400, 172] },
    lcd: [90, 34, 240, 240],
    power: [284, -12, 56, 24],
    enc: { master: knob(60, 340), 0: knob(160, 340), 1: knob(260, 340), 2: knob(360, 340),
           3: knob(60, 416), 4: knob(160, 416), 5: knob(260, 416), 6: knob(360, 416) },
    ctl: { ...buttonRows([[...ROWS[0], 'OCT-'], [...ROWS[1], 'OCT+']], 25, 462, 46, 54, 58),
           ...keybed(16, 598, 388, 20, 76, 20, 72, 6) },
  },
};

const mix = (a, b, t) => a + (b - a) * t;
const clamp01 = x => Math.min(1, Math.max(0, x));
const ease = x => { x = clamp01(x); return x * x * (3 - 2 * x); };
const box = (a, b, t) => (t === 0 ? a : t === 1 ? b : a.map((v, i) => mix(v, b[i], t)));
const boxes = (a, b, t) => Object.fromEntries(Object.keys(a).map(k => [k, box(a[k], b[k], t)]));

// A at t = 0 .. B at t = 1, staggered: the body, its trays and the screen move first, then the knobs, the buttons,
// and the keys last, each part over 70 % of the morph (eased), so parts do not all cross at once. A tray B has not
// (the phone has no OCT tray) merges into B's button tray and fades.
const LAG = { body: 0, enc: 0.1, btn: 0.2, key: 0.3 };
const lag = (t, d) => ease((t - d) / 0.7);
export function blend(A, Bl, t) {
  const deco = {}, tb = lag(t, LAG.body);
  for (const [name, a] of Object.entries(A.deco))
    deco[name] = Bl.deco[name] ? { box: box(a, Bl.deco[name], tb), opacity: 1 }
      : { box: box(a, Bl.deco.buttons, tb), opacity: Math.max(0, 1 - t * 2.5) };
  const ctl = {};
  for (const [k, a] of Object.entries(A.ctl)) ctl[k] = box(a, Bl.ctl[k], lag(t, k.startsWith('key') ? LAG.key : LAG.btn));
  return { W: mix(A.W, Bl.W, tb), H: mix(A.H, Bl.H, tb), deco, lcd: box(A.lcd, Bl.lcd, tb), power: box(A.power, Bl.power, tb),
           enc: boxes(A.enc, Bl.enc, lag(t, LAG.enc)), ctl };
}

// ---- the hero: scroll progress p (0 .. 1 through #hero) -> the pose. The device flies in tilted and blurred;
// the blur ends before the shine starts (a blur filter re-renders the whole moving scene every frame, a shine
// on top of it made Chrome stutter). Phones fly in the FM-1's own landscape form, then morph into the portrait
// simulator once it lies flat.
const POSE = {
  desk: { fly: 0.9, blurEnd: 0.42, shine: [0.48, 0.88], y: 10, z: -1150, rx: 57, ry: 7, rz: -17 },
  phone: { fly: 0.58, blurEnd: 0.36, shine: [0.4, 0.58], morph: [0.62, 0.9], y: 4, z: -650, rx: 50, ry: 0, rz: -9 },
};
const LAND = 0.6;                                  // the flight and the morph use the first 60 % of the track; the rest
export function heroPose(p, phone) {               // holds the simulator flat, a long steady place to play
  p = clamp01(p / LAND);
  const c = POSE[phone ? 'phone' : 'desk'];
  const m = ease(p / c.fly);
  const morph = phone ? ease((p - c.morph[0]) / (c.morph[1] - c.morph[0])) : 0;
  const blurT = clamp01(p / c.blurEnd);
  const blur = blurT >= 1 ? 0 : +(9 * (1 - ease(blurT))).toFixed(2);
  const s = clamp01((p - c.shine[0]) / (c.shine[1] - c.shine[0]));
  return {
    m, morph, blur,
    dim: +(0.42 * (1 - m)).toFixed(3),
    copy: 1 - ease(p / 0.28),
    glare: s > 0 && s < 1 ? +Math.sin(Math.PI * s).toFixed(3) : 0,
    sweep: s,
    shadow: +mix(0.8, 0.3, m).toFixed(3),
    transform: m >= 1 ? 'none'
      : `translate3d(0, ${mix(c.y, 0, m)}vh, ${mix(c.z, 0, m)}px) rotateX(${mix(c.rx, 0, m)}deg) `
        + `rotateY(${mix(c.ry, 0, m)}deg) rotateZ(${mix(c.rz, 0, m)}deg)`,
    arrived: m >= 1 && (!phone || morph >= 1),
  };
}

// ---- the body's edge for the 3D hero: the rounded outline (W x H, corner radius R) as a closed loop of straight
// strips, clockwise from the top edge: 4 sides and N chords per corner. app.js stands a wall on each strip, the
// body's depth deep: the same thickness a stack of full-size layers gave, at a fraction of the pixels to draw.
export function outline(W, H, R, N) {
  const out = [], arc = (cx, cy, from) => {
    for (let i = 0; i < N; i++) {
      const a0 = (from + 90 * i / N) * Math.PI / 180, a1 = (from + 90 * (i + 1) / N) * Math.PI / 180;
      out.push({ x0: cx + R * Math.cos(a0), y0: cy + R * Math.sin(a0), x1: cx + R * Math.cos(a1), y1: cy + R * Math.sin(a1) });
    }
  };
  out.push({ x0: R, y0: 0, x1: W - R, y1: 0 });
  arc(W - R, R, -90);
  out.push({ x0: W, y0: R, x1: W, y1: H - R });
  arc(W - R, H - R, 0);
  out.push({ x0: W - R, y0: H, x1: R, y1: H });
  arc(R, H - R, 90);
  out.push({ x0: 0, y0: H - R, x1: 0, y1: R });
  arc(R, R, 180);
  for (const s of out) for (const k of ['x0', 'y0', 'x1', 'y1']) s[k] = Math.round(s[k] * 1e9) / 1e9;
  return out;
}
