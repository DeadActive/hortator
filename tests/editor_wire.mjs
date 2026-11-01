// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The firmware's editor frames (tests/editor_test.c FILE: every reply and push it sent, hex, a line each) read with the
// page's codec (web/editor/proto.mjs) and written again: each must decode and re-encode byte for byte, so the
// firmware's field layout is the protocol's (web/EDITOR_PROTOCOL.md). Also INFO's numbers = device_data.json's.
// With the whole editor built for the host (tests/editor_xcheck.c), one request script runs through the firmware and
// through the page's mock device (web/editor/mock_device.mjs), both holding the same flash, and every reply must be
// the same byte for byte: the mock (what the page is built and tested against) and the protocol document cannot drift
// from the firmware unnoticed.
//   node tests/editor_wire.mjs build/host/editor_frames.txt [build/host/editor_xcheck]
import { readFileSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { CMD, CUE, NONE, RC, RK, RS, SEQ_PUSH, crc32, decodeFrame, decodeProject, encodePush, encodeProject, encodeReply,
  encodeRequest, isEditorFrame, sealProject,
} from "../web/editor/proto.mjs";
import { UPDATE_HANDSHAKE, createMockDevice } from "../web/editor/mock_device.mjs";

const data = JSON.parse(readFileSync(new URL("../web/editor/device_data.json", import.meta.url), "utf8"));
const lines = readFileSync(process.argv[2], "utf8").split("\n").filter(Boolean)
  .filter((h) => isEditorFrame(Uint8Array.from(h.match(/../g).map((x) => parseInt(x, 16)))));   // (not the test's ota frame)
const seen = new Set();
let bad = 0, info = null;
for (const hex of lines) {
  const b = Uint8Array.from(hex.match(/../g).map((x) => parseInt(x, 16)));
  let again;
  try {
    const d = decodeFrame(b);
    again = d.push ? encodePush(d.cmd, d) : encodeReply(d.cmd, d.seq, d.rc, d.rc ? { detail: d.detail, msg: d.msg } : d);
    seen.add(`${d.push ? "push" : "reply"} ${d.cmd}${d.rc ? " rc" : ""}`);
    if (!d.push && !d.rc && d.cmd === CMD.INFO) info = d;
  } catch (e) {
    console.log(`FAIL  ${hex}: ${e.message}`);
    bad++;
    continue;
  }
  if (again.length !== b.length || again.some((x, i) => x !== b[i])) {
    console.log(`FAIL  ${hex}: re-encoded differently`);
    bad++;
  }
}
const c = data.consts;
if (!info || info.pcount !== c.P_COUNT || info.gcount !== c.G_COUNT || info.nmodels !== c.NMODELS ||
    info.nfact !== c.PRE_NFACT || info.nuser !== c.PRE_NUSER || info.ntrk !== c.NTRK || info.nstep !== c.NSTEP ||
    info.nbanks !== data.banks.length || info.nvar !== c.VAR_N) {
  console.log("FAIL  INFO's numbers differ from web/editor/device_data.json");
  bad++;
}
console.log(`${bad ? "FAIL" : "ok  "}  ${lines.length} firmware frames (${seen.size} kinds) decoded by proto.mjs and ` +
  "re-encoded byte for byte; INFO = device_data.json");
if (process.argv[3]) bad += xcheck(process.argv[3]);
process.exit(bad ? 1 : 0);

/* ---- the firmware and the mock, the same script ---------------------------------------------------------------- */
function xcheck(bin) {
  const m = createMockDevice(data, { firmware: "DEV" }), P = Object.fromEntries(data.P.map((n, i) => [n, i]));
  const G = Object.fromEntries(data.G.map((n, i) => [n, i])), M = Object.fromEntries(data.models.map((x, i) => [x.name, i]));
  const hex = (b) => Buffer.from(b).toString("hex");
  const lines = [];                                    // the C harness' script
  const steps = [];                                    // [what, frame | null (a device action), mock action]
  let seq = 0;
  /* the same flash: the mock's seeded objects; the firmware boots from them and LOADs A, as the mock starts */
  lines.push(`flash upr ${hex(m.flash.upr.bytes)}`);
  for (let s = 0; s < 4; s++) {
    const f = m.flashObjects(s);
    if (!f) continue;
    if (f.vars) lines.push(`flash var ${s} ${hex(f.vars)}`);
    if (f.locks) lines.push(`flash lk ${s} ${hex(f.locks)}`);
    lines.push(`flash rec ${s} ${hex(f.record)}`);
  }
  lines.push("boot", "load 0");
  const dev = (line, fn) => steps.push([line, null, fn]);
  const rq = (what, cmd, args = {}, s) => steps.push([what, encodeRequest(cmd, s ?? (seq = (seq + 1) % SEQ_PUSH), args), null]);
  const raw = (what, bytes) => steps.push([what, Uint8Array.from(bytes), null]);   // any SysEx, not the editor's
  const set = (what, scope, track, id, value, s) => rq(what, CMD.SET, { scope, track, id, value }, s);
  const push = (line, what) => steps.push([what, null, null, line]);   // the pushes since the last such step
  const A = m.flashObjects(0), parts = [A.record, A.locks, A.vars];
  /* A as the editor before NUDGE saved it: an FDRF record (KEYF / SRR / INSERT in steps 0..6's spare bytes), its parts */
  const fa = decodeProject(A.record);
  fa.tracks[1].p[P.SRR] = 40;
  const F = sealProject({ record: encodeProject(fa, "FDRF"), locks: A.locks, vars: A.vars }, 0), fparts = [F.record, F.locks, F.vars];
  const begin = (slot, ps = parts) => ({ op: RS.BEGIN, kind: RK.PROJECT, slot, magic: (ps[0][0] | ps[0][1] << 8 | ps[0][2] << 16 | ps[0][3] << 24) >>> 0,
    parts: ps.map((p) => ({ size: p ? p.length : 0, crc: p ? crc32(p) : 0 })) });
  const pieces = (what, ps = parts) => ps.forEach((p, part) => { for (let o = 0; p && o < p.length; o += 256)
    rq(`${what} DATA ${part}@${o}`, CMD.RESTORE, { op: RS.DATA, part, offset: o, data: p.subarray(o, o + 256) }); });

  rq("INFO", CMD.INFO);
  set("SET MODEL: the model with its first factory preset", 0, 0, P.MODEL, M.S909);
  rq("DUMP after SET MODEL", CMD.DUMP, { track: 0 });
  set("SET MODEL ACID (track 2)", 0, 1, P.MODEL, M.ACID);
  set("SET MODEL ACID (track 3)", 0, 2, P.MODEL, M.ACID);
  set("SET MODEL ACID onto a third track: rc 2 18", 0, 3, P.MODEL, M.ACID);
  for (let k = 0; k < 4; k++) set(`SET RESON MODEL STRNG (track ${k + 1})`, 0, k, P.RMODEL, 1);
  set("SET RESON MODEL on a fifth track: rc 2 0 + the device's message", 0, 4, P.RMODEL, 1);
  set("SET RESON MODEL CHORD (track 1)", 0, 0, P.RMODEL, data.consts.RS_CHORD);
  set("SET RESON MODEL CHORD (track 2)", 0, 1, P.RMODEL, data.consts.RS_CHORD);
  set("SET RESON MODEL CHORD on a third track: rc 2 0", 0, 2, P.RMODEL, data.consts.RS_CHORD);
  set("SET LFO 1 PHASE: the degree unit as \"deg\"", 0, 0, P.LFO1_PHASE, 64);
  set("SET CLOCK USB", 1, 0, G.CLOCK, 1);
  set("SET BPM following a clock: rc 0, unchanged", 1, 0, G.BPM, 150);
  set("SET CLOCK INT", 1, 0, G.CLOCK, 0);
  set("SET a read-only global: rc 1 45 READ ONLY", 1, 0, G.INFO, 1);
  set("SET CHOKE (the retried write's first send)", 0, 0, P.CHOKE, 2, 100);
  dev("turn 0 12 3", () => m.turn(0, P.CHOKE, 3));
  set("SET CHOKE retried (same seq): its first reply byte for byte", 0, 0, P.CHOKE, 2, 100);
  dev("song 1", () => m.setSong(true));
  set("SET LEN while a song plays: rc 0, unchanged", 0, 0, P.SLEN, 32);
  rq("STEPS_SET while a song plays: rc 2 2 STOP TO EDIT", CMD.STEPS_SET, { slot: NONE, var: 0, track: 0, first: 0, steps: [{ flags: 1, cond: 0, note: 60 }] });
  rq("CUE while a song plays: rc 2 2", CMD.CUE, { kind: CUE.VAR, n: 1 });
  dev("song 0", () => m.setSong(false));
  rq("WATCH on", CMD.WATCH, { on: 1 });
  push("pushes 100", "WATCH on: nothing changed, no push");
  rq("STEPS_SET stopped, variation 2: switched, written", CMD.STEPS_SET, { slot: 0, var: 1, track: 1, first: 5, steps: [{ flags: 3, cond: 11, note: 64 }] });
  push("pushes 100", "STEPS_SET's switch (stopped): a STATE push, STEPS_CHANGED for the other tracks, none for the write");
  rq("PRESET_LOAD onto the selected track", CMD.PRESET_LOAD, { track: NONE, id: 7 });
  push("pushes 100", "PRESET_LOAD onto the selected track: CHANGED for MODEL and the SOUND knobs that moved");
  rq("PRESET_LOAD onto another track", CMD.PRESET_LOAD, { track: 6, id: 9 });
  push("pushes 100", "PRESET_LOAD onto another track: no push");
  { /* user presets (UPR2): one with its FX, one the voice only (moved from UPR1); the FX knobs' edited mark */
    const nf = data.presets.length, withFx = m.upr.findIndex((x) => x && !x.voiceOnly && x.fx.some((v, k) => v !== data.tp[data.uprFx[k]].def));
    const voice = m.upr.findIndex((x) => x && x.voiceOnly);
    if (withFx < 0 || voice < 0) throw new Error("the mock's seed has no user preset with FX / voice only");
    rq("PRESET_LOAD a user preset with FX onto track 6", CMD.PRESET_LOAD, { track: 5, id: nf + withFx });
    rq("DUMP: the user preset's FX on the track, not edited", CMD.DUMP, { track: 5 });
    set("SET DIST on it", 0, 5, P.DIST, 21);
    rq("DUMP: edited (an FX knob of a user preset with FX)", CMD.DUMP, { track: 5 });
    rq("PRESET_LOAD a voice-only user preset onto track 6", CMD.PRESET_LOAD, { track: 5, id: nf + voice });
    set("SET DLY on it", 0, 5, P.DLY, 99);
    rq("DUMP: its FX kept, not edited", CMD.DUMP, { track: 5 });
  }
  rq("SET MODEL of the selected track", CMD.SET, { scope: 0, track: NONE, id: P.MODEL, value: M.K909 });
  push("pushes 100", "SET MODEL: no push (the editor's own write)");
  rq("CUE variation 3, stopped", CMD.CUE, { kind: CUE.VAR, n: 2 });
  push("pushes 100", "CUE stopped: the switch's STATE push, STEPS_CHANGED");
  rq("CUE variation 2 again", CMD.CUE, { kind: CUE.VAR, n: 1 });
  push("pushes 100", "CUE stopped, back: STATE, the timing knobs as CHANGED, STEPS_CHANGED");
  rq("WATCH off", CMD.WATCH, { on: 0 });
  dev("turn 0 1 5", () => { m.tracks[0].p[1] = 5; });
  push("pushes 100", "WATCH off: no push");
  rq("INFO: the state after the switch", CMD.INFO);
  rq("STEPS_GET variation 2 track 2", CMD.STEPS_GET, { slot: NONE, var: 1, track: 1 });
  const nud = (flags, micro) => ({ flags, cond: 0, note: 60, ...(micro === undefined ? {} : { micro }) });
  rq("STEPS_SET with NUDGE (minor 1)", CMD.STEPS_SET, { slot: NONE, var: 1, track: 3, first: 2, steps: [nud(1, -30), nud(1, 50), nud(1, 0)] });
  rq("STEPS_SET without NUDGE (an older page): kept; a step written empty: 0", CMD.STEPS_SET, { slot: NONE, var: 1, track: 3, first: 2, steps: [nud(3), nud(0)] });
  rq("STEPS_GET: the steps' NUDGE", CMD.STEPS_GET, { slot: NONE, var: 1, track: 3 });
  rq("STEPS_GET A variation 1 (the FDRG record's NUDGE)", CMD.STEPS_GET, { slot: 0, var: 0, track: 2 });
  rq("STEPS_GET A variation 5 (the bank: flags bit 6, a micro byte)", CMD.STEPS_GET, { slot: 0, var: 4, track: 2 });
  rq("STEPS_GET A variation 5 track 4 (after a track with nudged steps)", CMD.STEPS_GET, { slot: 0, var: 4, track: 3 });
  dev("play", () => m.play());
  rq("STEPS_SET playing, variation 3: queued, rc 5 32", CMD.STEPS_SET, { slot: 0, var: 2, track: 0, first: 0, steps: [{ flags: 1, cond: 0, note: 60 }] });
  rq("STEPS_SET again before the switch: rc 5 32", CMD.STEPS_SET, { slot: 0, var: 2, track: 0, first: 0, steps: [{ flags: 1, cond: 0, note: 60 }] });
  rq("INFO: variation 3 queued", CMD.INFO);
  rq("SAVE playing: rc 3", CMD.SAVE, { slot: NONE });
  dev("stop", () => m.stop());
  dev("noflash 1", () => { m.faults.noFlash = true; });
  rq("SAVE without flash: rc 2 28", CMD.SAVE, { slot: NONE });
  rq("BACKUP_LIST without flash: rc 2 28", CMD.BACKUP_LIST);
  dev("noflash 0", () => { m.faults.noFlash = false; });
  dev("park 1", () => { m.faults.cuePark = 1; });
  rq("RESTORE BEGIN with variations, the park holding B's: rc 2 9 SAVE B FIRST", CMD.RESTORE, begin(2));
  dev("park -", () => { m.faults.cuePark = -1; });
  rq("RESTORE BEGIN into C", CMD.RESTORE, begin(2));
  dev("advance 2000", () => m.advance(2000));
  rq("GET while restoring (keeps it alive)", CMD.GET, { scope: 1, track: 0, id: G.BPM, count: 1 });
  dev("advance 2000", () => m.advance(2000));
  set("SET while restoring: rc 6 40", 0, 0, P.CHOKE, 1);
  set("SET read-only while restoring: rc 1 45", 1, 0, G.INFO, 1);
  rq("STEPS_SET while restoring: rc 6 40", CMD.STEPS_SET, { slot: NONE, var: 0, track: 0, first: 0, steps: [{ flags: 1, cond: 0, note: 60 }] });
  pieces("restore C");
  dev("otarace", () => { m.faults.updateAtCommit = true; });
  rq("COMMIT with the upgrade command pending: rc 6 0, dropped", CMD.RESTORE, { op: RS.COMMIT });
  rq("COMMIT again: rc 1 41 (no restore)", CMD.RESTORE, { op: RS.COMMIT });
  rq("RESTORE BEGIN into D", CMD.RESTORE, begin(3));
  raw("a DAW's identity request (F0 7E) while restoring: not answered, not update traffic", [0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7]);
  raw("another maker's SysEx (F0 00 20 29) while restoring: the same", [0xf0, 0x00, 0x20, 0x29, 0x02, 0x0c, 0x0e, 0x01, 0xf7]);
  rq("GET after them: answered (no silence)", CMD.GET, { scope: 1, track: 0, id: G.BPM, count: 1 });
  pieces("restore D");
  rq("COMMIT into D", CMD.RESTORE, { op: RS.COMMIT });
  rq("STEPS_GET D variation 2", CMD.STEPS_GET, { slot: 3, var: 1, track: 0 });
  rq("RESTORE BEGIN into C: an FDRF project (a file from the editor before NUDGE) with its parts", CMD.RESTORE, begin(2, fparts));
  pieces("restore C (FDRF)", fparts);
  rq("COMMIT into C (FDRF): written as it is", CMD.RESTORE, { op: RS.COMMIT });
  rq("BACKUP_LIST: C's record OLD, its lock bank and variations linked", CMD.BACKUP_LIST);
  rq("BACKUP_GET C's record: the FDRF bytes (its storage seq 2: written twice)", CMD.BACKUP_GET, { obj: 3, offset: 0, seq: 2 });
  rq("STEPS_GET C variation 1: converted at load, NUDGE 0", CMD.STEPS_GET, { slot: 2, var: 0, track: 2 });
  rq("STEPS_GET C variation 5: its variations (NUDGE kept)", CMD.STEPS_GET, { slot: 2, var: 4, track: 2 });
  rq("INFO at the end", CMD.INFO);
  raw("the installer's update handshake (F0 00 32 45 ...): left for the updater, no reply from the editor", UPDATE_HANDSHAKE);
  rq("INFO during the update silence: unanswered", CMD.INFO);
  dev("advance 9990", () => m.advance(9990));
  rq("INFO at 9.99 s: still silent", CMD.INFO);
  dev("advance 10", () => m.advance(10));
  rq("INFO 10 s after the update frame: answered", CMD.INFO);

  for (const [what, frame, , line] of steps) lines.push(line || (frame ? `frame ${hex(frame)}` : what));
  const r = spawnSync(bin, { input: lines.join("\n") + "\n", encoding: "utf8", maxBuffer: 1 << 24 });
  if (r.status !== 0) { console.log(`FAIL  ${bin}: ${r.stderr || r.status}`); return 1; }
  const fw = r.stdout.split("\n").filter(Boolean);
  let bad = 0, i = 0, n = 0, np = 0, acc = [];
  const keep = (frames) => { if (Array.isArray(frames)) for (const f of frames) if (f[5] === SEQ_PUSH) acc.push(hex(f)); };
  const show = (h) => { if (h === "-") return "(no reply)"; try { const d = decodeFrame(Buffer.from(h, "hex")); return JSON.stringify(d, (k, v) => (v instanceof Uint8Array ? `[${v.length} B]` : v)); } catch (e) { return `${h} (${e.message})`; } };
  for (const [what, frame, fn, line] of steps) {
    if (line) { // the pushes: as a set (the firmware's knob order is round robin, the mock's by id)
      keep(m.advance(Number(line.split(" ")[1])));
      keep(m.drain());
      const mp = acc.sort().join(" ") || "-", raw = fw[i++] ?? "(missing)", cp = raw === "-" ? "-" : raw.split(" ").sort().join(" ");
      acc = [];
      np++;
      const showAll = (h) => (h === "-" ? "(none)" : h.split(" ").map(show).join("\n                 "));
      if (process.env.XCHECK_SHOW) console.log(`      ${what}: ${showAll(cp)}`);
      if (mp !== cp) {
        bad++;
        console.log(`FAIL  xcheck ${what}\n        firmware ${showAll(cp)}\n        mock     ${showAll(mp)}`);
      }
      continue;
    }
    if (!frame) { keep(fn()); continue; }
    const all = m.feed(frame), out = all.find((f) => f[5] !== SEQ_PUSH), mh = out ? hex(out) : "-", ch = fw[i++] ?? "(missing)";
    keep(all);
    n++;
    if (process.env.XCHECK_SHOW) console.log(`      ${what}: ${show(ch)}`);
    if (mh !== ch) {
      bad++;
      console.log(`FAIL  xcheck ${what}\n        firmware ${show(ch)}\n        mock     ${show(mh)}`);
    }
  }
  console.log(`${bad ? "FAIL" : "ok  "}  xcheck: ${n} requests and ${np} push checkpoints through the firmware ` +
    `(tests/editor_xcheck.c) and the mock device, ${n + np - bad} the same byte for byte`);
  return bad;
}
