// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The slot layout and the IMA ADPCM codec follow upstream Felucca's (hugelton/Felucca 5d9694d,
// firmware/src/eng_sample.c, tools/sampleio.py; GPL-3.0-only, Copyright (C) 2026 Leo Kuroshita (@kurogedelic),
// Hügelton Instruments).
//
// The user sample slots, the page's side (web/EDITOR_PROTOCOL.md §2.7, §5 "Samples the page makes" / "Sample
// backups", Appendix). Pure: no DOM, no Web Audio (the Samples tab decodes files with decodeAudioData and hands the
// channels here); node tests drive it (test_sample.mjs, tests/sample_xcheck.mjs runs the firmware's own decoder on
// these bytes).
// - a source: mono mixdown, trim, normalize, a windowed-sinc resampler, the rates offered for a slot
// - IMA ADPCM: the encoder follows the device's decoder step for step (eng_sample.c sample_next: low nibble first,
//   predictor / index from 0, the same clamps), so adpcmDecode of its bytes is exactly what the FM-1 plays; the same
//   bytes as tools/sampleio.py ima_encode for the same 16-bit input
// - the 480-byte header (one zone over all keys), the firmware's checks on it, backup files (.fsmp)
// - the update loader's sweep: windows at a sector's +0xF00 that could read as an update record are found and avoided
import { crc32 } from "./proto.mjs";

export const SLOT_NAMES = ["USR1", "USR2", "USR3"];
export const SLOT_SIZE = [0x14000, 0xc000, 0xc000]; // eng_sample.c: USR1 80 KiB, USR2 / USR3 48 KiB
export const DATA_OFF = 512, HDR_SIZE = 480, SECTOR = 4096, WINDOW = 0xf00, WINDOW_N = 80;
export const SLOT_MAX = SLOT_SIZE.map((s) => s - DATA_OFF); // data bytes a slot holds
export const MAGIC = 0x504d5346; // "FSMP"
export const RATE_MAX = 44100;
export const RATES = [44100, 32000, 22050, 16000, 11025]; // offered (never above the source's)
export const ROOT = 60; // MIDI note the zone plays at its own pitch (C4)
const SHIFT_TRIES = 64; // the loader windows' avoidance: source samples dropped at most

export class SampleError extends Error { constructor(msg) { super(msg); this.sample = true; } }

/* ---------------------------------------------------------------- the source --- */
/** the channels' mean */
export function mixdown(channels) {
  if (channels.length === 1) return Float32Array.from(channels[0]);
  const n = Math.min(...channels.map((c) => c.length)), out = new Float32Array(n);
  for (const c of channels) for (let i = 0; i < n; i++) out[i] += c[i];
  for (let i = 0; i < n; i++) out[i] /= channels.length;
  return out;
}
/** samples [start, end) */
export const trim = (x, start, end) => x.slice(Math.max(0, start), Math.max(Math.max(0, start), Math.min(x.length, end)));
/** the peak to full scale (silence stays silence) */
export function normalize(x) {
  let pk = 0;
  for (const v of x) pk = Math.max(pk, Math.abs(v));
  return pk > 0 ? x.map((v) => v / pk) : Float32Array.from(x);
}
/** float -1..1 -> 16 bit, rounded, clamped */
export const toPcm16 = (x) => Int16Array.from(x, (v) => Math.max(-32768, Math.min(32767, Math.round(v * 32767))));
/** samples a source of n at `from` Hz becomes at `to` Hz (resample's length) */
export const lengthAt = (n, from, to) => (from === to ? n : Math.round(n * to / from));
/** windowed-sinc resampling (Blackman, 12 zero crossings each side, the cutoff at 0.46 of the lower rate); DC exact */
export function resample(x, from, to) {
  if (from === to) return Float32Array.from(x);
  const ratio = to / from, n = lengthAt(x.length, from, to), out = new Float32Array(n);
  const scale = Math.min(1, ratio), fc = 0.46 * scale, half = Math.ceil(12 / scale);
  for (let i = 0; i < n; i++) {
    const t = i / ratio, c = Math.floor(t);
    let acc = 0, ws = 0;
    for (let k = c - half + 1; k <= c + half; k++) {
      if (k < 0 || k >= x.length) continue;
      const d = t - k, u = d / half;
      if (u <= -1 || u >= 1) continue;
      const s = d === 0 ? 2 * fc : Math.sin(2 * Math.PI * fc * d) / (Math.PI * d);
      const w = s * (0.42 + 0.5 * Math.cos(Math.PI * u) + 0.08 * Math.cos(2 * Math.PI * u));
      acc += x[k] * w;
      ws += w;
    }
    out[i] = ws ? acc / ws : 0;
  }
  return out;
}
/** the source as the slot will hold it: mono (source.mono or the mixdown), trimmed, normalized (edit.normalize),
 * resampled to edit.hz, 16 bit. source: { channels, rate }; edit: { start, end, normalize, hz } */
export function render(source, edit) {
  const mono = source.mono || (source.mono = mixdown(source.channels));
  let x = trim(mono, edit.start ?? 0, edit.end ?? mono.length);
  if (edit.normalize) x = normalize(x);
  return toPcm16(resample(x, source.rate, edit.hz));
}

/* ------------------------------------------------------------- rates offered --- */
const LABEL = [[44100, "full"], [32000, "high"], [22050, "good"], [16000, "medium"], [11025, "lo-fi"], [0, "very lo-fi"]];
export const qualityOf = (hz) => LABEL.find(([h]) => hz >= h)[1];
/** the rates offered for nSrc source samples at srcHz into a slot holding max bytes: the source's (44.1 kHz at most),
 * the lower standard rates, and the highest rate that fits when that lies between them. Each: { hz, n, bytes, fits,
 * seconds, label }, highest first */
export function rateChoices(srcHz, nSrc, max) {
  const base = Math.min(srcHz, RATE_MAX), hz = [base, ...RATES.filter((h) => h < base)];
  const at = (h) => { const n = Math.max(1, lengthAt(nSrc, srcHz, h)), bytes = Math.ceil(n / 2);
    return { hz: h, n, bytes, fits: bytes <= max, seconds: n / h, label: qualityOf(h) }; };
  let fit = Math.floor(2 * max * srcHz / Math.max(1, nSrc));
  while (fit > 0 && !at(fit).fits) fit--;
  if (fit >= 4000 && fit < base && !hz.some((h) => Math.abs(h - fit) / h < 0.02)) hz.push(fit);
  return hz.sort((a, b) => b - a).map(at);
}
/** the highest rate that fits, else the lowest offered (it does not fit: the page says so) */
export const pickRate = (choices) => choices.find((c) => c.fits) || choices[choices.length - 1];
export const rateQ16 = (hz) => Math.round(hz * 65536 / 44100);
export const hzOfQ16 = (q) => q * 44100 / 65536;
/** a slot's length in seconds from SMP_INFO (n samples at rate Q16) */
export const seconds = (n, rate) => (rate ? n / hzOfQ16(rate) : 0);

/* ------------------------------------------------------------------ IMA ADPCM --- */
const STEP = Int16Array.from([7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73,
  80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
  876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
  5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
  32767]);
const ADJ = [-1, -1, -1, -1, 2, 4, 6, 8];
/** the device's decoder (eng_sample.c sample_next) on one nibble: [predictor, index] after it */
function step(pred, idx, code) {
  const st = STEP[idx];
  let vd = st >> 3;
  if (code & 4) vd += st;
  if (code & 2) vd += st >> 1;
  if (code & 1) vd += st >> 2;
  pred += code & 8 ? -vd : vd;
  pred = pred > 32767 ? 32767 : pred < -32768 ? -32768 : pred;
  idx += ADJ[code & 7];
  return [pred, idx < 0 ? 0 : idx > 88 ? 88 : idx];
}
/** 16-bit samples -> IMA ADPCM bytes (2 a byte, low nibble first; an odd count's last high nibble 0), from predictor 0
 * and index 0: each nibble the standard quantizer's, the state then advanced by the device's decoder */
export function adpcmEncode(pcm) {
  const out = new Uint8Array(Math.ceil(pcm.length / 2));
  let pred = 0, idx = 0;
  for (let i = 0; i < pcm.length; i++) {
    const st = STEP[idx];
    let diff = pcm[i] - pred, code = 0;
    if (diff < 0) { code = 8; diff = -diff; }
    if (diff >= st) { code |= 4; diff -= st; }
    if (diff >= st >> 1) { code |= 2; diff -= st >> 1; }
    if (diff >= st >> 2) code |= 1;
    [pred, idx] = step(pred, idx, code);
    out[i >> 1] |= i & 1 ? code << 4 : code;
  }
  return out;
}
/** the decoder's step index after each of the n samples of ADPCM bytes b (sampset.mjs: a set's START table) */
export function adpcmIndexes(b, n = b.length * 2) {
  const out = new Uint8Array(n);
  let pred = 0, idx = 0;
  for (let i = 0; i < n; i++) {
    [pred, idx] = step(pred, idx, i & 1 ? b[i >> 1] >> 4 : b[i >> 1] & 15);
    out[i] = idx;
  }
  return out;
}
/** IMA ADPCM bytes -> n 16-bit samples, exactly as the FM-1 decodes them */
export function adpcmDecode(b, n = b.length * 2) {
  const out = new Int16Array(n);
  let pred = 0, idx = 0;
  for (let i = 0; i < n; i++) {
    [pred, idx] = step(pred, idx, i & 1 ? b[i >> 1] >> 4 : b[i >> 1] & 15);
    out[i] = pred;
  }
  return out;
}

/* ------------------------------------------------------------------- the header --- */
/** a sample's name on the FM-1: ASCII 32..126, upper case, at most 8 (a file's extension dropped; none: SAMPLE) */
export function cleanName(s) {
  const t = String(s ?? "").trim().replace(/\.[A-Za-z0-9]{1,5}$/, "").replace(/[^\x20-\x7e]/g, "").toUpperCase().slice(0, 8).trim();
  return t || "SAMPLE";
}
const w16 = (b, o, v) => { b[o] = v & 255; b[o + 1] = v >> 8 & 255; };
const w32 = (b, o, v) => { w16(b, o, v & 0xffff); w16(b, o + 2, v >>> 16); };
const r16 = (b, o) => b[o] | b[o + 1] << 8;
const r32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
/** the 480-byte header (Appendix): one zone over all keys, not looped, root 60 */
export function buildHeader({ name, n, data, hz, root = ROOT }) {
  const h = new Uint8Array(HDR_SIZE), z = 32, nm = cleanName(name);
  w32(h, 0, MAGIC);
  w16(h, 4, 1);
  h[6] = 1;
  for (let i = 0; i < nm.length; i++) h[8 + i] = nm.charCodeAt(i);
  w32(h, 16, data.length);
  w32(h, 20, crc32(data));
  w32(h, z, 0);
  w32(h, z + 4, n);
  w32(h, z + 8, 0);
  w32(h, z + 12, n - 1);
  w32(h, z + 16, rateQ16(hz));
  w16(h, z + 20, root * 16);
  h[z + 26] = 127;
  return h;
}
/** the name as SMP_INFO sends it: the 8 bytes up to the first 0, ASCII 32..126 only */
export function headerName(h, at = 8) {
  let s = "";
  for (let i = 0; i < 8 && h[at + i]; i++) if (h[at + i] >= 32 && h[at + i] <= 126) s += String.fromCharCode(h[at + i]);
  return s;
}
/** the header's fields: { ok (the magic), version, nz, name, dataLen, crc, zones [{ off, n, ls, le, rate, hz, root16,
 * pred, idx, lo, hi, looped }] } */
export function parseHeader(h) {
  const nz = h[6], zones = [];
  for (let i = 0; i < Math.min(nz, 16); i++) {
    const z = 32 + 28 * i, rate = r32(h, z + 16);
    zones.push({ off: r32(h, z), n: r32(h, z + 4), ls: r32(h, z + 8), le: r32(h, z + 12), rate, hz: hzOfQ16(rate),
      root16: r16(h, z + 20) << 16 >> 16, pred: r16(h, z + 22) << 16 >> 16, idx: h[z + 24], lo: h[z + 25], hi: h[z + 26],
      looped: h[z + 27] });
  }
  return { ok: r32(h, 0) === MAGIC, version: r16(h, 4), nz, name: headerName(h), dataLen: r32(h, 16), crc: r32(h, 20), zones };
}
/** why the firmware would not play this header in a slot holding max data bytes (eng_sample.c smp_user_scan's checks),
 * or null */
export function headerProblem(p, max) {
  const size = max + DATA_OFF;
  if (!p.ok) return "not a sample (no FSMP header)";
  if (p.version !== 1) return `header version ${p.version}`;
  if (!p.nz || p.nz > 16) return `${p.nz} zones`;
  if (p.dataLen > max) return `${p.dataLen} bytes of data, the slot holds ${max}`;
  for (const [i, z] of p.zones.entries()) {
    if (z.off > p.dataLen || z.n > 2 * size || z.off + Math.floor((z.n + 1) / 2) > p.dataLen) return `zone ${i + 1} outside the data`;
    if (z.idx > 88 || !z.rate || z.rate > 4 * 65536 || z.ls > z.le || z.le >= z.n || z.lo > z.hi) return `zone ${i + 1} malformed`;
  }
  return null;
}

/* ------------------------------------------------------- the loader's sweep --- */
/** CRC-16, poly 0x1021, init 0, MSB first (ota.c ota_crc16: the update record's) */
export function crc16(b) {
  let c = 0;
  for (const x of b) {
    c ^= x << 8;
    for (let i = 0; i < 8; i++) c = c & 0x8000 ? ((c << 1) ^ 0x1021) & 0xffff : (c << 1) & 0xffff;
  }
  return c;
}
/** data offsets of the windows (a sector's +0xF00..+0xF4F, past the data's end 0xFF) that could read as an update
 * record: u16 at +0 not 0 and = the CRC-16 of +2..+79 (the loader also wants "AT" at +6: the CRC alone is a superset) */
export function loaderWindows(data) {
  const hits = [], w = new Uint8Array(WINDOW_N);
  for (let d = WINDOW - DATA_OFF; d < data.length; d += SECTOR) {
    w.fill(255);
    w.set(data.subarray(d, d + WINDOW_N));
    const v = r16(w, 0);
    if (v && v === crc16(w.subarray(2))) hits.push(d);
  }
  return hits;
}

/* ------------------------------------------------------------ encode for a slot --- */
/** 16-bit samples at hz -> { data, header, n, hz, name, shift }: encoded, its header built; a loader window moves the
 * sample by one source sample (drops the first) and encodes again (opts.windows: the check, tests) */
export function encodeSample(pcm, hz, name, opts = {}) {
  const windows = opts.windows || loaderWindows;
  for (let shift = 0; shift < SHIFT_TRIES && shift < pcm.length; shift++) {
    const x = pcm.subarray(shift), data = adpcmEncode(x);
    if (windows(data).length) continue;
    return { data, header: buildHeader({ name, n: x.length, data, hz }), n: x.length, hz, name: cleanName(name), shift };
  }
  throw new SampleError("This sample cannot be written safely: change the trim a little and try again.");
}

/* ------------------------------------------------------------------- backups --- */
/** a backup file: the raw slot, 512 + data bytes (the header, 32 bytes 0xFF, the data) */
export function backupBytes(header, data) {
  const b = new Uint8Array(DATA_OFF + data.length).fill(255, HDR_SIZE, DATA_OFF);
  b.set(header.subarray(0, HDR_SIZE));
  b.set(data, DATA_OFF);
  return b;
}
const date = (d = new Date()) => `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
export const backupName = (slot, name) =>
  `hortator-sample-${SLOT_NAMES[slot]}-${String(name || "SAMPLE").replace(/\s+/g, "-").replace(/[^A-Za-z0-9._-]/g, "") || "SAMPLE"}-${date()}.fsmp`;
const kib = (n) => `${Math.round(n / 1024)} KiB`;
/** a backup file checked for slot: { header (480 B), data, info (parseHeader) }; SampleError with the reason */
export function parseBackup(b, slot) {
  const bytes = b instanceof Uint8Array ? b : new Uint8Array(b);
  if (bytes.length < DATA_OFF + 1) throw new SampleError("Not a sample file (.fsmp): too short.");
  const header = bytes.slice(0, HDR_SIZE), info = parseHeader(header);
  if (!info.ok || info.version !== 1 || !info.nz || info.nz > 16) throw new SampleError("Not a sample file (.fsmp): no FSMP header.");
  if (bytes.length !== DATA_OFF + info.dataLen) throw new SampleError(`The file's size (${bytes.length} B) is not its header's (${DATA_OFF + info.dataLen} B): damaged.`);
  const data = bytes.slice(DATA_OFF);
  if (crc32(data) !== info.crc) throw new SampleError("The sample's CRC does not match: the file is damaged.");
  if (info.dataLen > SLOT_MAX[slot]) throw new SampleError(`Too big for ${SLOT_NAMES[slot]} (${kib(SLOT_SIZE[slot])}): ${kib(info.dataLen)} of data.`);
  const why = headerProblem(info, SLOT_MAX[slot]);
  if (why) throw new SampleError(`Not a sample the FM-1 plays: ${why}.`);
  if (loaderWindows(data).length) throw new SampleError("This file cannot be written safely to the FM-1 (it holds bytes its updater could take for an update record).");
  return { header, data, info };
}

/* ---------------------------------------------------------- a file's own rate --- */
/** the sample rate a WAV ("fmt ") or AIFF / AIFC ("COMM", 80-bit float) file states, 0 unknown: the page decodes
 * at that rate (an OfflineAudioContext of it), so decodeAudioData does not resample the file to the sound card's */
export function sniffRate(b) {
  const tag = (o) => String.fromCharCode(b[o], b[o + 1], b[o + 2], b[o + 3]);
  if (b.length < 12) return 0;
  const le = (o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
  const be = (o) => (b[o] << 24 | b[o + 1] << 16 | b[o + 2] << 8 | b[o + 3]) >>> 0;
  if (tag(0) === "RIFF" && tag(8) === "WAVE") {
    for (let o = 12; o + 8 <= b.length; o += 8 + le(o + 4) + (le(o + 4) & 1)) if (tag(o) === "fmt " && o + 16 <= b.length) return le(o + 12);
    return 0;
  }
  if (tag(0) === "FORM" && (tag(8) === "AIFF" || tag(8) === "AIFC")) {
    for (let o = 12; o + 8 <= b.length; o += 8 + be(o + 4) + (be(o + 4) & 1)) {
      if (tag(o) !== "COMM" || o + 26 > b.length) continue;
      const e = o + 16, exp = ((b[e] & 0x7f) << 8 | b[e + 1]) - 16383, hi = be(e + 2);
      return Math.round(hi * 2 ** (exp - 31));
    }
  }
  return 0;
}
