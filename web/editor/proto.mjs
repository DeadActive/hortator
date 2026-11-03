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
export const PROTO = { major: 1, minor: 3, magic: "HORTATOR" }; // minor 1: NUDGE (CAP.NUDGE); 2: UPR2 (CAP.UPR2); 3: SAMPLES
export const SEQ_PUSH = 0x7f, NONE = 0x7f, U14_NONE = 0x3fff, DESC_ORDER = 0x7f;
export const LIMITS = { req: 640, reply: 400, piece: 256, state: 17 };
export const TIMING = {
  timeout: 300, tries: 3, // every request (one in flight)
  longTimeout: 2000, // SAVE, RESTORE COMMIT, the sample writes (flash writes)
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
  SMP_INFO: 21, SMP_BEGIN: 22, SMP_DATA: 23, SMP_END: 24, SMP_READ: 25, SMP_CLEAR: 26, SMP_RENAME: 27, // minor 3 (§2.7)
};
export const PUSH = { CHANGED: 0x40, STATE: 0x41, STEPS_CHANGED: 0x42, LOCKS_CHANGED: 0x43 };
export const RC = { OK: 0, BAD: 1, REFUSED: 2, STOP: 3, FLASH: 4, STALE: 5, BUSY: 6 };
export const DETAIL = {
  NONE: 0, CUE_FULL: 1, CUE_SONG: 2, CUE_NOSLOT: 3, CUE_EMPTY: 4, CUE_PARK: 8, // CUE_PARK + slot
  EMPTY_SLOT: 16, PRESET_EMPTY: 17, ACID_CAP: 18, NOT_LOCKABLE: 19, LOCKS_FULL: 20, VARS_FULL: 21, NO_SLOT: 22,
  NEWER: 23, BAD_OBJECT: 24, CRC: 25, INVALID: 26, INCOMPLETE: 27, NO_FLASH: 28, BLOCKED: 29, NOT_PRESENT: 30,
  TOO_BIG: 31, CUE_FIRST: 32, CHANGED: 33, ORDER: 34, REFUSED_DATA: 35, RESTORING: 40, NOT_RESTORING: 41, STOP: 42,
  UNKNOWN: 43, LENGTH: 44, RANGE: 45, NO_CAP: 46,
};
export const CAP = { LIVE: 0x01, PRESETS: 0x02, PATTERNS: 0x04, BACKUP: 0x08, RESTORE: 0x10, NUDGE: 0x20, UPR2: 0x40, SAMPLES: 0x80 };
export const CMD_CAP = {
  [CMD.INFO]: 0, [CMD.PAGES]: CAP.LIVE, [CMD.DESC]: CAP.LIVE, [CMD.GET]: CAP.LIVE, [CMD.SET]: CAP.LIVE,
  [CMD.DUMP]: CAP.LIVE, [CMD.SELECT]: CAP.LIVE, [CMD.WATCH]: CAP.LIVE, [CMD.TEXT]: CAP.LIVE,
  [CMD.NAMES]: CAP.PRESETS, [CMD.PRESET_LOAD]: CAP.PRESETS,
  [CMD.STEPS_GET]: CAP.PATTERNS, [CMD.STEPS_SET]: CAP.PATTERNS, [CMD.LOCKS_GET]: CAP.PATTERNS,
  [CMD.LOCK_EDIT]: CAP.PATTERNS, [CMD.CUE]: CAP.PATTERNS, [CMD.SAVE]: CAP.PATTERNS,
  [CMD.BACKUP_LIST]: CAP.BACKUP, [CMD.BACKUP_GET]: CAP.BACKUP, [CMD.RESTORE]: CAP.RESTORE,
  [CMD.SMP_INFO]: CAP.SAMPLES, [CMD.SMP_BEGIN]: CAP.SAMPLES, [CMD.SMP_DATA]: CAP.SAMPLES, [CMD.SMP_END]: CAP.SAMPLES,
  [CMD.SMP_READ]: CAP.SAMPLES, [CMD.SMP_CLEAR]: CAP.SAMPLES, [CMD.SMP_RENAME]: CAP.SAMPLES,
};
/** commands that change the device: a repeated (cmd, seq) is not executed twice (the duplicate rule) */
export const WRITES = new Set([CMD.SET, CMD.SELECT, CMD.PRESET_LOAD, CMD.STEPS_SET, CMD.LOCK_EDIT, CMD.CUE, CMD.SAVE,
  CMD.RESTORE, CMD.SMP_BEGIN, CMD.SMP_DATA, CMD.SMP_END, CMD.SMP_CLEAR, CMD.SMP_RENAME]);
/** TIMING.longTimeout (+ RESTORE COMMIT): flash writes */
export const LONG = new Set([CMD.SAVE, CMD.SMP_BEGIN, CMD.SMP_DATA, CMD.SMP_END, CMD.SMP_CLEAR, CMD.SMP_RENAME]);
export const SCOPE = { TRACK: 0, GLOBAL: 1, MODEL: 2 };
/** the core.h ids the page names (protocol 1: ids are the firmware's enums, §2.2; test_editor.mjs checks them against
 * device_data.json): P_MODEL P_RMODEL P_ITYPE (scope 0); G_GMODE G_SYNC (scope 1). Everything else comes from PAGES /
 * DESC */
export const ID = { MODEL: 0, RMODEL: 48, ITYPE: 71, GMODE: 22, SYNC: 12 };
export const GR = { LFO: 10 }; // PAGES graph: the LFO pages (columns 1-2 LFO 1, 3-4 LFO 2)
export const DF = { SPLIT: 0x01, ORDER: 0x02, LOCK: 0x04, RO: 0x08, CONTEXT: 0x10 };
export const SF = { RESTORING: 0x01, SONG: 0x02, NOFLASH: 0x04 };
export const STEP = { ON: 0x01, ACC: 0x02, SLIDE: 0x04, TIE: 0x08, RAT_SHIFT: 4, NT_OFFSET: 60, MICRO_OFFSET: 64, MICRO_MAX: 50 };
export const CUE = { SLOT: 0, VAR: 1 };
export const LK = { SET: 0, CLEAR: 1, CLEAR_STEP: 2, CLEAR_TRACK: 3, MOTION: 1, LOCK: 2 };
export const RS = { BEGIN: 0, DATA: 1, COMMIT: 2, ABORT: 3 };
export const RK = { PRESETS: 0, PROJECT: 1 };
export const PART = { RECORD: 0, LOCKS: 1, VARS: 2 };
export const OBJ = { PRESETS: 0, PROJECT: 1, LOCKS: 5, VARS: 9, N: 13 }; // + slot
export const BF = { UNSAVED: 0x01, LOST: 0x02, OLD: 0x04 };
export const SZ = { PRESETS: 3204, PRESETS_V1: 1284, PROJECT: 3840, LOCKS: 3208, VARS_MIN: 22, VARS_MAX: 3584 };
// FDRG FDRF LKB1 VAR1 UPR2; UPR1: the user presets before minor 2 (128 voices)
export const MAGIC = { PROJ: 0x47524446, PROJ_F: 0x46524446, LK: 0x31424b4c, VAR: 0x31524156, UPR: 0x32525055, UPR1: 0x31525055 };
/** a record in the current layout: FDRG, or FDRF (the same bytes, but steps 0..6's spare bytes hold the parameters
 * past the block, no NUDGE; it links a lock bank and variations as FDRG does) */
export const isFullRecord = (m) => m === MAGIC.PROJ || m === MAGIC.PROJ_F;
/** the older project records the firmware converts at load: magic -> size ("FDR1" .. "FDRF") */
export const OLD_PROJECTS = new Map([[0x31524446, 1564], [0x32524446, 2624], [0x33524446, 2656], [0x34524446, 2912],
  [0x35524446, 3024], [0x36524446, 3024], [0x37524446, 3028], [0x38524446, 3544], [0x39524446, 3592],
  [0x41524446, 3672], [0x42524446, 3672], [0x43524446, 3688], [0x44524446, 3712], [0x45524446, 3840], [0x46524446, 3840]]);
/** byte layouts (little endian) of the objects as flash stores them; checked against the firmware by the tests */
export const LAYOUT = {
  ntrk: 8, nstep: 64, pcount: 82, gcount: 42, pStored: 69, gStored: 41, nuser: 96, nuserOld: 128, nvar: 8, lkMax: 1024, nameLen: 12,
  xpDef: [0, 0, 0, 64, 96, 96, 127], // P_RKEY P_SRR P_ITYPE P_IA P_IB P_IC P_IMIX: their power-on values (xp; FDRF: step rsv)
  xp2Def: [0, 0, 40, 0], // P_SATK P_SSUS P_SREL P_SLOOP (the SAMPLE model's ATK SUS REL LOOP): xp2; 0 in every record before them
  xp3Def: [0, 0], // P_SPLAY P_SCUT (the SAMPLE model's PLAY: KEY 0, CHOP 1; CUT OFF 0, ON 1): xp3; 0 before them
  envDef: [0, 0, 40, 0, 0, 0], // a SAMPLE user preset's ATK SUS REL LOOP PLAY CUT: their power-on values
  envMax: [127, 127, 127, 4, 1, 1], // .. their ranges (from 0; LOOP OFF FWD PING LOOPI ONE)
  uprKbit: [3, 1, 2], // the SOUND knobs (DRIVE DECAY TONE) whose bytes' top bits are a SAMPLE preset's bits 23..25
  smpl: 14, // DM_SMPL: the SAMPLE model, the only one whose user presets hold ATK SUS REL LOOP
  uprFenv: 15, // FILTER ENV's place in a user preset's FX (-64..63; the others 0..127)
  timing: [22, 23, 24, 30], // P_SLEN P_SDIV P_SSWING P_SRC: a variation's own timing
  project: { size: 3840, g: 8, sel: 90, rsv: 91, t: 94, trk: 394, trkStep: 138, lk_n: 3246, lk_link: 3248, var_link: 3252,
    var_len: 3256, xp: 3258, xp2: 3314, xp3: 3346, song: 3762, name: 3798, pre: 3810, pre_ed: 3826, sum: 3836 },
  locks: { size: 3208, slot: 4, on: 5, count: 6, key: 8, val: 2056, lock: 3080 },
  vars: { slot: 4, used: 6, d: 8 },
  presets: { size: 3204, slot: 33, fx: 10, mv: 3172, sizeV1: 1284 },
  // the FX a user preset holds (presets.c UPR_FX), in the record's order, and their power-on values (a voice-only slot's)
  uprFx: [14, 15, 16, 17, 48, 49, 50, 51, 52, 53, 54, 69, 55, 56, 57, 58, 59, 71, 72, 73, 74, 75, 70],
  uprFxDef: [0, 0, 0, 0, 0, 48, 72, 64, 100, 0, 64, 0, 0, 127, 0, 0, 40, 0, 64, 96, 96, 127, 0],
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
/* NUDGE (minor 1): each step's micro (-50..+50) as micro + 64, after the steps (["micro", "nudge", "steps"]): in the step
 * objects as `micro`; written when any step has one (a missing one: 0), read when the frame has bytes left */
const NUDGE_F = ["micro", "nudge", "steps"];
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
    ["track", "u7"], ["used", "u7"], ["live", "u7"], ["timing", "u7", 4], ["steps", "step", 64], NUDGE_F] },
  [CMD.STEPS_SET]: { req: [["slot", "u7"], ["var", "u7"], ["track", "u7"], ["first", "u7"], ["steps", ["list", "step"]], NUDGE_F],
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
  /* the user sample slots (minor 3, §2.7): slot 0..2 = USR1..USR3; offsets and lengths in bytes */
  [CMD.SMP_INFO]: { req: [["slot", "u7"]], rep: [["slot", "u7"], ["valid", "u7"], ["name", "str"], ["len", "u35"], ["crc", "u35"],
    ["max", "u35"], ["nz", "u7"], ["n", "u35"], ["rate", "u35"]] },
  [CMD.SMP_BEGIN]: { req: [["slot", "u7"], ["len", "u35"], ["crc", "u35"]], rep: [["slot", "u7"], ["max", "u35"]] },
  [CMD.SMP_DATA]: { req: [["slot", "u7"], ["offset", "u35"], ["data", "pack"]], rep: [["slot", "u7"], ["offset", "u35"], ["len", "u14"]] },
  [CMD.SMP_END]: { req: [["slot", "u7"], ["header", "pack"]], rep: [["slot", "u7"], ["valid", "u7"]] },
  [CMD.SMP_READ]: { req: [["slot", "u7"], ["offset", "u35"], ["crc", "u35"]],
    rep: [["slot", "u7"], ["offset", "u35"], ["len", "u14"], ["data", "pack"]] },
  [CMD.SMP_CLEAR]: { req: [["slot", "u7"]], rep: [["slot", "u7"]] },
  [CMD.SMP_RENAME]: { req: [["slot", "u7"], ["name", "str"]], rep: [["slot", "u7"], ["name", "str"]] },
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
    if (type === "nudge") {
      const st = o[n] || [];
      if (st.some((s) => s && s.micro !== undefined))
        for (const s of st) w.u7(Math.max(-STEP.MICRO_MAX, Math.min(STEP.MICRO_MAX, s.micro | 0)) + STEP.MICRO_OFFSET);
    } else if (n !== undefined) {
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
    if (type === "nudge") { if (r.left()) for (const s of o[n]) s.micro = r.u7() - STEP.MICRO_OFFSET; } else if (n !== undefined) { o[name] = []; for (let i = 0; i < n; i++) o[name].push(get(r, type, o)); } else o[name] = get(r, type, o);
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
    message: "This FM-1 does not answer the editor: it runs Felucca, the stock firmware or a Hortator before 1.0.0 " +
      "(or it is busy updating). Install Hortator 1.0.0 or later to use the editor." };
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
export const stepIsSet = (s) => (s.flags | s.cond | (s.micro | 0)) !== 0 || s.note !== STEP.NT_OFFSET; // varpack.c var_set
export const emptyStep = () => ({ flags: 0, cond: 0, note: STEP.NT_OFFSET, micro: 0 });
const microOf = (s) => Math.max(-STEP.MICRO_MAX, Math.min(STEP.MICRO_MAX, s.micro | 0));

/** the user presets: UPR = the slot's used byte (1: the voice and its FX; 1 | 2: the voice only, a preset moved from
 * UPR1: it loads as a factory preset does) */
export const UPR = { USED: 1, VOICE: 2 };
/** the labels of the FX a user preset holds (LAYOUT.uprFx's order), as the FM-1's pages name them */
export const UPR_FX_LABELS = ["DIST", "CHO", "DLY", "REV", "RESON", "R.TUNE", "R.DECAY", "R.MIX", "R.TONE", "R.STRCT",
  "R.POS", "R.KEYF", "FILTER", "CUT", "RESO", "F.ENV", "F.DECAY", "INSERT", "INS A", "INS B", "INS C", "INS MIX", "SRR"];
export const fxDefaults = () => [...LAYOUT.uprFxDef];
const i8 = (x) => (x << 24) >> 24;
const c8 = (x) => Math.max(-128, Math.min(127, x | 0));
const noMoves = () => Array(LAYOUT.nuserOld - LAYOUT.nuser).fill(0);
/** an older record ("UPR1", 1284 B) <-> 128 slots: null or { model, knobs[8] } */
export function decodePresetsV1(b) {
  if (b.length !== SZ.PRESETS_V1 || dv(b).getUint32(0, true) !== MAGIC.UPR1) throw new Error("user presets: not a UPR1 record");
  return Array.from({ length: LAYOUT.nuserOld }, (_, i) => {
    const o = 4 + i * 10;
    return b[o] === 1 ? { model: b[o + 1], knobs: [...b.subarray(o + 2, o + 10)].map(i8) } : null;
  });
}
export function encodePresetsV1(slots) {
  const b = new Uint8Array(SZ.PRESETS_V1), v = dv(b);
  v.setUint32(0, MAGIC.UPR1, true);
  for (let i = 0; i < LAYOUT.nuserOld; i++) {
    const s = slots[i], o = 4 + i * 10;
    if (!s) continue;
    b[o] = 1;
    b[o + 1] = s.model;
    for (let k = 0; k < 8; k++) v.setInt8(o + 2 + k, c8(s.knobs[k]));
  }
  return b;
}
/** 128 older slots (UPR1's, or a version 1 file's) into the 96 of now, as the firmware moves them at its first start
 * (project.c upr_migrate): slots 1..96 stay, the used ones of 97..128 go into the lowest empty slots (moved: where
 * each went, slot + 1, 0 none: the projects' references follow), the rest are lost (counted). Each the voice only,
 * its FX the power-on values. from[i]: the older slot new slot i came from. nmodels: a slot naming no model is empty */
export function migratePresets(old, nmodels = 128) {
  const slots = Array(LAYOUT.nuser).fill(null), moved = noMoves(), from = Array(LAYOUT.nuser).fill(-1);
  let to = 0, lost = 0;
  old.forEach((s, i) => {
    if (!s || s.model >= nmodels) return;
    let j = i;
    if (i >= LAYOUT.nuser) {
      while (to < LAYOUT.nuser && slots[to]) to++;
      if (to >= LAYOUT.nuser) { lost++; return; }
      j = to;
      moved[i - LAYOUT.nuser] = j + 1;
    }
    slots[j] = { ...s, knobs: [...s.knobs], fx: fxDefaults(), voiceOnly: true };
    from[j] = i;
  });
  return { slots, moved, lost, from };
}
/** the user presets record ("UPR2", 3204 B) -> { slots: 96 x (null | { model, knobs[8], fx[23], voiceOnly }), moved:
 * [32] (UPR1's slots 97..128: new slot + 1, 0 none), lost: 0, old: false, bad: a slot or move the FM-1 refuses }. An
 * older "UPR1" record (1284 B, an FM-1 before minor 2) is moved into 96 slots as the firmware does (migratePresets:
 * old: true, lost: how many did not fit) */
export function decodePresets(b) {
  if (b.length === SZ.PRESETS_V1 && dv(b).getUint32(0, true) === MAGIC.UPR1) return { ...migratePresets(decodePresetsV1(b)), old: true, bad: false };
  if (b.length !== SZ.PRESETS) throw new Error(`user presets: ${b.length} bytes, ${SZ.PRESETS} expected`);
  const v = dv(b), L = LAYOUT.presets;
  if (v.getUint32(0, true) !== MAGIC.UPR) throw new Error("user presets: no UPR2 magic");
  let bad = false;
  const slots = Array.from({ length: LAYOUT.nuser }, (_, i) => {
    const o = 4 + i * L.slot, u = b[o];
    if (u !== UPR.USED && u !== (UPR.USED | UPR.VOICE)) { bad ||= u !== 0; return null; }
    const fb = [...b.subarray(o + L.fx, o + L.fx + LAYOUT.uprFx.length)];
    const kb = [...b.subarray(o + 2, o + 10)], smpl = b[o + 1] === LAYOUT.smpl;
    const hi = LAYOUT.uprKbit.reduce((e, k, j) => e | ((kb[k] >> 7) << (23 + j)), 0);
    if (smpl) LAYOUT.uprKbit.forEach((k) => { kb[k] &= 0x7f; }); // (the bits off DRIVE DECAY TONE)
    const s = { model: b[o + 1], knobs: kb.map(i8), fx: fb.map(fxValue), voiceOnly: u !== UPR.USED };
    if (smpl) s.env = envUnpack((fb.reduce((e, x, k) => e | (fxBit(x, k) << k), 0) | hi) >>> 0); // (SAMPLE only)
    return s;
  });
  const moved = [...b.subarray(L.mv, L.mv + LAYOUT.nuserOld - LAYOUT.nuser)];
  bad ||= moved.some((x) => x > LAYOUT.nuser);
  return { slots, moved, lost: 0, old: false, bad };
}
/** a SAMPLE user preset's ATK SUS REL LOOP PLAY CUT (presets.c upr_env): 26 bits, each value's offset from its power-on
 * value (7, 7, 7, 3, 1, 1 bits), bit k < 23 in FX byte k: the top bit of a 0..127 FX, FILTER ENV's (-64..63) its top bit
 * unlike bit 6; bits 23..25 the top bits of SOUND knobs DRIVE's, DECAY's, TONE's bytes. 0: the defaults (the slot byte for byte as
 * before). fxByte / fxValue: an FX byte with its bit, and back */
const ENV_W = [7, 7, 7, 3, 1, 1];
export function envPack(env) {
  let e = 0, sh = 0;
  ENV_W.forEach((w, i) => { e |= (((env?.[i] ?? LAYOUT.envDef[i]) - LAYOUT.envDef[i]) & ((1 << w) - 1)) << sh; sh += w; });
  return e >>> 0;
}
export function envUnpack(e) {
  let sh = 0;
  return ENV_W.map((w, i) => { const v = (LAYOUT.envDef[i] + (e >>> sh)) & ((1 << w) - 1); sh += w; return Math.min(LAYOUT.envMax[i], v); });
}
export function fxValue(byte, k) { const b = byte & 0x7f; return k === LAYOUT.uprFenv ? (b ^ 0x40) - 0x40 : b; }
export function fxBit(byte, k) { return (k === LAYOUT.uprFenv ? (byte >> 7) ^ (byte >> 6) : byte >> 7) & 1; }
export function fxByte(v, k, bit) {
  const b = (k === LAYOUT.uprFenv ? Math.max(-64, Math.min(63, v)) : Math.max(0, Math.min(127, v))) & 0x7f;
  return b | ((k === LAYOUT.uprFenv ? ((b >> 6) & 1) ^ bit : bit) << 7);
}
/** 96 slots (null | { model, knobs[8], fx[23], voiceOnly, env? [ATK, SUS, REL, LOOP, PLAY, CUT] (SAMPLE only; none: the
 * defaults) }) and the moves -> the UPR2 record. A slot without fx: the FX's power-on values, the voice only */
export function encodePresets(slots, moved = noMoves()) {
  const b = new Uint8Array(SZ.PRESETS), v = dv(b), L = LAYOUT.presets;
  v.setUint32(0, MAGIC.UPR, true);
  for (let i = 0; i < LAYOUT.nuser; i++) {
    const s = slots[i], o = 4 + i * L.slot;
    if (!s) continue;
    const fx = Array.isArray(s.fx) ? s.fx : null;
    b[o] = fx && !s.voiceOnly ? UPR.USED : UPR.USED | UPR.VOICE;
    b[o + 1] = s.model;
    for (let k = 0; k < 8; k++) v.setInt8(o + 2 + k, c8(s.knobs[k]));
    const e = s.model === LAYOUT.smpl ? envPack(s.env) : 0;
    LAYOUT.uprKbit.forEach((k, j) => { if ((e >>> (23 + j)) & 1) b[o + 2 + k] = Math.max(0, Math.min(127, s.knobs[k])) | 0x80; });
    (fx || LAYOUT.uprFxDef).forEach((x, k) => { b[o + L.fx + k] = fxByte(x, k, (e >>> k) & 1); });
  }
  for (let i = 0; i < LAYOUT.nuserOld - LAYOUT.nuser; i++) b[L.mv + i] = Math.max(0, Math.min(LAYOUT.nuser, moved[i] | 0));
  return b;
}

/** a project record ("FDRG", 3840 B; "FDRF" too: the same layout, its parameters past the block in steps 0..6's spare
 * bytes and every NUDGE 0, as the firmware converts it at load (old: true); an older one: only magic, size and sum are
 * read). full: decoded (FDRG / FDRF) */
export function decodeProject(b) {
  const v = dv(b), L = LAYOUT.project, magic = v.getUint32(0, true), size = v.getUint32(4, true);
  const sumOk = b.length >= 8 && fnv1a(b, b.length - 4) === v.getUint32(b.length - 4, true);
  if (!isFullRecord(magic)) {
    const old = OLD_PROJECTS.get(magic);
    return { magic, format: magicText(magic), old: !!old, full: false, ok: !!old && size === b.length && old === b.length && sumOk };
  }
  const f = magic === MAGIC.PROJ_F;
  const ok = b.length === L.size && size === L.size && sumOk;
  const g = [];
  for (let i = 0; i < LAYOUT.gStored; i++) g.push(v.getInt16(L.g + 2 * i, true));
  g.push(b[L.rsv]); // G_DMODE rides in rsv[0]
  const tracks = [];
  for (let k = 0; k < LAYOUT.ntrk; k++) {
    const o = L.t + k * L.trk, p = [], steps = [];
    for (let i = 0; i < LAYOUT.pStored; i++) p.push(v.getInt16(o + 2 * i, true));
    const nx = LAYOUT.xpDef.length, n2 = LAYOUT.xp2Def.length;
    for (let j = 0; j < nx; j++) // proj_xp_get (FDRF: proj_xp_move's source, steps 0..6's spare bytes)
      p.push(LAYOUT.xpDef[j] + v.getInt8(f ? o + L.trkStep + 4 * j + 3 : L.xp + nx * k + j));
    for (let j = 0; j < n2; j++) p.push(LAYOUT.xp2Def[j] + v.getInt8(L.xp2 + n2 * k + j)); // .. xp2 (FDRF: 0)
    LAYOUT.xp3Def.forEach((d, j) => p.push(d + v.getInt8(L.xp3 + LAYOUT.xp3Def.length * k + j))); // .. xp3
    for (let i = 0; i < LAYOUT.nstep; i++) {
      const s = o + L.trkStep + 4 * i;
      steps.push({ flags: b[s] & 0x3f, cond: b[s + 1], note: v.getInt8(s + 2) + STEP.NT_OFFSET,
        micro: f ? 0 : Math.max(-STEP.MICRO_MAX, Math.min(STEP.MICRO_MAX, v.getInt8(s + 3))) });
    }
    tracks.push({ p, steps, pre: v.getUint16(L.pre + 2 * k, true), preEd: b[L.pre_ed + k] });
  }
  let name = "";
  for (let i = 0; i < LAYOUT.nameLen && b[L.name + i]; i++) name += String.fromCharCode(b[L.name + i]);
  if ([...name].some((c) => !NAME_SET.includes(c))) name = "";
  return { magic, format: magicText(magic), old: f, full: true, ok, g, sel: b[L.sel], tracks, name, song: b.slice(L.song, L.song + 36),
    lk_n: v.getUint16(L.lk_n, true), lk_link: v.getUint32(L.lk_link, true), var_link: v.getUint32(L.var_link, true),
    var_len: v.getUint16(L.var_len, true), sum: v.getUint32(L.sum, true) };
}
/** p: { g[42], sel, tracks[8] { p[80], steps[64] (micro: NUDGE), pre (stored reference), preEd }, name, song?, lk_n,
 * lk_link, var_link, var_len } -> 3840 B with its sum; format "FDRF": the record an editor before NUDGE wrote (tests) */
export function encodeProject(p, format = "FDRG") {
  const b = new Uint8Array(SZ.PROJECT), v = dv(b), L = LAYOUT.project, f = format === "FDRF", nx = LAYOUT.xpDef.length;
  const n2 = LAYOUT.xp2Def.length;
  v.setUint32(0, f ? MAGIC.PROJ_F : MAGIC.PROJ, true);
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
      if (!f) v.setInt8(q + 3, microOf(s));
    });
    for (let j = 0; j < nx; j++) // proj_xp_enc
      b[f ? o + L.trkStep + 4 * j + 3 : L.xp + nx * k + j] = (Math.max(-128, Math.min(255, t.p[LAYOUT.pStored + j])) - LAYOUT.xpDef[j]) & 255;
    for (let j = 0; j < n2 && !f; j++) // .. xp2 (an FDRF record: 0 there)
      b[L.xp2 + n2 * k + j] = (Math.max(-128, Math.min(255, t.p[LAYOUT.pStored + nx + j] ?? LAYOUT.xp2Def[j])) - LAYOUT.xp2Def[j]) & 255;
    LAYOUT.xp3Def.forEach((d, j) => { if (!f) b[L.xp3 + LAYOUT.xp3Def.length * k + j] = (Math.max(-128, Math.min(255, t.p[LAYOUT.pStored + nx + n2 + j] ?? d)) - d) & 255; });
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
          const mb = q < o + n ? b[q] >> 6 & 1 : 0; // flags bit 6: a micro byte follows (NUDGE)
          if (q + 3 + mb > o + n) throw new Error("variations: malformed");
          row.push({ flags: b[q] & 0x3f, cond: b[q + 1], note: v.getInt8(q + 2) + STEP.NT_OFFSET,
            micro: mb ? Math.max(-STEP.MICRO_MAX, Math.min(STEP.MICRO_MAX, v.getInt8(q + 3))) : 0 });
          q += 3 + mb;
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
        const m = microOf(s); // varpack.c var_enc: flags bit 6 and the micro byte, only for a nudged step
        tail.push(s.flags & 0x3f | (m ? 0x40 : 0), s.cond, (s.note - STEP.NT_OFFSET) & 255, ...(m ? [m & 255] : []));
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
 * variations' slot bytes, the record's links and sum (FDRG, FDRF: its magic kept). Older records are returned as they are */
export function sealProject({ record, locks, vars }, slot) {
  const r = Uint8Array.from(record), v = dv(r);
  if (!isFullRecord(v.getUint32(0, true))) return { record: r, locks, vars };
  const lk = locks ? Uint8Array.from(locks) : null, vb = vars ? Uint8Array.from(vars) : null;
  if (lk) { lk[LAYOUT.locks.slot] = slot; v.setUint32(LAYOUT.project.lk_link, lockLink(lk), true); }
  if (vb) { vb[LAYOUT.vars.slot] = slot; v.setUint32(LAYOUT.project.var_link, varLink(vb), true); }
  v.setUint32(LAYOUT.project.sum, fnv1a(r, LAYOUT.project.sum), true);
  return { record: r, locks: lk, vars: vb };
}

/* ------------------------------------------------------------------- files --- */
export const FILE_VERSION = 2; // 2: user presets are 96 slots with their FX (UPR2); 1 files are still read
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
const bankOf = (p) => (Array.isArray(p) ? { slots: p, moved: noMoves() } : { slots: p.slots, moved: p.moved || noMoves() });
/** bank: { slots: 96 x (null | { name, model, knobs[8], fx[23], voiceOnly, env? (SAMPLE: ATK SUS REL LOOP) }), moved }
 * (or the slots alone);
 * modelNames: the device's model names (DESC of MODEL) */
export function makePresetsFile(bank, modelNames = [], meta) {
  const { slots, moved } = bankOf(bank);
  return { ...envelope("hortator-presets", meta), slots: presetsBody(slots, modelNames), moved: [...moved] };
}
function presetsBody(slots, modelNames) {
  return Array.from({ length: LAYOUT.nuser }, (_, i) => {
    const s = slots[i];
    if (!s) return null;
    const fx = Array.isArray(s.fx) ? s.fx : fxDefaults();
    return { name: s.name || userPresetName(i, modelNames[s.model] || "USER"), model: s.model,
      modelName: modelNames[s.model] || "", knobs: [...s.knobs], fx: [...fx], ...(s.voiceOnly || !Array.isArray(s.fx) ? { voiceOnly: true } : {}),
      ...(s.model === LAYOUT.smpl && Array.isArray(s.env) ? { env: [...s.env] } : {}) };
  });
}
const int8s = (a, n) => Array.isArray(a) && a.length === n && a.every((k) => Number.isInteger(k) && k >= -128 && k <= 127);
function readSlot(s, withFx) {
  if (s === null) return null;
  if (!s || !Number.isInteger(s.model) || s.model < 0 || s.model > 127 || !int8s(s.knobs, 8)
    || (withFx && s.fx !== undefined && s.fx !== null && !int8s(s.fx, LAYOUT.uprFx.length))) throw new FileError("damaged", "A preset slot is damaged.");
  const fx = withFx && Array.isArray(s.fx) ? [...s.fx] : null;
  if (s.env !== undefined && (!Array.isArray(s.env) || (s.env.length < 4 || s.env.length > 6)
    || !s.env.every((v, i) => Number.isInteger(v) && v >= 0 && v <= LAYOUT.envMax[i]))) throw new FileError("damaged", "A preset slot is damaged.");
  return { name: typeof s.name === "string" ? s.name.slice(0, 24) : "", model: s.model, modelName: s.modelName,
    knobs: [...s.knobs], fx: fx || fxDefaults(), voiceOnly: !fx || s.voiceOnly === true,
    ...(s.model === LAYOUT.smpl ? { env: LAYOUT.envDef.map((d, i) => s.env?.[i] ?? d) } : {}) };
}
/** a file's presets -> { slots (96), moved, lost, raw }. Version 1 (128 voices, before UPR2): moved into 96 slots as
 * the firmware moves an older bank (migratePresets: the voice only; a slot's automatic name follows it to its new
 * slot, a name given stays); raw: the 128 as read (their UPR1 checksum) */
function readPresetBank(a, version, moved) {
  const strip = (s) => s && { name: s.name, model: s.model, knobs: s.knobs, fx: s.fx, voiceOnly: s.voiceOnly, ...(s.env ? { env: s.env } : {}) };
  if (version < 2) {
    if (!Array.isArray(a) || a.length !== LAYOUT.nuserOld) throw new FileError("damaged", "The file needs 128 preset slots.");
    const raw = a.map((s) => readSlot(s, false)), m = migratePresets(raw);
    m.slots.forEach((s, j) => { if (s && s.name === userPresetName(m.from[j], s.modelName || "")) s.name = ""; });
    return { slots: m.slots.map(strip), moved: m.moved, lost: m.lost, raw };
  }
  if (!Array.isArray(a) || a.length !== LAYOUT.nuser) throw new FileError("damaged", `The file needs ${LAYOUT.nuser} preset slots.`);
  const mv = moved === undefined || moved === null ? noMoves() : moved;
  if (!Array.isArray(mv) || mv.length !== LAYOUT.nuserOld - LAYOUT.nuser || !mv.every((x) => Number.isInteger(x) && x >= 0 && x <= LAYOUT.nuser))
    throw new FileError("damaged", "The presets' moves are damaged.");
  return { slots: a.map((s) => strip(readSlot(s, true))), moved: [...mv], lost: 0, raw: null };
}
/** a hortator-presets file -> { slots, moved, lost, firmware }. Its checksum (upr2; upr1 in a version 1 file), when
 * there, must match the record its slots make */
export function readPresetsFile(f) {
  checkEnvelope(f, "hortator-presets");
  const b = readPresetBank(f.slots, f.version, f.moved), sum = f.version < 2 ? f.upr1 : f.upr2;
  if (sum !== undefined) {
    const rec = f.version < 2 ? encodePresetsV1(b.raw) : encodePresets(b.slots, b.moved);
    if (!sum || sum.size !== rec.length || String(sum.crc32).toLowerCase() !== crcHex(crc32(rec)))
      throw new FileError("damaged", "The presets file is damaged (CRC mismatch): nothing was opened.");
  }
  return { slots: b.slots, moved: b.moved, lost: b.lost, firmware: f.firmware };
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
  if (!p.full) return { name: "", models: [], variations: [1], locks: 0, format: p.format };
  const variations = [1];
  if (vars) decodeVars(vars).entries.forEach((e, i) => e && variations.push(i + 2));
  const nl = locks ? decodeLocks(locks).events.filter((e) => e.kind === LK.LOCK).length : 0;
  return { name: p.name, models: p.tracks.map((t) => modelNames[t.p[0]] ?? t.p[0]), variations, locks: nl, format: p.format };
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
  if (!p.ok) throw new FileError(p.old || p.full ? "damaged" : "unknown",
    `${what}: not a Hortator project record (${p.format}).`);
  return { slot: SLOT_NAMES.indexOf(f.slot), ...objs, summary: summarize(objs) };
}
export function readProjectFile(f) {
  checkEnvelope(f, "hortator-project");
  return readProjectBody(f, "Project");
}
/** presets: the bank ({ slots, moved }, or the 96 slots alone); projects: [4] x (null | { record, locks, vars, unsaved }) */
export function makeBackupFile(presets, projects, meta = {}, modelNames = []) {
  const { slots, moved } = bankOf(presets);
  const out = { ...envelope("hortator-backup", meta), presets: { slots: presetsBody(slots, modelNames), moved: [...moved] }, projects: {} };
  SLOT_NAMES.forEach((n, s) => {
    const p = projects[s];
    out.projects[n] = p ? projectBody(p, s, p.unsaved, modelNames) : null;
  });
  return out;
}
/** -> { slots, moved, lost, projects }: a version 1 backup's 128 presets moved into 96 as the firmware does (lost: how
 * many did not fit; its projects' references to slots 97..128 follow them through moved) */
export function readBackupFile(f) {
  checkEnvelope(f, "hortator-backup");
  if (!f.presets || !f.projects) throw new FileError("damaged", "The backup has no presets or projects.");
  const b = readPresetBank(f.presets.slots, f.version, f.presets.moved);
  return { slots: b.slots, moved: b.moved, lost: b.lost,
    projects: SLOT_NAMES.map((n) => (f.projects[n] ? readProjectBody(f.projects[n], `Project ${n}`) : null)) };
}
