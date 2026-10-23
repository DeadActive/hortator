// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The lab pieces' traces, drawn like the FM-1's LEDs: a bright line with a glow on the plate's black.
import { INK } from './lab_ui.js?v=b1815a759f';
import { ditherPattern } from './dither.js?v=b1815a759f';

const LABEL = '16px "Hortator Terminus", monospace';

// a waveform, min / max per pixel column, of a[0 .. frames): centred; scale 1 = full scale fills the box's height
export function wave(g, w, h, a, frames, { color = INK.led, glow = INK.glow, fill = false, scale = 1 } = {}) {
  const mid = h / 2, n = Math.min(frames, a.length), s = mid * scale;
  g.save();
  g.strokeStyle = INK.grid;
  g.lineWidth = 1;
  g.beginPath();
  g.moveTo(0, Math.round(mid) + 0.5);
  g.lineTo(w, Math.round(mid) + 0.5);
  g.stroke();
  g.fillStyle = g.strokeStyle = color;
  g.shadowColor = glow;
  g.shadowBlur = glow ? 8 : 0;
  g.beginPath();
  for (let x = 0; x < w; x++) {
    const i0 = Math.floor(x / w * n), i1 = Math.max(i0 + 1, Math.floor((x + 1) / w * n));
    let lo = 0, hi = 0;
    for (let i = i0; i < i1 && i < n; i++) { if (a[i] < lo) lo = a[i]; if (a[i] > hi) hi = a[i]; }
    g.rect(x, mid - hi * s, 1, Math.max(1, (hi - lo) * s));
  }
  g.fill();
  g.restore();
}

// the peak of a, in dB of full scale
export function peakDb(a, n = a.length) {
  let pk = 0;
  for (let i = 0; i < n; i++) pk = Math.max(pk, Math.abs(a[i]));
  return 20 * Math.log10(pk + 1e-9);
}

// a time axis under a waveform: seconds at the box's bottom, a note in the middle
export function timeAxis(g, w, h, seconds, note) {
  g.save();
  g.font = LABEL;
  g.fillStyle = INK.dim;
  g.textBaseline = 'bottom';
  g.fillText('0', 0, h);
  const s = seconds < 1 ? `${Math.round(seconds * 1000)} ms` : `${seconds.toFixed(2)} s`;
  g.textAlign = 'right';
  g.fillText(s, w, h);
  if (note) {
    g.textAlign = 'center';
    g.fillText(note, w / 2, h);
  }
  g.restore();
}

// a spectrum (dB per bin of an fs / n grid) on a log frequency axis, 30 Hz to 16 kHz, -84 .. 0 dB
// norm: drawn to its own peak (two spectra compared by shape, not level)
export function spectrum(g, w, h, db, fs, { color = INK.led, glow = INK.glow, fill = true, norm = false } = {}) {
  const lo = Math.log(30), hi = Math.log(16000), bins = db.length, hz = fs / 2 / bins, top = 4, bot = h - 16;
  if (norm) {
    let pk = -200;
    for (let k = 1; k < bins; k++) pk = Math.max(pk, db[k]);
    db = db.map(d => d - pk);
  }
  const x = f => (Math.log(f) - lo) / (hi - lo) * w;
  const y = d => top + Math.min(1, Math.max(0, -d / 84)) * (bot - top);
  g.save();
  g.font = LABEL;
  g.strokeStyle = INK.grid;
  g.fillStyle = INK.dim;
  g.textBaseline = 'bottom';
  for (const [f, t] of [[100, '100'], [1000, '1k'], [10000, '10k']]) {
    g.beginPath();
    g.moveTo(Math.round(x(f)) + 0.5, top);
    g.lineTo(Math.round(x(f)) + 0.5, bot);
    g.stroke();
    g.textAlign = x(f) + 3 + g.measureText(t).width > w ? 'right' : 'left';
    g.fillText(t, g.textAlign === 'right' ? x(f) - 3 : x(f) + 3, h);
  }
  g.beginPath();                                      // the loudest bin per pixel column
  g.moveTo(0, bot);
  for (let px = 0; px <= w; px++) {
    const f0 = Math.exp(lo + px / w * (hi - lo)) / hz, f1 = Math.exp(lo + (px + 1) / w * (hi - lo)) / hz;
    let m = -200;
    if (f1 - f0 < 1) {                                // narrower than a bin (the low end): between its two bins
      const k = Math.max(1, Math.floor(f0)), u = f0 - k;
      m = k + 1 < bins ? db[k] * (1 - u) + db[k + 1] * u : db[bins - 1];
    } else for (let k = Math.floor(f0); k < f1 && k < bins; k++) m = Math.max(m, db[k]);
    g.lineTo(px, y(m));
  }
  g.lineTo(w, bot);
  if (fill) {                                         // the area under it, dithered (a quarter of the tone)
    g.fillStyle = ditherPattern(g, color, 0.25);
    g.fill();
  }
  g.strokeStyle = color;
  g.lineWidth = 1.5;
  g.shadowColor = glow;
  g.shadowBlur = glow ? 8 : 0;
  g.stroke();
  g.restore();
}

// a marker at frame i of a waveform's window of n frames: a dashed line and a label beside it
export function marker(g, w, h, i, n, text) {
  const x = Math.round(Math.min(1, i / n) * w) + 0.5;
  g.save();
  g.strokeStyle = INK.dry;
  g.setLineDash([3, 4]);
  g.beginPath();
  g.moveTo(x, 0);
  g.lineTo(x, h);
  g.stroke();
  g.font = LABEL;
  g.fillStyle = INK.dry;
  g.textBaseline = 'top';
  g.fillText(text, x + 6, 2);
  g.restore();
}
