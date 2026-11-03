// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The web editor's samples against the firmware's decoder: sounds encoded by web/editor/sample.mjs (the Samples tab's
// encoder), each slot image (header + data) read by the firmware itself (tests/sample_xcheck.c: eng_sample.c's
// smp_user_scan, then sample_next as a voice plays it). Every decoded sample must equal the page's decoder's (its
// preview) bit for bit, and the firmware must accept every header the page writes. Also: tools/sampleio.py's
// user_slot writes the same header and data for the same 16-bit input. Sets of several samples (sampset.mjs, chopped
// slices among them) read by the firmware's SLOT list (tests/sample_set_xcheck.c): their samples in the page's order,
// each decoded bit for bit as the page's.
//   node tests/sample_xcheck.mjs build/host/sample_xcheck build/host/sample_set_xcheck
import { execFileSync, spawnSync } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import * as S from "../web/editor/sample.mjs";
import * as T from "../web/editor/sampset.mjs";
import * as C from "../web/editor/chop.mjs";

const bin = process.argv[2], setBin = process.argv[3];
if (!bin || !setBin) { console.log("usage: node tests/sample_xcheck.mjs SAMPLE_XCHECK_BINARY SAMPLE_SET_XCHECK_BINARY"); process.exit(1); }
const dir = mkdtempSync(join(dirname(bin), "smpx-"));
let bad = 0, n = 0;
const rnd = (seed) => () => { seed = (seed * 1103515245 + 12345) >>> 0; return seed / 4294967296 - 0.5; };
const g = rnd(9);
const SOUNDS = [
  ["sine 440 Hz, 44.1 kHz", 44100, Float32Array.from({ length: 30000 }, (_, i) => 0.7 * Math.sin(2 * Math.PI * 440 * i / 44100))],
  ["white noise, 22.05 kHz", 22050, Float32Array.from({ length: 20001 }, () => 1.9 * g())],
  ["full-scale square (the clamps)", 32000, Float32Array.from({ length: 16000 }, (_, i) => (i % 40 < 20 ? 1 : -1))],
  ["a kick: swept sine, decaying", 44100, Float32Array.from({ length: 40000 }, (_, i) => Math.exp(-i / 6000) * Math.sin(2 * Math.PI * 50 * i / 44100 * (1 + 4 * Math.exp(-i / 900))))],
  ["silence", 11025, new Float32Array(5000)],
  ["one sample", 44100, Float32Array.from([0.5])],
  ["two samples", 44100, Float32Array.from([-0.5, 0.25])],
  ["clicks and steps", 16000, Float32Array.from({ length: 9999 }, (_, i) => (i % 1000 === 0 ? 1 : i % 3000 < 1500 ? 0.3 : -0.3))],
  ["chirp 20 Hz..20 kHz, normalized", 44100, S.normalize(Float32Array.from({ length: 44100 }, (_, i) => 0.2 * Math.sin(2 * Math.PI * (20 + 9990 * i / 44100) * i / 44100)))],
];
for (const [what, hz, x] of SOUNDS) {
  const e = S.encodeSample(S.toPcm16(x), hz, what.slice(0, 8));
  const file = join(dir, `s${n++}.fsmp`);
  writeFileSync(file, S.backupBytes(e.header, e.data));
  const r = spawnSync(bin, [file], { maxBuffer: 1 << 24 });
  const out = r.stdout, nl = out.indexOf(10), head = out.subarray(0, nl).toString().split(" ").map(Number);
  const fw = new Int16Array(out.buffer.slice(out.byteOffset + nl + 1, out.byteOffset + out.length));
  const page = S.adpcmDecode(e.data, e.n);
  const same = r.status === 0 && head[0] === 1 && head[1] === e.n && fw.length === page.length && fw.every((v, i) => v === page[i]);
  if (!same) bad++;
  console.log(`${same ? "ok  " : "FAIL"}  ${what}: ${e.n} samples, the firmware's decode = the page's (bit for bit)${same ? "" : ` (status ${r.status}, ${head})`}`);
}
/* the firmware refuses what the page refuses */
{
  const e = S.encodeSample(S.toPcm16(Float32Array.from({ length: 1000 }, (_, i) => Math.sin(i))), 22050, "X");
  const h = Uint8Array.from(e.header);
  h[32 + 24] = 89; // step index past 88
  const file = join(dir, "bad.fsmp");
  writeFileSync(file, S.backupBytes(h, e.data));
  const r = spawnSync(bin, [file]);
  const same = r.status === 2 && typeof S.headerProblem(S.parseHeader(h), S.SLOT_MAX[0]) === "string";
  if (!same) bad++;
  console.log(`${same ? "ok  " : "FAIL"}  a malformed zone: refused by the firmware and by the page's check`);
}
/* tools/sampleio.py: the same slot */
{
  const pcm = S.toPcm16(Float32Array.from({ length: 7001 }, (_, i) => 0.6 * Math.sin(i / 9) + 0.2 * Math.sin(i / 2.1)));
  const e = S.encodeSample(pcm, 22050, "PYTHON");
  let res = "skipped (no python3)";
  try {
    const hex = execFileSync("python3", ["-c", `import sys, json; sys.path.insert(0, "tools"); import sampleio as s
h, d = s.user_slot("python", [(json.loads(sys.stdin.read()), 60, None, None)]); sys.stdout.write(h.hex() + " " + d.hex())`],
    { input: JSON.stringify(Array.from(pcm)), cwd: join(dirname(new URL(import.meta.url).pathname), "..") }).toString().split(" ");
    res = hex[0] === Buffer.from(e.header).toString("hex") && hex[1] === Buffer.from(e.data).toString("hex") ? "same" : "different";
  } catch (x) { if (!/ENOENT/.test(String(x))) res = `error: ${String(x).split("\n")[0]}`; }
  if (res !== "same" && !res.startsWith("skipped")) bad++;
  console.log(`${res === "same" ? "ok  " : res.startsWith("skipped") ? "skip" : "FAIL"}  tools/sampleio.py user_slot at 22.05 kHz: the page's header and data (${res})`);
}
/* tools/fm1_sample_upload.py: the same loader windows, the same packing */
{
  const bufs = [new Uint8Array(9000), Uint8Array.from({ length: 13000 }, (_, i) => (i * 37 + (i >> 3)) & 255)];
  const p = Uint8Array.from({ length: 9000 }, (_, i) => (i * 11) & 255), w = new Uint8Array(80);
  for (let i = 2; i < 80; i++) w[i] = i * 3;
  const c = S.crc16(w.subarray(2)); w[0] = c & 255; w[1] = c >> 8;
  p.set(w, 0xd00 + 4096);
  bufs.push(p);
  let res = "skipped (no python3)";
  try {
    const got = JSON.parse(execFileSync("python3", ["-c", `import sys, json; sys.path.insert(0, "tools"); import fm1_sample_upload as t
bufs = [bytes(b) for b in json.loads(sys.stdin.read())]
print(json.dumps([[t.loader_windows(b) for b in bufs], [list(t.unpack7(t.pack7(b))) == list(b) for b in bufs]]))`],
    { input: JSON.stringify(bufs.map((b) => Array.from(b))), cwd: join(dirname(new URL(import.meta.url).pathname), "..") }).toString());
    res = JSON.stringify(got[0]) === JSON.stringify(bufs.map((b) => S.loaderWindows(b))) && got[1].every(Boolean) && got[0][2].length === 1 ? "same" : "different";
  } catch (x) { if (!/ENOENT/.test(String(x))) res = `error: ${String(x).split("\n")[0]}`; }
  if (res !== "same" && !res.startsWith("skipped")) bad++;
  console.log(`${res === "same" ? "ok  " : res.startsWith("skipped") ? "skip" : "FAIL"}  tools/fm1_sample_upload.py: the page's loader windows, pack7 round trip (${res})`);
}
/* sets of several samples: the firmware's SLOT order and decode */
{
  const g2 = rnd(21), pcm = (n, f) => S.toPcm16(Float32Array.from({ length: n }, (_, i) => f(i)));
  const kick = pcm(9001, (i) => Math.exp(-i / 3000) * Math.sin(2 * Math.PI * 60 * i / 22050 * (1 + 3 * Math.exp(-i / 700))));
  const snare = pcm(6000, (i) => Math.exp(-i / 1500) * (0.6 * g2() + 0.3 * Math.sin(i / 5)));
  const hat = pcm(3001, (i) => Math.exp(-i / 400) * 1.6 * g2());
  const sets = [
    ["3 slots at 22.05, 44.1, 32 kHz", [T.makeSlot(kick, 22050, "kick"), T.makeSlot(snare, 44100, "snare"), T.makeSlot(hat, 32000, "hat")], 1],
    ["16 slots, odd and even lengths", Array.from({ length: 16 }, (_, i) => T.makeSlot(pcm(301 + 77 * i, (j) => Math.sin(j / (2 + i)) * (i % 3 ? 0.9 : 1.4)), 44100, `s${i}`)), 0],
    ["2 slots (the names trailer, the full-scale clamps)", [T.makeSlot(pcm(5000, (i) => (i % 40 < 20 ? 1 : -1)), 11025, "sq"), T.makeSlot(pcm(4999, () => 1.9 * g2()), 16000, "noise")], 2],
  ];
  // a reordered set: its slots' bytes copied
  { const a = T.composeSet(sets[0][1], "kit", S.SLOT_MAX[0]), b = T.parseSet(a.header, a.data); sets.push(["reordered (bytes copied)", [b.slots[2], b.slots[0], b.slots[1]], 0]); }
  // chopped: a loop cut on 1/8 at 120 BPM
  {
    const rate = 22050, x = Float32Array.from({ length: rate * 2 }, (_, i) => Math.exp(-(i % 5512) / 600) * Math.sin(i / 4) * 0.8);
    const all = S.toPcm16(x), sl = C.slices(all.length, C.beatCuts(all.length, rate, 120, 8));
    sets.push(["a loop chopped into 8 (1/8 at 120 BPM)", sl.map(([a, b], i) => T.makeSlot(all.slice(a, b), rate, `loop ${i + 1}`)), 0]);
  }
  // with a gap byte (a planted update record moved off its window)
  {
    const a = T.makeSlot(pcm(8000, () => 1.2 * g2()), 44100, "a"), b = T.makeSlot(pcm(12000, () => 1.2 * g2()), 44100, "b"), w = new Uint8Array(80);
    for (let i = 2; i < 80; i++) w[i] = (i * 7) & 255;
    const c = S.crc16(w.subarray(2)); w[0] = c & 255; w[1] = c >> 8;
    b.data.set(w, 0xd00 + 4096 - 4000);
    sets.push(["with a gap byte (a loader window avoided)", [a, b], 0]);
  }
  for (const [what, slots, k] of sets) {
    const img = T.composeSet(slots, what.slice(0, 8), S.SLOT_MAX[k]), file = join(dir, `set${n++}.fsmp`);
    writeFileSync(file, S.backupBytes(img.header, img.data));
    const r = spawnSync(setBin, [file], { maxBuffer: 1 << 26 }), o = r.stdout;
    const v = new Int32Array(o.buffer.slice(o.byteOffset, o.byteOffset + o.length));
    let at = 1, same = r.status === 0 && v[0] === slots.length && S.loaderWindows(img.data).length === 0;
    for (let i = 0; same && i < slots.length; i++) {
      const [zi, zn, off16] = [v[at], v[at + 1], v[at + 2]], page = S.adpcmDecode(slots[i].data, slots[i].n);
      at += 3;
      same = zi === i && zn === slots[i].n && off16 === 0 && page.every((x, j) => x === v[at + j]);
      at += zn;
    }
    if (!same) bad++;
    console.log(`${same ? "ok  " : "FAIL"}  set: ${what}: the firmware's SLOT 1..${slots.length} = the page's slots, each decoded bit for bit${img.gaps.some(Boolean) ? ` (gaps ${img.gaps})` : ""}${same ? "" : ` (status ${r.status}, count ${v[0]})`}`);
  }
}
rmSync(dir, { recursive: true, force: true });
console.log(bad ? `SAMPLE XCHECK FAILED (${bad})` : "sample xcheck: the page's samples play on the firmware as previewed");
process.exit(bad ? 1 : 0);
