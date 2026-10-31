// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
//
// The Patterns tab's logic (tab_patterns.mjs PatternsModel) against the mock device: node, no browser, no hardware.
//   node web/editor/test_tab_patterns.mjs          (also run by node web/test_web.mjs)
// - the grid is what STEPS_GET says; an edit is a STEPS_SET and the device's steps match
// - owner decision 3: a variation not playing, stopped -> the FM-1 switches at once; playing -> rc 5, the page waits
//   for the STATE push and sends the edit again, never CUE; a song playing -> "STOP TO EDIT"; other slots read-only
// - STEPS_CHANGED / LOCKS_CHANGED keep it live; LOCK_EDIT set / clear one / step / track
// - the cue keys: CUE sent, queued -> playing from STATE, refusals shown; copy variation = the source; SAVE refusals

import { pathToFileURL } from "node:url";
import * as P from "./proto.mjs";
import { createMockDevice, loadDeviceData } from "./mock_device.mjs";
import { connect } from "./session.mjs";
import { CONDS, PatternsModel, cueKeys, noteName, plain, stepGlyph, withRatch } from "./tab_patterns.mjs";

const { CMD, CUE, LK, NONE, RC, DETAIL, STEP } = P;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function until(cond, ms = 3000) {
  for (const t0 = Date.now(); Date.now() - t0 < ms; await sleep(2)) if (cond()) return true;
  return cond();
}
const FAST = { timeout: 25, longTimeout: 60, tries: 3, silence: 60, retryInfo: 25, keepAlive: 100000 };
const J = JSON.stringify;

export async function runPatternTests(log = console.log) {
  let passed = 0, failed = 0;
  const ok = (cond, what) => { if (cond) passed++; else { failed++; log(`FAIL patterns: ${what}`); } };
  const data = await loadDeviceData();

  /* pure helpers */
  ok(J(CONDS.map((c) => [c.code, c.name])) === J(data.conds.map((c) => [c.store, c.name])), "PROB names and order = the device's (device_data conds)");
  ok(noteName(60) === "C4" && noteName(61) === "C#4" && noteName(0) === "C-1", "note names (60 = C4)");
  const r3 = withRatch({ flags: STEP.ON | STEP.ACC, cond: 0, note: 60 }, 3);
  ok(r3.flags === (STEP.ON | STEP.ACC | 2 << STEP.RAT_SHIFT) && stepGlyph(r3) === "3" && stepGlyph({ flags: 1, cond: 57, note: 60 }) === "F"
    && stepGlyph({ flags: 1, cond: 58, note: 60 }) === "!" && stepGlyph({ flags: 1, cond: 5, note: 60 }) === "%" && stepGlyph({ flags: 0, cond: 57, note: 60 }) === "",
  "RATCH bits, the cell's marks (ratchet, FILL / !FILL, PROB)");

  const start = async (o) => {
    const m = createMockDevice(data, o), r = await connect(m.access, { timing: FAST }), s = r.session;
    const pm = new PatternsModel(s);
    await pm.load();
    await until(() => !pm.loading && s.link.idle);
    return { m, s, pm };
  };
  const cmds = (m, from = 0) => m.received.slice(from).map((f) => { try { return P.decodeRequest(f); } catch { return null; } }).filter(Boolean);
  const cues = (m, from = 0) => cmds(m, from).filter((x) => x.cmd === CMD.CUE).length;
  const liveMatches = (m, pm) => pm.tracks.every((t, k) => J(t.steps) === J(m.tracks[k].steps) && J(t.timing) === J(m.timingOf(m.tracks[k])));
  const settle = (s, pm) => until(() => !pm.loading && s.link.idle);

  /* ---------------------------------------------------------------- grid + edits --- */
  {
    const { m, s, pm } = await start();
    ok(pm.view.slot === 0 && pm.view.var === 0 && pm.live && pm.editable && liveMatches(m, pm)
      && pm.tracks[0].steps[0].flags === (STEP.ON | STEP.ACC) && pm.tracks[0].timing[0] === 16, "the grid = STEPS_GET of the playing variation (steps, LEN)");
    ok(pm.locks[2].length === 8 && pm.locks[3].every((e) => e.kind === LK.MOTION) && pm.stepLocks(2, 1).length === 1, "the locks = LOCKS_GET per track (LOCK and MOTION)");

    const n0 = m.received.length;
    ok(await pm.toggle(1, 2) && (m.tracks[1].steps[2].flags & STEP.ON) && cmds(m, n0).some((x) => x.cmd === CMD.STEPS_SET && x.args.first === 2), "click: STEPS_SET, the device's step is on");
    await pm.setStep(1, 2, { note: 67, cond: 57, flags: withRatch(pm.tracks[1].steps[2], 2).flags | STEP.ACC });
    const d = m.tracks[1].steps[2];
    ok(d.note === 67 && d.cond === 57 && ((d.flags >> 4) & 3) === 1 && d.flags & STEP.ACC && liveMatches(m, pm) && (s.state.dirty & 1),
      "NOTE / PROB (FILL) / RATCH / ACC: the device's step = the page's; A unsaved");

    /* the device's side: STEPS_CHANGED, LOCKS_CHANGED */
    m.editStep(5, 9, { flags: STEP.ON | STEP.ACC, note: 62 });
    await until(() => pm.tracks[5].steps[9].flags & STEP.ON);
    await settle(s, pm);
    ok(liveMatches(m, pm), "a step edited on the FM-1: STEPS_CHANGED, the grid follows");
    m.lockOnDevice(6, 3, m.P.FCUT, 77);
    await until(() => pm.locks[6].length === 1);
    ok(pm.locks[6][0].param === m.P.FCUT && pm.locks[6][0].value === 77, "a lock set on the FM-1: LOCKS_CHANGED, the locks follow");

    /* locks: set, edit, clear one, clear the step, clear the track */
    const E1 = m.P.E1, bank = () => m.img().locks.filter((e) => e.track === 2 && e.kind === LK.LOCK);
    const set = await pm.lockSet(2, 1, E1, 50);
    ok(set && bank().find((e) => e.step === 1 && e.param === E1).value === 50 && pm.stepLocks(2, 1)[0].value === 50, "lock edit: LOCK_EDIT SET, the bank and the page agree");
    await pm.lockSet(2, 1, m.P.PAN, -20);
    ok(pm.stepLocks(2, 1).length === 2 && (await pm.textOf(2, m.P.PAN, -20)).length > 0 && (await pm.descOf(2, E1)).label, "a second lock on the step; its text and knob name from TEXT / DESC");
    await pm.lockClear(2, 1, m.P.PAN);
    ok(pm.stepLocks(2, 1).length === 1 && !bank().some((e) => e.param === m.P.PAN), "clear one lock");
    await pm.lockClearStep(2, 1);
    ok(!pm.stepLocks(2, 1).length && bank().length === 7, "clear the step's locks");
    await pm.lockClearTrack(2);
    ok(!pm.locks[2].length && !bank().length, "clear the track's locks");
    const motion = pm.locks[3].length;
    await pm.lockClearTrack(3);
    ok(pm.locks[3].length === motion && motion === 4, "clear track: MOTION events stay");
    const lk = await pm.lockSet(2, 4, m.P.MODEL, 3);
    ok(lk === null && /NOT LOCKABLE/.test(pm.note), "a knob that cannot be locked: the device's refusal shown");

    /* other slots: read-only */
    await pm.setView(1, 0);
    await settle(s, pm);
    const b0 = m.slots[1].steps1;
    const n1 = m.received.length;
    ok(!pm.editable && !pm.follow && pm.tracks.every((t, k) => J(t.steps) === J(b0[k])) && pm.locks[2].length === 4, "another slot: its steps and locks, shown");
    ok(!(await pm.setStep(0, 1, { flags: STEP.ON })) && !(await pm.lockSet(0, 0, E1, 1)) && /read-only/.test(pm.note)
      && !cmds(m, n1).some((x) => x.cmd === CMD.STEPS_SET || x.cmd === CMD.LOCK_EDIT), "another slot: read-only, nothing sent");
    await pm.followNow();
    await settle(s, pm);
    ok(pm.follow && pm.live && liveMatches(m, pm), "follow the FM-1 again");

    /* owner decision 3, stopped: the FM-1 switches at once */
    const c0 = m.received.length;
    await pm.setView(0, 1);
    await settle(s, pm);
    ok(!pm.live && pm.editable && J(pm.tracks[0].steps) === J(m.slots[0].vars[0].steps[0]), "another variation of the loaded slot: shown from the slot");
    ok(await pm.setStep(0, 5, { flags: STEP.ON, note: 64 }) && m.varCur === 1 && m.tracks[0].steps[5].note === 64 && !m.playing, "stopped, a variation not playing: edit -> the FM-1 switches at once, the edit lands");
    await until(() => s.state.var === 1);
    await settle(s, pm);
    ok(s.state.var === 1 && pm.follow && pm.live && cues(m, c0) === 0 && liveMatches(m, pm), "... the STATE push: the view is live; no CUE sent");

    /* owner decision 3, playing: rc 5, wait for STATE, send again, never CUE */
    m.play();
    await until(() => s.state.playing);
    await pm.setView(0, 2);
    await settle(s, pm);
    const c1 = m.received.length, live6 = J(m.tracks[3].steps[6]);
    const pending = pm.setStep(3, 6, { flags: STEP.ON | STEP.ACC });
    await until(() => pm.wait && s.state.qvar === 2);
    const sets1 = cmds(m, c1).filter((x) => x.cmd === CMD.STEPS_SET).length;
    ok(pm.wait && m.varCur === 1 && m.q.var === 2 && J(m.tracks[3].steps[6]) === live6 && sets1 === 1, "playing: STEPS_SET -> rc 5 (the switch queued), the page waits");
    await sleep(20);
    ok(cmds(m, c1).filter((x) => x.cmd === CMD.STEPS_SET).length === 1 && !m.q.now, "... nothing sent while it waits (no 'at the next step')");
    m.deliver(m.boundary()); // the pattern's end: the switch, a STATE push
    ok(await pending && m.varCur === 2 && m.tracks[3].steps[6].flags === (STEP.ON | STEP.ACC) && !pm.wait, "the STATE push shows VAR 3 playing: the edit is sent again and lands");
    ok(cues(m, c1) === 0 && cmds(m, c1).filter((x) => x.cmd === CMD.STEPS_SET).length === 2, "... two STEPS_SET, never CUE");
    await settle(s, pm);

    /* playing, the switch cancelled on the FM-1 (the playing variation's key): the edit is dropped and the grid reverts */
    await pm.setView(0, 4);
    await settle(s, pm);
    const was = J(pm.tracks[0].steps[2]);
    const drop = pm.setStep(0, 2, { flags: STEP.ON | STEP.ACC, cond: 3 });
    await until(() => s.state.qvar === 4);
    m.q = { slot: -1, var: -1, now: false }; // cancelled on the device
    m.kick();
    ok(!(await drop) && /cancelled/.test(pm.note) && m.varCur === 2, "the switch cancelled on the FM-1: the edit is not applied, said so");
    await settle(s, pm);
    ok(J(pm.tracks[0].steps[2]) === was && cues(m, c1) === 0, "... the grid shows the device's steps again; still no CUE");

    /* a song plays: STOP TO EDIT */
    await pm.followNow();
    await settle(s, pm);
    m.setSong(true);
    const before = J(m.tracks[0].steps[1]);
    ok(!(await pm.toggle(0, 1)) && /STOP TO EDIT/.test(pm.note) && J(m.tracks[0].steps[1]) === before, "a song plays: STEPS_SET refused, 'STOP TO EDIT' shown");
    await settle(s, pm);
    ok(J(pm.tracks[0].steps[1]) === before, "... the grid shows the device's step again");
    m.setSong(false);

    /* RESTORING */
    m.faults.refuse[CMD.STEPS_SET] = { rc: RC.BUSY, detail: DETAIL.RESTORING, msg: "RESTORING" };
    ok(!(await pm.toggle(0, 1)) && /RESTORING: a restore is running/.test(pm.note), "RESTORING: the refusal shown plainly");
    await settle(s, pm);

    /* the cue keys */
    const c2 = m.received.length;
    ok(await pm.cue(CUE.VAR, 0) && cues(m, c2) === 1, "a variation key: CUE sent");
    let k = cueKeys(s.state);
    ok(k.vars[0].queued && !k.vars[0].playing && k.vars[2].playing && m.playing, "queued: blinking key (STATE), the playing one lit");
    m.deliver(m.boundary());
    await until(() => s.state.var === 0);
    k = cueKeys(s.state);
    ok(k.vars[0].playing && !k.vars[0].queued && s.state.qvar === NONE, "the pattern's end: STATE -> playing, no longer queued");
    await settle(s, pm);
    ok(pm.view.var === 0 && pm.live && liveMatches(m, pm), "the view follows the switch");
    ok(await pm.cue(CUE.SLOT, 1) && cueKeys(s.state).slots[1].queued && cueKeys(s.state).slots[0].playing, "a slot key: B queued, A playing");
    m.deliver(m.boundary());
    await until(() => s.state.slot === 1);
    await settle(s, pm);
    ok(cueKeys(s.state).slots[1].playing && pm.view.slot === 1 && pm.editable && liveMatches(m, pm) && pm.locks[2].length === 4, "B playing: the grid and the locks follow");
    m.faults.cueFull = true;
    ok(!(await pm.cue(CUE.VAR, 5)) && /VARIATIONS FULL/.test(pm.note), "a cue refused: the device's message shown");
    m.faults.cueFull = false;
    ok(!(await pm.cue(CUE.SLOT, 3)) && /SLOT D EMPTY/.test(pm.note), "an empty slot: SLOT D EMPTY");

    /* SAVE */
    ok(!(await pm.save()) && /STOP TO SAVE: stop the FM-1 first/.test(pm.note), "SAVE while playing: STOP TO SAVE, said plainly");
    m.stop();
    await until(() => !s.state.playing);
    m.faults.noFlash = true;
    ok(!(await pm.save()) && /NO FLASH: .*restarts/.test(pm.note), "SAVE without flash: NO FLASH, said plainly");
    m.faults.noFlash = false;
    await pm.cue(CUE.SLOT, 0);
    await until(() => s.state.slot === 0);
    ok(s.state.dirty & 1, "A has unsaved changes");
    ok(await pm.save() && !(s.state.dirty & 1) && /Saved slot A/.test(pm.note), "SAVE: A saved, no longer unsaved");
    ok(plain({ message: "SAVE ERROR", rc: RC.FLASH, detail: 0 }).includes("still holds"), "SAVE ERROR: said plainly");
    s.close();
    pm.destroy();
  }

  /* ---------------------------------------------------------------- copy variation --- */
  {
    const { m, s, pm } = await start();
    const src = m.slots[0].vars[1]; // A VAR 3
    const c0 = m.received.length;
    ok(await pm.copy({ slot: 0, var: 2 }, 6) && m.varCur === 6 && m.tracks.every((t, k) => J(t.steps) === J(src.steps[k]) && J(m.timingOf(t)) === J(src.timing[k])),
      "copy A VAR 3 -> A VAR 7 (stopped): steps and timing = the source");
    await until(() => s.state.var === 6);
    await settle(s, pm);
    ok(pm.view.var === 6 && pm.live && liveMatches(m, pm) && cues(m, c0) === 0 && (s.state.vmask[0] >> 6 & 1), "... the FM-1 plays VAR 7, the grid shows it; no CUE");

    const bLocks = m.slots[1].locks.filter((e) => e.kind === LK.LOCK).map((e) => J([e.track, e.step, e.param, e.value])).sort();
    const aMotion = m.slots[0].locks.filter((e) => e.kind === LK.MOTION).length;
    ok(await pm.copy({ slot: 1, var: 0 }, 7, { locks: true }) && m.tracks.every((t, k) => J(t.steps) === J(m.slots[1].steps1[k])),
      "copy B VAR 1 -> A VAR 8: steps = the source");
    const aLocks = m.img().locks.filter((e) => e.kind === LK.LOCK).map((e) => J([e.track, e.step, e.param, e.value])).sort();
    ok(J(aLocks) === J(bLocks) && m.img().locks.filter((e) => e.kind === LK.MOTION).length === aMotion, "... with locks: A's LOCKs = B's, A's MOTION kept");

    /* playing: the first STEPS_SET queues the switch; the copy completes after the pattern's end */
    m.play();
    await until(() => s.state.playing);
    const c1 = m.received.length, cp = pm.copy({ slot: 0, var: 1 }, 2), src2 = m.slots[0].vars[0];
    await until(() => pm.wait && s.state.qvar === 2);
    ok(m.varCur === 7 && pm.working, "copy while playing: waits for the queued switch");
    m.deliver(m.boundary());
    ok(await cp && m.varCur === 2 && m.tracks.every((t, k) => J(t.steps) === J(src2.steps[k]) && J(m.timingOf(t)) === J(src2.timing[k])) && cues(m, c1) === 0,
      "... after the switch: VAR 3 = A VAR 2; never CUE");
    ok(!(await pm.copy({ slot: 0, var: 5 }, 4)) && /not used/.test(pm.note), "copy from an unused variation: refused, nothing written");
    ok(!(await pm.copy({ slot: 0, var: 2 }, 2)) && /same/.test(pm.note), "copy onto itself: refused");
    s.close();
    pm.destroy();
  }
  {
    const { s, pm } = await start({ powerOnKit: true });
    ok(pm.view.slot === NONE && pm.editable && !(await pm.copy({ slot: 0, var: 0 }, 1)) && /Load a slot/.test(pm.note), "the power-on kit: shown and editable; copy needs a loaded slot");
    s.close();
    pm.destroy();
  }
  return { passed, failed };
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const { passed, failed } = await runPatternTests();
  console.log(failed ? `PATTERNS TESTS FAILED (${failed} of ${passed + failed})` : `patterns tests passed (${passed})`);
  process.exit(failed ? 1 : 0);
}
