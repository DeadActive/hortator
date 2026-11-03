// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
//
// Sample sets and chopping, the page's pure logic (sampset.mjs, chop.mjs); node, no browser, no hardware; run by
// web/editor/test_editor.mjs (node web/editor/test_sampset.mjs alone):
// - a set's image: one slot exactly as a single sample always was; several: one key each from C2, each slot's bytes
//   copied (its decode bit for bit), the names trailer, the firmware's checks passed, the loader's windows cleared by gap
//   bytes (no sample changes), the limits (16 slots, the slot's room)
// - a set read back: its slots in the firmware's SLOT order; a set the page did not make (zones sharing their data,
//   loops) listed by its distinct samples and marked not plain
// - chopping: equal slices, beats (1/4, 1/8, 1/16 at a tempo), the tempo of a loop detected, transients found (the
//   sensitivity: more cuts, never fewer), cuts edited, the slices that fit
import { pathToFileURL } from "node:url";
import * as S from "./sample.mjs";
import * as T from "./sampset.mjs";
import * as C from "./chop.mjs";

const eqb = (a, b) => !!a && !!b && a.length === b.length && a.every((x, i) => x === b[i]);
const rnd = (seed) => () => { seed = (seed * 1103515245 + 12345) >>> 0; return seed / 4294967296 - 0.5; };
const tone = (n, hz, rate, amp = 0.6) => Int16Array.from({ length: n }, (_, i) => Math.round(32767 * amp * Math.exp(-i / (n / 3)) * Math.sin(2 * Math.PI * hz * i / rate)));
/** a drum loop: a hit (a decaying noise burst + a thump) at each of the times (s), length s seconds */
function loop(rate, seconds, times, seed = 3) {
  const g = rnd(seed), x = new Float32Array(Math.round(rate * seconds));
  for (const t of times) {
    const a = Math.round(t * rate);
    for (let i = 0; a + i < x.length && i < rate * 0.12; i++) x[a + i] += Math.exp(-i / (rate * 0.02)) * (0.6 * g() + 0.5 * Math.sin(2 * Math.PI * 80 * i / rate));
  }
  for (let i = 0; i < x.length; i++) x[i] += 0.0005 * g(); // a little noise floor
  return x;
}
const near = (a, b, tol) => a.length === b.length && a.every((x, i) => Math.abs(x - b[i]) <= tol);

export async function runSampsetTests(log = console.log) {
  let passed = 0, failed = 0;
  const ok = (cond, what) => { if (cond) passed++; else { failed++; log(`FAIL sets / chop: ${what}`); } };

  /* ---- one slot: exactly the single sample of before */
  const kick = tone(9001, 60, 22050), snare = tone(6000, 200, 44100), hat = tone(3001, 5000, 32000, 0.3);
  {
    const e = S.encodeSample(kick, 22050, "kick 01"), s = T.composeSet([T.makeSlot(kick, 22050, "kick 01")], "kick 01", S.SLOT_MAX[0]);
    ok(eqb(s.header, e.header) && eqb(s.data, e.data) && s.offs[0] === 0, "one slot: the header and data a single sample always had (byte for byte)");
  }
  /* ---- three slots */
  const slots = [T.makeSlot(kick, 22050, "kick"), T.makeSlot(snare, 44100, "snare"), T.makeSlot(hat, 32000, "hat")];
  const set = T.composeSet(slots, "kit", S.SLOT_MAX[1]), info = S.parseHeader(set.header);
  ok(info.ok && info.nz === 3 && info.name === "KIT" && info.dataLen === set.data.length && S.headerProblem(info, S.SLOT_MAX[1]) === null,
    "three slots: one header, three zones, the firmware's checks pass");
  ok(info.zones.map((z) => `${z.lo}-${z.hi}@${z.root16 / 16}`).join() === "0-36@36,37-37@37,38-127@38" && info.zones.every((z) => !z.looped && z.ls === 0 && z.le === z.n - 1),
    "three slots: one key each from C2 (the first down to 0, the last up to 127), not looped");
  ok(info.zones.every((z, i) => z.n === slots[i].n && z.rate === slots[i].rate && eqb(S.adpcmDecode(set.data.subarray(z.off), z.n), S.adpcmDecode(slots[i].data, slots[i].n))),
    "three slots: each its own rate, its bytes decode as the sample (bit for bit)");
  ok(set.data.length === T.setBytes(slots) && T.setBytes(slots) === 4501 + 3000 + 1501 + 8 * 3 + 5, "the data: the slots' bytes and the names trailer");
  {
    const back = T.parseSet(set.header, set.data);
    ok(back.plain && back.slots.map((s) => s.name).join() === "KICK,SNARE,HAT" && back.slots.every((s, i) => eqb(s.data, slots[i].data) && s.n === slots[i].n),
      "read back: the slots, their names, their bytes; the page's own layout (plain)");
    const moved = T.composeSet([back.slots[2], back.slots[0], back.slots[1]], "kit", S.SLOT_MAX[1]), again = T.parseSet(moved.header, moved.data);
    ok(again.slots.map((s) => s.name).join() === "HAT,KICK,SNARE" && eqb(again.slots[0].data, slots[2].data) && eqb(again.slots[2].data, slots[1].data),
      "reordered: each slot's bytes copied, the names follow");
  }
  /* ---- limits */
  {
    const tiny = T.makeSlot(Int16Array.from({ length: 10 }, (_, i) => i * 100), 44100, "t");
    ok(T.composeSet(Array(16).fill(tiny), "x", 1000).header[6] === 16, "16 slots: a set");
    let e = null;
    try { T.composeSet(Array(17).fill(tiny), "x", 100000); } catch (x) { e = x; }
    ok(e && e.sample && /16/.test(e.message), "17 slots: refused");
    e = null;
    try { T.composeSet(slots, "x", 5000); } catch (x) { e = x; }
    ok(e && e.sample && /holds 5000/.test(e.message), "too big for the slot: refused");
    const r = T.roomFor(slots, S.SLOT_MAX[1]);
    ok(r.room === 13 && r.free === S.SLOT_MAX[1] - T.setBytes(slots) - 8, "the room for one more: the bytes left (its name's 8 bytes taken off), 13 slots left");
    ok(T.roomFor([slots[0]], 10000).free === 10000 - 4501 - T.namesBytes(2) && T.roomFor(Array(16).fill(tiny), 1e6).free === 0, "the room: one slot (the trailer comes with the second), a full set");
  }
  /* ---- the loader's windows: a gap byte moves the zone, no sample changes */
  {
    let calls = 0;
    const fake = (d) => (calls++ < 3 ? [d.length > 6000 ? 5000 : 0] : []);
    const s = T.composeSet(slots, "kit", S.SLOT_MAX[1], { windows: fake });
    ok(s.gaps[0] + s.gaps[1] === 3 && s.data.length === T.setBytes(slots) + 3, `a window: gap bytes before the slot holding it (gaps ${s.gaps})`);
    const p = S.parseHeader(s.header);
    ok(p.zones.every((z, i) => z.off === s.offs[i] && eqb(S.adpcmDecode(s.data.subarray(z.off), z.n), S.adpcmDecode(slots[i].data, slots[i].n)))
      && S.headerProblem(p, S.SLOT_MAX[1]) === null && T.parseSet(s.header, s.data).slots.map((x) => x.name).join() === "KICK,SNARE,HAT",
    "with gaps: every slot plays as before, the names still read");
    // a real one: slot 2's bytes hold an update record where the image puts sector 1's window
    const g = rnd(5), a = T.makeSlot(Int16Array.from({ length: 8000 }, () => Math.round(20000 * g())), 44100, "a");
    const b = T.makeSlot(Int16Array.from({ length: 12000 }, () => Math.round(20000 * g())), 44100, "b");
    const w = new Uint8Array(80);
    for (let i = 2; i < 80; i++) w[i] = (i * 7) & 255;
    w[6] = 0x41; w[7] = 0x54;
    const c = S.crc16(w.subarray(2)); w[0] = c & 255; w[1] = c >> 8;
    b.data.set(w, 0xd00 + 4096 - 4000); // data offset 0xd00 + 4096 = sector 1's +0xF00; b starts at 4000
    const raw = new Uint8Array(4000 + 6000); raw.set(a.data); raw.set(b.data, 4000);
    ok(S.loaderWindows(raw).length === 1, "(the planted record sits on a window when packed without a gap)");
    const r = T.composeSet([a, b], "w", S.SLOT_MAX[0]);
    ok(S.loaderWindows(r.data).length === 0 && r.gaps[1] >= 1 && eqb(r.data.subarray(r.offs[1], r.offs[1] + 6000), b.data),
      `a planted update record: slot 2 moved by ${r.gaps[1]} gap byte(s), its bytes unchanged, no window left`);
  }
  /* ---- a set the page did not make: shared data, loops (an upstream multi-zone set) */
  {
    const d = new Uint8Array(3000).fill(0x17), h = new Uint8Array(480);
    const z = (i, off, n, lo, hi, root, looped = 0) => { const o = 32 + 28 * i, v = new DataView(h.buffer);
      v.setUint32(o, off, true); v.setUint32(o + 4, n, true); v.setUint32(o + 12, n - 1, true); v.setUint32(o + 16, 65536, true);
      v.setInt16(o + 20, root * 16, true); h[o + 25] = lo; h[o + 26] = hi; h[o + 27] = looped; };
    new DataView(h.buffer).setUint32(0, S.MAGIC, true); h[4] = 1; h[6] = 4;
    h.set([80, 69, 82, 67], 8); new DataView(h.buffer).setUint32(16, 3000, true);
    z(0, 1000, 2000, 60, 127, 60); z(1, 0, 2000, 0, 40, 36); z(2, 0, 2000, 41, 59, 50, 1); z(3, 1000, 2000, 50, 55, 52);
    const p = T.parseSet(h, d);
    ok(!p.plain && p.nz === 4 && p.slots.length === 2 && p.slots[0].zone === 1 && p.slots[1].zone === 3 && p.slots.every((s) => s.n === 2000 && s.data.length === 1000),
      "a set sharing data: 2 distinct samples in the firmware's order (each by its lowest zone), not plain");
  }

  /* ---- chop: cuts */
  ok(C.equalCuts(1000, 4).join() === "250,500,750" && C.equalCuts(10, 1).length === 0, "equal slices: N - 1 cuts");
  ok(C.beatCuts(88200, 44100, 120, 4).join() === "22050,44100,66150" && C.beatCuts(88200, 44100, 120, 8).length === 7
    && C.beatCuts(88200, 44100, 120, 16).length === 15 && C.beatCuts(88200, 44100, 120, 16, 1000)[0] === 1000,
  "beats: 1/4, 1/8, 1/16 at 120 BPM over 2 s (an offset moves the grid)");
  ok(C.slices(1000, [750, 250, 250, 0, 1000, 500]).map((s) => s.join("-")).join() === "0-250,250-500,500-750,750-1000", "slices: the cuts sorted, repeats and ends dropped");
  {
    let cuts = [250, 500, 750];
    cuts = C.addCut(cuts, 600, 1000); ok(cuts.join() === "250,500,600,750", "a cut added");
    cuts = C.moveCut(cuts, 2, 900, 1000); ok(cuts.join() === "250,500,750,900", "a cut moved (kept in order)");
    cuts = C.removeCut(cuts, 0); ok(cuts.join() === "500,750,900", "a cut removed");
    ok(C.nearestCut([500, 750, 900], 740) === 1 && C.nearestCut([], 5) === -1, "the cut nearest a point");
  }
  /* ---- chop: the tempo of a loop */
  {
    const rate = 22050, beat = 60 / 128, times = [];
    for (let i = 0; i < 32; i++) times.push(i * beat / 2); // 4 bars of eighths at 128 BPM
    const x = loop(rate, 16 * beat, times), t = C.detectTempo(x, rate);
    ok(Math.abs(t.bpm - 128) < 0.2, `tempo: a 4-bar loop at 128 BPM (${t.bpm})`);
    const times2 = [];
    for (let i = 0; i < 20; i++) times2.push(0.05 + i * (60 / 95)); // 95 BPM, not a whole number of bars
    const t2 = C.detectTempo(loop(rate, 0.05 + 20 * 60 / 95 + 0.3, times2), rate);
    ok(Math.abs(t2.bpm - 95) < 2, `tempo: hits at 95 BPM, the length no help (${t2.bpm})`);
  }
  /* ---- chop: transients */
  {
    const rate = 44100, times = [0.1, 0.5, 0.75, 1.25, 1.4], x = loop(rate, 1.8, times);
    const at = C.detectTransients(x, rate, 50);
    ok(near(at, times.map((t) => Math.round(t * rate)), rate * 0.006), `transients: the five hits within 6 ms (${at.map((a) => (a / rate).toFixed(3))})`);
    const lo = C.detectTransients(x, rate, 0), hi = C.detectTransients(x, rate, 100);
    ok(lo.length <= at.length && hi.length >= at.length, `sensitivity: lower finds no more, higher no fewer (${lo.length} ${at.length} ${hi.length})`);
    ok(C.detectTransients(new Float32Array(44100), rate, 100).length === 0, "transients: none in silence");
    const soft = loop(rate, 1.8, times).map((v, i) => (i >= 0.74 * rate && i < 0.9 * rate ? v * 0.1 : v)); // the third hit 20 dB down
    ok(C.detectTransients(soft, rate, 90).length >= 5 && C.detectTransients(soft, rate, 10).length <= C.detectTransients(soft, rate, 90).length,
      "a soft hit: found at a high sensitivity");
  }
  /* ---- chop: what fits */
  {
    const f = C.fitting([1000, 2000, 3000, 4000], 6500, 16);
    ok(f.n === 3 && f.bytes === 6000, "fitting: in order until the room runs out");
    ok(C.fitting([10, 10, 10], 1000, 2).n === 2, "fitting: the slots left");
  }
  return { passed, failed };
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const r = await runSampsetTests();
  console.log(r.failed ? `SETS / CHOP TESTS FAILED (${r.failed} of ${r.passed + r.failed})` : `sets / chop tests passed (${r.passed})`);
  process.exit(r.failed ? 1 : 0);
}
