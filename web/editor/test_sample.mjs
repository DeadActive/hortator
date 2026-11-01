// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
//
// The user sample slots (web/EDITOR_PROTOCOL.md §2.7, §5, Appendix), node, no browser, no hardware; run by
// web/editor/test_editor.mjs:
// - sample.mjs: the ADPCM encoder / decoder (the firmware's decoder, step for step; the same bytes as tools/sampleio.py),
//   mono mixdown, trim, normalize, resampling and the rates offered, the slot header, the loader windows and their
//   avoidance, backup files (.fsmp) checked
// - the mock device's SMP_INFO .. SMP_RENAME: every rule of §2.7 (limits, hostile requests, order, the header last, a
//   power cut, STOP, RESTORING, update traffic, the idle drop, the loader window, READ / CLEAR / RENAME)
import { execFileSync } from "node:child_process";
import * as P from "./proto.mjs";
import * as S from "./sample.mjs";
import { createMockDevice, loadDeviceData } from "./mock_device.mjs";

const { CMD, RC, DETAIL } = P;

/* the firmware's decoder (eng_sample.c sample_next), written out again here: the test's own reference */
const STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
  107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
  963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
  6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767];
const ADJ = [-1, -1, -1, -1, 2, 4, 6, 8];
function refDecode(b, n) {
  const out = new Int16Array(n);
  let pred = 0, idx = 0;
  for (let pos = 0; pos < n; pos++) {
    const byte = b[pos >> 1], code = pos & 1 ? byte >> 4 : byte & 15, step = STEP[idx];
    let vd = step >> 3;
    if (code & 4) vd += step;
    if (code & 2) vd += step >> 1;
    if (code & 1) vd += step >> 2;
    pred = Math.max(-32768, Math.min(32767, pred + (code & 8 ? -vd : vd)));
    idx = Math.max(0, Math.min(88, idx + ADJ[code & 7]));
    out[pos] = pred;
  }
  return out;
}
const sine = (n, hz, rate, amp = 0.8) => Float32Array.from({ length: n }, (_, i) => amp * Math.sin(2 * Math.PI * hz * i / rate));
const rnd = (seed) => () => { seed = (seed * 1103515245 + 12345) >>> 0; return seed / 4294967296; };
const eqb = (a, b) => !!a && !!b && a.length === b.length && a.every((x, i) => x === b[i]);
const rms = (a) => Math.sqrt(a.reduce((s, x) => s + x * x, 0) / a.length);
const u16 = (b, o) => b[o] | b[o + 1] << 8;
const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;

/** data bytes whose loader window (the slot's sector +0xF00, data offset 0xD00 in sector 0) reads as a record */
function plantRecord(data, at = 0xd00, magic = true) {
  const r = new Uint8Array(80);
  for (let i = 2; i < 80; i++) r[i] = (i * 29 + 5) & 255;
  if (magic) { r[6] = 0x41; r[7] = 0x54; }
  const c = S.crc16(r.subarray(2, 80));
  r[0] = c & 255;
  r[1] = c >> 8;
  data.set(r, at);
  return data;
}

export async function runSampleTests(log = console.log) {
  let passed = 0, failed = 0;
  const ok = (cond, what) => { if (cond) passed++; else { failed++; log(`FAIL samples: ${what}`); } };

  /* ---- constants: the slots of §2.7 */
  ok(S.SLOT_MAX.join() === "81408,48640,48640" && S.SLOT_SIZE.join() === `${0x14000},${0xc000},${0xc000}` && S.DATA_OFF === 512
    && S.HDR_SIZE === 480 && S.SLOT_NAMES.join() === "USR1,USR2,USR3", "slots: USR1 80 KiB, USR2 / USR3 48 KiB, data at +512");
  ok(S.crc16(new TextEncoder().encode("123456789")) === 0x31c3 && S.crc16(new Uint8Array(78).fill(255)) === 0xb613,
    "CRC-16 (poly 0x1021, init 0): the check value; 78 erased bytes give 0xB613 (an erased window never matches)");
  ok(S.rateQ16(44100) === 65536 && S.rateQ16(22050) === 32768 && S.rateQ16(11025) === 16384 && Math.abs(S.hzOfQ16(S.rateQ16(32000)) - 32000) < 1,
    "rate: Hz / 44100 x 65536 and back");

  /* ---- ADPCM: the firmware's decoder, bit for bit */
  {
    const g = rnd(1), x = Int16Array.from({ length: 5001 }, (_, i) => Math.round(30000 * Math.sin(i / 7) * (i % 400 < 200 ? 1 : 0.1) + (g() - 0.5) * 3000));
    const b = S.adpcmEncode(x), d = S.adpcmDecode(b, x.length);
    ok(b.length === 2501 && b[2500] >> 4 === 0 && eqb(d, refDecode(b, x.length)), "ADPCM: 2 samples a byte (low nibble first, the odd one's high nibble 0); decode = the firmware's");
    const err = rms(Array.from(d, (v, i) => v - x[i])), sig = rms(Array.from(x));
    ok(20 * Math.log10(sig / err) > 20, `ADPCM: the round trip follows the source (SNR ${(20 * Math.log10(sig / err)).toFixed(1)} dB)`);
    const all = Uint8Array.from({ length: 4096 }, (_, i) => (i * 73 + (i >> 5)) & 255); // every code, clamping included
    ok(eqb(S.adpcmDecode(all, 8192), refDecode(all, 8192)), "ADPCM decode: arbitrary bytes (every code, the clamps) as the firmware");
    const full = Int16Array.from({ length: 2000 }, (_, i) => (i % 50 < 25 ? 32767 : -32768));
    ok(eqb(S.adpcmDecode(S.adpcmEncode(full), 2000), refDecode(S.adpcmEncode(full), 2000)), "ADPCM: a full-scale square, clamped as the firmware");
    let py = "skipped";
    try {
      const out = execFileSync("python3", ["-c", `import sys, json; sys.path.insert(0, "tools"); import sampleio as s
x = json.loads(sys.stdin.read()); b, st = s.ima_encode(x, 0); sys.stdout.write(b.hex())`], { input: JSON.stringify(Array.from(x)), cwd: new URL("../..", import.meta.url).pathname });
      py = out.toString() === Buffer.from(b).toString("hex") ? "same" : "different";
    } catch (e) { py = `skipped (${e.message.split("\n")[0]})`; }
    ok(py === "same" || py.startsWith("skipped"), `ADPCM: the same bytes as tools/sampleio.py ima_encode (${py})`);
    if (py.startsWith("skipped")) log(`samples: sampleio comparison ${py}`);
  }

  /* ---- source processing */
  {
    const l = sine(1000, 440, 44100, 0.5), r = sine(1000, 440, 44100, -0.5);
    const m = S.mixdown([l, r]);
    ok(m.length === 1000 && m.every((v) => Math.abs(v) < 1e-6) && eqb(Array.from(S.mixdown([l])), Array.from(l)), "mixdown: the channels' mean (one channel: itself)");
    const t = S.trim(Float32Array.from({ length: 10 }, (_, i) => i), 2, 7);
    ok(eqb(Array.from(t), [2, 3, 4, 5, 6]) && S.trim(new Float32Array(10), 5, 5).length === 0, "trim: [start, end)");
    const nrm = S.normalize(sine(500, 100, 8000, 0.25));
    ok(Math.abs(Math.max(...nrm.map(Math.abs)) - 1) < 1e-6 && S.normalize(new Float32Array(10)).every((v) => v === 0), "normalize: the peak to full scale (silence stays)");
    const p16 = S.toPcm16(Float32Array.from([0, 1, -1, 2, -2, 0.5]));
    ok(eqb(Array.from(p16), [0, 32767, -32767, 32767, -32768, 16384]), "to 16 bit: rounded, clamped");
    const up = S.resample(sine(48000, 1000, 48000), 48000, 44100);
    ok(Math.abs(up.length - 44100) <= 1 && Math.abs(rms(Array.from(up.subarray(1000, 43000))) - 0.8 / Math.SQRT2) < 0.01, "resample 48 k -> 44.1 k: the length, a 1 kHz tone kept");
    const hi = S.resample(sine(44100, 9000, 44100), 44100, 11025);
    ok(Math.abs(hi.length - 11025) <= 1 && rms(Array.from(hi.subarray(500, 10500))) < 0.05, "resample 44.1 k -> 11 k: a 9 kHz tone (above the new Nyquist) filtered out");
    const dc = S.resample(new Float32Array(8000).fill(0.5), 32000, 22050);
    ok(Math.abs(dc[dc.length >> 1] - 0.5) < 1e-3 && eqb(Array.from(S.resample(l, 44100, 44100)), Array.from(l)), "resample: DC kept; the same rate: unchanged");
  }

  /* ---- rates offered */
  {
    const one = S.rateChoices(48000, 48000, S.SLOT_MAX[1]); // 1 s at 48 k into USR2
    ok(one[0].hz === 44100 && one[0].fits && one[0].n === 44100 && one[0].bytes === 22050 && S.pickRate(one).hz === 44100,
      "rates: 48 kHz -> 44.1 kHz (the highest kept), 1 s fits USR2");
    const three = S.rateChoices(44100, 3 * 44100, S.SLOT_MAX[1]);
    const best = S.pickRate(three);
    ok(!three[0].fits && three.some((c) => c.hz === 32000 && c.fits) && best.fits && best.hz >= 32000 && best.bytes <= S.SLOT_MAX[1]
      && three.every((c) => c.bytes === Math.ceil(c.n / 2) && c.fits === (c.bytes <= S.SLOT_MAX[1])), "rates: 3 s at 44.1 k does not fit USR2; lower rates offered, the highest that fits picked");
    const low = S.rateChoices(22050, 22050, S.SLOT_MAX[0]);
    ok(low[0].hz === 22050 && low.every((c) => c.hz <= 22050) && low.some((c) => c.hz === 11025), "rates: a 22.05 kHz source stays at its rate (never up), halving offered");
    const huge = S.rateChoices(44100, 60 * 44100, S.SLOT_MAX[2]);
    ok(!S.pickRate(huge).fits || S.pickRate(huge).hz < 2000, "rates: a minute into USR3: nothing sensible fits (the page says so)");
    ok(three.every((c) => typeof c.label === "string" && c.label.length > 0 && Math.abs(c.seconds - c.n / c.hz) < 1e-9), "rates: each with its length and a quality word");
  }

  /* ---- the header (Appendix) */
  const enc = S.encodeSample(S.toPcm16(sine(20000, 220, 22050)), 22050, "kick 01");
  {
    const h = enc.header, z = 32;
    ok(h.length === 480 && u32(h, 0) === 0x504d5346 && u16(h, 4) === 1 && h[6] === 1 && h[7] === 0
      && new TextDecoder().decode(h.subarray(8, 16)) === "KICK 01\0" && u32(h, 16) === enc.data.length && u32(h, 20) === P.crc32(enc.data)
      && u32(h, 24) === 0 && u32(h, 28) === 0, "header: FSMP, version 1, 1 zone, the name (upper case, 0-padded), data length, CRC-32");
    ok(u32(h, z) === 0 && u32(h, z + 4) === enc.n && u32(h, z + 8) === 0 && u32(h, z + 12) === enc.n - 1 && u32(h, z + 16) === 32768
      && u16(h, z + 20) === 960 && u16(h, z + 22) === 0 && h[z + 24] === 0 && h[z + 25] === 0 && h[z + 26] === 127 && h[z + 27] === 0
      && h.subarray(60).every((b) => b === 0), "header: one zone over all keys (off 0, n, loop 0..n-1, rate, root 60 x 16, lo 0 hi 127, not looped), the rest 0");
    const ph = S.parseHeader(h);
    ok(ph.ok && ph.name === "KICK 01" && ph.dataLen === enc.data.length && ph.crc === P.crc32(enc.data) && ph.zones.length === 1
      && ph.zones[0].n === enc.n && Math.abs(ph.zones[0].hz - 22050) < 1 && !S.headerProblem(ph, S.SLOT_MAX[1]), "header: parsed back, passes the firmware's checks");
    const bad = (f) => { const b = Uint8Array.from(h); f(b); return S.headerProblem(S.parseHeader(b), S.SLOT_MAX[1]); };
    const w32 = (b, o, v) => { b[o] = v & 255; b[o + 1] = v >> 8 & 255; b[o + 2] = v >> 16 & 255; b[o + 3] = v >>> 24; };
    ok([(b) => { b[0] = 0; }, (b) => { b[4] = 2; }, (b) => { b[6] = 0; }, (b) => { b[6] = 17; }, (b) => w32(b, 16, S.SLOT_MAX[1] + 1),
      (b) => w32(b, z, enc.data.length + 1), (b) => w32(b, z + 4, 2 * enc.data.length + 1), (b) => { b[z + 24] = 89; },
      (b) => w32(b, z + 16, 0), (b) => w32(b, z + 16, (4 << 16) + 1), (b) => w32(b, z + 8, enc.n), (b) => w32(b, z + 12, enc.n),
      (b) => { b[z + 25] = 100; b[z + 26] = 50; }].every((f) => typeof bad(f) === "string"),
    "header: the firmware's checks (magic, version, zones 1..16, the slot's room, each zone inside the data, index, rate, loop, keys)");
    ok(S.cleanName("  my kick.wav ") === "MY KICK" && S.cleanName("Ünïcode-ß") === "NCODE-" && S.cleanName("abcdefghijk") === "ABCDEFGH"
      && S.cleanName("") === "SAMPLE", "name: ASCII 32..126, upper case, at most 8 (empty: SAMPLE)");
  }

  /* ---- the loader windows (Appendix) */
  {
    const d = new Uint8Array(8000);
    ok(S.loaderWindows(d).length === 0 && S.loaderWindows(new Uint8Array(8000).fill(255)).length === 0, "loader windows: zeros, erased bytes: none");
    const hit = plantRecord(new Uint8Array(8000));
    ok(eqb(S.loaderWindows(hit), [0xd00]), "loader windows: a record at sector 0 +0xF00 (data offset 0xD00) found");
    const noMagic = plantRecord(new Uint8Array(8000), 0xd00 + 4096, false);
    ok(eqb(S.loaderWindows(noMagic), [0xd00 + 4096]), "loader windows: the CRC alone counts (a superset of the loader's test), sector 1 too");
    const off = plantRecord(new Uint8Array(8000), 0xd01);
    ok(S.loaderWindows(off).length === 0, "loader windows: a record off the window's place: harmless");
    const short = new Uint8Array(0xd00 + 40); // the data ends in the window: past it 0xFF, as flash holds it
    const r = new Uint8Array(80).fill(255);
    for (let i = 2; i < 40; i++) r[i] = i;
    const c = S.crc16(r.subarray(2, 80));
    short[0xd00] = c & 255; short[0xd01] = c >> 8;
    short.set(r.subarray(2, 40), 0xd02);
    ok(eqb(S.loaderWindows(short), [0xd00]), "loader windows: a window cut by the data's end reads 0xFF past it");
    let calls = 0;
    const av = S.encodeSample(S.toPcm16(sine(9000, 300, 22050)), 22050, "X", { windows: (dd) => (++calls <= 2 ? [0xd00] : S.loaderWindows(dd)) });
    ok(av.shift === 2 && av.n === 8998 && calls === 3 && S.loaderWindows(av.data).length === 0, "avoidance: a hit moves the sample by a source sample and encodes again");
    let thrown = null;
    try { S.encodeSample(S.toPcm16(sine(9000, 300, 22050)), 22050, "X", { windows: () => [0xd00] }); } catch (e) { thrown = e; }
    ok(thrown instanceof S.SampleError, "avoidance: gives up after its tries (never sends a window)");
  }

  /* ---- backup files */
  {
    const file = S.backupBytes(enc.header, enc.data);
    ok(file.length === 512 + enc.data.length && eqb(file.subarray(0, 480), enc.header) && file.subarray(480, 512).every((b) => b === 255)
      && eqb(file.subarray(512), enc.data), "backup file: the header block (480 B, then 0xFF) and the data");
    const back = S.parseBackup(file, 1);
    ok(eqb(back.header, enc.header) && eqb(back.data, enc.data) && back.info.name === "KICK 01", "backup file: read back");
    const err = (b, slot = 1) => { try { S.parseBackup(b, slot); return ""; } catch (e) { return e instanceof S.SampleError ? e.message : `!${e.message}`; } };
    const flip = Uint8Array.from(file); flip[600] ^= 1;
    const mag = Uint8Array.from(file); mag[0] = 0x41;
    const big = S.encodeSample(S.toPcm16(sine(100000, 220, 44100)), 44100, "BIG");
    const bigFile = S.backupBytes(big.header, big.data);
    const planted = plantRecord(Uint8Array.from(file.subarray(512)));
    const hdr2 = S.buildHeader({ name: "P", n: planted.length * 2, data: planted, hz: 22050 });
    ok(/CRC/i.test(err(flip)) && /not a sample/i.test(err(mag)) && /size/i.test(err(file.subarray(0, file.length - 1)))
      && /Too big for USR2/.test(err(bigFile, 1)) && err(bigFile, 0) === "" && /cannot be written safely/.test(err(S.backupBytes(hdr2, planted)))
      && /not a sample/i.test(err(new Uint8Array(100))), "backup file: refused when damaged, not a sample, the wrong size, too big for the slot, or holding a loader window");
    ok(/^hortator-sample-USR2-KICK-01-\d{4}-\d\d-\d\d\.fsmp$/.test(S.backupName(1, "KICK 01")), "backup file name");
  }

  /* ---- the mock device ------------------------------------------------------------------------------------- */
  const data = await loadDeviceData();
  const m = createMockDevice(data);
  const q = (cmd, args, seq) => m.request(cmd, args, seq);
  const refusal = (r, rc, detail) => r && r.rc === rc && r.detail === detail;
  const info = (k) => q(CMD.SMP_INFO, { slot: k });
  const upload = (k, e, opts = {}) => { // BEGIN, the pieces, END: the first refusal or END's reply
    let r = q(CMD.SMP_BEGIN, { slot: k, len: e.data.length, crc: P.crc32(e.data) });
    if (r.rc) return r;
    for (let off = 0; off < e.data.length; off += 256) {
      if (opts.stopAt === off) return r;
      r = q(CMD.SMP_DATA, { slot: k, offset: off, data: e.data.subarray(off, off + 256) });
      if (r.rc) return r;
    }
    return q(CMD.SMP_END, { slot: k, header: e.header });
  };
  const readAll = (k) => {
    const i = info(k), total = 512 + i.len, b = new Uint8Array(total);
    for (let off = 0; off < total; off += 256) {
      const r = q(CMD.SMP_READ, { slot: k, offset: off, crc: i.crc });
      if (r.rc || r.offset !== off || r.len !== Math.min(256, total - off)) return null;
      b.set(r.data, off);
    }
    return b;
  };
  {
    const i = q(CMD.INFO);
    ok(i.minor === 3 && i.caps === 0xff && P.has(i, CMD.SMP_BEGIN), "mock INFO: minor 3, caps 0xFF (SAMPLES)");
    const e = [0, 1, 2].map(info);
    ok(e.every((x, k) => x.rc === 0 && x.slot === k && x.valid === 0 && x.name === "" && x.len === 0 && x.crc === 0 && x.max === S.SLOT_MAX[k]
      && x.nz === 0 && x.n === 0 && x.rate === 0), "SMP_INFO: three empty slots, each with its room");
    ok(refusal(info(3), RC.BAD, DETAIL.RANGE) && refusal(q(CMD.SMP_INFO, { slot: 127 }), RC.BAD, DETAIL.RANGE), "SMP_INFO: slot > 2 refused");
    ok(P.timeoutOf(CMD.SMP_DATA) === P.TIMING.longTimeout && P.timeoutOf(CMD.SMP_BEGIN) === P.TIMING.longTimeout && P.timeoutOf(CMD.SMP_END) === P.TIMING.longTimeout
      && P.timeoutOf(CMD.SMP_CLEAR) === P.TIMING.longTimeout && P.timeoutOf(CMD.SMP_RENAME) === P.TIMING.longTimeout
      && P.timeoutOf(CMD.SMP_INFO) === P.TIMING.timeout && P.timeoutOf(CMD.SMP_READ) === P.TIMING.timeout, "timeouts: the sample writes 2 s, INFO / READ 300 ms");
    ok([CMD.SMP_BEGIN, CMD.SMP_DATA, CMD.SMP_END, CMD.SMP_CLEAR, CMD.SMP_RENAME].every((c) => P.WRITES.has(c))
      && !P.WRITES.has(CMD.SMP_INFO) && !P.WRITES.has(CMD.SMP_READ), "the duplicate rule: the sample writes");
  }
  {
    /* an upload, byte for byte; the header last */
    const r0 = q(CMD.SMP_BEGIN, { slot: 1, len: enc.data.length, crc: P.crc32(enc.data) });
    ok(r0.rc === 0 && r0.slot === 1 && r0.max === S.SLOT_MAX[1] && info(1).valid === 0 && m.smp.flash[1].subarray(0, 4096).every((b) => b === 255),
      "BEGIN: the header sector erased, the slot reads empty");
    let off = 0;
    for (; off < enc.data.length; off += 256) {
      const r = q(CMD.SMP_DATA, { slot: 1, offset: off, data: enc.data.subarray(off, off + 256) });
      if (r.rc || r.offset !== off || r.len !== Math.min(256, enc.data.length - off)) break;
    }
    ok(off >= enc.data.length && info(1).valid === 0 && m.smp.flash[1].subarray(0, 480).every((b) => b === 255)
      && eqb(m.smp.flash[1].subarray(512, 512 + enc.data.length), enc.data), "DATA: the pieces at +512 in order; the header still erased");
    const end = q(CMD.SMP_END, { slot: 1, header: enc.header });
    const i = info(1);
    ok(end.rc === 0 && end.slot === 1 && end.valid === 1 && i.valid === 1 && i.name === "KICK 01" && i.len === enc.data.length
      && i.crc === P.crc32(enc.data) && i.nz === 1 && i.n === enc.n && i.rate === 32768 && i.max === S.SLOT_MAX[1], "END: the header written, the slot valid (SMP_INFO)");
    const back = readAll(1);
    ok(back && eqb(back, S.backupBytes(enc.header, enc.data)), "READ: the raw slot (header block, then the data) = what was sent");
    ok(m.smp.writes.every((w) => w.slot === 1 && w.off >= 0 && w.off + w.n <= S.SLOT_SIZE[1]) && m.smp.writes.at(-1).off === 0 && m.smp.writes.at(-1).n === 4
      && m.smp.writes.at(-2).off === 4, "the header written last, its magic last of all; every write inside USR2");
    const rp = q(CMD.SMP_READ, { slot: 1, offset: 0, crc: 1 });
    ok(refusal(rp, RC.STALE, DETAIL.CHANGED) && refusal(q(CMD.SMP_READ, { slot: 0, offset: 0, crc: 0 }), RC.REFUSED, DETAIL.NOT_PRESENT)
      && refusal(q(CMD.SMP_READ, { slot: 1, offset: 100, crc: i.crc }), RC.BAD, DETAIL.RANGE)
      && refusal(q(CMD.SMP_READ, { slot: 1, offset: 512 + i.len + 255 & ~255, crc: i.crc }), RC.BAD, DETAIL.RANGE), "READ: another CRC (changed), an empty slot, a bad offset refused");
  }
  {
    /* refusals and hostile requests: nothing changes */
    const before = Uint8Array.from(m.smp.flash[1]), nw = m.smp.writes.length;
    ok(refusal(q(CMD.SMP_BEGIN, { slot: 1, len: S.SLOT_MAX[1] + 1, crc: 0 }), RC.REFUSED, DETAIL.TOO_BIG)
      && refusal(q(CMD.SMP_BEGIN, { slot: 1, len: 0xffffffff, crc: 0 }), RC.REFUSED, DETAIL.TOO_BIG)
      && refusal(q(CMD.SMP_BEGIN, { slot: 1, len: 0, crc: 0 }), RC.BAD, DETAIL.RANGE)
      && refusal(q(CMD.SMP_BEGIN, { slot: 3, len: 10, crc: 0 }), RC.BAD, DETAIL.RANGE) && info(1).valid === 1
      && eqb(m.smp.flash[1], before) && m.smp.writes.length === nw, "BEGIN: too big (also 2^32 - 1), 0 bytes, slot 3: refused, the sample kept");
    ok(refusal(q(CMD.SMP_DATA, { slot: 1, offset: 0, data: new Uint8Array(256) }), RC.BAD, DETAIL.NOT_RESTORING)
      && refusal(q(CMD.SMP_END, { slot: 1, header: enc.header }), RC.BAD, DETAIL.NOT_RESTORING), "DATA / END with no upload running: rc 1 detail 41");
    const short = m.feed(Uint8Array.from([0xf0, 0x7d, 0x48, 0x54, CMD.SMP_BEGIN, 5, 1, 0, 0, 0xf7])).map(P.decodeFrame)[0];
    ok(refusal(short, RC.BAD, DETAIL.LENGTH), "a request too short: BAD LENGTH");
    m.play();
    ok(refusal(q(CMD.SMP_BEGIN, { slot: 0, len: 100, crc: 0 }), RC.STOP, DETAIL.STOP) && q(CMD.SMP_BEGIN, { slot: 0, len: 100, crc: 0 }).msg === "STOP TO UPLOAD"
      && refusal(q(CMD.SMP_CLEAR, { slot: 1 }), RC.STOP, DETAIL.STOP) && refusal(q(CMD.SMP_RENAME, { slot: 1, name: "X" }), RC.STOP, DETAIL.STOP)
      && info(1).valid === 1, "playing: BEGIN (STOP TO UPLOAD), CLEAR, RENAME refused");
    m.stop();
    m.faults.noFlash = true;
    ok(refusal(q(CMD.SMP_BEGIN, { slot: 0, len: 100, crc: 0 }), RC.REFUSED, DETAIL.NO_FLASH) && info(1).valid === 1, "no flash this boot: refused");
    m.faults.noFlash = false;
    m.faults.blocked = [1];
    const bl = q(CMD.SMP_BEGIN, { slot: 1, len: 100, crc: 0 });
    ok(refusal(bl, RC.REFUSED, DETAIL.BLOCKED) && /USR2/.test(bl.msg) && q(CMD.SMP_BEGIN, { slot: 0, len: 100, crc: 0 }).rc === 0, "USR2's clamp failed this boot: USR2 refused, USR1 not");
    m.faults.blocked = [];
    m.restore = { kind: 1, slot: 0, parts: [], at: m.t };
    ok(refusal(q(CMD.SMP_BEGIN, { slot: 0, len: 100, crc: 0 }), RC.BUSY, DETAIL.RESTORING), "a restore running: BEGIN refused (rc 6 detail 40)");
    m.restore = null;
    m.faults.updateAtCommit = true;
    ok(refusal(q(CMD.SMP_BEGIN, { slot: 0, len: 100, crc: 0 }), RC.BUSY, DETAIL.NONE) && !m.smp.up, "the upgrade command pending at BEGIN: rc 6 detail 0, nothing running");
    m.faults.updateAtCommit = false;
  }
  {
    /* order, retries, lengths */
    const e = S.encodeSample(S.toPcm16(sine(30000, 110, 44100)), 44100, "BASS");
    q(CMD.SMP_BEGIN, { slot: 0, len: e.data.length, crc: P.crc32(e.data) });
    const d = (off, b = e.data.subarray(off, off + 256)) => q(CMD.SMP_DATA, { slot: 0, offset: off, data: b });
    ok(refusal(d(256), RC.STALE, DETAIL.ORDER) && d(0).rc === 0 && d(0).rc === 0 && refusal(d(0, new Uint8Array(256)), RC.STALE, DETAIL.ORDER)
      && refusal(d(512), RC.STALE, DETAIL.ORDER) && d(256).rc === 0, "DATA: in order; the last piece again (a retry) answered when flash holds it; anything else OUT OF ORDER");
    ok(refusal(d(512, e.data.subarray(512, 700)), RC.BAD, DETAIL.LENGTH) && refusal(q(CMD.SMP_DATA, { slot: 1, offset: 512, data: e.data.subarray(512, 768) }), RC.BAD, DETAIL.NOT_RESTORING)
      && refusal(d(1 << 30), RC.STALE, DETAIL.ORDER), "DATA: a short piece (BAD LENGTH), another slot (NO UPLOAD), a far offset (OUT OF ORDER)");
    const seq = 50, f = P.encodeRequest(CMD.SMP_DATA, seq, { slot: 0, offset: 512, data: e.data.subarray(512, 768) });
    const a = m.feed(f)[0], nw = m.smp.writes.length, b = m.feed(f)[0];
    ok(eqb(a, b) && m.smp.writes.length === nw && P.decodeFrame(a).rc === 0, "the duplicate rule: a DATA retry with its seq gets the first reply, not written twice");
    m.play();
    ok(refusal(d(768), RC.STOP, DETAIL.STOP) && m.smp.up, "DATA while playing: STOP TO UPLOAD, the upload stays");
    m.stop();
    ok(refusal(q(CMD.SMP_END, { slot: 0, header: e.header }), RC.REFUSED, DETAIL.INCOMPLETE) && m.smp.up, "END before the last piece: INCOMPLETE, the upload stays");
    for (let off = 768; off < e.data.length; off += 256) d(off);
    const h2 = Uint8Array.from(e.header); h2[26 + 32] = 0; h2[25 + 32] = 1; // lo 1 > hi 0
    ok(refusal(q(CMD.SMP_END, { slot: 0, header: h2 }), RC.REFUSED, DETAIL.INVALID) && !m.smp.up && info(0).valid === 0, "END: a header the firmware would not play: INVALID, dropped, the slot empty");
    ok(m.smp.writes.every((w) => w.off + w.n <= S.SLOT_SIZE[w.slot] && w.off >= 0), "every write inside its slot");
  }
  {
    /* CRC, update, idle, power cut */
    const e = S.encodeSample(S.toPcm16(sine(4000, 500, 22050)), 22050, "SNARE");
    const go = (k, crc = P.crc32(e.data)) => { q(CMD.SMP_BEGIN, { slot: k, len: e.data.length, crc }); for (let off = 0; off < e.data.length; off += 256) q(CMD.SMP_DATA, { slot: k, offset: off, data: e.data.subarray(off, off + 256) }); };
    const hx = Uint8Array.from(e.header); hx[20] ^= 1; // the header's CRC as BEGIN's, the data's another
    go(2, u32(hx, 20));
    ok(refusal(q(CMD.SMP_END, { slot: 2, header: hx }), RC.REFUSED, DETAIL.CRC) && !m.smp.up && info(2).valid === 0, "END: the data's CRC is not BEGIN's: CRC MISMATCH, dropped, empty");
    go(2);
    m.faults.updateAtCommit = true;
    ok(refusal(q(CMD.SMP_END, { slot: 2, header: e.header }), RC.BUSY, DETAIL.NONE) && !m.smp.up && info(2).valid === 0, "END: the upgrade command pending: UPDATE, dropped, empty");
    go(2);
    m.updateFrame();
    m.advance(P.TIMING.silence + 1);
    ok(refusal(q(CMD.SMP_END, { slot: 2, header: e.header }), RC.BAD, DETAIL.NOT_RESTORING) && info(2).valid === 0, "update traffic mid-upload: dropped, the slot empty");
    go(2);
    m.advance(P.TIMING.restoreIdle + 1);
    ok(!m.smp.up && info(2).valid === 0, "3 s without a request: the upload dropped");
    go(2);
    ok(q(CMD.SMP_END, { slot: 2, header: e.header }).valid === 1 && info(2).valid === 1, "an upload into USR3");
    m.powerCut();
    ok(info(2).valid === 1 && info(2).name === "SNARE", "a power cut after END: the sample stays");
    go(2);
    m.powerCut();
    ok(info(2).valid === 0 && refusal(q(CMD.SMP_END, { slot: 2, header: e.header }), RC.BAD, DETAIL.NOT_RESTORING), "a power cut mid-upload: the slot empty, never garbage");
    go(2);
    m.powerCut({ inHeader: e.header }); // END's writes torn after the header's bytes 4..479, before its magic
    ok(info(2).valid === 0, "a power cut while END writes the header (the magic last): empty");
  }
  {
    /* the loader window: refused, dropped */
    const planted = plantRecord(Uint8Array.from({ length: 6000 }, (_, i) => (i * 13) & 255));
    q(CMD.SMP_BEGIN, { slot: 0, len: planted.length, crc: P.crc32(planted) });
    let r;
    for (let off = 0; off < planted.length; off += 256) { r = q(CMD.SMP_DATA, { slot: 0, offset: off, data: planted.subarray(off, off + 256) }); if (r.rc) break; }
    ok(refusal(r, RC.REFUSED, DETAIL.REFUSED_DATA) && r.offset === undefined && !m.smp.up && info(0).valid === 0
      && m.smp.flash[0].subarray(0xf00, 0xf50).every((b) => b === 255), "DATA: a piece that plants an update record at +0xF00: DATA REFUSED, never written, dropped");
  }
  {
    /* CLEAR, RENAME */
    const before = readAll(1);
    const rn = q(CMD.SMP_RENAME, { slot: 1, name: "HAT 2" });
    const after = readAll(1), i = info(1);
    ok(rn.rc === 0 && rn.name === "HAT 2" && i.name === "HAT 2" && i.valid === 1 && eqb(after.subarray(16), before.subarray(16))
      && new TextDecoder().decode(after.subarray(8, 16)) === "HAT 2\0\0\0", "RENAME: the name changed, the rest of the slot byte for byte");
    ok(refusal(q(CMD.SMP_RENAME, { slot: 1, name: "ABCDEFGHI" }), RC.BAD, DETAIL.RANGE) && refusal(q(CMD.SMP_RENAME, { slot: 1, name: "" }), RC.BAD, DETAIL.RANGE)
      && refusal(q(CMD.SMP_RENAME, { slot: 0, name: "X" }), RC.REFUSED, DETAIL.NOT_PRESENT) && refusal(q(CMD.SMP_RENAME, { slot: 5, name: "X" }), RC.BAD, DETAIL.RANGE),
    "RENAME: 9 characters, none, an empty slot, slot 5 refused");
    q(CMD.SMP_BEGIN, { slot: 0, len: 1000, crc: 0 });
    ok(q(CMD.SMP_CLEAR, { slot: 1 }).rc === 0 && info(1).valid === 0 && m.smp.up && m.smp.up.slot === 0, "CLEAR: the slot empty (another slot's upload goes on)");
    ok(q(CMD.SMP_CLEAR, { slot: 0 }).rc === 0 && !m.smp.up, "CLEAR drops an upload running into that slot");
    ok(refusal(q(CMD.SMP_CLEAR, { slot: 3 }), RC.BAD, DETAIL.RANGE), "CLEAR: slot 3 refused");
    const o = createMockDevice(data, { caps: 0x7f, minor: 2 });
    ok(refusal(o.request(CMD.SMP_INFO, { slot: 0 }), RC.BAD, DETAIL.NO_CAP), "a firmware without SAMPLES: rc 1 detail 46");
    const seeded = createMockDevice(data);
    seeded.seedSample(0, enc.header, enc.data);
    ok(seeded.request(CMD.SMP_INFO, { slot: 0 }).valid === 1 && seeded.request(CMD.SMP_INFO, { slot: 0 }).name === "KICK 01", "seedSample: a slot holding a sample (page development)");
  }
  return { passed, failed };
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const r = await runSampleTests();
  console.log(r.failed ? `SAMPLE TESTS FAILED (${r.failed} of ${r.passed + r.failed})` : `sample tests passed (${r.passed})`);
  process.exit(r.failed ? 1 : 0);
}
