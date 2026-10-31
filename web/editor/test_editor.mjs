// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
//
// Hortator editor protocol 1 (web/EDITOR_PROTOCOL.md): node checks, no browser, no hardware. From the repo root:
//   node web/editor/test_editor.mjs
// - the copies agree: firmware/src/editor_proto.h, proto.mjs, the doc's tables, device_data.json (= the firmware's
//   tables now: tools/gen_editor_data.sh --check, when a C compiler is there)
// - the codec: pack7, the value types, every command's request and reply round trip, the frame whitelist
// - the objects and files: byte-exact round trips, re-sealing, refusals of newer / damaged / foreign files
// - the mock device: every command and its refusals, pushes, cue, backup, restore, the update silence, duplicates

import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import * as P from "./proto.mjs";
import { OTA_REQ, UPDATE_HANDSHAKE, createMockDevice, isUpdateTraffic, loadDeviceData } from "./mock_device.mjs";

const { CMD, PUSH, RC, DETAIL, CAP, NONE, LK, RS, RK, SCOPE, STEP, U14_NONE } = P;
let failed = 0, passed = 0;
const ok = (cond, what) => { if (cond) passed++; else { failed++; console.log(`FAIL ${what}`); } };
const eqb = (a, b) => !!a && !!b && a.length === b.length && a.every((x, i) => x === b[i]);
const eqn = (a, b) => (!a && !b) || eqb(a, b); // (null = null)
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const HERE = new URL(".", import.meta.url).pathname, ROOT = join(HERE, "../..");
const data = await loadDeviceData();
const fresh = (o) => createMockDevice(data, o);

/* ------------------------------------------------------------ the copies agree --- */
{
  const h = readFileSync(join(ROOT, "firmware/src/editor_proto.h"), "utf8").replace(/\/\*[\s\S]*?\*\//g, "");
  const E = {};
  for (const m of h.matchAll(/enum\s*\{([^}]*)\}/g)) {
    let v = -1;
    for (const part of m[1].split(",").map((s) => s.trim()).filter(Boolean)) {
      const [n, x] = part.split("=").map((s) => s.trim());
      v = x !== undefined ? (/^ED_/.test(x.split(/\s/)[0]) ? E[x.split(/\s/)[0]] + Number(x.split("+")[1] || 0) : Number(x)) : v + 1;
      E[n] = v;
    }
  }
  for (const m of h.matchAll(/#define\s+(ED_\w+)\s+(0x[0-9A-Fa-f]+|\d+)u?\b/g)) E[m[1]] = Number(m[2]);
  const toC = (k) => k.replace(/([a-z])([A-Z])/g, "$1_$2").toUpperCase();
  ok(Object.entries(CMD).every(([k, v]) => E[`ED_${k}`] === v) && E.ED_CMD_N === 21, "header: command ids = proto.mjs CMD");
  ok(Object.entries(PUSH).every(([k, v]) => E[`ED_P_${k}`] === v), "header: push ids");
  const rcName = { OK: "ED_OK", BAD: "ED_E_BAD", REFUSED: "ED_E_REFUSED", STOP: "ED_E_STOP", FLASH: "ED_E_FLASH", STALE: "ED_E_STALE", BUSY: "ED_E_BUSY" };
  ok(Object.entries(RC).every(([k, v]) => E[rcName[k]] === v), "header: rc codes");
  ok(Object.entries(DETAIL).every(([k, v]) => E[`ED_D_${toC(k)}`] === v), "header: detail codes");
  ok(Object.entries(CAP).every(([k, v]) => E[`ED_CAP_${k}`] === v), "header: capability bits");
  ok(Object.entries(P.DF).every(([k, v]) => E[`ED_DF_${k === "RO" ? "RO" : k}`] === v) && E.ED_SF_RESTORING === P.SF.RESTORING
    && E.ED_SF_SONG === P.SF.SONG && E.ED_SF_NOFLASH === P.SF.NOFLASH, "header: DESC / state flags");
  ok(E.ED_H0 === P.HDR[0] && E.ED_H1 === P.HDR[1] && E.ED_H2 === P.HDR[2] && E.ED_PROTO_MAJOR === P.PROTO.major
    && E.ED_PROTO_MINOR === P.PROTO.minor && E.ED_SEQ_PUSH === P.SEQ_PUSH && E.ED_NONE === NONE && E.ED_U14_NONE === U14_NONE
    && E.ED_REQ_MAX === P.LIMITS.req && E.ED_REPLY_MAX === P.LIMITS.reply && E.ED_PIECE === P.LIMITS.piece
    && E.ED_STATE_N === P.LIMITS.state && E.ED_DESC_ORDER === P.DESC_ORDER, "header: framing constants");
  ok(E.ED_SILENCE_MS === P.TIMING.silence && E.ED_WATCH_MS === P.TIMING.watch && E.ED_RESTORE_IDLE_MS === P.TIMING.restoreIdle
    && E.ED_DUP_MS === P.TIMING.dup, "header: timing");
  ok(E.ED_SZ_PRESETS === P.SZ.PRESETS && E.ED_SZ_PROJECT === P.SZ.PROJECT && E.ED_SZ_LOCKS === P.SZ.LOCKS
    && E.ED_SZ_VARS_MIN === P.SZ.VARS_MIN && E.ED_SZ_VARS_MAX === P.SZ.VARS_MAX && E.ED_OBJ_PROJECT === P.OBJ.PROJECT
    && E.ED_OBJ_LOCKS === P.OBJ.LOCKS && E.ED_OBJ_VARS === P.OBJ.VARS && E.ED_OBJ_N === P.OBJ.N, "header: objects and sizes");
  ok(E.ED_STEP_ON === STEP.ON && E.ED_STEP_ACC === STEP.ACC && E.ED_STEP_SLIDE === STEP.SLIDE && E.ED_STEP_TIE === STEP.TIE
    && E.ED_STEP_RAT_SHIFT === STEP.RAT_SHIFT && E.ED_NT_OFFSET === STEP.NT_OFFSET && E.ED_LK_CLEAR_TRACK === LK.CLEAR_TRACK
    && E.ED_RS_ABORT === RS.ABORT && E.ED_RK_PROJECT === RK.PROJECT && E.ED_SC_MODEL === SCOPE.MODEL
    && E.ED_LK_KIND_LOCK === LK.LOCK && E.ED_CUE_VAR === P.CUE.VAR, "header: field enums");
  /* the per-command argument lengths (the device's length check) = what encodeRequest builds */
  const len = (cmd, args) => P.encodeRequest(cmd, 0, args).length - 7;
  const k = { scope: 0, track: 0, id: 0 };
  const L = [[CMD.INFO, {}, "INFO"], [CMD.PAGES, { first: 0 }, "PAGES"], [CMD.DESC, { ...k, first: 0 }, "DESC"],
    [CMD.GET, { ...k, count: 1 }, "GET"], [CMD.SET, { ...k, value: 0 }, "SET"], [CMD.DUMP, { track: 0 }, "DUMP"],
    [CMD.SELECT, { track: 0 }, "SELECT"], [CMD.WATCH, { on: 1 }, "WATCH"], [CMD.NAMES, { bank: 0, first: 0 }, "NAMES"],
    [CMD.PRESET_LOAD, { track: 0, id: 0 }, "PRESET_LOAD"], [CMD.STEPS_GET, { slot: 0, var: 0, track: 0 }, "STEPS_GET"],
    [CMD.LOCKS_GET, { slot: 0, track: 0, first: 0 }, "LOCKS_GET"],
    [CMD.LOCK_EDIT, { op: 0, track: 0, step: 0, param: 0, value: 0 }, "LOCK_EDIT"], [CMD.CUE, { kind: 0, n: 0 }, "CUE"],
    [CMD.SAVE, { slot: 0 }, "SAVE"], [CMD.BACKUP_LIST, {}, "BACKUP_LIST"], [CMD.BACKUP_GET, { obj: 0, offset: 0, seq: 0 }, "BACKUP_GET"],
    [CMD.TEXT, { ...k, value: 0 }, "TEXT"]];
  ok(L.every(([c, a, n]) => len(c, a) === E[`ED_LEN_${n}`]), "header: ED_LEN_* = the request lengths");
  ok(len(CMD.STEPS_SET, { slot: 0, var: 0, track: 0, first: 0, steps: [P.emptyStep()] }) === E.ED_LEN_STEPS_SET_MIN + 3
    && len(CMD.RESTORE, { op: RS.BEGIN, kind: 0, slot: 0, magic: 0, parts: [0, 1, 2].map(() => ({ size: 0, crc: 0 })) }) === E.ED_LEN_RESTORE_BEGIN
    && len(CMD.RESTORE, { op: RS.DATA, part: 0, offset: 0, data: [] }) === E.ED_LEN_RESTORE_DATA_MIN
    && len(CMD.RESTORE, { op: RS.COMMIT }) === E.ED_LEN_RESTORE_OP, "header: variable-length minimums");

  const doc = readFileSync(join(ROOT, "web/EDITOR_PROTOCOL.md"), "utf8");
  const rows = new Map([...doc.matchAll(/^\| (\d+) \| ([A-Z_]+) \| /gm)].map((m) => [m[2], Number(m[1])]));
  ok(Object.entries(CMD).every(([n, v]) => rows.get(n) === v), "doc: the command table = CMD");
  const pushes = new Map([...doc.matchAll(/^\| 0x(4[0-9A-F]) \| ([A-Z_]+) \| /gm)].map((m) => [m[2], parseInt(m[1], 16)]));
  ok(Object.entries(PUSH).every(([n, v]) => pushes.get(n) === v), "doc: the push table = PUSH");
  ok(/Hortator editor protocol 1/.test(doc) && /hugelton\/Felucca/.test(doc) && /DEADACTIVE/.test(doc) && /GPL-3\.0/.test(doc),
    "doc: titled protocol 1, credits upstream Felucca (GPL-3.0) and DEADACTIVE");

  /* proto.mjs's layouts and magics = the firmware's (device_data.json) */
  const lay = data.layout, c = data.consts;
  ok(Object.entries(P.LAYOUT.project).every(([n, v]) => lay.project[n] === v) && Object.entries(P.LAYOUT.locks).every(([n, v]) => lay.locks[n] === v)
    && Object.entries(P.LAYOUT.vars).every(([n, v]) => lay.vars[n] === v) && lay.presets.size === P.LAYOUT.presets.size
    && lay.presets.slot === P.LAYOUT.presets.slot && lay.songSize === 36, "layout: object offsets = the firmware's structs");
  ok(P.LAYOUT.pcount === c.P_COUNT && P.LAYOUT.gcount === c.G_COUNT && P.LAYOUT.pStored === c.P_NSTORED && P.LAYOUT.gStored === c.G_NSTORED
    && P.LAYOUT.nuser === c.PRE_NUSER && P.LAYOUT.lkMax === c.LK_MAX && P.LAYOUT.nameLen === c.NAME_LEN && P.SZ.VARS_MAX === c.VAR_BYTES
    && P.NAME_SET === data.nameSet, "layout: counts = the firmware's");
  ok(P.LAYOUT.xpDef.every((d, j) => data.tp[c.P_NSTORED + j].def === d)
    && same(P.LAYOUT.timing, ["SLEN", "SDIV", "SSWING", "SRC"].map((n) => data.P.indexOf(n))), "layout: the step-byte parameters' defaults, the timing ids");
  ok(P.MAGIC.PROJ === data.magic.PROJ && P.MAGIC.LK === data.magic.LK && P.MAGIC.VAR === data.magic.VAR && P.MAGIC.UPR === data.magic.UPR
    && same([...P.OLD_PROJECTS], data.oldProjects), "layout: magics and the older records' sizes");
  ok(P.LAYOUT.preNone === data.preNone && P.LAYOUT.preRecUser === data.preRecUser && same(data.fmt.slice(0, 3), ["INT", "PCT", "BIPCT"])
    && data.fmt.length === 27, "layout: preset references, the F_ list");
  let gen = "skipped (no cc)";
  try { execFileSync("cc", ["--version"], { stdio: "ignore" }); gen = ""; } catch { /* no compiler */ }
  if (!gen) {
    try { execFileSync("sh", [join(ROOT, "tools/gen_editor_data.sh"), "--check"], { stdio: "pipe" }); gen = "ok"; } catch (e) { gen = String(e.stderr || e); }
    ok(gen === "ok", `device_data.json = tests/editor_ids.c's output now (${gen})`);
  } else console.log(`device_data.json check ${gen}`);
}

/* ------------------------------------------------------------------- the codec --- */
{
  let all = true;
  for (let n = 0; n <= 300; n++) {
    const d = Uint8Array.from({ length: n }, (_, i) => (i * 37 + n * 11) & 255), p = P.pack7(d);
    all &&= p.length === P.packedSize(n) && p.every((x) => x < 128) && eqb(P.unpack7(p), d);
  }
  ok(all && P.pack7(new Uint8Array(256)).length === 293, "pack7: round trips 0..300 bytes, 256 -> 293");
  let bad = 0;
  for (const f of [() => P.unpack7([0]), () => P.unpack7([0, 1, 2, 3, 4, 5, 6, 7, 0]), () => P.unpack7([0, 128])]) try { f(); } catch { bad++; }
  ok(bad === 3, "pack7: a group without data or a byte above 127 is refused");
  const w = new P.Writer().u7(127).u14(16383).v14(-8192).v14(8191).u35(0xffffffff).u35(0).str("A ~");
  const r = new P.Reader(w.b);
  ok(r.u7() === 127 && r.u14() === 16383 && r.v14() === -8192 && r.v14() === 8191 && r.u35() === 0xffffffff && r.u35() === 0
    && r.str() === "A ~" && !r.left(), "values: u7 u14 v14 u35 str round trip at their limits");
  bad = 0;
  for (const f of [() => new P.Writer().u7(128), () => new P.Writer().v14(8192), () => new P.Writer().str("é"), () => new P.Writer().u35(-1)]) try { f(); } catch { bad++; }
  ok(bad === 4, "values: out of range refused");
  ok(P.crc32(new TextEncoder().encode("123456789")) === 0xcbf43926 && P.fnv1a(new TextEncoder().encode("a")) === 0xe40c292c,
    "checksums: CRC-32 and FNV-1a check values");
  const blob = Uint8Array.from({ length: 1000 }, (_, i) => (i * 7) & 255);
  ok([0, 1, 2, 3, 999, 1000].every((n) => P.toBase64(blob.slice(0, n)) === Buffer.from(blob.slice(0, n)).toString("base64")
    && eqb(P.fromBase64(P.toBase64(blob.slice(0, n))), blob.slice(0, n))), "base64: as node's, round trip");
  /* the whitelist: only F0 7D 48 54 frames of known commands with a request seq */
  bad = 0;
  for (const f of [() => P.encodeRequest(0, 0), () => P.encodeRequest(21, 0), () => P.encodeRequest(PUSH.STATE, 0, {}),
    () => P.encodeRequest(CMD.INFO, 127), () => P.encodeRequest(CMD.INFO, -1),
    () => P.encodeRequest(CMD.RESTORE, 0, { op: RS.DATA, part: 0, offset: 0, data: new Uint8Array(600) })]) try { f(); } catch { bad++; }
  ok(bad === 6, "whitelist: no unknown command, push id, bad seq or request over 640 bytes can be built");
}

/* ------------------------------------------------- every command against the mock --- */
const m = fresh();
const seen = []; // every frame the device sent (size and header checked at the end)
const feed0 = m.feed.bind(m);
m.feed = (f) => { const out = feed0(f); seen.push(...out); return out; };
const q = (cmd, args) => m.request(cmd, args);
{
  const info = q(CMD.INFO);
  ok(info.rc === 0 && info.magic === "HORTATOR" && info.major === 1 && info.caps === 0x1f && info.ntrk === 8 && info.pcount === 76
    && info.gcount === 42 && info.nmodels === 45 && info.nfact === 151 && info.nuser === 128 && info.maxreq === 640
    && info.state.slot === 0 && info.state.used === 7 && info.state.dirty === 0 && P.checkInfo(info).ok, "INFO: the device, A loaded, nothing unsaved");
  ok(info.state.vmask[0] === 0b10111 && info.state.vmask[1] === 0b11 && info.state.vmask[3] === 1, "INFO: the variations used per slot");
  ok(P.classifyPort("Hortator") === "fm1" && P.classifyPort("Hortator 8ch MIDI 1") === "fm1" && P.classifyPort("Felucca") === "fm1"
    && P.classifyPort("FM-1") === "fm1" && P.classifyPort("Felucca Update") === "update" && P.classifyPort("FM-1_BLE") === null
    && P.classifyPort("IAC Driver Bus 1") === null, "detect: a port's name only says whom to ask first (FM-1 names), not who answers");
  const newer = fresh({ major: 2 }).request(CMD.INFO), foreign = fresh({ magic: "SOMETHNG" }).request(CMD.INFO), old = fresh({ editor: false });
  ok(P.checkInfo(newer).why === "newer" && P.checkInfo(foreign).why === "magic" && old.request(CMD.INFO) === undefined
    && P.checkInfo(null).why === "noeditor" && P.checkInfo({ ...info, major: 0 }).why === "older", "detect: refuses a newer major, another magic, no editor, an older major");

  const pages = [];
  for (let first = 0; ;) {
    const r = q(CMD.PAGES, { first });
    pages.push(...r.pages);
    first += r.pages.length;
    if (first >= r.total || !r.pages.length) break;
  }
  const t0 = q(CMD.DUMP, { track: NONE });
  ok(pages.length > 20 && pages.filter((p) => /^DEST \d$/.test(p.title)).length === 4 && !pages.some((p) => ["PROJECT", "COPY", "TOOLS", "MOTION", "SONG", "STEP", "TRACKS"].includes(p.title))
    && pages[0].title === "SOUND" && same(pages[0].ids, [0, 1, 2, 3]) && pages.some((p) => p.title === "DLY" && p.ids[0] === data.G.indexOf("DMODE")),
  "PAGES: paged, DEST x 4, no slot / motion / song / tools pages, SOUND list, DLY MODE");
  ok(t0.p.length === 76 && t0.g.length === 42 && t0.p[0] === data.models.findIndex((x) => x.name === "BDTRX"),
    "DUMP: 76 parameters, 42 globals, the model");
  const names = [];
  let d0;
  for (let first = 0; ;) {
    d0 = q(CMD.DESC, { scope: SCOPE.TRACK, track: NONE, id: 0, first });
    names.push(...d0.items);
    first += d0.items.length;
    if (first >= d0.nnames || !d0.items.length) break;
  }
  const ord = q(CMD.DESC, { scope: SCOPE.TRACK, track: NONE, id: 0, first: P.DESC_ORDER });
  ok(d0.label === "MODEL" && d0.nnames === 45 && same(names, data.models.map((x) => x.name)) && (d0.flags & P.DF.ORDER) && same(ord.items, data.modelList),
    "DESC: MODEL's 45 names over two replies, its knob order");
  const div = q(CMD.DESC, { scope: SCOPE.TRACK, track: 0, id: data.P.indexOf("SDIV"), first: P.DESC_ORDER });
  const ldest = q(CMD.DESC, { scope: SCOPE.TRACK, track: 0, id: data.P.indexOf("LFO1_DEST"), first: 0 });
  const e1 = q(CMD.DESC, { scope: SCOPE.TRACK, track: 0, id: 2, first: 0 }), mdl = q(CMD.DESC, { scope: SCOPE.MODEL, track: 0, id: 0, first: 0 });
  const ro = q(CMD.DESC, { scope: SCOPE.GLOBAL, track: 0, id: data.G.indexOf("SLOT"), first: 0 });
  ok(same(div.items, data.tp[data.P.indexOf("SDIV")].order) && ldest.nnames === 19 && ldest.items[0] === "OFF" && ldest.items[11] === "R.TUN"
    && (e1.flags & (P.DF.LOCK | P.DF.CONTEXT)) === (P.DF.LOCK | P.DF.CONTEXT) && mdl.label === data.models[0].edit[0].label
    && (ro.flags & P.DF.RO) && same(ro.items, ["A", "B", "C", "D"]), "DESC: an order, LDEST names, LOCK / CONTEXT / RO flags, scope MODEL");
  const vals = [];
  for (let id = 0; id < 76;) { const r = q(CMD.GET, { scope: 0, track: 0, id, count: 76 - id }); vals.push(...r.values); id += r.values.length; }
  ok(vals.length === 76 && vals.every((v, i) => v.value === t0.p[i]) && vals[0].text === "BDTRX", "GET: all 76 values with their texts, about 25 a reply");
  const lvl = data.P.indexOf("LEVEL"), s1 = q(CMD.SET, { scope: 0, track: 0, id: lvl, value: 500 }), tx = q(CMD.TEXT, { scope: 0, track: 0, id: lvl, value: 3 });
  ok(s1.value === 127 && q(CMD.DUMP, { track: 0 }).p[lvl] === 127 && tx.value === 3 && q(CMD.DUMP, { track: 0 }).p[lvl] === 127,
    "SET: clamped, kept; TEXT changes nothing");
  const r1 = q(CMD.SET, { scope: SCOPE.GLOBAL, track: 0, id: data.G.indexOf("LOAD"), value: 1 });
  ok(r1.rc === RC.BAD && r1.detail === DETAIL.RANGE && r1.msg, "SET: a read-only global -> rc 1");
  m.request(CMD.SET, { scope: 0, track: 1, id: 0, value: data.consts.ACID });
  const acid = q(CMD.SET, { scope: 0, track: 2, id: 0, value: data.consts.ACID });
  ok(acid.rc === 0 && q(CMD.SET, { scope: 0, track: 3, id: 0, value: data.consts.ACID }).detail === DETAIL.ACID_CAP, "SET: MODEL ACID on a third track refused");
  m.request(CMD.SET, { scope: 0, track: 1, id: 0, value: 25 });
  m.request(CMD.SET, { scope: 0, track: 2, id: 0, value: 31 });
  ok(q(CMD.SELECT, { track: 3 }).track === 3 && q(CMD.SELECT, { track: NONE }).track === 3 && q(CMD.SELECT, { track: 8 }).rc === RC.BAD, "SELECT: select, query, range");
  m.request(CMD.SELECT, { track: 0 });

  const banks = [0, 1, 2].map((bank) => {
    const out = [];
    for (let first = 0; ;) { const r = q(CMD.NAMES, { bank, first }); out.push(...r.presets); first += r.presets.length; if (first >= r.total || !r.presets.length) return { r, out }; }
  });
  ok(banks.map((b) => b.out.length).join() === "55,40,56" && banks[0].r.name === "MACHINEDRUM" && banks.flatMap((b) => b.out).every((x, i) => x.id === i && x.name === data.presets[i].name)
    && q(CMD.NAMES, { bank: 3, first: 0 }).rc === RC.BAD, "NAMES: the three factory banks by id, the user bank refused");
  const pl = q(CMD.PRESET_LOAD, { track: 5, id: 60 }), emptyUser = m.upr.findIndex((x) => !x);
  ok(pl.model === data.presets[60].model && q(CMD.DUMP, { track: 5 }).preset === 60
    && q(CMD.PRESET_LOAD, { track: 5, id: 151 + emptyUser }).detail === DETAIL.PRESET_EMPTY, "PRESET_LOAD: factory preset loaded, an empty user slot refused");
}

/* patterns: steps, switch-to-edit, locks, cue, save --------------------------------------------- */
{
  const g = q(CMD.STEPS_GET, { slot: NONE, var: 0, track: 0 });
  ok(g.live === 1 && g.used === 1 && same(g.timing, [16, 2, 0, 0]) && g.steps[0].flags === (STEP.ON | STEP.ACC) && g.steps[1].flags === 0,
    "STEPS_GET: the live steps and timing");
  const v3 = q(CMD.STEPS_GET, { slot: 0, var: 2, track: 2 }), v4 = q(CMD.STEPS_GET, { slot: 0, var: 3, track: 0 });
  ok(v3.live === 0 && v3.used === 1 && v3.steps.slice(0, 16).every((s) => s.flags & STEP.ON) && v4.used === 0 && same(v4.timing, [0, 0, 0, 0])
    && q(CMD.STEPS_GET, { slot: 3, var: 0, track: 0 }).detail === DETAIL.EMPTY_SLOT, "STEPS_GET: another variation, an unused one, an empty slot");
  const st = { flags: STEP.ON | STEP.TIE | (2 << STEP.RAT_SHIFT), cond: 57, note: 67 };
  m.play();
  const no = q(CMD.STEPS_SET, { slot: 0, var: 1, track: 0, first: 3, steps: [st] }), queued = m.state();
  const again = q(CMD.STEPS_SET, { slot: 0, var: 1, track: 0, first: 3, steps: [st] });
  ok(no.rc === RC.STALE && no.detail === DETAIL.CUE_FIRST && queued.var === 0 && queued.qvar === 1 && again.detail === DETAIL.CUE_FIRST
    && !m.q.now, "STEPS_SET playing, another variation: its key pressed (switch queued), rc 5 CUE FIRST; resent early: rc 5 again, no 'at the next step'");
  m.boundary();
  const yes = q(CMD.STEPS_SET, { slot: NONE, var: 1, track: 0, first: 3, steps: [st] });
  m.stop();
  ok(yes.count === 1 && m.state().var === 1 && same(q(CMD.STEPS_GET, { slot: 0, var: 1, track: 0 }).steps[3], st)
    && (m.state().dirty & 1), "STEPS_SET: resent after the switch (STATE push): the edit lands, A unsaved");
  const now = q(CMD.STEPS_SET, { slot: 0, var: 2, track: 7, first: 0, steps: [st] });
  ok(now.rc === RC.OK && m.state().var === 2 && same(q(CMD.STEPS_GET, { slot: NONE, var: 2, track: 7 }).steps[0], st),
    "STEPS_SET stopped, another variation: switched at once (its key), the edit lands, rc 0");
  m.setSong(true);
  const sng = q(CMD.STEPS_SET, { slot: NONE, var: 2, track: 7, first: 0, steps: [st] });
  m.setSong(false);
  ok(sng.rc === RC.REFUSED && sng.detail === DETAIL.CUE_SONG && sng.msg === "STOP TO EDIT", "STEPS_SET while a song plays: rc 2 detail 2 STOP TO EDIT");
  const lk = q(CMD.LOCKS_GET, { slot: NONE, track: 2, first: 0 });
  const set = q(CMD.LOCK_EDIT, { op: LK.SET, track: 2, step: 0, param: 2, value: 500 });
  const mute = q(CMD.LOCK_EDIT, { op: LK.SET, track: 2, step: 0, param: data.P.indexOf("MUTE"), value: 1 });
  ok(lk.total === 8 && lk.locks.every((e) => e.kind === LK.LOCK && e.param === 2) && set.count === 9 && set.value === data.models[m.tracks[2].p[0]].edit[1].max
    && mute.detail === DETAIL.NOT_LOCKABLE, "LOCKS_GET / LOCK_EDIT: list, set (clamped), not lockable");
  ok(q(CMD.LOCK_EDIT, { op: LK.CLEAR, track: 2, step: 0, param: 2, value: 0 }).count === 8
    && q(CMD.LOCK_EDIT, { op: LK.CLEAR_STEP, track: 2, step: 1, param: 0, value: 0 }).count === 7
    && q(CMD.LOCK_EDIT, { op: LK.CLEAR_TRACK, track: 3, step: 0, param: 0, value: 0 }).count === 0
    && q(CMD.LOCKS_GET, { slot: 0, track: 3, first: 0 }).total === 4, "LOCK_EDIT: clear one, a step, a track (MOTION events stay)");
  ok(q(CMD.CUE, { kind: P.CUE.SLOT, n: 3 }).detail === DETAIL.CUE_EMPTY && /SLOT D EMPTY/.test(m.request(CMD.CUE, { kind: 0, n: 3 }).msg),
    "CUE: an empty slot refused with the device's message");
  m.setSong(true);
  const song = q(CMD.CUE, { kind: P.CUE.VAR, n: 0 });
  m.setSong(false);
  m.faults.cueFull = true;
  const full = q(CMD.CUE, { kind: P.CUE.VAR, n: 0 });
  m.faults.cueFull = false;
  m.faults.cuePark = 2;
  const park = q(CMD.CUE, { kind: P.CUE.SLOT, n: 1 });
  m.faults.cuePark = -1;
  ok(song.detail === DETAIL.CUE_SONG && full.detail === DETAIL.CUE_FULL && park.detail === DETAIL.CUE_PARK + 2 && park.msg === "SAVE C FIRST",
    "CUE: SONG, VARIATIONS FULL, SAVE c FIRST refusals");
  ok(q(CMD.SAVE, { slot: NONE }).rc === 0 && !(m.state().dirty & 1), "SAVE: stopped, A saved");
  m.play();
  const qd = q(CMD.CUE, { kind: P.CUE.SLOT, n: 1 });
  ok(qd.state.qslot === 1 && qd.state.slot === 0 && q(CMD.SAVE, { slot: NONE }).rc === RC.STOP, "CUE while playing: queued; SAVE while playing: STOP");
  q(CMD.CUE, { kind: P.CUE.SLOT, n: 1 });
  ok(m.q.now === true, "CUE: the same key again -> at the next step");
  m.boundary();
  ok(m.state().slot === 1 && m.state().qslot === NONE && q(CMD.STEPS_GET, { slot: NONE, var: 0, track: 0 }).timing[0] === 32, "CUE: the switch at the boundary");
  q(CMD.CUE, { kind: P.CUE.VAR, n: 1 });
  q(CMD.CUE, { kind: P.CUE.VAR, n: 0 });
  ok(m.state().qvar === NONE, "CUE: the playing variation's key cancels the queue");
  /* duplicates: the same (cmd, seq) is not done twice */
  const seq = 40;
  m.request(CMD.CUE, { kind: P.CUE.SLOT, n: 2 }, seq);
  m.request(CMD.CUE, { kind: P.CUE.SLOT, n: 2 }, seq);
  const once = m.q.now === false;
  m.request(CMD.CUE, { kind: P.CUE.SLOT, n: 2 }, seq + 1);
  ok(once && m.q.now === true, "duplicates: a retried CUE (same seq) acts once, a new seq acts");
  m.stop();
  m.request(CMD.CUE, { kind: P.CUE.SLOT, n: 0 });
}

/* pushes ------------------------------------------------------------------------- */
{
  q(CMD.WATCH, { on: 1 });
  const fcut = data.P.indexOf("FCUT");
  const pushes = m.turn(m.sel, fcut, 33).map(P.decodeFrame).filter((x) => x.cmd === PUSH.CHANGED);
  ok(pushes.length === 1 && pushes[0].push && pushes[0].cmd === PUSH.CHANGED && pushes[0].id === fcut && pushes[0].value === 33 && pushes[0].text,
    "WATCH: a knob turned on the device -> CHANGED with its text");
  q(CMD.SET, { scope: 0, track: NONE, id: fcut, value: 50 });
  ok(m.pushed.length === 0, "WATCH: the editor's own SET is not echoed");
  const sel = m.select(4).map(P.decodeFrame);
  ok(sel.some((x) => x.cmd === PUSH.STATE && x.state.sel === 4), "WATCH: a selection on the device -> STATE");
  const ed = m.editStep(4, 9, { flags: STEP.ON }).map(P.decodeFrame), lo = m.lockOnDevice(4, 9, 2, 10).map(P.decodeFrame);
  ok(ed.some((x) => x.cmd === PUSH.STEPS_CHANGED && x.track === 4) && lo.some((x) => x.cmd === PUSH.LOCKS_CHANGED && x.track === 4),
    "WATCH: steps / locks changed on the device -> STEPS_CHANGED / LOCKS_CHANGED");
  m.play();
  const sw = m.request(CMD.CUE, { kind: P.CUE.VAR, n: 0 }) && m.boundary().map(P.decodeFrame);
  const sc = sw.filter((x) => x.cmd === PUSH.STEPS_CHANGED);
  ok(sw.some((x) => x.cmd === PUSH.STATE) && sc.length === 1 && sc[0].track === NONE,
    "WATCH: a variation switch -> STATE and one STEPS_CHANGED for several tracks (7F), as editor_pattern.c");
  for (let i = 1; i <= 6; i++) m.tracks[m.sel].p[i] = (m.tracks[m.sel].p[i] + 1) % 100; // six knobs in one pass
  const many = m.poll();
  ok(many.length === 4 && m.poll().length === 2, "WATCH: at most 4 pushes a pass, the rest next pass");
  m.stop();
  m.request(CMD.CUE, { kind: P.CUE.VAR, n: 1 }); // stopped: the switch at once
  const swp = m.pushed;
  ok(swp.some((x) => x.cmd === PUSH.STATE && x.state.var === 1), "WATCH: a stopped CUE's switch -> a STATE push in the same pass");
  m.request(CMD.CUE, { kind: P.CUE.VAR, n: 0 });
  m.drain();
  m.advance(3001);
  ok(m.turn(0, fcut, 70).length === 0, "WATCH: ends 3 s after the last request");
  m.request(CMD.CUE, { kind: P.CUE.VAR, n: 0 });
  m.request(CMD.SELECT, { track: 0 });
}

/* backup, files, restore ------------------------------------------------------------- */
function backupAll(dev) {
  const list = dev.request(CMD.BACKUP_LIST).objects, objs = [];
  for (const o of list) {
    if (!o.present) { objs.push(null); continue; }
    const parts = [];
    for (let off = 0; off < o.size; off += 256) parts.push(...dev.request(CMD.BACKUP_GET, { obj: o.obj, offset: off, seq: o.seq }).data);
    objs.push(Uint8Array.from(parts));
  }
  return { list, objs };
}
function restore(dev, kind, slot, parts) {
  const b = dev.request(CMD.RESTORE, { op: RS.BEGIN, kind, slot, magic: new DataView(parts[0].buffer, parts[0].byteOffset).getUint32(0, true),
    parts: [0, 1, 2].map((i) => (parts[i] ? { size: parts[i].length, crc: P.crc32(parts[i]) } : { size: 0, crc: 0 })) });
  if (b.rc) return b;
  for (let i = 0; i < 3; i++) for (let off = 0; parts[i] && off < parts[i].length; off += 256) {
    const r = dev.request(CMD.RESTORE, { op: RS.DATA, part: i, offset: off, data: parts[i].slice(off, off + 256) });
    if (r.rc) return r;
  }
  return dev.request(CMD.RESTORE, { op: RS.COMMIT });
}
{
  const { list, objs } = backupAll(m);
  ok(list.length === 13 && list.every((o, i) => o.obj === i) && objs.every((b, i) => !b || P.crc32(b) === list[i].crc)
    && !list[4].present && !list[8].present && !list[12].present && list[11].present === 0 && list[9].present === 1,
  "BACKUP: 13 objects listed, read in pieces, CRCs match; D empty, C without variations");
  ok(eqb(objs[0], m.flash.upr.bytes) && eqb(objs[1], m.flash.rec[0].bytes) && eqb(objs[5], m.flash.lk[0].bytes) && eqb(objs[9], m.flash.vr[0].bytes),
    "BACKUP: the bytes are flash's");
  ok(eqb(P.encodePresets(P.decodePresets(objs[0])), objs[0]) && eqb(P.encodeProject(P.decodeProject(objs[1])), objs[1])
    && eqb(P.encodeLocks(P.decodeLocks(objs[5])), objs[5]) && eqb(P.encodeVars(P.decodeVars(objs[9])), objs[9]),
  "objects: presets, record, lock bank, variations decode -> encode byte for byte");
  const stale = m.request(CMD.BACKUP_GET, { obj: 1, offset: 0, seq: list[1].seq + 1 }), absent = m.request(CMD.BACKUP_GET, { obj: 4, offset: 0, seq: 0 });
  ok(stale.rc === RC.STALE && stale.detail === DETAIL.CHANGED && absent.detail === DETAIL.NOT_PRESENT
    && m.request(CMD.BACKUP_GET, { obj: 1, offset: 100, seq: list[1].seq }).rc === RC.BAD, "BACKUP_GET: changed, not present, a bad offset");
  m.request(CMD.SET, { scope: 0, track: 0, id: data.P.indexOf("PAN"), value: 20 });
  ok(m.request(CMD.BACKUP_LIST).objects[1].flags & P.BF.UNSAVED, "BACKUP_LIST: unsaved edits flagged");

  /* files */
  const modelNames = data.models.map((x) => x.name), meta = { firmware: "0.28.0", created: "2026-10-31T00:00:00.000Z" };
  const projects = [0, 1, 2, 3].map((s) => (objs[1 + s] ? { record: objs[1 + s], locks: objs[5 + s], vars: objs[9 + s], unsaved: s === 0 } : null));
  const slots = P.decodePresets(objs[0]);
  const file = JSON.parse(JSON.stringify(makeBackup()));
  function makeBackup() { return P.makeBackupFile(slots, projects, meta, modelNames); }
  const back = P.readBackupFile({ ...file, unknownKey: 1 });
  ok(file.version === 1 && file.format === "hortator-backup" && file.projects.A.summary.name === "TECHNO 1" && same(file.projects.A.summary.variations, [1, 2, 3, 5])
    && file.projects.A.summary.locks === P.decodeLocks(objs[5]).events.filter((e) => e.kind === LK.LOCK).length && file.projects.A.summary.unsaved && file.projects.D === null && file.presets.slots[slots.findIndex(Boolean)].name.endsWith(String(slots.findIndex(Boolean) + 1).padStart(2, "0")),
  "files: backup-all with summaries, device-style names");
  ok(back.projects.every((p, s) => (!p && !projects[s]) || (eqb(p.record, projects[s].record) && eqn(p.locks, projects[s].locks) && eqn(p.vars, projects[s].vars)))
    && eqb(P.encodePresets(back.slots), objs[0]), "files: backup -> JSON -> read: byte-exact, unknown keys ignored");
  const refuseWhy = (f, fn) => { try { fn(f); return "accepted"; } catch (e) { return e.why; } };
  const damaged = JSON.parse(JSON.stringify(file));
  damaged.projects.A.objects.record.crc32 = "00000000";
  ok(refuseWhy({ ...file, version: 2 }, P.readBackupFile) === "newer" && refuseWhy({ ...file, format: "hortator-project" }, P.readBackupFile) === "format"
    && refuseWhy(damaged, P.readBackupFile) === "damaged", "files: newer version, wrong format, damaged CRC refused");
  const pf = JSON.parse(JSON.stringify(P.makeProjectFile(projects[1], 1, meta, modelNames)));
  const rp = P.readProjectFile(pf);
  ok(pf.slot === "B" && rp.slot === 1 && eqb(rp.record, projects[1].record) && refuseWhy({ ...pf, version: 9 }, P.readProjectFile) === "newer",
    "files: a project file round trip");
  /* presets: rename, reorder, delete in the list -> the UPR1 record */
  const pfile = JSON.parse(JSON.stringify(P.makePresetsFile(slots, modelNames, meta)));
  const used = pfile.slots.map((s, i) => (s ? i : -1)).filter((i) => i >= 0);
  pfile.slots[used[0]].name = "MY KICK";
  [pfile.slots[used[1]], pfile.slots[127]] = [pfile.slots[127], pfile.slots[used[1]]];
  pfile.slots[used[2]] = null;
  const edited = P.readPresetsFile(pfile).slots, rec = P.encodePresets(edited), dec = P.decodePresets(rec);
  ok(edited[used[0]].name === "MY KICK" && same(dec[127], slots[used[1]]) && dec[used[2]] === null && same(dec[used[0]], slots[used[0]]),
    "files: presets renamed / reordered / deleted -> the right UPR1 record");
  const wr = restore(m, RK.PRESETS, 0, [rec]);
  ok(wr.rc === 0 && eqb(m.flash.upr.bytes, rec) && same(m.upr[127], slots[used[1]]), "RESTORE presets: the edited list written");

  /* project restore: A -> D (copy to another slot), re-sealed */
  const before = m.request(CMD.BACKUP_LIST).objects;
  const cp = restore(m, RK.PROJECT, 3, [objs[1], objs[5], objs[9]]);
  const after = backupAll(m);
  ok(cp.rc === 0 && cp.slot === 3 && (cp.state.used & 8) && after.objs[4] && after.objs[8][4] === 3 && after.objs[12][4] === 3
    && P.decodeProject(after.objs[4]).ok && P.decodeProject(after.objs[4]).lk_link === P.lockLink(after.objs[8])
    && P.decodeProject(after.objs[4]).var_link === P.varLink(after.objs[12]) && before[4].present === 0,
  "RESTORE project A into D: written, re-sealed (slot bytes, links, sum)");
  ok(same(m.request(CMD.STEPS_GET, { slot: 3, var: 2, track: 2 }).steps, m.request(CMD.STEPS_GET, { slot: 0, var: 2, track: 2 }).steps)
    && m.request(CMD.LOCKS_GET, { slot: 3, track: 2, first: 0 }).total === m.request(CMD.LOCKS_GET, { slot: 0, track: 2, first: 0 }).total,
  "RESTORE: D now plays A's variations and locks");
  /* refusals */
  m.play();
  const pr = restore(m, RK.PROJECT, 3, [objs[2], objs[6], objs[10]]);
  m.stop();
  const newer = objs[1].slice();
  newer[3] = 0x47; // "FDRG"
  const nw = restore(m, RK.PROJECT, 3, [newer]);
  const wrongCrc = m.request(CMD.RESTORE, { op: RS.BEGIN, kind: RK.PROJECT, slot: 3, magic: P.MAGIC.PROJ, parts: [{ size: 3840, crc: 1 }, { size: 0, crc: 0 }, { size: 0, crc: 0 }] });
  for (let off = 0; off < 3840; off += 256) m.request(CMD.RESTORE, { op: RS.DATA, part: 0, offset: off, data: objs[2].slice(off, off + 256) });
  const crcBad = m.request(CMD.RESTORE, { op: RS.COMMIT });
  ok(pr.rc === RC.STOP && nw.rc === RC.REFUSED && nw.detail === DETAIL.NEWER && wrongCrc.rc === 0 && crcBad.detail === DETAIL.CRC,
    "RESTORE: refused while playing, a newer record, a CRC mismatch");
  m.request(CMD.RESTORE, { op: RS.BEGIN, kind: RK.PROJECT, slot: 2, magic: P.MAGIC.PROJ, parts: [{ size: 3840, crc: P.crc32(objs[2]) }, { size: 0, crc: 0 }, { size: 0, crc: 0 }] });
  const order = m.request(CMD.RESTORE, { op: RS.DATA, part: 0, offset: 512, data: objs[2].slice(512, 768) });
  const busy = m.request(CMD.SET, { scope: 0, track: 0, id: 9, value: 10 }), rd = m.request(CMD.DUMP, { track: 0 });
  ok(order.rc === RC.STALE && order.detail === DETAIL.ORDER && busy.rc === RC.BUSY && busy.detail === DETAIL.RESTORING && rd.rc === 0
    && (m.state().flags & P.SF.RESTORING), "RESTORE: out of order, RESTORING refuses writes, reads go on");
  m.request(CMD.RESTORE, { op: RS.DATA, part: 0, offset: 0, data: objs[2].slice(0, 256) });
  ok(m.request(CMD.RESTORE, { op: RS.DATA, part: 0, offset: 0, data: objs[2].slice(0, 256) }).rc === 0, "RESTORE: a retried piece is accepted again");
  m.advance(3001);
  ok(!(m.state().flags & P.SF.RESTORING) && m.request(CMD.RESTORE, { op: RS.COMMIT }).detail === DETAIL.NOT_RESTORING, "RESTORE: 3 s idle -> dropped");
  /* an older record (FDRE) restores as part 0 alone */
  const fdre = objs[1].slice(), dvw = new DataView(fdre.buffer);
  dvw.setUint32(0, 0x45524446, true);
  dvw.setUint32(3836, P.fnv1a(fdre, 3836), true);
  const ol = restore(m, RK.PROJECT, 3, [fdre]);
  ok(ol.rc === 0 && m.request(CMD.BACKUP_LIST).objects[4].flags & P.BF.OLD && !m.request(CMD.BACKUP_LIST).objects[8].present,
    "RESTORE: an older record (FDRE) accepted alone, listed OLD");
}

/* the update path: silence, faults ----------------------------------------------------- */
{
  const d = fresh();
  d.request(CMD.RESTORE, { op: RS.BEGIN, kind: RK.PRESETS, slot: 0, magic: P.MAGIC.UPR, parts: [{ size: 1284, crc: 0 }, { size: 0, crc: 0 }, { size: 0, crc: 0 }] });
  d.updateFrame();
  ok(d.request(CMD.INFO) === undefined && !d.restore, "silence: an update frame -> no reply, the restore dropped");
  d.advance(9000);
  ok(d.request(CMD.INFO) === undefined, "silence: still silent at 9 s");
  d.advance(1001);
  ok(d.request(CMD.INFO).rc === 0, "silence: answers again after 10 s");
  d.otaReq();
  ok(d.request(CMD.INFO) === undefined, "silence: the upgrade command too");
  d.advance(10001);
  d.faults.noReply = 2;
  ok(d.request(CMD.INFO) === undefined && d.request(CMD.INFO) === undefined && d.request(CMD.INFO).rc === 0, "faults: n requests unanswered (timeouts)");
  d.faults.refuse[CMD.SAVE] = { rc: RC.FLASH, msg: "SAVE ERROR" };
  ok(d.request(CMD.SAVE, { slot: NONE }).rc === RC.FLASH && d.request(CMD.SAVE, { slot: NONE }).rc === 0, "faults: a refusal injected once");
  const u = d.feed(Uint8Array.from([0xf0, 0x7d, 0x48, 0x54, 21, 3, 0xf7])).map(P.decodeFrame);
  const caps = fresh({ caps: P.CAP.LIVE }).request(CMD.STEPS_GET, { slot: 0, var: 0, track: 0 });
  ok(u[0].rc === RC.BAD && u[0].detail === DETAIL.UNKNOWN && u[0].seq === 3 && caps.rc === RC.BAD && caps.detail === DETAIL.NO_CAP,
    "unknown command -> rc 1 (43); a missing capability -> rc 1 (46)");
  ok(d.feed(Uint8Array.from([0xf0, 0x7d, 0x46, 0x4c, 1, 0xf7])).length === 0 && !d.silent, "another 7D frame (upstream Felucca's) is ignored, no silence");
  const r = fresh();
  r.request(CMD.RESTORE, { op: RS.BEGIN, kind: RK.PRESETS, slot: 0, magic: P.MAGIC.UPR, parts: [{ size: 1284, crc: 0 }, { size: 0, crc: 0 }, { size: 0, crc: 0 }] });
  const others = [[0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7], [0xf0, 0x7f, 0x7f, 0x04, 0x01, 0x00, 0x40, 0xf7], [0xf0, 0x00, 0x20, 0x29, 0x02, 0xf7],
    [0xf0, 0x00, 0x32, 0x44, 0, 0, 0, 0x40, 0x7f, 0xf7], [0xf0, 0x22, 0x24, 0x35, 0x7d, 0x00, 0xf7]];
  ok(others.every((f) => r.feed(Uint8Array.from(f)).length === 0) && !r.silent && r.restore && r.request(CMD.INFO).rc === 0,
    "other SysEx (a DAW's F0 7E identity request, F0 7F, another maker's, a near miss): no silence, the restore kept");
  const all = Array.from({ length: 32 }, (_, c) => { const f = [...UPDATE_HANDSHAKE]; f[3] = 1 | c << 2; return f; });
  ok(all.every((f) => { const x = fresh(); x.feed(Uint8Array.from(f)); return x.silent; }) && isUpdateTraffic(OTA_REQ)
    && (() => { const x = fresh(); x.otaReq(); return x.silent; })(), "every update frame (wire 00 32 xx, xx & 3 = 1) and the upgrade command silence it");
}

/* Web MIDI access ------------------------------------------------------------------- */
{
  const d = fresh({ name: "Hortator" });
  const a = d.access, inp = [...a.inputs.values()][0], out = [...a.outputs.values()][0];
  const got = await new Promise((res) => { inp.onmidimessage = (e) => res(P.decodeFrame(e.data)); out.send(P.encodeRequest(CMD.INFO, 9)); });
  ok(inp.name === "Hortator" && got.seq === 9 && got.magic === "HORTATOR", "access: a Web MIDI port pair answering asynchronously");
  ok(d.received.every((f) => f[1] === 0x7d && f[2] === 0x48 && f[3] === 0x54), "whitelist: every frame the tests sent through the access is an editor frame");
}

ok(seen.length > 100 && seen.every((f) => f.length <= P.LIMITS.reply && P.isEditorFrame(f)), "every reply and push <= 400 bytes, editor framed");
ok(m.received.every((f) => P.isEditorFrame(f) && f.length - 2 <= P.LIMITS.req), "every request the tests built <= 640 bytes, editor framed");
console.log(failed ? `EDITOR TESTS FAILED (${failed} of ${passed + failed})` : `editor tests passed (${passed})`);
process.exit(failed ? 1 : 0);
