// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
//
// Chopping a long sample into slices (the Samples tab's Chop), the page only: the FM-1 receives the slices as the
// slots of a set (sampset.mjs). Pure: no DOM; node tests drive it (test_sampset.mjs).
// - cuts are sample positions inside the sound (0 and its end never are one); slices run from cut to cut
// - by beats: a cut every 1/4, 1/8 or 1/16 at a tempo, from an offset; or N equal slices
// - the tempo: the hits' onset strength (the rise of the frame level, 5 ms frames) autocorrelated over 60..200 BPM,
//   each tempo scored with its double and half periods (a beat whose halves also hit wins over three or five of
//   them) and weighed by how near it is to 120 BPM (one octave either side: a factor 0.6); when the sound is then within 4 % of a whole number of beats (1, 2, 3, 4, 6, 8, 12, 16,
//   24, 32, 48 or 64) the tempo that makes it exact (a loop cut on its bars)
// - transients: the frame level (3 ms frames, the signal and its first difference) rising by more than a threshold
//   over 6 ms (18 dB at sensitivity 0 down to 4 dB at 100), at most 60 dB under the loudest frame, the strongest rise
//   within 9 ms, at least 40 ms apart; each placed on the first sample reaching a tenth of the hit's peak, then back
//   to the zero crossing before it (1 ms at most)

const DB_FLOOR = 60, GAP_S = 0.04, PRIOR_BPM = 120, SNAP = 0.04;
const WHOLE = [1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64];

/* ------------------------------------------------------------------------- cuts --- */
const clean = (cuts, n) => [...new Set(cuts.map(Math.round))].filter((c) => c > 0 && c < n).sort((a, b) => a - b);
/** N equal slices of n samples: their N - 1 cuts */
export const equalCuts = (n, N) => clean(Array.from({ length: Math.max(0, N - 1) }, (_, i) => n * (i + 1) / N), n);
/** a cut every 1/div of a bar (div 4, 8, 16) at bpm, from offset (a cut itself when not 0) */
export function beatCuts(n, rate, bpm, div, offset = 0) {
  const step = rate * 60 / bpm * 4 / div, out = [];
  if (!(step >= 1)) return [];
  for (let t = offset > 0 ? offset : offset + step; t < n; t += step) out.push(t);
  return clean(out, n);
}
/** [start, end) of each slice */
export function slices(n, cuts) {
  const c = [0, ...clean(cuts, n), n];
  return c.slice(1).map((e, i) => [c[i], e]);
}
export const addCut = (cuts, at, n) => clean([...cuts, at], n);
export const removeCut = (cuts, i) => cuts.filter((_, j) => j !== i);
export const moveCut = (cuts, i, at, n) => clean([...removeCut(cuts, i), at], n);
/** the index of the cut nearest at, -1 none */
export function nearestCut(cuts, at) {
  let best = -1, d = Infinity;
  cuts.forEach((c, i) => { if (Math.abs(c - at) < d) { d = Math.abs(c - at); best = i; } });
  return best;
}
/** slices costing these bytes, in order, into free bytes and room slots: { n, bytes } */
export function fitting(costs, free, room) {
  let n = 0, bytes = 0;
  while (n < costs.length && n < room && bytes + costs[n] <= free) bytes += costs[n++];
  return { n, bytes };
}

/* ------------------------------------------------------------------ the analysis --- */
/** the level of each frame of hop samples (2 hops long), dB: the signal and its first difference */
function levels(x, hop) {
  const nf = Math.max(0, Math.floor((x.length - hop) / hop)), L = new Float64Array(nf);
  for (let k = 0; k < nf; k++) {
    let e = 0;
    const a = k * hop, b = Math.min(x.length, a + 2 * hop);
    for (let i = a; i < b; i++) { const d = x[i] - (i ? x[i - 1] : 0); e += x[i] * x[i] + d * d; }
    L[k] = 10 * Math.log10(e / (b - a) + 1e-12);
  }
  return L;
}
/** the hits: sample positions, sensitivity 0..100 */
export function detectTransients(x, rate, sensitivity = 50) {
  const hop = Math.max(16, Math.round(rate * 0.003)), L = levels(x, hop), nf = L.length;
  if (nf < 4) return [];
  const top = Math.max(...L), thr = 18 - 0.14 * Math.max(0, Math.min(100, sensitivity)), R = new Float64Array(nf);
  if (top < -100) return [];
  for (let k = 2; k < nf; k++) R[k] = L[k] - Math.min(L[k - 1], L[k - 2]);
  const gap = Math.round(GAP_S * rate), out = [];
  for (let k = 2; k < nf; k++) {
    if (R[k] < thr || L[k] < top - DB_FLOOR) continue;
    let peak = true;
    for (let j = Math.max(2, k - 3); j <= Math.min(nf - 1, k + 3) && peak; j++) if (R[j] > R[k] || (R[j] === R[k] && j < k)) peak = false;
    if (!peak) continue;
    const at = place(x, (k - 2) * hop, (k + 3) * hop, rate);
    if (at > 0 && (!out.length || at - out[out.length - 1] >= gap)) out.push(at);
  }
  return out;
}
function place(x, a, b, rate) {
  a = Math.max(0, a);
  b = Math.min(x.length, b);
  let pk = 0;
  for (let i = a; i < b; i++) pk = Math.max(pk, Math.abs(x[i]));
  let i = a;
  while (i < b && Math.abs(x[i]) < 0.1 * pk) i++;
  for (let j = i, lim = Math.max(0, i - Math.round(rate * 0.001)); j > lim; j--) if ((x[j] >= 0) !== (x[j - 1] >= 0)) return j;
  return i;
}
/** the tempo of a sound: { bpm, whole (beats, when the length made it exact; else 0) } */
export function detectTempo(x, rate) {
  const hop = Math.max(16, Math.round(rate / 200)), fps = rate / hop, L = levels(x, hop), nf = L.length;
  if (nf < 16) return { bpm: PRIOR_BPM, whole: 0 };
  const o = new Float64Array(nf);
  for (let k = 1; k < nf; k++) o[k] = Math.max(0, L[k] - L[k - 1]);
  const mean = o.reduce((a, v) => a + v, 0) / nf;
  for (let k = 0; k < nf; k++) o[k] -= mean;
  const lagMax = Math.min(nf - 1, Math.ceil(2 * 60 * fps / 60) + 2), ac = new Float64Array(lagMax + 1);
  for (let l = 1; l <= lagMax; l++) { let s = 0; for (let k = l; k < nf; k++) s += o[k] * o[k - l]; ac[l] = s / (nf - l); }
  const at = (l) => { if (l < 1 || l >= lagMax) return 0; const i = Math.floor(l), f = l - i; return ac[i] * (1 - f) + ac[i + 1] * f; };
  let best = PRIOR_BPM, bs = -Infinity;
  for (let b = 60; b <= 200; b += 0.05) {
    const l = 60 * fps / b, w = Math.exp(-0.5 * (Math.log2(b / PRIOR_BPM) / 1) ** 2);
    const s = w * (at(l) + 0.5 * at(2 * l) + 0.5 * at(l / 2));
    if (s > bs) { bs = s; best = b; }
  }
  const beats = x.length / rate * best / 60;
  const w = WHOLE.filter((n) => Math.abs(beats - n) / n < SNAP).sort((a, b) => Math.abs(beats - a) / a - Math.abs(beats - b) / b)[0];
  if (w) return { bpm: Math.round(w * 60 * rate / x.length * 100) / 100, whole: w };
  return { bpm: Math.round(best * 10) / 10, whole: 0 };
}
