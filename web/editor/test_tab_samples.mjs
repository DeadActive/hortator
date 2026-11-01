// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
//
// The Samples tab's logic (tab_samples.mjs SamplesModel) against the mock device: node, no browser, no hardware.
//   node web/editor/test_tab_samples.mjs          (also run by node web/test_web.mjs)
// - the three slots from SMP_INFO; a file loaded: mono, the rate kept (44.1 kHz at most) or lowered to fit the slot
// - an upload byte for byte (the mock's flash = the encoded sample); the preview = what the FM-1 decodes
// - READ back, a backup file, a restore into another slot; files refused before anything is sent
// - refusals: playing (nothing sent), too long, the FM-1 starting to play mid-upload; Cancel (CLEAR, the slot empty)
// - rename, clear; a firmware without SAMPLES: the tab says to update
import { pathToFileURL } from "node:url";
import * as P from "./proto.mjs";
import * as S from "./sample.mjs";
import { createMockDevice, loadDeviceData } from "./mock_device.mjs";
import { connect } from "./session.mjs";
import { SamplesModel } from "./tab_samples.mjs";

const { CMD } = P;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function until(cond, ms = 3000) {
  for (const t0 = Date.now(); Date.now() - t0 < ms; await sleep(2)) if (cond()) return true;
  return cond();
}
const FAST = { timeout: 25, longTimeout: 60, tries: 3, silence: 60, backoff: 25, backoffMax: 100, giveUp: 100000, keepAlive: 100000 };
const eqb = (a, b) => !!a && !!b && a.length === b.length && a.every((x, i) => x === b[i]);
const sine = (n, hz, rate, amp = 0.5) => Float32Array.from({ length: n }, (_, i) => amp * Math.sin(2 * Math.PI * hz * i / rate));

export async function runSamplesTests(log = console.log) {
  let passed = 0, failed = 0;
  const ok = (cond, what) => { if (cond) passed++; else { failed++; log(`FAIL samples tab: ${what}`); } };
  const data = await loadDeviceData();
  const m = createMockDevice(data);
  const s = (await connect(m.access, { timing: FAST })).session, sm = new SamplesModel(s);
  const fail = (p) => p.then(() => null, (e) => e);
  const sent = (from) => m.received.slice(from).map((f) => { try { return P.decodeRequest(f).cmd; } catch { return null; } });

  await sm.refresh();
  ok(sm.available && sm.slots.length === 3 && sm.slots.every((x, k) => x && !x.valid && x.max === S.SLOT_MAX[k]), "the three slots, empty, each with its room");

  /* a 3 s stereo file at 48 kHz: mono, offered for USR2 at the highest rate that fits */
  sm.setSource({ name: "deep kick.wav", rate: 48000, channels: [sine(144000, 60, 48000), sine(144000, 60, 48000)] });
  ok(sm.edit.name === "DEEP KIC" && sm.edit.start === 0 && sm.edit.end === 144000 && sm.edit.slot === 0 && sm.edit.hz === 44100
    && sm.plan().fits, "a file loaded: its name, all of it, the first empty slot, 44.1 kHz fits USR1");
  sm.setEdit({ slot: 1 });
  const c = sm.choices();
  ok(!c[0].fits && sm.edit.hz === S.pickRate(c).hz && sm.edit.hz < 44100 && sm.plan().fits && sm.plan().bytes <= S.SLOT_MAX[1],
    `USR2: 44.1 kHz does not fit, the highest rate that fits is picked (${sm.edit.hz} Hz)`);
  sm.setEdit({ end: 48000, normalize: true });
  ok(sm.edit.hz === 44100 && sm.plan().fits && Math.abs(sm.plan().seconds - 1) < 0.001, "trimmed to 1 s: back to 44.1 kHz (the highest that fits)");
  const enc = sm.prepare(), pv = sm.preview();
  ok(enc.n === 44100 && pv.hz === 44100 && eqb(pv.pcm, S.adpcmDecode(enc.data, enc.n)) && Math.max(...pv.pcm.map(Math.abs)) > 30000,
    "the preview: decoded from the encoded bytes (what the FM-1 plays), normalized");

  /* an upload */
  let from = m.received.length;
  await sm.upload();
  ok(eqb(m.smp.flash[1].subarray(512, 512 + enc.data.length), enc.data) && eqb(m.smp.flash[1].subarray(0, 480), enc.header)
    && sm.slots[1].valid && sm.slots[1].name === "DEEP KIC" && Math.abs(sm.slots[1].seconds - 1) < 0.001 && !sm.error && /USR2/.test(sm.message),
  "upload: the mock's USR2 = the encoded sample, its card read again");
  const cmds = sent(from);
  ok(cmds[0] === CMD.SMP_BEGIN && cmds.filter((x) => x === CMD.SMP_DATA).length === Math.ceil(enc.data.length / 256) && cmds.includes(CMD.SMP_END)
    && !s.restoring, "upload: BEGIN, the pieces, END; the lock released");

  /* READ back, a backup, a restore */
  const back = await sm.readSlot(1);
  ok(eqb(back.data, enc.data) && eqb(back.header, enc.header), "READ: the slot back, byte for byte");
  const sp = await sm.slotPreview(1);
  ok(eqb(sp.pcm, S.adpcmDecode(enc.data, enc.n)) && Math.abs(sp.hz - 44100) < 1, "the slot's sample previewed (after READ)");
  const bk = await sm.backup(1);
  ok(eqb(bk.bytes, S.backupBytes(enc.header, enc.data)) && /^hortator-sample-USR2-DEEP-KIC-/.test(bk.name), "backup: the raw slot to a .fsmp file");
  await sm.restore(2, bk.bytes);
  ok(eqb(m.smp.flash[2].subarray(0, 512 + enc.data.length), bk.bytes.map((b, i) => (i >= 480 && i < 512 ? 255 : b))) && sm.slots[2].valid
    && sm.slots[2].name === "DEEP KIC", "restore: the file into USR3");
  sm.setSource({ name: "long", rate: 44100, channels: [sine(150000, 100, 44100)] });
  sm.setEdit({ slot: 0 });
  await sm.upload();
  const big = await sm.backup(0);
  from = m.received.length;
  let e = await fail(sm.restore(1, big.bytes));
  ok(e && /Too big for USR2/.test(e.message) && sent(from).length === 0 && sm.error, "restore: a file too big for USR2 refused, nothing sent");
  const bad = Uint8Array.from(bk.bytes); bad[700] ^= 4;
  e = await fail(sm.restore(1, bad));
  ok(e && /damaged/.test(e.message) && sent(from).length === 0, "restore: a damaged file refused, nothing sent");

  /* refusals */
  sm.setSource({ name: "snare", rate: 22050, channels: [sine(11025, 200, 22050)] });
  sm.setEdit({ slot: 1 });
  m.play();
  await until(() => s.state.playing);
  from = m.received.length;
  e = await fail(sm.upload());
  ok(e && /stop/i.test(e.message) && !sent(from).includes(CMD.SMP_BEGIN) && sm.slots[1].valid, "playing: the upload refused before anything is sent");
  m.stop();
  await until(() => !s.state.playing);
  sm.setSource({ name: "minute", rate: 44100, channels: [sine(44100 * 60, 100, 44100)] });
  sm.setEdit({ slot: 2, hz: 44100 });
  from = m.received.length;
  e = await fail(sm.upload());
  ok(e && /too long/i.test(e.message) && sent(from).length === 0 && !sm.plan().fits, "too long for the slot: refused before anything is sent");

  /* the FM-1 starts playing mid-upload; Cancel */
  sm.setSource({ name: "hat", rate: 44100, channels: [sine(40000, 3000, 44100)] });
  sm.setEdit({ slot: 1 });
  let p = sm.upload();
  await until(() => m.smp.up && m.smp.up.next >= 2048);
  m.play();
  e = await fail(p);
  ok(e && /playing/i.test(e.message) && /USR2 is empty/.test(e.message) && !(await (async () => { await sm.refresh(); return sm.slots[1].valid; })()),
    "the FM-1 starts playing mid-upload: stopped, the page says USR2 is empty");
  m.stop();
  await until(() => !s.state.playing);
  p = sm.upload();
  await until(() => m.smp.up && m.smp.up.next >= 2048);
  from = m.received.length;
  sm.cancel();
  e = await fail(p);
  ok(e && e.cancelled && sent(from).includes(CMD.SMP_CLEAR) && !m.smp.up && !sm.slots[1].valid && /Cancelled/.test(sm.message) && !s.restoring,
    "Cancel: CLEAR sent, the upload dropped, USR2 empty, the lock released");

  /* rename, clear */
  await sm.rename(2, "new snare");
  ok(sm.slots[2].name === "NEW SNAR" && m.request(CMD.SMP_INFO, { slot: 2 }).name === "NEW SNAR", "rename: upper case, 8 characters");
  await sm.clear(2);
  ok(!sm.slots[2].valid && /USR3/.test(sm.message), "clear: USR3 empty");
  sm.destroy();
  s.close();

  /* a firmware without SAMPLES */
  const o = createMockDevice(data, { caps: 0x7f, minor: 2 });
  const so = (await connect(o.access, { timing: FAST })).session, om = new SamplesModel(so);
  await om.refresh();
  ok(!om.available && /Update the FM-1/.test(om.message) && o.received.every((f) => P.decodeRequest(f).cmd < CMD.SMP_INFO), "a firmware without SAMPLES: nothing asked, the tab says to update");
  om.destroy();
  so.close();
  return { passed, failed };
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const r = await runSamplesTests();
  console.log(r.failed ? `SAMPLES TAB TESTS FAILED (${r.failed} of ${r.passed + r.failed})` : `samples tab tests passed (${r.passed})`);
  process.exit(r.failed ? 1 : 0);
}
