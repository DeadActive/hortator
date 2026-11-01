// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
//
// The Presets tab's logic (tab_presets.mjs PresetsModel) against the mock device: node, no browser, no hardware.
//   node web/editor/test_tab_presets.mjs          (also run by node web/test_web.mjs)
// - the factory banks (151: MACHINEDRUM 55, MODERN 40, CLASSIC 56), search, PRESET_LOAD onto the selected track
// - the user list read from flash (object 0), a presets file round trip byte for byte
// - rename / reorder / delete / "Save track N here" -> the right UPR2 record on the FM-1 (and on another FM-1)
// - each user preset's FX shown and edited, kept or dropped (the voice only)
// - files: unknown keys ignored, a newer version refused, a bad CRC refused; a version 1 file (128 voices) moved in
// - an FM-1 before UPR2: its presets read (moved in as it will move them), not written
// - the FM-1's refusals shown (playing: STOP FIRST), cancel: ABORT, the FM-1 unchanged

import { pathToFileURL } from "node:url";
import * as P from "./proto.mjs";
import { createMockDevice, loadDeviceData } from "./mock_device.mjs";
import { connect } from "./session.mjs";
import { PresetsModel, explain } from "./tab_presets.mjs";

const { CMD, RS, LAYOUT, SF } = P;
const FAST = { timeout: 40, longTimeout: 120, tries: 3, silence: 60, backoff: 25, backoffMax: 100, giveUp: 100000, keepAlive: 100000 };
const eqB = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);

export async function runPresetsTests(log = console.log) {
  let passed = 0, failed = 0;
  const ok = (cond, what) => { if (cond) passed++; else { failed++; log(`FAIL presets: ${what}`); } };
  const data = await loadDeviceData();
  const open = async (o) => {
    const m = createMockDevice(data, o), r = await connect(m.access, { timing: FAST });
    if (!r.ok) throw new Error(r.message);
    const pm = new PresetsModel(r.session, { storage: null });
    await pm.load();
    return { m, s: r.session, pm };
  };
  const flashUpr = (m) => Uint8Array.from(m.flash.upr.bytes);
  const sessions = [];
  try {
    /* ---------------------------------------------------------------- factory --- */
    const { m, s, pm } = await open();
    sessions.push(s);
    ok(pm.banks.map((b) => `${b.name} ${b.presets.length}`).join() === "MACHINEDRUM 55,MODERN 40,CLASSIC 56"
      && pm.factory().length === 151 && pm.factory().every((p, i) => p.id === i && p.name === data.presets[i].name),
    "factory: 151 presets in three banks, the device's names and ids");
    const k808 = pm.factory("k808");
    ok(k808.length > 0 && k808.every((p) => pm.modelNames[p.model] === "K808" || /K808/.test(p.name))
      && pm.factory("", 1).length === 40 && pm.factory("", 1).every((p) => p.bank === 1) && pm.factory("zzzq").length === 0,
    "factory: search by name or model, filter by bank");
    await s.select(2);
    const id = data.banks[2].start + 3, seen = [], rel = [];
    const off = s.on("changed", (p) => seen.push(p)), off2 = s.on("reload", (w) => rel.push(w));
    const lr = await pm.loadPreset(id);
    await new Promise((r) => setTimeout(r, 20));
    const t2 = m.tracks[2];
    ok(lr.track === 2 && t2.p[m.P.MODEL] === data.presets[id].model && t2.pre === id
      && seen.some((p) => p.id === 0 && p.track === 2 && p.value === data.presets[id].model) && rel.length === 1 && rel[0].track === 2,
    "factory: Load onto the selected track (PRESET_LOAD): its moved knobs pushed as CHANGED (as the firmware), a reload of track 3");
    seen.length = 0;
    rel.length = 0;
    await pm.loadPreset(data.banks[0].start + 1, 5);
    await new Promise((r) => setTimeout(r, 20));
    off();
    off2();
    ok(!seen.some((p) => p.track === 5) && rel.length === 1 && rel[0].track === 5, "factory: onto another track: no push (not selected), a reload of track 6");

    /* --------------------------------------------------------------- user list --- */
    const before = flashUpr(m);
    ok(pm.device && pm.slots.length === 96 && pm.slots.every((x, i) => (x === null) === (m.upr[i] === null))
      && pm.slots.every((x, i) => !x || (x.model === m.upr[i].model && x.knobs.join() === m.upr[i].knobs.join()
        && x.fx.join() === m.upr[i].fx.join() && x.voiceOnly === m.upr[i].voiceOnly
        && x.name === P.userPresetName(i, pm.modelNames[x.model])))
      && pm.slots.some((x) => x && x.voiceOnly) && pm.slots.some((x) => x && !x.voiceOnly)
      && eqB(pm.record(), before) && !pm.differs, "user: the 96 slots read from flash with their FX (or the voice only), named as the FM-1 names them");

    const text = JSON.stringify(pm.toFile({ created: "2026-10-31T12:00:00.000Z" }));
    const f = JSON.parse(text);
    ok(f.format === "hortator-presets" && f.version === 2 && f.slots.length === 96 && f.upr2.size === 3204 && f.moved.length === 32
      && f.upr2.crc32 === P.crcHex(P.crc32(before)) && f.slots.filter(Boolean).every((x) => x.fx.length === 23),
    "file: hortator-presets, version 2, 96 slots with their FX, the record's CRC-32");
    const pm2 = new PresetsModel(s, { storage: null });
    pm2.modelNames = pm.modelNames;
    pm2.fromFile(text, "a.json");
    ok(eqB(pm2.record(), before) && JSON.stringify(pm2.toFile({ created: "2026-10-31T12:00:00.000Z" })) === text,
      "file: round trip byte-exact (record and file text)");

    /* -------------------------------------------------- rename / reorder / delete --- */
    const used = pm.slots.map((x, i) => (x ? i : -1)).filter((i) => i >= 0);
    const [a, b, c] = used;
    pm.rename(a, "  MY KICK\u0007  ");
    ok(pm.slots[a].name === "MY KICK" && eqB(pm.record(), before) && !pm.differs, "rename: the name changes, the record does not");
    pm.rename(a, "X".repeat(40));
    ok(pm.slots[a].name.length === 24, "rename: names cut to 24 characters");
    const rec = (i) => Array.from(before.subarray(4 + 33 * i, 37 + 33 * i));
    const want = Array.from(before);
    // move a -> 95 (the rest shift up), delete b (now at b - 1)
    const order = [...Array(96).keys()];
    order.splice(95, 0, ...order.splice(a, 1));
    order.forEach((src, dst) => want.splice(4 + 33 * dst, 33, ...rec(src)));
    want.splice(4 + 33 * (b - 1), 33, ...Array(33).fill(0));
    pm.move(a, 95);
    pm.remove(b - 1);
    const r1 = pm.record();
    ok(eqB(r1, want) && pm.slots[95].name === "X".repeat(24) && pm.changed().filter(Boolean).length > 0,
      "reorder + delete: the UPR2 record moved / zeroed as the list shows (the FX with their slot)");
    const tsave = 5;
    await pm.saveTrack(c, tsave);
    const tk = m.tracks[tsave].p;
    ok(pm.slots[c].model === tk[m.P.MODEL] && pm.slots[c].knobs.join() === tk.slice(m.P.E0, m.P.E0 + 8).join()
      && pm.slots[c].fx.join() === LAYOUT.uprFx.map((id) => tk[id]).join() && !pm.slots[c].voiceOnly
      && pm.record()[4 + 33 * c] === 1, "Save track N here: the track's MODEL, SOUND knobs and FX (DUMP) in the slot");

    /* ------------------------------------------------------------------- FX --- */
    const cut = LAYOUT.uprFx.indexOf(m.P.FCUT), ftype = LAYOUT.uprFx.indexOf(m.P.FTYPE), rtune = LAYOUT.uprFx.indexOf(m.P.RTUNE);
    ok(pm.fxDesc.length === 23 && pm.fxDesc[rtune].min === 24 && pm.fxDesc[rtune].max === 96 && pm.fxDesc[ftype].names.length === 5
      && pm.fxText(ftype, 1) === pm.fxDesc[ftype].names[1] && pm.fxText(cut, 64) === "64",
    "FX: each one's range and names as the FM-1 describes them (DESC)");
    pm.setFx(c, cut, 300);
    const r2 = pm.record();
    pm.setFx(c, rtune, 0);
    ok(pm.slots[c].fx[cut] === 127 && r2[4 + 33 * c + 10 + cut] === 127 && pm.slots[c].fx[rtune] === 24, "FX: an edit kept inside its range, in the record");
    pm.keepFx(c, false);
    const r3 = pm.record();
    pm.setFx(c, cut, 5);
    ok(r3[4 + 33 * c] === 3 && pm.slots[c].voiceOnly && pm.slots[c].fx[cut] === 127, "FX: the voice only (used 3); its FX not edited then");
    pm.keepFx(c, true);
    ok(pm.record()[4 + 33 * c] === 1 && eqB(pm.record(), r2.map((x, i) => (i === 4 + 33 * c + 10 + rtune ? 24 : x))), "FX: kept again (used 1)");

    /* ------------------------------------------------------------------ files --- */
    const g = JSON.parse(text);
    g.future = { x: 1 };
    g.slots[a].colour = "red";
    pm2.fromFile(JSON.stringify(g));
    ok(eqB(pm2.record(), before), "file: unknown keys ignored");
    const thrown = (fn) => { try { fn(); return null; } catch (e) { return e; } };
    const e1 = thrown(() => pm2.fromFile(JSON.stringify({ ...JSON.parse(text), version: 3 })));
    ok(e1 && e1.why === "newer" && /newer editor/.test(explain(e1)), "file: a newer version refused (made by a newer editor)");
    const h = JSON.parse(text);
    h.slots[a].knobs[1] = (h.slots[a].knobs[1] + 1) & 63;
    const e2 = thrown(() => pm2.fromFile(JSON.stringify(h)));
    ok(e2 && e2.why === "damaged" && /CRC/.test(e2.message) && eqB(pm2.record(), before), "file: a bad CRC refused, the list kept");
    const e3 = thrown(() => pm2.fromFile(JSON.stringify({ ...JSON.parse(text), format: "hortator-project" })));
    const e4 = thrown(() => pm2.fromFile("{not json"));
    ok(e3 && e3.why === "format" && e4 && e4.why === "format", "file: another format or not JSON refused");
    delete h.upr2;
    pm2.fromFile(JSON.stringify(h));
    ok(pm2.slots[a].knobs[1] === h.slots[a].knobs[1], "file: a hand-made file without upr2 is read");
    /* a version 1 file (the editor before UPR2: 128 voices): moved in as the FM-1 moves its own */
    const old = Array(128).fill(null);
    for (let i = 0; i < 96; i++) if (i !== 3) old[i] = { name: P.userPresetName(i, "K808"), model: 0, modelName: "K808", knobs: [0, 1, 2, 3, 4, 5, 6, 7] };
    old[100] = { name: "MY TOM", model: 10, modelName: "TOM", knobs: [0, 0, 0, 0, 0, 0, 0, 0] };
    old[120] = { name: P.userPresetName(120, "CYMB"), model: 8, modelName: "CYMB", knobs: [0, 0, 0, 0, 0, 0, 0, 0] };
    pm2.fromFile(JSON.stringify({ format: "hortator-presets", version: 1, slots: old }), "old.json");
    ok(pm2.slots[3].name === "MY TOM" && pm2.slots[3].voiceOnly && pm2.lost === 1 && pm2.moved[4] === 4 && pm2.moved[24] === 0
      && /1 preset .*did not fit/.test(pm2.fileNote) && pm2.slots[0].name === P.userPresetName(0, pm2.modelNames[0]),
    "file: a version 1 file moved in (97..128 into the empty slots, the voice only), what did not fit counted and said");

    /* -------------------------------------------------------- write to the FM-1 --- */
    ok(/Replace the FM-1's 96 user presets/.test(pm.confirmText()) && /change/.test(pm.confirmText()),
      "write: a confirmation text says what is replaced");
    m.play();
    const sends = m.received.length, ePlay = await pm.write().catch((e) => e);
    ok(ePlay && ePlay.rc === P.RC.STOP && /STOP FIRST/.test(explain(ePlay)) && /Stop the FM-1/.test(explain(ePlay))
      && eqB(flashUpr(m), before) && !m.restore && m.received.length === sends + 1,
    "write while playing: the FM-1's STOP FIRST shown, nothing sent after BEGIN, flash unchanged");
    m.stop();

    const seq0 = m.flash.upr.seq, prog = [];
    pm.write((p) => { prog.push(p.done); if (p.done >= 256) pm.cancel(); }).catch((e) => e);
    const eAbort = await (async () => { while (pm.job) await new Promise((r) => setTimeout(r, 2)); return prog; })();
    const lastOps = m.received.slice(-1).map((x) => P.decodeRequest(x)).map((x) => x.args.op);
    ok(eAbort.length >= 2 && eqB(flashUpr(m), before) && m.flash.upr.seq === seq0 && !m.restore
      && !(m.state().flags & SF.RESTORING) && lastOps[0] === RS.ABORT && eqB(P.encodePresets(m.upr, m.uprMoved), before),
    "cancel during a write: ABORT sent, the FM-1's flash and RAM unchanged, not RESTORING");

    const want2 = pm.record(), progress = [];
    await pm.write((p) => progress.push(p.done));
    ok(eqB(flashUpr(m), want2) && eqB(P.encodePresets(m.upr, m.uprMoved), want2) && !pm.differs && progress.at(-1) === 3204
      && progress.length === 14 && pm.slots[95].name === "X".repeat(24), "write: the FM-1 holds the list (BEGIN, 13 pieces, COMMIT), names kept");
    const k = pm.slots.findIndex(Boolean);
    const lu = await pm.loadUser(k, 0);
    ok(pm.canLoadUser(k) && lu.id === pm.nfact + k && m.tracks[0].p[m.P.MODEL] === pm.slots[k].model,
      "a user slot the FM-1 holds loads onto a track");

    /* ---------------------------------------------------------- another FM-1 --- */
    const o = await open({ seed: 99 });
    sessions.push(o.s);
    ok(!eqB(flashUpr(o.m), want2), "another FM-1: other presets before");
    o.pm.fromFile(JSON.stringify(pm.toFile()), "from-1.json");
    await o.pm.write();
    ok(eqB(flashUpr(o.m), want2), "another FM-1: the file restored onto it byte for byte");
    o.m.faults.refuse[CMD.RESTORE] = { rc: P.RC.BUSY, detail: P.DETAIL.RESTORING, msg: "RESTORING" };
    const eBusy = await o.pm.write().catch((e) => e);
    ok(eBusy && /RESTORING/.test(explain(eBusy)), "write: the FM-1's RESTORING refusal shown");
    const bad = new PresetsModel(o.s, { storage: null });
    bad.modelNames = o.pm.modelNames;
    bad.slots[0] = { name: "X", model: 120, knobs: [0, 0, 0, 0, 0, 0, 0, 0] };
    const eModel = await bad.write().catch((e) => e);
    ok(/not in this firmware/.test(eModel.message), "write: a model the FM-1 lacks refused before sending");

    /* ----------------------------------------------------- an FM-1 before UPR2 --- */
    const ol = await open({ seed: 5, caps: P.CAP.LIVE | P.CAP.PRESETS | P.CAP.PATTERNS | P.CAP.BACKUP | P.CAP.RESTORE | P.CAP.NUDGE });
    sessions.push(ol.s);
    const oldUsed = ol.m.upr.filter(Boolean).length, sends0 = ol.m.received.length;
    const eOld = await ol.pm.write().catch((e) => e);
    ok(ol.m.upr.length === 128 && ol.pm.deviceOld && ol.pm.slots.length === 96 && !ol.pm.canWrite
      && ol.pm.slots.filter(Boolean).length + ol.pm.lost === oldUsed && ol.pm.slots.filter(Boolean).every((x) => x.voiceOnly)
      && eOld && /Update the FM-1/.test(eOld.message) && ol.m.received.length === sends0,
    "an FM-1 before UPR2: its 128 presets read as it will move them (the voice only), Write refused before sending");
  } finally {
    sessions.forEach((x) => x.close());
  }
  return { passed, failed };
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const { passed, failed } = await runPresetsTests();
  console.log(failed ? `PRESETS TESTS FAILED (${failed} of ${passed + failed})` : `presets tests passed (${passed})`);
  process.exit(failed ? 1 : 0);
}
