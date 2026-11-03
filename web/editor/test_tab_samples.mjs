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
// - sets: a sample added as a slot (the first one: the set a single upload makes), the slots listed, played, renamed,
//   reordered, deleted (pending until Write), one trimmed / normalized in place, the room and the 16-slot limit, a
//   failed write keeps the set (write again, save it as a file), a set the page did not make (laid out again on a
//   confirm); chop: by beats, equal slices, transients, the cuts edited, the slices that fit added
import { pathToFileURL } from "node:url";
import * as P from "./proto.mjs";
import * as S from "./sample.mjs";
import { createMockDevice, loadDeviceData } from "./mock_device.mjs";
import { connect } from "./session.mjs";
import { SamplesModel } from "./tab_samples.mjs";
import * as T from "./sampset.mjs";

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
  const sent2 = (dev, from) => dev.received.slice(from).map((f) => { try { return P.decodeRequest(f).cmd; } catch { return null; } });
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

  /* ---- sets: several samples in a slot */
  {
    const m2 = createMockDevice(data), s2 = (await connect(m2.access, { timing: FAST })).session, sm2 = new SamplesModel(s2);
    const hdr = (k) => m2.smp.flash[k].subarray(0, 480), dat = (k) => { const p = S.parseHeader(hdr(k)); return m2.smp.flash[k].subarray(512, 512 + p.dataLen); };
    await sm2.refresh();
    await sm2.openSet(1);
    ok(sm2.sets[1].slots.length === 0 && sm2.room(1).free === S.SLOT_MAX[1] && sm2.room(1).room === 16, "an empty set: all its room, 16 slots");
    sm2.setSource({ name: "kick.wav", rate: 22050, channels: [sine(8000, 60, 22050)] });
    sm2.setEdit({ slot: 1 });
    ok(sm2.edit.into === "add" && sm2.plan().max === S.SLOT_MAX[1], "a file loaded: added to the set by default, the set's room offered");
    await sm2.addToSet();
    const one = S.encodeSample(S.render(sm2.source, sm2.edit), 22050, "KICK");
    ok(eqb(hdr(1), one.header) && eqb(dat(1), one.data) && sm2.slots[1].valid && !sm2.sets[1].dirty,
      "the first sample added: the set a single upload always made (byte for byte)");
    const room1 = sm2.room(1).free;
    sm2.setSource({ name: "snare.wav", rate: 44100, channels: [sine(6000, 200, 44100)] });
    sm2.setEdit({ slot: 1 });
    ok(sm2.plan().max === room1 && room1 === S.SLOT_MAX[1] - 4000 - T.namesBytes(2), "the second: the room left offered (its name's bytes too)");
    let from = m.received.length;
    await sm2.addToSet();
    let p = S.parseHeader(hdr(1));
    ok(p.nz === 2 && sm2.sets[1].slots.map((x) => x.name).join() === "KICK,SNARE" && eqb(dat(1).subarray(0, 4000), one.data)
      && sm2.slots[1].nz === 2 && !sm2.sets[1].dirty && /SNARE/.test(sm2.message), "added: two slots, the first one's bytes as they were");
    const snare = sm2.sets[1].slots[1], pv = sm2.slotSound(1, 1);
    ok(eqb(pv.pcm, S.adpcmDecode(snare.data, snare.n)) && Math.abs(pv.hz - 44100) < 1, "a slot played: decoded as the FM-1 decodes it, at its rate");
    /* manage: pending until Write */
    sm2.renameSlot(1, 0, "big kick");
    sm2.moveSlot(1, 1, 0);
    ok(sm2.sets[1].dirty && sm2.sets[1].slots.map((x) => x.name).join() === "SNARE,BIG KICK" && T.parseSet(hdr(1), dat(1)).slots[0].name === "KICK",
      "rename, move: pending (the FM-1 unchanged)");
    await sm2.writeSet(1);
    let back = T.parseSet(hdr(1), dat(1));
    ok(back.slots.map((x) => x.name).join() === "SNARE,BIG KICK" && eqb(back.slots[1].data, one.data) && eqb(back.slots[0].data, snare.data) && !sm2.sets[1].dirty,
      "Write: the set in its new order, each slot's bytes copied");
    {
      const sm3 = new SamplesModel(s2);
      await sm3.refresh();
      await sm3.openSet(1);
      ok(sm3.sets[1].slots.map((x) => x.name).join() === "SNARE,BIG KICK" && sm3.sets[1].plain, "read from the FM-1 by another page: the slots and their names");
      sm3.destroy();
    }
    /* one slot trimmed and normalized in place */
    sm2.editSlot(1, 0);
    ok(sm2.edit.into === "replace" && sm2.edit.index === 0 && sm2.edit.slot === 1 && sm2.edit.hz === 44100 && sm2.source.length === 6000 && sm2.edit.name === "SNARE",
      "a slot opened in the editor: its sound, its rate, replace it");
    sm2.setEdit({ start: 100, normalize: true });
    await sm2.replaceInSet();
    back = T.parseSet(hdr(1), dat(1));
    ok(back.slots[0].n === 5900 && Math.max(...S.adpcmDecode(back.slots[0].data, 5900).map(Math.abs)) > 30000 && eqb(back.slots[1].data, one.data)
      && back.slots[0].name === "SNARE", "replaced: trimmed and normalized, the other slot as it was");
    /* delete: one slot left, the single-sample layout */
    sm2.deleteSlot(1, 0);
    await sm2.writeSet(1);
    p = S.parseHeader(hdr(1));
    ok(p.nz === 1 && p.zones[0].lo === 0 && p.zones[0].hi === 127 && p.zones[0].root16 === 960 && p.dataLen === 4000 && eqb(dat(1), one.data),
      "deleted down to one: a single sample over all keys again (no names)");
    /* room, 16 slots */
    sm2.setSource({ name: "long", rate: 44100, channels: [sine(44100 * 3, 100, 44100)] });
    sm2.setEdit({ slot: 1, hz: 44100 });
    from = m2.received.length;
    let e = await fail(sm2.addToSet());
    ok(e && /too long/i.test(e.message) && !sent2(m2, from).includes(CMD.SMP_BEGIN), "too long for the room left: refused before anything is sent");
    const tiny = T.makeSlot(Int16Array.from({ length: 64 }, (_, i) => (i % 8) * 900), 44100, "t");
    for (let i = 1; i < 16; i++) sm2.sets[1].slots.push({ ...tiny, name: `T${i}` });
    sm2.setSource({ name: "tick", rate: 44100, channels: [sine(100, 1000, 44100)] });
    sm2.setEdit({ slot: 1 });
    e = await fail(sm2.addToSet());
    ok(e && /16/.test(e.message) && sm2.room(1).room === 0, "16 slots: no more added");
    /* a failed write keeps the set */
    m2.play();
    await until(() => s2.state.playing);
    e = await fail(sm2.writeSet(1));
    ok(e && /playing/i.test(e.message) && sm2.sets[1].dirty && sm2.sets[1].slots.length === 16, "playing: the write refused, the set kept as edited");
    m2.stop();
    await until(() => !s2.state.playing);
    const pend = sm2.setFile(1);
    const pf = S.parseBackup(pend.bytes, 1);
    ok(pf.info.nz === 16 && /^hortator-sample-USR2-/.test(pend.name), "the set as edited saved as a file (it restores like a backup)");
    let pr = sm2.writeSet(1);
    await until(() => m2.smp.up && m2.smp.up.next >= 256);
    m2.play();
    e = await fail(pr);
    ok(e && /empty/.test(e.message) && sm2.sets[1].dirty && sm2.sets[1].failed, "the FM-1 starts playing mid-write: USR2 empty, the set kept to write again");
    m2.stop();
    await until(() => !s2.state.playing);
    await sm2.writeSet(1);
    ok(S.parseHeader(hdr(1)).nz === 16 && !sm2.sets[1].dirty && !sm2.sets[1].failed, "written again: 16 slots");
    /* a set the page did not make */
    {
      const h = new Uint8Array(480), d = Uint8Array.from({ length: 3000 }, (_, i) => (i * 7) & 255), v = new DataView(h.buffer);
      const z = (i, off, n, lo, hi, root) => { const o = 32 + 28 * i; v.setUint32(o, off, true); v.setUint32(o + 4, n, true);
        v.setUint32(o + 12, n - 1, true); v.setUint32(o + 16, 65536, true); v.setInt16(o + 20, root * 16, true); h[o + 25] = lo; h[o + 26] = hi; };
      v.setUint32(0, S.MAGIC, true); h[4] = 1; h[6] = 3; h.set([80, 69, 82, 67], 8); v.setUint32(16, 3000, true); v.setUint32(20, P.crc32(d), true);
      z(0, 0, 2000, 0, 59, 48); z(1, 1000, 4000, 60, 127, 72); z(2, 0, 2000, 30, 40, 36);
      m2.seedSample(2, h, d);
      await sm2.refresh();
      await sm2.openSet(2);
      ok(!sm2.sets[2].plain && sm2.sets[2].slots.length === 2, "a set the page did not make: its 2 distinct samples, not plain");
      e = null;
      try { sm2.renameSlot(2, 0, "x"); } catch (x) { e = x; }
      ok(e && e.relayout && T.parseSet(hdr(2), dat(2)).nz === 3, "changing it: refused until laid out again (the page asks)");
      sm2.relayout(2);
      sm2.renameSlot(2, 0, "low");
      await sm2.writeSet(2);
      const q = T.parseSet(hdr(2), dat(2));
      ok(q.plain && q.nz === 2 && q.slots[0].name === "LOW" && eqb(q.slots[1].data, d.subarray(1000, 3000)) && eqb(q.slots[0].data, d.subarray(0, 1000)), "laid out again: plain slots, each sample's bytes kept");
    }
    /* chop */
    {
      const rate = 22050, beat = 0.5, x = new Float32Array(rate * 4);
      for (let b = 0; b < 8; b++) for (let i = 0; i < 2000; i++) x[b * beat * rate + i] = Math.exp(-i / 300) * Math.sin(i / 3);
      await sm2.clear(0);
      sm2.setSource({ name: "break.wav", rate, channels: [x] });
      sm2.setEdit({ slot: 0 });
      sm2.chopStart();
      ok(sm2.chop && sm2.chop.mode === "auto" && sm2.chop.cuts.length === 7, `chop: transients found (${sm2.chop.cuts.length + 1} slices)`);
      sm2.chopSet({ mode: "beats", bpm: 120, div: 8 });
      ok(sm2.chop.cuts.length === 15 && sm2.chop.cuts[0] === 5513, "chop by beats: 1/8 at 120 BPM: 16 slices");
      sm2.chopSet({ mode: "equal", count: 4 });
      ok(sm2.chop.cuts.join() === "22050,44100,66150", "chop: 4 equal slices");
      sm2.chopSet({ mode: "beats", bpm: 120, div: 4 });
      ok(Math.abs(sm2.chopTempo().bpm - 120) < 1 && sm2.chop.cuts.length === 7, "chop: the tempo detected; 1/4: 8 slices");
      sm2.chopCut("add", 33000);
      sm2.chopCut("move", 0, 11100);
      sm2.chopCut("remove", 1);
      ok(sm2.chop.cuts.join() === "11100,33000,33075,44100,55125,66150,77175" && sm2.chop.edited, `chop: cuts added, moved, removed (${sm2.chop.cuts})`);
      const pl = sm2.chopPlan();
      ok(pl.slices.length === 8 && pl.fit === 8 && pl.lost === 0 && pl.slices[2].n === 75, "chop plan: every slice fits USR1");
      const sp = sm2.chopPreview(0);
      ok(sp.pcm.length === pl.slices[0].n && sp.hz === sm2.edit.hz, "a slice previewed (decoded as the FM-1 plays it)");
      await sm2.chopAdd();
      const q = T.parseSet(hdr(0), dat(0));
      ok(q.slots.length === 8 && q.slots.map((x) => x.name).join() === "BREAK 1,BREAK 2,BREAK 3,BREAK 4,BREAK 5,BREAK 6,BREAK 7,BREAK 8"
        && /8 slices/.test(sm2.message) && !sm2.chop && q.slots.every((x, i) => x.n === pl.slices[i].n), "chop: the slices added as slots, named after the file");
      // too many for the room: USR3 (2 slots of 3000 B) gets what fits
      sm2.setSource({ name: "big", rate: 44100, channels: [sine(44100 * 4, 300, 44100)] });
      sm2.setEdit({ slot: 2 });
      sm2.chopStart();
      sm2.chopSet({ mode: "equal", count: 8 });
      const pl1 = sm2.chopPlan();
      ok(pl1.fit === 8 && sm2.edit.hz < 44100, `chop: the rate picked leaves room for every slice and its name (${sm2.edit.hz} Hz)`);
      sm2.setEdit({ hz: 44100 });
      sm2.chopSet({ count: 8 });
      ok(sm2.edit.hz === 44100, "chop: a rate chosen by hand stays");
      const pl2 = sm2.chopPlan();
      ok(pl2.fit < 8 && pl2.fit > 0 && pl2.lost === 8 - pl2.fit, `chop plan: ${pl2.fit} of 8 fit USR3`);
      await sm2.chopAdd();
      ok(T.parseSet(hdr(2), dat(2)).slots.length === 2 + pl2.fit && new RegExp(`${pl2.lost} did not fit`).test(sm2.message), "chop: what fits added, the page says how many did not");
    }
    sm2.destroy();
    s2.close();
  }

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
