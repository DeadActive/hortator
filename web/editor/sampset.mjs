// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
//
// A user sample set (USR1..USR3) of several samples, the page's side (web/EDITOR_PROTOCOL.md §5 "Sets the page
// makes"). Pure: no DOM; node tests drive it (test_sampset.mjs; tests/sample_xcheck.mjs plays every slot on the
// firmware's own decoder and checks its SLOT order).
// - a set = up to 16 slots, each one zone of the slot format (sample.mjs: IMA ADPCM from predictor 0 / index 0, its own
//   rate). The page composes the whole image (header + data) and uploads it as one sample upload (SMP_BEGIN .. SMP_END):
//   the header goes last, so a failed upload leaves the set empty, never half a set.
// - the slots' keys: one slot over all keys at root 60 (as a single sample always was); two or more one key each from
//   C2 (note 36 + i), the first stretched down to 0 and the last up to 127. The firmware's SLOT knob takes the samples
//   in key order (dm_sample.c sl_make), so SLOT n = the page's slot n.
// - moving, deleting or renaming a slot copies its ADPCM bytes (its decode stays bit for bit); only trimming,
//   normalizing or replacing one encodes it again.
// - the slots' names (the header has only the set's 8 characters): with two or more slots, a trailer at the data's end
//   (8 characters a slot, their count, "SLNM"), inside the data length and its CRC, outside every zone: the firmware
//   never reads it (it plays zones only).
// - START in every slot: with two or more slots, before the names, the START table the firmware reads at a hit
//   (dm_sample.c smp_user_seek; the header's rsv2: "SEEK", its offset in the data): per slot the decoder's state
//   (predictor, step index: 3 bytes) (n * s) >> 7 nibbles in, s 1..127 (381 bytes a slot). One slot: none (the
//   firmware works its states out itself, the image stays as a single sample always was).
// - the update loader's windows (+0xF00 of each sector): the page moves the zone holding one by a gap byte (0) before it
//   and checks the whole image again: no sample changes.
// - a set the page did not make (a backup of upstream sets: zones sharing their data, loops, other keys): its distinct
//   samples (the same offset, length and rate: one sample, as the firmware's SLOT counts them) are listed; changing it
//   lays it out again as plain slots (the page asks first).
import { crc32 } from "./proto.mjs";
import * as S from "./sample.mjs";

export const MAX_SLOTS = 16, BASE_NOTE = 36, NAME_LEN = 8, SEEK_BYTES = 127 * 3, SEEK_MAGIC = 0x4b454553; // "SEEK"
const TAG = [0x53, 0x4c, 0x4e, 0x4d]; // "SLNM"
export const SLOT_TRAILER = SEEK_BYTES + NAME_LEN; // the trailer's bytes a slot (its START table, its name)
const GAP_TRIES = 512;

/** a slot: { name, data (ADPCM), n (samples), rate (Hz / 44100 x 65536) } */
export const slotBytes = (s) => Math.ceil(s.n / 2);
/** the trailer's bytes for a set of n slots (none for one): the START table and the names */
export const namesBytes = (n) => (n >= 2 ? (SEEK_BYTES + NAME_LEN) * n + 1 + TAG.length : 0);
/** the decoder's states (n * s) >> 7 nibbles into ADPCM data b, s 1..127: [[predictor, step index]] (dm_sample.c
 * smp_seek_fill, the device's decoder) */
export function seekStates(b, n) {
  const pcm = S.adpcmDecode(b, n), idx = S.adpcmIndexes(b, n), out = [];
  for (let s = 1; s < 128; s++) { const at = Math.floor(n * s / 128); out.push(at ? [pcm[at - 1], idx[at - 1]] : [0, 0]); }
  return out;
}
/** the data bytes a set of these slots needs (no gaps) */
export const setBytes = (slots) => slots.reduce((a, s) => a + slotBytes(s), 0) + namesBytes(slots.length);
/** the bytes left for one more slot in a slot holding max: { free, room (slots left) } */
export function roomFor(slots, max) {
  const room = MAX_SLOTS - slots.length;
  return { free: room > 0 ? Math.max(0, max - setBytes(slots) - (namesBytes(slots.length + 1) - namesBytes(slots.length))) : 0, room };
}
/** the keys of n slots: [{ lo, hi, root }] */
export function keyLayout(n) {
  if (n === 1) return [{ lo: 0, hi: 127, root: S.ROOT }];
  return Array.from({ length: n }, (_, i) => ({ lo: i === 0 ? 0 : BASE_NOTE + i, hi: i === n - 1 ? 127 : BASE_NOTE + i, root: BASE_NOTE + i }));
}
/** 16-bit samples at hz -> a slot */
export const makeSlot = (pcm, hz, name) => ({ name: S.cleanName(name), data: S.adpcmEncode(pcm), n: pcm.length, rate: S.rateQ16(hz) });
/** a slot's sound as the FM-1 plays it: { pcm, hz } */
export const decodeSlot = (s) => ({ pcm: S.adpcmDecode(s.data, s.n), hz: S.hzOfQ16(s.rate) });
/** a slot as a source for the sample editor (trim, normalize, a lower rate): { name, rate, channels } */
export function slotSource(s) {
  const { pcm } = decodeSlot(s);
  return { name: s.name, rate: Math.round(S.hzOfQ16(s.rate)), channels: [Float32Array.from(pcm, (v) => v / 32767)] };
}

const w16 = (b, o, v) => { b[o] = v & 255; b[o + 1] = v >> 8 & 255; };
const w32 = (b, o, v) => { w16(b, o, v & 0xffff); w16(b, o + 2, v >>> 16); };
function header(name, zones, data, seekAt) {
  const h = new Uint8Array(S.HDR_SIZE), nm = S.cleanName(name);
  w32(h, 0, S.MAGIC);
  w16(h, 4, 1);
  h[6] = zones.length;
  for (let i = 0; i < nm.length; i++) h[8 + i] = nm.charCodeAt(i);
  w32(h, 16, data.length);
  w32(h, 20, crc32(data));
  if (seekAt !== undefined) { w32(h, 24, SEEK_MAGIC); w32(h, 28, seekAt); }
  zones.forEach((z, i) => {
    const o = 32 + 28 * i;
    w32(h, o, z.off);
    w32(h, o + 4, z.n);
    w32(h, o + 8, 0);
    w32(h, o + 12, z.n - 1);
    w32(h, o + 16, z.rate);
    w16(h, o + 20, z.root * 16);
    h[o + 25] = z.lo;
    h[o + 26] = z.hi;
  });
  return h;
}

/** the set's image: { header (480 B), data, offs (each slot's data offset), gaps }; SampleError when it does not fit
 * max, has no slot or more than 16, or no gap clears the loader's windows (opts.windows: the check, tests) */
export function composeSet(slots, name, max, opts = {}) {
  const windows = opts.windows || S.loaderWindows, n = slots.length;
  if (!n) throw new S.SampleError("The set has no sample.");
  if (n > MAX_SLOTS) throw new S.SampleError(`A set holds ${MAX_SLOTS} samples at most.`);
  if (slots.some((s) => !s.n || !s.rate)) throw new S.SampleError("An empty sample cannot go into a set.");
  const keys = keyLayout(n), gaps = new Array(n + 1).fill(0); // gaps[n]: before the names trailer
  for (let t = 0; t < GAP_TRIES; t++) {
    const len = setBytes(slots) + gaps.reduce((a, g) => a + g, 0);
    if (len > max) throw new S.SampleError(`The set needs ${len} bytes, the slot holds ${max}: remove or shorten a sample.`);
    const data = new Uint8Array(len), offs = [], ends = [];
    let at = 0;
    slots.forEach((s, i) => {
      at += gaps[i];
      offs.push(at);
      data.set(s.data.subarray(0, slotBytes(s)), at);
      at += slotBytes(s);
      ends.push(at);
    });
    let seekAt;
    if (n >= 2) {
      at += gaps[n];
      seekAt = at;
      slots.forEach((s, i) => seekStates(s.data, s.n).forEach(([p, x], k) => {
        const q = at + SEEK_BYTES * i + 3 * k;
        data[q] = p & 255; data[q + 1] = (p >> 8) & 255; data[q + 2] = x;
      }));
      at += SEEK_BYTES * n;
      slots.forEach((s, i) => { const nm = S.cleanName(s.name); for (let c = 0; c < nm.length; c++) data[at + NAME_LEN * i + c] = nm.charCodeAt(c); });
      data[at + NAME_LEN * n] = n;
      data.set(TAG, at + NAME_LEN * n + 1);
      ends.push(at + NAME_LEN * n + 1 + TAG.length);
    }
    const hits = windows(data);
    if (!hits.length) {
      const zones = slots.map((s, i) => ({ off: offs[i], n: s.n, rate: s.rate, ...keys[i] }));
      return { header: header(name, zones, data, seekAt), data, offs, gaps: gaps.slice(0, n), name: S.cleanName(name) };
    }
    const j = ends.findIndex((e) => e > hits[0]); // the part (a slot, the trailer) the window starts in, or the next one
    gaps[j < 0 ? 0 : j]++;
  }
  throw new S.SampleError("This set cannot be written safely: change one of its samples a little and try again.");
}

const same = (a, b) => a.off === b.off && a.n === b.n && a.rate === b.rate;
/** the names trailer of data (n names), or null */
function trailer(data, n, end) {
  const len = NAME_LEN * n + 1 + TAG.length, at = data.length - len;
  if (n < 2 || at < end || TAG.some((c, i) => data[data.length - TAG.length + i] !== c) || data[data.length - TAG.length - 1] !== n) return null;
  return Array.from({ length: n }, (_, i) => S.headerName(data, at + NAME_LEN * i));
}
/** a slot image (header, data) as a set: { name, slots, plain, nz }. slots: the distinct samples in the firmware's
 * SLOT order (each by its lowest zone, by key, then table order), each { name, data (its bytes), n, rate, zone };
 * plain: the page's own layout (one zone a sample, the keys of keyLayout, no loop): changing it keeps the set as it is */
export function parseSet(h, data) {
  const info = S.parseHeader(h), z = info.zones, list = [];
  z.forEach((a, j) => {
    if (!a.n) return;
    const i = list.findIndex((x) => same(z[x], a));
    if (i >= 0) { if (a.lo < z[list[i]].lo) list[i] = j; } else if (list.length < MAX_SLOTS) list.push(j);
  });
  list.sort((a, b) => z[a].lo - z[b].lo || a - b);
  const end = Math.max(0, ...z.map((a) => a.off + Math.ceil(a.n / 2)));
  const names = trailer(data, list.length, end), keys = keyLayout(list.length);
  const slots = list.map((j, i) => ({
    name: names ? names[i] || `SLOT ${i + 1}` : list.length === 1 ? info.name || "SAMPLE" : `SLOT ${i + 1}`,
    data: data.slice(z[j].off, z[j].off + Math.ceil(z[j].n / 2)), n: z[j].n, rate: z[j].rate, zone: j,
  }));
  const plain = list.length === info.nz && list.every((j, i) => j === i && !z[j].looped && z[j].lo === keys[i].lo
    && z[j].hi === keys[i].hi && z[j].root16 === keys[i].root * 16);
  return { name: info.name, slots, plain, nz: info.nz };
}
