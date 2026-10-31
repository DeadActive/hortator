// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// Protocol shape after upstream Felucca's editor protocol (hugelton/Felucca 5d9694d, GPL-3.0-only,
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments).
//
// Hortator editor protocol 1, the page's side (web/EDITOR_PROTOCOL.md is the specification; firmware/src/editor_proto.h
// the firmware's copy of the numbers, web/editor/test_editor.mjs checks they agree): constants, the one table every
// frame is built from, 7-bit packing, CRC-32 / FNV-1a, the stored objects (user presets, project record, lock bank,
// variations) and the three file formats. No DOM, no Web MIDI: the page shell (Link, ports) and the mock device both
// use it.

/* ------------------------------------------------------------------ constants --- */
export const HDR = [0x7d, 0x48, 0x54]; // F0 7D 48 54 <cmd> <seq> ... F7
export const PROTO = { major: 1, minor: 0, magic: "HORTATOR" };
export const SEQ_PUSH = 0x7f, NONE = 0x7f, U14_NONE = 0x3fff, DESC_ORDER = 0x7f;
export const LIMITS = { req: 640, reply: 400, piece: 256, state: 17 };
export const TIMING = {
  timeout: 300, tries: 3, // every request (one in flight)
  longTimeout: 2000, // SAVE, RESTORE COMMIT (flash writes)
  silence: 10000, // update traffic: the device answers nothing this long
  // after a silence: INFO once, then backing off (10 s doubling to 60 s), and no more after 2 min busy (an update may
  // still run: during ota_session the FM-1 holds one SysEx frame, a probe could cost the update a retry): Reconnect
  backoff: 10000, backoffMax: 60000, giveUp: 120000,
  watch: 3000, keepAlive: 1000, // WATCH ends 3 s after the last request; the page sends WATCH 1 every 1 s
  restoreIdle: 3000, dup: 3000,
};
export const CMD = {
  INFO: 1, PAGES: 2, DESC: 3, GET: 4, SET: 5, DUMP: 6, SELECT: 7, WATCH: 8, NAMES: 9, PRESET_LOAD: 10,
  STEPS_GET: 11, STEPS_SET: 12, LOCKS_GET: 13, LOCK_EDIT: 14, CUE: 15, SAVE: 16, BACKUP_LIST: 17, BACKUP_GET: 18,
  RESTORE: 19, TEXT: 20,
};
export const PUSH = { CHANGED: 0x40, STATE: 0x41, STEPS_CHANGED: 0x42, LOCKS_CHANGED: 0x43 };
export const RC = { OK: 0, BAD: 1, REFUSED: 2, STOP: 3, FLASH: 4, STALE: 5, BUSY: 6 };
export const DETAIL = {
  NONE: 0, CUE_FULL: 1, CUE_SONG: 2, CUE_NOSLOT: 3, CUE_EMPTY: 4, CUE_PARK: 8, // CUE_PARK + slot
  EMPTY_SLOT: 16, PRESET_EMPTY: 17, ACID_CAP: 18, NOT_LOCKABLE: 19, LOCKS_FULL: 20, VARS_FULL: 21, NO_SLOT: 22,
  NEWER: 23, BAD_OBJECT: 24, CRC: 25, INVALID: 26, INCOMPLETE: 27, NO_FLASH: 28, BLOCKED: 29, NOT_PRESENT: 30,
  CUE_FIRST: 32, CHANGED: 33, ORDER: 34, RESTORING: 40, NOT_RESTORING: 41, STOP: 42,
  UNKNOWN: 43, LENGTH: 44, RANGE: 45, NO_CAP: 46,
};
export const CAP = { LIVE: 0x01, PRESETS: 0x02, PATTERNS: 0x04, BACKUP: 0x08, RESTORE: 0x10 };
export const CMD_CAP = {
  [CMD.INFO]: 0, [CMD.PAGES]: CAP.LIVE, [CMD.DESC]: CAP.LIVE, [CMD.GET]: CAP.LIVE, [CMD.SET]: CAP.LIVE,
  [CMD.DUMP]: CAP.LIVE, [CMD.SELECT]: CAP.LIVE, [CMD.WATCH]: CAP.LIVE, [CMD.TEXT]: CAP.LIVE,
  [CMD.NAMES]: CAP.PRESETS, [CMD.PRESET_LOAD]: CAP.PRESETS,
  [CMD.STEPS_GET]: CAP.PATTERNS, [CMD.STEPS_SET]: CAP.PATTERNS, [CMD.LOCKS_GET]: CAP.PATTERNS,
  [CMD.LOCK_EDIT]: CAP.PATTERNS, [CMD.CUE]: CAP.PATTERNS, [CMD.SAVE]: CAP.PATTERNS,
  [CMD.BACKUP_LIST]: CAP.BACKUP, [CMD.BACKUP_GET]: CAP.BACKUP, [CMD.RESTORE]: CAP.RESTORE,
};
/** commands that change the device: a repeated (cmd, seq) is not executed twice (the duplicate rule) */
export const WRITES = new Set([CMD.SET, CMD.SELECT, CMD.PRESET_LOAD, CMD.STEPS_SET, CMD.LOCK_EDIT, CMD.CUE, CMD.SAVE,
  CMD.RESTORE]);
export const LONG = new Set([CMD.SAVE]); // + RESTORE COMMIT: TIMING.longTimeout
export const SCOPE = { TRACK: 0, GLOBAL: 1, MODEL: 2 };
export const GR = { LFO: 10 }; // PAGES graph: the LFO pages (columns 1-2 LFO 1, 3-4 LFO 2)
export const DF = { SPLIT: 0x01, ORDER: 0x02, LOCK: 0x04, RO: 0x08, CONTEXT: 0x10 };
export const SF = { RESTORING: 0x01, SONG: 0x02, NOFLASH: 0x04 };
export const STEP = { ON: 0x01, ACC: 0x02, SLIDE: 0x04, TIE: 0x08, RAT_SHIFT: 4, NT_OFFSET: 60 };
export const CUE = { SLOT: 0, VAR: 1 };
export const LK = { SET: 0, CLEAR: 1, CLEAR_STEP: 2, CLEAR_TRACK: 3, MOTION: 1, LOCK: 2 };
export const RS = { BEGIN: 0, DATA: 1, COMMIT: 2, ABORT: 3 };
export const RK = { PRESETS: 0, PROJECT: 1 };
export const PART = { RECORD: 0, LOCKS: 1, VARS: 2 };
export const OBJ = { PRESETS: 0, PROJECT: 1, LOCKS: 5, VARS: 9, N: 13 }; // + slot
export const BF = { UNSAVED: 0x01, LOST: 0x02, OLD: 0x04 };
export const SZ = { PRESETS: 1284, PROJECT: 3840, LOCKS: 3208, VARS_MIN: 22, VARS_MAX: 3584 };
export const MAGIC = { PROJ: 0x46524446, LK: 0x31424b4c, VAR: 0x31524156, UPR: 0x31525055 }; // FDRF LKB1 VAR1 UPR1
/** the older project records the firmware converts at load: magic -> size ("FDR1" .. "FDRE") */
export const OLD_PROJECTS = new Map([[0x31524446, 1564], [0x32524446, 2624], [0x33524446, 2656], [0x34524446, 2912],
  [0x35524446, 3024], [0x36524446, 3024], [0x37524446, 3028], [0x38524446, 3544], [0x39524446, 3592],
  [0x41524446, 3672], [0x42524446, 3672], [0x43524446, 3688], [0x44524446, 3712], [0x45524446, 3840]]);
/** byte layouts (little endian) of the objects as flash stores them; checked against the firmware by the tests */
export const LAYOUT = {
  ntrk: 8, nstep: 64, pcount: 76, gcount: 42, pStored: 69, gStored: 41, nuser: 128, nvar: 8, lkMax: 1024, nameLen: 12,
  xpDef: [0, 0, 0, 64, 96, 96, 127], // P_RKEY P_SRR P_ITYPE P_IA P_IB P_IC P_IMIX: their power-on values (step rsv)
  timing: [22, 23, 24, 30], // P_SLEN P_SDIV P_SSWING P_SRC: a variation's own timing
  project: { size: 3840, g: 8, sel: 90, rsv: 91, t: 94, trk: 394, trkStep: 138, lk_n: 3246, lk_link: 3248, var_link: 3252,
    var_len: 3256, song: 3762, name: 3798, pre: 3810, pre_ed: 3826, sum: 3836 },
  locks: { size: 3208, slot: 4, on: 5, count: 6, key: 8, val: 2056, lock: 3080 },
  vars: { slot: 4, used: 6, d: 8 },
  presets: { size: 1284, slot: 10 },
  preNone: 0xffff, preRecUser: 0x8000,
};
export const NAME_SET = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._/#+";

export class ProtoError extends Error {
  constructor(msg, detail = DETAIL.LENGTH) { super(msg); this.rc = RC.BAD; this.detail = detail; }
}

/* ------------------------------------------------------------ value encoding --- */
export class Writer {
  constructor() { this.b = []; }
  u7(v) { if (!Number.isInteger(v) || v < 0 || v > 127) throw new ProtoError(`u7 ${v}`, DETAIL.RANGE); this.b.push(v); return this; }
  u14(v) { if (!Number.isInteger(v) || v < 0 || v > 0x3fff) throw new ProtoError(`u14 ${v}`, DETAIL.RANGE); this.b.push(v & 127, v >> 7); return this; }
  v14(v) { if (!Number.isInteger(v) || v < -8192 || v > 8191) throw new ProtoError(`v14 ${v}`, DETAIL.RANGE); return this.u14(v + 8192); }
  u35(v) {
    if (!Number.isInteger(v) || v < 0 || v > 0xffffffff) throw new ProtoError(`u35 ${v}`, DETAIL.RANGE);
    for (let i = 0; i < 5; i++) { this.b.push(v % 128); v = Math.floor(v / 128); }
    return this;
  }
  str(s) {
    for (const ch of String(s)) {
      const c = ch.charCodeAt(0);
      if (ch.length !== 1 || c < 32 || c > 126) throw new ProtoError(`string byte ${c}`, DETAIL.RANGE);
      this.b.push(c);
    }
    this.b.push(0);
    return this;
  }
  pack(d) { for (const x of pack7(d)) this.b.push(x); return this; }
}
export class Reader {
  constructor(b, i = 0) { this.b = b; this.i = i; }
  need(n) { if (this.i + n > this.b.length) throw new ProtoError("frame too short"); }
  u7() { this.need(1); return this.b[this.i++]; }
  u14() { this.need(2); const v = this.b[this.i] | this.b[this.i + 1] << 7; this.i += 2; return v; }
  v14() { return this.u14() - 8192; }
  u35() { this.need(5); let v = 0; for (let k = 4; k >= 0; k--) v = v * 128 + this.b[this.i + k]; this.i += 5; return v >>> 0; }
  str() {
    let s = "";
    for (;;) {
      const c = this.u7();
      if (!c) return s;
      if (c < 32 || c > 126) throw new ProtoError(`string byte ${c}`, DETAIL.RANGE);
      s += String.fromCharCode(c);
    }
  }
  pack() { const d = unpack7(this.b.slice(this.i)); this.i = this.b.length; return d; }
  left() { return this.b.length - this.i; }
}

/** pack7 (upstream ed_unpack7's format): groups of up to 7 bytes, each preceded by one byte holding their top bits
 * (bit j = bit 7 of byte j of the group) */
export function pack7(d) {
  const out = [];
  for (let i = 0; i < d.length; i += 7) {
    const n = Math.min(7, d.length - i);
    let hi = 0;
    for (let j = 0; j < n; j++) hi |= (d[i + j] >> 7 & 1) << j;
    out.push(hi);
    for (let j = 0; j < n; j++) out.push(d[i + j] & 127);
  }
  return out;
}
export function unpack7(b) {
  const out = [];
  for (let i = 0; i < b.length; i += 8) {
    const n = Math.min(8, b.length - i);
    if (n < 2) throw new ProtoError("pack7: a group without data");
    const hi = b[i];
    for (let j = 1; j < n; j++) {
      if (b[i + j] > 127 || hi > 127) throw new ProtoError("pack7: byte above 127", DETAIL.RANGE);
      out.push(b[i + j] | (hi >> (j - 1) & 1) << 7);
    }
  }
  return Uint8Array.from(out);
}
export const packedSize = (n) => n + Math.ceil(n / 7);

/* -------------------------------------------------------------------- schema --- */
// A field: [name, type] or [name, type, n] (n of them: an array). Types: u7 u14 v14 u35 str pack, "state", "step",
// ["list", item] (a u7 count, then the items; item: a type or a field list), or a field list (a struct).
// A switch: { on: "op", cases: { 0: [...], 1: [...] } } (the op byte is the first field).
const STATE = [["playing", "u7"], ["slot", "u7"], ["var", "u7"], ["qslot", "u7"], ["qvar", "u7"], ["sel", "u7"],
  ["flags", "u7"], ["used", "u7"], ["dirty", "u7"], ["vmask", "u14", 4]];
const STEP_F = [["flags", "u7"], ["cond", "u7"], ["note", "u7"]];
const VALUE = [["value", "v14"], ["text", "str"], ["unit", "str"]];
const KNOB = [["scope", "u7"], ["track", "u7"], ["id", "u14"]];
const PART_F = [["size", "u14"], ["crc", "u35"]];
export const SCHEMA = {
  [CMD.INFO]: { req: [], rep: [["magic", "str"], ["firmware", "str"], ["major", "u7"], ["minor", "u7"], ["caps", "u14"],
    ["ntrk", "u7"], ["nstep", "u7"], ["pcount", "u7"], ["gcount", "u7"], ["nmodels", "u7"], ["nfact", "u14"],
    ["nuser", "u14"], ["nbanks", "u7"], ["nslot", "u7"], ["nvar", "u7"], ["maxreq", "u14"], ["ble", "u7"],
    ["state", "state"]] },
  [CMD.PAGES]: { req: [["first", "u7"]], rep: [["total", "u7"], ["first", "u7"],
    ["pages", ["list", [["title", "str"], ["scope", "u7"], ["graph", "u7"], ["ids", "u14", 4]]]]] },
  [CMD.DESC]: { req: [...KNOB, ["first", "u7"]], rep: [...KNOB, ["fmt", "u7"], ["flags", "u7"], ["min", "v14"],
    ["max", "v14"], ["def", "v14"], ["label", "str"], ["unit", "str"], ["nnames", "u7"], ["first", "u7"],
    ["items", ["list", (o) => (o.first === DESC_ORDER ? "u7" : "str")]]] },
  [CMD.GET]: { req: [...KNOB, ["count", "u7"]], rep: [...KNOB, ["values", ["list", VALUE]]] },
  [CMD.SET]: { req: [...KNOB, ["value", "v14"]], rep: [...KNOB, ...VALUE] },
  [CMD.DUMP]: { req: [["track", "u7"]], rep: [["track", "u7"], ["model", "u7"], ["preset", "u14"], ["edited", "u7"],
    ["p", ["list", "v14"]], ["g", ["list", "v14"]]] },
  [CMD.SELECT]: { req: [["track", "u7"]], rep: [["track", "u7"]] },
  [CMD.WATCH]: { req: [["on", "u7"]], rep: [["on", "u7"]] },
  [CMD.NAMES]: { req: [["bank", "u7"], ["first", "u7"]], rep: [["bank", "u7"], ["name", "str"], ["total", "u7"],
    ["first", "u7"], ["presets", ["list", [["id", "u14"], ["model", "u7"], ["name", "str"]]]]] },
  [CMD.PRESET_LOAD]: { req: [["track", "u7"], ["id", "u14"]], rep: [["track", "u7"], ["id", "u14"], ["model", "u7"]] },
  [CMD.STEPS_GET]: { req: [["slot", "u7"], ["var", "u7"], ["track", "u7"]], rep: [["slot", "u7"], ["var", "u7"],
    ["track", "u7"], ["used", "u7"], ["live", "u7"], ["timing", "u7", 4], ["steps", "step", 64]] },
  [CMD.STEPS_SET]: { req: [["slot", "u7"], ["var", "u7"], ["track", "u7"], ["first", "u7"], ["steps", ["list", "step"]]],
    rep: [["slot", "u7"], ["var", "u7"], ["track", "u7"], ["first", "u7"], ["count", "u7"]] },
  [CMD.LOCKS_GET]: { req: [["slot", "u7"], ["track", "u7"], ["first", "u14"]], rep: [["slot", "u7"], ["track", "u7"],
    ["total", "u14"], ["first", "u14"], ["locks", ["list", [["step", "u7"], ["param", "u7"], ["kind", "u7"], ["value", "v14"]]]]] },
  [CMD.LOCK_EDIT]: { req: [["op", "u7"], ["track", "u7"], ["step", "u7"], ["param", "u7"], ["value", "v14"]],
    rep: [["op", "u7"], ["track", "u7"], ["step", "u7"], ["param", "u7"], ["value", "v14"], ["count", "u14"]] },
  [CMD.CUE]: { req: [["kind", "u7"], ["n", "u7"]], rep: [["kind", "u7"], ["n", "u7"], ["state", "state"]] },
  [CMD.SAVE]: { req: [["slot", "u7"]], rep: [["slot", "u7"], ["state", "state"]] },
  [CMD.BACKUP_LIST]: { req: [], rep: [["objects", ["list", [["obj", "u7"], ["present", "u7"], ["size", "u14"],
    ["crc", "u35"], ["seq", "u35"], ["flags", "u7"]]]]] },
  [CMD.BACKUP_GET]: { req: [["obj", "u7"], ["offset", "u14"], ["seq", "u35"]],
    rep: [["obj", "u7"], ["offset", "u14"], ["len", "u14"], ["data", "pack"]] },
  [CMD.RESTORE]: {
    req: { on: "op", cases: {
      [RS.BEGIN]: [["kind", "u7"], ["slot", "u7"], ["magic", "u35"], ["parts", PART_F, 3]],
      [RS.DATA]: [["part", "u7"], ["offset", "u14"], ["data", "pack"]],
      [RS.COMMIT]: [], [RS.ABORT]: [] } },
    rep: { on: "op", cases: {
      [RS.BEGIN]: [["kind", "u7"], ["slot", "u7"]],
      [RS.DATA]: [["part", "u7"], ["offset", "u14"], ["len", "u14"]],
      [RS.COMMIT]: [["kind", "u7"], ["slot", "u7"], ["state", "state"]], [RS.ABORT]: [] } },
  },
  [CMD.TEXT]: { req: [...KNOB, ["value", "v14"]], rep: [...KNOB, ...VALUE] },
};
export const PUSH_SCHEMA = {
  [PUSH.CHANGED]: [...KNOB, ...VALUE],
  [PUSH.STATE]: [["state", "state"]],
  [PUSH.STEPS_CHANGED]: [["slot", "u7"], ["var", "u7"], ["track", "u7"]],
  [PUSH.LOCKS_CHANGED]: [["slot", "u7"], ["track", "u7"]],
};
export const NAMES = Object.fromEntries([...Object.entries(CMD), ...Object.entries(PUSH)].map(([k, v]) => [v, k]));

function put(w, type, v, o) {
  if (typeof type === "function") type = type(o);
  if (Array.isArray(type) && type[0] === "list") {
    if (!Array.isArray(v)) throw new ProtoError("list expected", DETAIL.RANGE);
    w.u7(v.length);
    for (const x of v) put(w, type[1], x, o);
  } else if (Array.isArray(type)) putFields(w, type, v);
  else if (type === "state") putFields(w, STATE, v);
  else if (type === "step") putFields(w, STEP_F, v);
  else w[type](v);
}
function putFields(w, fields, o) {
  if (!o || typeof o !== "object") throw new ProtoError("fields expected", DETAIL.RANGE);
  for (const [name, type, n] of fields) {
    if (n !== undefined) {
      if (!o[name] || o[name].length !== n) throw new ProtoError(`${name}: ${n} expected`, DETAIL.RANGE);
      for (const x of o[name]) put(w, type, x, o);
    } else put(w, type, o[name], o);
  }
}
function get(r, type, o) {
  if (typeof type === "function") type = type(o);
  if (Array.isArray(type) && type[0] === "list") {
    const n = r.u7(), a = [];
    for (let i = 0; i < n; i++) a.push(get(r, type[1], o));
    return a;
  }
  if (Array.isArray(type)) return getFields(r, type);
  if (type === "state") return getFields(r, STATE);
  if (type === "step") return getFields(r, STEP_F);
  return r[type]();
}
function getFields(r, fields) {
  const o = {};
  for (const [name, type, n] of fields) {
    if (n !== undefined) { o[name] = []; for (let i = 0; i < n; i++) o[name].push(get(r, type, o)); } else o[name] = get(r, type, o);
  }
  return o;
}
const pick = (spec, o) => (Array.isArray(spec) ? spec : spec.cases[o[spec.on]]);
function encodeBody(w, spec, o) {
  if (!Array.isArray(spec)) {
    const f = spec.cases[o[spec.on]];
    if (!f) throw new ProtoError(`${spec.on} ${o[spec.on]}`, DETAIL.RANGE);
    w.u7(o[spec.on]);
    putFields(w, f, o);
  } else putFields(w, spec, o);
}
function decodeBody(r, spec) {
  if (Array.isArray(spec)) return getFields(r, spec);
  const op = r.u7(), f = spec.cases[op];
  if (!f) throw new ProtoError(`${spec.on} ${op}`, DETAIL.RANGE);
  return { [spec.on]: op, ...getFields(r, f) };
}
const wrap = (b) => Uint8Array.from([0xf0, ...HDR, ...b, 0xf7]);
function strip(frame) {
  const f = frame instanceof Uint8Array ? frame : Uint8Array.from(frame);
  if (f[0] !== 0xf0 || f[f.length - 1] !== 0xf7 || f[1] !== HDR[0] || f[2] !== HDR[1] || f[3] !== HDR[2])
    throw new ProtoError("not a Hortator editor frame");
  for (let i = 1; i < f.length - 1; i++) if (f[i] > 127) throw new ProtoError("data byte above 127", DETAIL.RANGE);
  return f.subarray(4, f.length - 1);
}
export const isEditorFrame = (f) => f.length >= 7 && f[0] === 0xf0 && f[1] === HDR[0] && f[2] === HDR[1] && f[3] === HDR[2]
  && f[f.length - 1] === 0xf7;

/** the ONLY way the page builds a frame: a known command, a request seq 0..126, its schema's fields */
export function encodeRequest(cmd, seq, args = {}) {
  const s = SCHEMA[cmd];
  if (!s) throw new ProtoError(`unknown command ${cmd}`, DETAIL.UNKNOWN);
  if (!Number.isInteger(seq) || seq < 0 || seq >= SEQ_PUSH) throw new ProtoError(`seq ${seq}`, DETAIL.RANGE);
  const w = new Writer().u7(cmd).u7(seq);
  encodeBody(w, s.req, args);
  const f = wrap(w.b);
  if (f.length - 2 > LIMITS.req) throw new ProtoError("request too long");
  return f;
}
export function decodeRequest(frame) { // the device's side (mock, tests)
  const r = new Reader(strip(frame));
  const cmd = r.u7(), seq = r.u7();
  const s = SCHEMA[cmd];
  if (!s) return { cmd, seq, unknown: true };
  const args = decodeBody(r, s.req);
  if (r.left()) throw new ProtoError("frame too long");
  return { cmd, seq, args };
}
/** a reply: rc 0 with its schema's fields, else detail + the device's message */
export function encodeReply(cmd, seq, rc, body = {}) {
  const w = new Writer().u7(cmd).u7(seq).u7(rc);
  if (rc) w.u7(body.detail || 0).str(body.msg || "");
  else encodeBody(w, SCHEMA[cmd].rep, body);
  return wrap(w.b);
}
export function encodePush(cmd, body) {
  const w = new Writer().u7(cmd).u7(SEQ_PUSH);
  putFields(w, PUSH_SCHEMA[cmd], body);
  return wrap(w.b);
}
/** a frame from the device: { push: true, cmd, ...fields } or { cmd, seq, rc, detail, msg, ...fields };
 * an unknown push: { push: true, cmd, unknown: true } (ignore it) */
export function decodeFrame(frame) {
  const r = new Reader(strip(frame));
  const cmd = r.u7(), seq = r.u7();
  if (seq === SEQ_PUSH) {
    if (!PUSH_SCHEMA[cmd]) return { push: true, cmd, unknown: true };
    return { push: true, cmd, ...getFields(r, PUSH_SCHEMA[cmd]) };
  }
  const rc = r.u7();
  if (rc) return { cmd, seq, rc, detail: r.u7(), msg: r.str() };
  if (!SCHEMA[cmd]) throw new ProtoError(`reply to unknown command ${cmd}`, DETAIL.UNKNOWN);
  return { cmd, seq, rc, ...decodeBody(r, SCHEMA[cmd].rep) };
}
export const timeoutOf = (cmd, args = {}) => (LONG.has(cmd) || (cmd === CMD.RESTORE && args.op === RS.COMMIT)
  ? TIMING.longTimeout : TIMING.timeout);

/* --------------------------------------------------------------- detection --- */
/** a MIDI port by its name, only to choose whom to ask: "fm1" (an FM-1 name: ask it INFO), "update" (the loader's
 * update mode: send nothing), null (other gear: asked only when the user searches). The name proves nothing — macOS
 * keeps the name it first saw for a device, so a Hortator FM-1 can show up as "Felucca" — INFO's answer decides. */
export function classifyPort(name = "") {
  if (/Update/i.test(name) && /Felucca|Hortator|FM-1/i.test(name)) return "update";
  if (/BLE|Bluetooth/i.test(name)) return null;
  if (/Hortator|Felucca|FM-1/i.test(name)) return "fm1";
  return null;
}
/** an INFO reply (or null: no reply after the tries) -> { ok } or { ok: false, why, message } */
export function checkInfo(info) {
  if (!info) return { ok: false, why: "noeditor",
    message: "This FM-1 does not answer the editor: it runs Felucca, the stock firmware or a Hortator before 0.28.0 " +
      "(or it is busy updating). Install Hortator 0.28.0 or later to use the editor." };
  if (info.rc || info.magic !== PROTO.magic) return { ok: false, why: "magic", message: "Not a Hortator editor device." };
  if (info.major > PROTO.major) return { ok: false, why: "newer",
    message: `This FM-1 runs a newer Hortator (editor protocol ${info.major}): use the editor from its release.` };
  if (info.major < PROTO.major) return { ok: false, why: "older", message: "Update this FM-1 to use the editor." };
  return { ok: true };
}
export const has = (info, cmd) => ((info.caps & (CMD_CAP[cmd] ?? 0)) === (CMD_CAP[cmd] ?? 0));

/* ------------------------------------------------------------ checksums --- */
const CRC_T = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
  return t;
})();
/** zlib CRC-32 (storage.c st_crc32) */
export function crc32(d, n = d.length) {
  let c = 0xffffffff;
  for (let i = 0; i < n; i++) c = CRC_T[(c ^ d[i]) & 255] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
/** FNV-1a (project.c proj_hash) */
export function fnv1a(d, n = d.length) {
  let h = 2166136261;
  for (let i = 0; i < n; i++) h = Math.imul(h ^ d[i], 16777619) >>> 0;
  return h >>> 0;
}
export const crcHex = (c) => (c >>> 0).toString(16).padStart(8, "0");
const B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
export function toBase64(d) {
  let s = "";
  for (let i = 0; i < d.length; i += 3) {
    const n = d[i] << 16 | (d[i + 1] ?? 0) << 8 | (d[i + 2] ?? 0);
    s += B64[n >> 18 & 63] + B64[n >> 12 & 63] + (i + 1 < d.length ? B64[n >> 6 & 63] : "=") + (i + 2 < d.length ? B64[n & 63] : "=");
  }
  return s;
}
export function fromBase64(s) {
  s = String(s).replace(/\s+/g, "");
  if (s.length % 4 || /[^A-Za-z0-9+/=]/.test(s) || /=[^=]/.test(s)) throw new Error("bad base64");
  const out = [];
  for (let i = 0; i < s.length; i += 4) {
    const v = [0, 1, 2, 3].map((k) => (s[i + k] === "=" ? 0 : B64.indexOf(s[i + k])));
    const n = v[0] << 18 | v[1] << 12 | v[2] << 6 | v[3];
    out.push(n >> 16 & 255);
    if (s[i + 2] !== "=") out.push(n >> 8 & 255);
    if (s[i + 3] !== "=") out.push(n & 255);
  }
  return Uint8Array.from(out);
}

/* ------------------------------------------------------- stored objects --- */
const dv = (b) => new DataView(b.buffer, b.byteOffset, b.byteLength);
export const magicText = (m) => String.fromCharCode(m & 255, m >> 8 & 255, m >> 16 & 255, m >>> 24);
export const stepIsSet = (s) => (s.flags | s.cond) !== 0 || s.note !== STEP.NT_OFFSET;
export const emptyStep = () => ({ flags: 0, cond: 0, note: STEP.NT_OFFSET });

/** the user presets record ("UPR1", 1284 B) <-> 128 slots: null or { model, knobs[8] } (int8) */
export function decodePresets(b) {
  if (b.length !== SZ.PRESETS) throw new Error(`user presets: ${b.length} bytes, ${SZ.PRESETS} expected`);
  const v = dv(b);
  if (v.getUint32(0, true) !== MAGIC.UPR) throw new Error("user presets: no UPR1 magic");
  const slots = [];
  for (let i = 0; i < LAYOUT.nuser; i++) {
    const o = 4 + i * LAYOUT.presets.slot;
    slots.push(b[o] === 1 ? { model: b[o + 1], knobs: [...b.subarray(o + 2, o + 10)].map((x) => (x << 24) >> 24) } : null);
  }
  return slots;
}
export function encodePresets(slots) {
  const b = new Uint8Array(SZ.PRESETS), v = dv(b);
  v.setUint32(0, MAGIC.UPR, true);
  for (let i = 0; i < LAYOUT.nuser; i++) {
    const s = slots[i], o = 4 + i * LAYOUT.presets.slot;
    if (!s) continue;
    b[o] = 1;
    b[o + 1] = s.model;
    for (let k = 0; k < 8; k++) v.setInt8(o + 2 + k, Math.max(-128, Math.min(127, s.knobs[k] | 0)));
  }
  return b;
}

/** a project record ("FDRF", 3840 B; an older one: only magic, size and sum are read) */
export function decodeProject(b) {
  const v = dv(b), L = LAYOUT.project, magic = v.getUint32(0, true), size = v.getUint32(4, true);
  const sumOk = b.length >= 8 && fnv1a(b, b.length - 4) === v.getUint32(b.length - 4, true);
  if (magic !== MAGIC.PROJ) {
    const old = OLD_PROJECTS.get(magic);
    return { magic, format: magicText(magic), old: !!old, ok: !!old && size === b.length && old === b.length && sumOk };
  }
  const ok = b.length === L.size && size === L.size && sumOk;
  const g = [];
  for (let i = 0; i < LAYOUT.gStored; i++) g.push(v.getInt16(L.g + 2 * i, true));
  g.push(b[L.rsv]); // G_DMODE rides in rsv[0]
  const tracks = [];
  for (let k = 0; k < LAYOUT.ntrk; k++) {
    const o = L.t + k * L.trk, p = [], steps = [];
    for (let i = 0; i < LAYOUT.pStored; i++) p.push(v.getInt16(o + 2 * i, true));
    for (let j = 0; j < LAYOUT.pcount - LAYOUT.pStored; j++)
      p.push(LAYOUT.xpDef[j] + v.getInt8(o + L.trkStep + 4 * j + 3)); // proj_xp_get
    for (let i = 0; i < LAYOUT.nstep; i++) {
      const s = o + L.trkStep + 4 * i;
      steps.push({ flags: b[s] & 0x3f, cond: b[s + 1], note: v.getInt8(s + 2) + STEP.NT_OFFSET });
    }
    tracks.push({ p, steps, pre: v.getUint16(L.pre + 2 * k, true), preEd: b[L.pre_ed + k] });
  }
  let name = "";
  for (let i = 0; i < LAYOUT.nameLen && b[L.name + i]; i++) name += String.fromCharCode(b[L.name + i]);
  if ([...name].some((c) => !NAME_SET.includes(c))) name = "";
  return { magic, format: "FDRF", old: false, ok, g, sel: b[L.sel], tracks, name, song: b.slice(L.song, L.song + 36),
    lk_n: v.getUint16(L.lk_n, true), lk_link: v.getUint32(L.lk_link, true), var_link: v.getUint32(L.var_link, true),
    var_len: v.getUint16(L.var_len, true), sum: v.getUint32(L.sum, true) };
}
/** p: { g[42], sel, tracks[8] { p[76], steps[64], pre (stored reference), preEd }, name, song?, lk_n, lk_link,
 * var_link, var_len } -> 3840 B with its sum */
export function encodeProject(p) {
  const b = new Uint8Array(SZ.PROJECT), v = dv(b), L = LAYOUT.project;
  v.setUint32(0, MAGIC.PROJ, true);
  v.setUint32(4, SZ.PROJECT, true);
  for (let i = 0; i < LAYOUT.gStored; i++) v.setInt16(L.g + 2 * i, p.g[i], true);
  b[L.rsv] = p.g[LAYOUT.gStored] & 255;
  b[L.sel] = p.sel;
  p.tracks.forEach((t, k) => {
    const o = L.t + k * L.trk;
    for (let i = 0; i < LAYOUT.pStored; i++) v.setInt16(o + 2 * i, t.p[i], true);
    t.steps.forEach((s, i) => {
      const q = o + L.trkStep + 4 * i;
      b[q] = s.flags & 0x3f;
      b[q + 1] = s.cond;
      v.setInt8(q + 2, s.note - STEP.NT_OFFSET);
    });
    for (let j = 0; j < LAYOUT.pcount - LAYOUT.pStored; j++)
      b[o + L.trkStep + 4 * j + 3] = (Math.max(-128, Math.min(255, t.p[LAYOUT.pStored + j])) - LAYOUT.xpDef[j]) & 255;
    v.setUint16(L.pre + 2 * k, t.pre, true);
    b[L.pre_ed + k] = t.preEd ? 1 : 0;
  });
  v.setUint16(L.lk_n, p.lk_n || 0, true);
  v.setUint32(L.lk_link, p.lk_link || 0, true);
  v.setUint32(L.var_link, p.var_link || 0, true);
  v.setUint16(L.var_len, p.var_len || 0, true);
  if (p.song) b.set(p.song.slice(0, 36), L.song);
  for (let i = 0; i < Math.min(LAYOUT.nameLen, (p.name || "").length); i++) b[L.name + i] = p.name.charCodeAt(i);
  v.setUint32(L.sum, fnv1a(b, L.sum), true);
  return b;
}

/** a lock bank ("LKB1", 3208 B) <-> { slot, on, events: [{ track, step, param, kind (1 motion, 2 lock), value }] } */
export function decodeLocks(b) {
  const v = dv(b), L = LAYOUT.locks;
  if (b.length !== L.size) throw new Error(`lock bank: ${b.length} bytes`);
  const count = v.getUint16(L.count, true), events = [];
  for (let i = 0; i < Math.min(count, LAYOUT.lkMax); i++) {
    const key = v.getUint16(L.key + 2 * i, true);
    events.push({ track: key >> 13, step: key >> 7 & 63, param: key & 127,
      kind: (b[L.lock + (i >> 3)] >> (i & 7) & 1) ? LK.LOCK : LK.MOTION, value: v.getInt8(L.val + i) });
  }
  return { magic: v.getUint32(0, true), slot: b[L.slot], on: b[L.on], count, events };
}
export const lockOrd = (e) => ((e.track << 13 | e.step << 7 | e.param) << 1) | (e.kind === LK.LOCK ? 1 : 0);
export function encodeLocks({ slot, on = 0, events }) {
  const b = new Uint8Array(SZ.LOCKS), v = dv(b), L = LAYOUT.locks, ev = [...events].sort((x, y) => lockOrd(x) - lockOrd(y));
  if (ev.length > LAYOUT.lkMax) throw new Error("lock bank: more than 1024 events");
  v.setUint32(0, MAGIC.LK, true);
  b[L.slot] = slot;
  b[L.on] = on;
  v.setUint16(L.count, ev.length, true);
  ev.forEach((e, i) => {
    v.setUint16(L.key + 2 * i, e.track << 13 | e.step << 7 | e.param, true);
    v.setInt8(L.val + i, e.value);
    if (e.kind === LK.LOCK) b[L.lock + (i >> 3)] |= 1 << (i & 7);
  });
  return b;
}

/** variations 2..8 ("VAR1", 8 + used B) <-> { slot, entries[7]: null (unused) or { timing[8][4], steps[8][64] } } */
export function decodeVars(b) {
  const v = dv(b), L = LAYOUT.vars, used = v.getUint16(L.used, true), entries = [];
  if (b.length !== 8 + used) throw new Error("variations: length != 8 + used");
  let o = L.d;
  for (let e = 1; e < LAYOUT.nvar; e++) {
    if (o + 2 > b.length) throw new Error("variations: malformed");
    const n = v.getUint16(o, true);
    o += 2;
    if (!n) { entries.push(null); continue; }
    if (o + n > b.length || n < 96) throw new Error("variations: malformed");
    const timing = [], steps = [];
    let q = o + 96;
    for (let k = 0; k < LAYOUT.ntrk; k++) {
      const t = o + 12 * k;
      timing.push([b[t], b[t + 1], b[t + 2], b[t + 3]]);
      const row = [];
      for (let i = 0; i < LAYOUT.nstep; i++) {
        if (b[t + 4 + (i >> 3)] >> (i & 7) & 1) {
          if (q + 3 > o + n) throw new Error("variations: malformed");
          row.push({ flags: b[q] & 0x3f, cond: b[q + 1], note: v.getInt8(q + 2) + STEP.NT_OFFSET });
          q += 3;
        } else row.push(emptyStep());
      }
      steps.push(row);
    }
    if (q !== o + n) throw new Error("variations: malformed");
    entries.push({ timing, steps });
    o += n;
  }
  if (o !== b.length) throw new Error("variations: malformed");
  return { magic: v.getUint32(0, true), slot: b[L.slot], used, entries };
}
export function encodeVars({ slot, entries }) {
  const d = [];
  for (let e = 0; e < LAYOUT.nvar - 1; e++) {
    const x = entries[e];
    if (!x) { d.push(0, 0); continue; }
    const body = [], tail = [];
    for (let k = 0; k < LAYOUT.ntrk; k++) {
      body.push(...x.timing[k]);
      const map = [0, 0, 0, 0, 0, 0, 0, 0];
      x.steps[k].forEach((s, i) => {
        if (!stepIsSet(s)) return;
        map[i >> 3] |= 1 << (i & 7);
        tail.push(s.flags & 0x3f, s.cond, (s.note - STEP.NT_OFFSET) & 255);
      });
      body.push(...map);
    }
    const n = body.length + tail.length;
    d.push(n & 255, n >> 8, ...body, ...tail);
  }
  if (d.length > SZ.VARS_MAX - 8) throw new Error("variations: full");
  const b = new Uint8Array(8 + d.length), v = dv(b);
  v.setUint32(0, MAGIC.VAR, true);
  b[LAYOUT.vars.slot] = slot;
  v.setUint16(LAYOUT.vars.used, d.length, true);
  b.set(d, 8);
  return b;
}
export const lockLink = (lk) => (fnv1a(lk) | 1) >>> 0; // project.c proj_link
export const varLink = (vb) => (fnv1a(vb) | 1) >>> 0; // project.c var_link_of (8 + used bytes)
/** a project's three objects sealed for slot (the device does this at RESTORE COMMIT): the lock bank's and the
 * variations' slot bytes, the record's links and sum. Older records are returned as they are */
export function sealProject({ record, locks, vars }, slot) {
  const r = Uint8Array.from(record), v = dv(r);
  if (v.getUint32(0, true) !== MAGIC.PROJ) return { record: r, locks, vars };
  const lk = locks ? Uint8Array.from(locks) : null, vb = vars ? Uint8Array.from(vars) : null;
  if (lk) { lk[LAYOUT.locks.slot] = slot; v.setUint32(LAYOUT.project.lk_link, lockLink(lk), true); }
  if (vb) { vb[LAYOUT.vars.slot] = slot; v.setUint32(LAYOUT.project.var_link, varLink(vb), true); }
  v.setUint32(LAYOUT.project.sum, fnv1a(r, LAYOUT.project.sum), true);
  return { record: r, locks: lk, vars: vb };
}

/* ------------------------------------------------------------------- files --- */
export const FILE_VERSION = 1;
export class FileError extends Error { constructor(why, msg) { super(msg); this.why = why; } }
const envelope = (format, meta = {}) => ({ format, version: FILE_VERSION, firmware: meta.firmware || "", protocol: PROTO.major,
  created: meta.created || new Date().toISOString() });
function checkEnvelope(f, format) {
  if (!f || typeof f !== "object" || f.format !== format) throw new FileError("format", `Not a ${format} file.`);
  if (!Number.isInteger(f.version) || f.version < 1) throw new FileError("format", "No file version.");
  if (f.version > FILE_VERSION) throw new FileError("newer", "This file was made by a newer editor.");
}
const SLOT_NAMES = ["A", "B", "C", "D"];
/** the device's automatic name of a user slot (presets.c upr_name): "K808 01", "SDTRX128" */
export function userPresetName(slot, modelName) {
  const m = modelName.slice(0, 5), nn = slot + 1;
  return m + (nn < 100 || m.length < 5 ? " " : "") + String(nn).padStart(2, "0");
}
/** slots: 128 x (null | { name, model, knobs[8] }); modelNames: the device's model names (DESC of MODEL) */
export function makePresetsFile(slots, modelNames = [], meta) {
  return { ...envelope("hortator-presets", meta), slots: presetsBody(slots, modelNames) };
}
function presetsBody(slots, modelNames) {
  return Array.from({ length: LAYOUT.nuser }, (_, i) => {
    const s = slots[i];
    return s ? { name: s.name || userPresetName(i, modelNames[s.model] || "USER"), model: s.model,
      modelName: modelNames[s.model] || "", knobs: [...s.knobs] } : null;
  });
}
function readPresetSlots(a) {
  if (!Array.isArray(a) || a.length !== LAYOUT.nuser) throw new FileError("damaged", "The file needs 128 preset slots.");
  return a.map((s) => {
    if (s === null) return null;
    if (!s || !Number.isInteger(s.model) || s.model < 0 || s.model > 127 || !Array.isArray(s.knobs) || s.knobs.length !== 8
      || !s.knobs.every((k) => Number.isInteger(k) && k >= -128 && k <= 127)) throw new FileError("damaged", "A preset slot is damaged.");
    return { name: typeof s.name === "string" ? s.name.slice(0, 24) : "", model: s.model, knobs: [...s.knobs] };
  });
}
export function readPresetsFile(f) {
  checkEnvelope(f, "hortator-presets");
  return { slots: readPresetSlots(f.slots), firmware: f.firmware };
}
const objOut = (b, seq) => (b ? { size: b.length, crc32: crcHex(crc32(b)), ...(seq !== undefined ? { seq } : {}), data: toBase64(b) } : null);
function objIn(o, what) {
  if (o === null || o === undefined) return null;
  let b;
  try { b = fromBase64(o.data); } catch { throw new FileError("damaged", `${what}: damaged data.`); }
  if (b.length !== o.size || crcHex(crc32(b)) !== String(o.crc32).toLowerCase()) throw new FileError("damaged", `${what}: CRC or size mismatch.`);
  return b;
}
/** a readable summary of a project's objects (never trusted on read: recomputed) */
export function summarize({ record, locks, vars }, modelNames = []) {
  const p = decodeProject(record);
  if (p.format !== "FDRF") return { name: "", models: [], variations: [1], locks: 0, format: p.format };
  const variations = [1];
  if (vars) decodeVars(vars).entries.forEach((e, i) => e && variations.push(i + 2));
  const nl = locks ? decodeLocks(locks).events.filter((e) => e.kind === LK.LOCK).length : 0;
  return { name: p.name, models: p.tracks.map((t) => modelNames[t.p[0]] ?? t.p[0]), variations, locks: nl, format: "FDRF" };
}
/** objs: { record, locks, vars } (Uint8Array | null) as BACKUP_GET read them; slot 0..3; unsaved: the device's flag */
export function makeProjectFile(objs, slot, meta = {}, modelNames = []) {
  return { ...envelope("hortator-project", meta), ...projectBody(objs, slot, meta.unsaved, modelNames) };
}
function projectBody(objs, slot, unsaved, modelNames) {
  return { slot: SLOT_NAMES[slot], summary: { ...summarize(objs, modelNames), unsaved: !!unsaved },
    objects: { record: objOut(objs.record), locks: objOut(objs.locks), variations: objOut(objs.vars) } };
}
function readProjectBody(f, what) {
  const o = f && f.objects;
  if (!o || !o.record) throw new FileError("damaged", `${what}: no project record.`);
  const objs = { record: objIn(o.record, what), locks: objIn(o.locks, what), vars: objIn(o.variations, what) };
  const p = decodeProject(objs.record);
  if (!p.ok) throw new FileError(p.old || p.format === "FDRF" ? "damaged" : "unknown",
    `${what}: not a Hortator project record (${p.format}).`);
  return { slot: SLOT_NAMES.indexOf(f.slot), ...objs, summary: summarize(objs) };
}
export function readProjectFile(f) {
  checkEnvelope(f, "hortator-project");
  return readProjectBody(f, "Project");
}
/** presets: 128 slots; projects: [4] x (null | { record, locks, vars, unsaved }) */
export function makeBackupFile(presets, projects, meta = {}, modelNames = []) {
  const out = { ...envelope("hortator-backup", meta), presets: { slots: presetsBody(presets, modelNames) }, projects: {} };
  SLOT_NAMES.forEach((n, s) => {
    const p = projects[s];
    out.projects[n] = p ? projectBody(p, s, p.unsaved, modelNames) : null;
  });
  return out;
}
export function readBackupFile(f) {
  checkEnvelope(f, "hortator-backup");
  if (!f.presets || !f.projects) throw new FileError("damaged", "The backup has no presets or projects.");
  return { slots: readPresetSlots(f.presets.slots),
    projects: SLOT_NAMES.map((n) => (f.projects[n] ? readProjectBody(f.projects[n], `Project ${n}`) : null)) };
}
