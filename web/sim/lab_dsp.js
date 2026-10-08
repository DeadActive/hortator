// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The lab pieces' measurements of what the firmware rendered (no sound of their own): a hit's length, its spectrum.

// the last sample above -60 dB of the peak (tests/sim_lab_test.c tail_len)
export function tailLength(a) {
  let pk = 0, last = 0;
  for (let i = 0; i < a.length; i++) pk = Math.max(pk, Math.abs(a[i]));
  for (let i = 0; i < a.length; i++) if (Math.abs(a[i]) > pk * 0.001) last = i;
  return last;
}

// in place, radix 2: re / im of length n = 2^k
export function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      [re[i], re[j]] = [re[j], re[i]];
      [im[i], im[j]] = [im[j], im[i]];
    }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const a = -2 * Math.PI / len, wr = Math.cos(a), wi = Math.sin(a);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const p = i + k, q = p + len / 2;
        const tr = re[q] * cr - im[q] * ci, ti = re[q] * ci + im[q] * cr;
        re[q] = re[p] - tr; im[q] = im[p] - ti;
        re[p] += tr; im[p] += ti;
        [cr, ci] = [cr * wr - ci * wi, cr * wi + ci * wr];
      }
    }
  }
}

// the magnitude spectrum in dB (0 dB: a full-scale sine) of the first n samples, Hann windowed: n / 2 bins
export function spectrum(a, n = 8192) {
  const re = new Float64Array(n), im = new Float64Array(n);
  for (let i = 0; i < n && i < a.length; i++) re[i] = a[i] * (0.5 - 0.5 * Math.cos(2 * Math.PI * i / n));
  fft(re, im);
  const db = new Float32Array(n / 2);
  for (let k = 0; k < n / 2; k++) db[k] = 20 * Math.log10(Math.hypot(re[k], im[k]) * 4 / n + 1e-9);
  return db;
}
