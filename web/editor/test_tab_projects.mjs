// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
//
// The Projects tab's logic (tab_projects.mjs ProjectsModel) against the mock device: node, no browser, no hardware.
//   node web/editor/test_tab_projects.mjs          (also run by node web/test_web.mjs)
// - the cards from BACKUP_LIST (saved / unsaved, size, summary); a project file round trip byte-exact (flash, not RAM)
// - restore a file into its slot (byte-exact) and into another slot (re-sealed by the device); copy a slot
// - refusals shown as the device words them: STOP FIRST while playing, SAVE c FIRST, UPDATE at the commit, NO FLASH
// - abort mid-restore and update traffic mid-restore: nothing written, the restore dropped, the page says so
// - "Back up all" / "Restore all": the hortator-backup file round trip; a version 1 backup's 128 presets moved in;
//   an FM-1 before UPR2: backed up (its presets as it will move them), not restored onto

import { pathToFileURL } from "node:url";
import * as P from "./proto.mjs";
import { createMockDevice, loadDeviceData } from "./mock_device.mjs";
import { connect } from "./session.mjs";
import { ProjectsModel } from "./tab_projects.mjs";
import { PresetsModel } from "./tab_presets.mjs";

const { CMD, RS } = P;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function until(cond, ms = 3000) {
  for (const t0 = Date.now(); Date.now() - t0 < ms; await sleep(2)) if (cond()) return true;
  return cond();
}
const FAST = { timeout: 25, longTimeout: 60, tries: 3, silence: 60, backoff: 25, backoffMax: 100, giveUp: 100000, keepAlive: 100000 };
const eqb = (a, b) => (!a && !b) || (!!a && !!b && a.length === b.length && a.every((x, i) => x === b[i]));
const json = (f) => JSON.parse(JSON.stringify(f)); // through a file and back

export async function runProjectsTests(log = console.log) {
  let passed = 0, failed = 0;
  const ok = (cond, what) => { if (cond) passed++; else { failed++; log(`FAIL projects: ${what}`); } };
  const m = createMockDevice(await loadDeviceData());
  const r = await connect(m.access, { timing: FAST });
  const s = r.session, pm = new ProjectsModel(s);
  const flash = (slot) => ({ record: m.objBytes(1 + slot), locks: m.objBytes(5 + slot), vars: m.objBytes(9 + slot) });
  const sameObjs = (a, b) => eqb(a.record, b.record) && eqb(a.locks, b.locks) && eqb(a.vars, b.vars);
  const fail = (p) => p.then(() => null, (e) => e);
  const sentOps = (from) => m.received.slice(from).map((f) => { try { return P.decodeRequest(f); } catch { return null; } })
    .filter((x) => x && x.cmd === CMD.RESTORE).map((x) => x.args.op);

  /* the cards */
  await pm.refresh();
  const [A, B, C, D] = pm.cards;
  ok(A.present && B.present && C.present && !D.present && A.summary?.name === "TECHNO 1" && A.summary.variations.join() === "1,2,3,5"
    && A.size === m.objBytes(1).length + m.objBytes(5).length + m.objBytes(9).length && !A.unsaved && pm.names.length > 40,
  "cards: A–C present, D empty, A's summary, size = its three objects");

  /* a project file: flash's bytes, byte for byte, through JSON */
  const bk = await pm.backupProject(0);
  const back = P.readProjectFile(json(bk.file));
  ok(bk.file.format === "hortator-project" && bk.name.startsWith("hortator-project-A-") && bk.file.firmware === "0.28.0"
    && bk.file.slot === "A" && sameObjs(back, flash(0)) && back.vars && back.locks, "project file round trip: byte-exact, A's three objects");

  /* restore into its own slot: flash equals the file byte for byte */
  await pm.restoreProjectFile(json(bk.file), 0);
  ok(sameObjs(flash(0), back) && /Restored into A/.test(pm.message) && !pm.error && !m.restore, "restore into A: BACKUP_GET = the file");

  /* restore into another slot: the device re-seals it for D */
  await pm.restoreProjectFile(json(bk.file), 3);
  await until(() => !pm.loading);
  pm.cache.clear();
  await pm.readList();
  const d = await pm.readProject(3);
  ok(sameObjs(d, P.sealProject(back, 3)) && d.locks[P.LAYOUT.locks.slot] === 3 && d.vars[P.LAYOUT.vars.slot] === 3
    && P.decodeProject(d.record).ok && P.summarize(d).name === "TECHNO 1", "restore A's file into D: BACKUP_GET = the file re-sealed for D");
  await pm.refresh();
  ok(pm.cards[3].present && pm.cards[3].summary?.name === "TECHNO 1", "the cards read again after a restore");

  /* copy a slot */
  const bFlash = flash(1);
  await pm.copySlot(1, 3);
  ok(sameObjs(flash(3), P.sealProject(bFlash, 3)) && sameObjs(flash(1), bFlash) && /B copied to D/.test(pm.message), "copy B to D: D = B re-sealed, B untouched");

  /* refusals: as the device words them, nothing written */
  const d0 = flash(3);
  m.play();
  await until(() => s.state.playing);
  let e = await fail(pm.restoreProjectFile(json(bk.file), 3));
  ok(e && /STOP FIRST/.test(e.message) && /Nothing was written/.test(e.message) && pm.message === e.message && pm.error
    && sameObjs(flash(3), d0) && !m.restore, "while playing: STOP FIRST, nothing written");
  m.stop();
  await until(() => !s.state.playing);
  m.faults.cuePark = 1;
  e = await fail(pm.restoreProjectFile(json(bk.file), 3));
  m.faults.cuePark = -1;
  ok(e && /SAVE B FIRST/.test(e.message) && sameObjs(flash(3), d0) && !m.restore, "the park holds B's unsaved variations: SAVE B FIRST");
  m.faults.updateAtCommit = true;
  e = await fail(pm.copySlot(0, 3));
  ok(e && /update is starting \(UPDATE\)/.test(e.message) && sameObjs(flash(3), d0) && !m.restore, "the upgrade command at the commit (rc 6): dropped, said so");

  /* abort mid-restore */
  let n0 = m.received.length;
  const offA = pm.on("progress", (op) => { if (op && op.done === 4) pm.abort(); });
  e = await fail(pm.restoreProjectFile(json(bk.file), 3));
  offA();
  ok(e && /cancelled: nothing was written/.test(e.message) && sameObjs(flash(3), d0) && !m.restore
    && sentOps(n0).join() === [RS.BEGIN, RS.DATA, RS.DATA, RS.DATA, RS.DATA, RS.ABORT].join(), "abort mid-restore: ABORT sent, flash unchanged");

  /* update traffic mid-restore */
  n0 = m.received.length;
  const offU = pm.on("progress", (op) => { if (op && op.done === 3 && !m.silent) m.updateFrame(); });
  e = await fail(pm.restoreProjectFile(json(bk.file), 3));
  offU();
  ok(e && /dropped: the FM-1 went busy \(update\?\)/.test(e.message) && pm.message === e.message && s.busy && !m.restore
    && sameObjs(flash(3), d0) && !sentOps(n0).includes(RS.COMMIT), "update traffic mid-restore: the restore dropped, the page says so");
  m.advance(P.TIMING.silence);
  await until(() => !s.busy && !pm.loading, 2000);
  ok(!s.busy && s.connected, "after the update silence: connected again");

  /* unsaved changes: flagged, not in the backup */
  m.turn(0, m.P.E0, m.tracks[0].p[m.P.E0] === 10 ? 11 : 10);
  await pm.refresh();
  const ub = await pm.backupProject(0);
  ok(pm.cards[0].unsaved && ub.unsaved && ub.file.summary.unsaved && /unsaved changes are not in the file/.test(pm.message)
    && sameObjs(P.readProjectFile(json(ub.file)), flash(0)) && /A's unsaved changes on the FM-1 are lost/.test(pm.replaces(0))
    && /A is loaded/.test(pm.replaces(0)), "unsaved changes: flagged, the file holds flash's version, the confirmation says what is lost");

  /* back up everything / restore everything */
  const all = await pm.backupAll();
  const file = json(all.file), rb = P.readBackupFile(file);
  ok(file.format === "hortator-backup" && all.name.startsWith("hortator-backup-") && eqb(P.encodePresets(rb.slots, rb.moved), m.flash.upr.bytes)
    && [0, 1, 2, 3].every((k) => sameObjs(rb.projects[k], flash(k))) && all.unsaved.join() === "A"
    && file.presets.slots.some((x) => x && /\d\d$/.test(x.name)), "back up all: presets + A–D, byte-exact; unsaved listed");
  const before = [0, 1, 2, 3].map(flash), upr0 = m.flash.upr.bytes;
  m.upr = m.upr.map(() => null);
  m.writeUpr();
  await pm.copySlot(2, 0); // A replaced by C
  ok(!sameObjs(flash(0), before[0]) && !eqb(m.flash.upr.bytes, upr0), "(the device changed before restoring everything)");
  await pm.restoreAll(file);
  ok(eqb(m.flash.upr.bytes, upr0) && [0, 1, 2, 3].every((k) => sameObjs(flash(k), before[k])) && /Everything restored/.test(pm.message)
    && !m.restore, "restore all: presets and A–D byte-exact as backed up");
  ok(/96 user presets and projects A, B, C, D/.test(pm.replacesAll(rb)), "restore all: the confirmation names what is replaced");
  {
    /* a version 1 backup (the editor before UPR2: 128 voices, no FX): moved in as the firmware moves its own */
    const v1 = json(file);
    v1.version = 1;
    const k8 = [0, 0, 0, 0, 0, 0, 0, 0];
    v1.presets = { slots: Array.from({ length: 128 }, (_, i) => (i < 95 || i === 100 || i === 120 ? { name: `P${i}`, model: 1, knobs: k8 } : null)) };
    const b1 = P.readBackupFile(v1);
    ok(b1.lost === 1 && /96 user presets/.test(pm.replacesAll(b1)) && /1 preset .*not fit/.test(pm.replacesAll(b1)),
      "restore all, a version 1 backup: the confirmation says what does not fit");
    await pm.restoreAll(v1);
    const got = P.decodePresets(m.flash.upr.bytes);
    ok(got.slots[95] && got.slots[95].voiceOnly && got.moved[4] === 96 && got.moved[24] === 0 && m.upr[95].voiceOnly
      && [0, 1, 2, 3].every((k) => sameObjs(flash(k), before[k])) && /Everything restored/.test(pm.message),
    "restore all, a version 1 backup: 97..128 moved in (the voice only, the moves written for the projects), A–D as saved");
    await pm.restoreAll(file);
  }

  /* one restore at a time across the tabs: the session's restore lock */
  {
    const pr = new PresetsModel(s, { storage: null }), ev = [];
    await pr.load();
    const offR = s.on("restore", (l) => ev.push(l && l.owner));
    const n0 = m.received.length;
    const w = pr.write();
    const blocked = pm.restoreBlocked, eCopy = await fail(pm.copySlot(0, 3)), eAll = await fail(pm.restoreAll(file));
    const begins = () => m.received.slice(n0).map((f) => P.decodeRequest(f)).filter((x) => x.cmd === CMD.RESTORE && x.args.op === RS.BEGIN);
    ok(/Writing the user presets to the FM-1 \(Presets tab\)/.test(blocked) && eCopy && /Wait for it/.test(eCopy.message)
      && eAll && /Presets tab/.test(eAll.message) && !s.restoreBlocked("Presets"),
    "restore lock: while Presets writes, Projects' copy and restore are refused before sending, with the reason");
    await w;
    ok(begins().length === 1 && !s.restoring && !pm.restoreBlocked && ev.join() === "Presets,", "restore lock: one BEGIN went out; released after the write");
    let inside = "";
    const offS = s.on("restore", (l) => { if (l && l.owner === "Projects") inside = s.restoreBlocked("Presets"); });
    await pm.copySlot(0, 3);
    const ePr = await fail(pr.write());
    ok(/Projects tab/.test(inside) && !s.restoring && !ePr, "restore lock: Projects holds it during a copy (Presets sees why); free again after");
    const rel = [], offL = s.on("reload", (x) => rel.push(x));
    await m.request(CMD.CUE, { kind: P.CUE.SLOT, n: 3 }); // (D loaded: a copy into it loads it again)
    await until(() => s.state.slot === 3);
    await pm.copySlot(0, 3);
    ok(rel.some((x) => x.slot === 3), "reload: a restore into the loaded slot tells the views (session reload { slot })");
    await m.request(CMD.CUE, { kind: P.CUE.SLOT, n: 0 });
    [offR, offS, offL].forEach((f) => f());
  }

  /* no flash this boot */
  m.faults.noFlash = true;
  await pm.refresh();
  ok(/NO FLASH/.test(pm.message) && pm.error, "no flash this boot: the device's NO FLASH shown");
  m.faults.noFlash = false;

  {
    /* an FM-1 before UPR2: backed up as it will move its presets; Restore all refused before anything is sent */
    const o = createMockDevice(await loadDeviceData(), { seed: 3, caps: P.CAP.LIVE | P.CAP.PRESETS | P.CAP.PATTERNS | P.CAP.BACKUP | P.CAP.RESTORE | P.CAP.NUDGE });
    const ro = await connect(o.access, { timing: FAST }), po = new ProjectsModel(ro.session);
    await po.refresh();
    const ab = await po.backupAll(), fb = P.readBackupFile(json(ab.file));
    const n0 = o.received.length, eo = await fail(po.restoreAll(json(ab.file)));
    ok(fb.slots.length === 96 && fb.slots.filter(Boolean).length + P.decodePresets(o.flash.upr.bytes).lost === o.upr.filter(Boolean).length
      && eo && /Update the FM-1/.test(eo.message) && !o.received.slice(n0).some((f) => f[4] === CMD.RESTORE),
    "an FM-1 before UPR2: backed up (its 128 presets as it will move them), Restore all refused before sending");
    po.destroy();
    ro.session.close();
  }
  const notEditor = m.received.filter((f) => !(f[0] === 0xf0 && f[1] === 0x7d && f[2] === 0x48 && f[3] === 0x54 && f[f.length - 1] === 0xf7));
  ok(notEditor.length === 1 && notEditor[0][1] === 0x00, "every frame the page sent is an editor frame (the one F0 00 frame is the test's update)");
  pm.destroy();
  s.close();
  return { passed, failed };
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const { passed, failed } = await runProjectsTests();
  console.log(failed ? `PROJECTS TESTS FAILED (${failed} of ${passed + failed})` : `projects tests passed (${passed})`);
  process.exit(failed ? 1 : 0);
}
