// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The web editor's samples against the firmware's decoder: sounds encoded by web/editor/sample.mjs (the Samples tab's
// encoder), each slot image (header + data) read by the firmware itself (tests/sample_xcheck.c: eng_sample.c's
// smp_user_scan, then sample_next as a voice plays it). Every decoded sample must equal the page's decoder's (its
// preview) bit for bit, and the firmware must accept every header the page writes. Also: tools/sampleio.py's
// user_slot writes the same header and data for the same 16-bit input.
//   node tests/sample_xcheck.mjs build/host/sample_xcheck
import { execFileSync, spawnSync } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import * as S from "../web/editor/sample.mjs";

const bin = process.argv[2];
if (!bin) { console.log("usage: node tests/sample_xcheck.mjs SAMPLE_XCHECK_BINARY"); process.exit(1); }
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
rmSync(dir, { recursive: true, force: true });
console.log(bad ? `SAMPLE XCHECK FAILED (${bad})` : "sample xcheck: the page's samples play on the firmware as previewed");
process.exit(bad ? 1 : 0);
