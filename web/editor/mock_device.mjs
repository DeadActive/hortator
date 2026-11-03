// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// Mock device harness after upstream Felucca's editor tests (hugelton/Felucca 5d9694d, GPL-3.0-only,
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments).
//
// A Hortator FM-1 in memory, speaking Hortator editor protocol 1 (web/EDITOR_PROTOCOL.md): the page's tests and its
// offline development run against it. Its tables are the firmware's (device_data.json, tools/gen_editor_data.sh); its
// flash holds the objects byte for byte as the firmware lays them out (proto.mjs). Seeded: 96 user presets (some
// used, most with their FX, some the voice only), projects A-C (D empty) with variations and locks, A loaded, stopped.
// caps without CAP.UPR2: an FM-1 before minor 2 (its user presets "UPR1": 128 voices).
//
//   const m = createMockDevice(await loadDeviceData(), opts)
//   m.access                      a Web MIDI access: one input and one output named "Hortator" (opts.name)
//   m.feed(frame) -> frames       one frame in, what the device answers in that pass (reply, then pushes)
//   m.request(cmd, args) -> obj   encode, feed, decode (tests); m.poll() -> push frames of one main-loop pass
//   device side: m.turn(track, id, v) m.turnGlobal(id, v) m.select(t) m.play() m.stop() m.boundary()
//                m.editStep(track, step, s) m.lockOnDevice(track, step, param, v) m.setSong(on) m.advance(ms)
//   faults:      m.faults.noReply = n (the next n requests unanswered), m.faults.refuse[cmd] = { rc, detail, msg, after }
//                (once, after `after` more requests of cmd),
//                m.faults.flashError, m.faults.cueFull, m.faults.cuePark = slot (the park holds that slot's unsaved
//                variations), m.faults.noFlash (no flash this boot), m.faults.updateAtCommit (the upgrade command lands
//                just before a COMMIT's writes; a sample BEGIN / END's), m.faults.blocked = [1] (USR2's 48 KiB clamp
//                failed this boot; 2: USR3's), m.updateFrame(), m.otaReq()
//   samples:     m.smp.flash[k] (the slot's flash bytes), m.smp.up (the upload running), m.smp.writes (every erase /
//                program: { slot, off, n }), m.seedSample(k, header, data), m.powerCut({ inHeader: header })
//   m.g[G.CLOCK] (following an external clock: BPM stays the clock's) and m.song (a song plays) as the firmware
// tests/editor_wire.mjs runs one request script through this mock and the firmware's (tests/editor_xcheck.c) and
// compares the replies byte for byte.
// opts: name ("Hortator"), editor (false: an FM-1 without the editor, it never answers), firmware ("1.0.0"),
//       major / minor, magic, caps, latency (ms, the access' replies), powerOnKit (no slot loaded), seed
import {
  BF, CAP, CMD, CMD_CAP, CUE, DESC_ORDER, DETAIL, DF, LAYOUT, LIMITS, LK, MAGIC, NONE, OLD_PROJECTS, PROTO, PUSH,
  RC, RK, RS, SCOPE, SEQ_PUSH, STEP, SZ, TIMING, U14_NONE, WRITES, crc32, decodeFrame, decodeLocks,
  decodePresets, decodeProject, decodeRequest, decodeVars, emptyStep, encodeLocks, encodePresets, encodePresetsV1, encodeProject,
  encodePush, encodeReply, encodeRequest, encodeVars, isFullRecord, lockLink, lockOrd, sealProject, varLink,
} from "./proto.mjs";
import { DATA_OFF, HDR_SIZE, SECTOR, SLOT_MAX, SLOT_SIZE, WINDOW, WINDOW_N, crc16, headerProblem, parseHeader } from "./sample.mjs";

export async function loadDeviceData() {
  const inlined = globalThis.HORTATOR_DEVICE_DATA; // the published page carries it (web/make_site.py inlines it)
  if (inlined) return JSON.parse(JSON.stringify(inlined));
  const url = new URL("./device_data.json", import.meta.url);
  if (typeof process !== "undefined" && process.versions && process.versions.node) {
    const { readFile } = await import("node:fs/promises");
    return JSON.parse(await readFile(url, "utf8"));
  }
  return (await fetch(url)).json();
}

const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const clone = (x) => JSON.parse(JSON.stringify(x));
const N_NOTE = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const RO_G = ["MIDI", "INFO", "SLOT", "NAME", "LOAD", "SAVE", "CLRSEQ", "INITSND"];
const MSG = {
  [DETAIL.CUE_FULL]: "VARIATIONS FULL", [DETAIL.CUE_SONG]: "STOP SONG TO CUE", [DETAIL.CUE_NOSLOT]: "SAVE TO A SLOT FIRST",
  [DETAIL.PRESET_EMPTY]: "EMPTY SLOT", [DETAIL.ACID_CAP]: "ACID ON 2 TRACKS", [DETAIL.NOT_LOCKABLE]: "NOT LOCKABLE",
  [DETAIL.LOCKS_FULL]: "LOCKS FULL", [DETAIL.VARS_FULL]: "VARIATIONS FULL", [DETAIL.NO_SLOT]: "SAVE TO A SLOT FIRST",
  [DETAIL.NEWER]: "UPDATE THE FM-1 FIRST", [DETAIL.BAD_OBJECT]: "NOT A HORTATOR OBJECT", [DETAIL.CRC]: "CRC MISMATCH",
  [DETAIL.INVALID]: "INVALID DATA", [DETAIL.INCOMPLETE]: "INCOMPLETE", [DETAIL.NO_FLASH]: "NO FLASH",
  [DETAIL.NOT_PRESENT]: "NO DATA", [DETAIL.CUE_FIRST]: "CUE FIRST", [DETAIL.CHANGED]: "CHANGED: READ AGAIN",
  [DETAIL.ORDER]: "OUT OF ORDER", [DETAIL.RESTORING]: "RESTORING", [DETAIL.NOT_RESTORING]: "NO RESTORE",
  [DETAIL.STOP]: "STOP FIRST", [DETAIL.UNKNOWN]: "UNKNOWN COMMAND", [DETAIL.LENGTH]: "BAD LENGTH",
  [DETAIL.RANGE]: "OUT OF RANGE", [DETAIL.NO_CAP]: "NOT IN THIS FIRMWARE", [DETAIL.TOO_BIG]: "TOO BIG",
  [DETAIL.REFUSED_DATA]: "DATA REFUSED",
};
class Refusal { constructor(rc, detail, msg) { this.rc = rc; this.detail = detail; this.msg = msg ?? MSG[detail] ?? ""; } }
const refuse = (rc, detail, msg) => { throw new Refusal(rc, detail, msg); };
/** the installer's update handshake (web/fm1ota.js HS_QUERY: pack7(00 59 11 00 00 00 FF)) and upgrade command */
export const UPDATE_HANDSHAKE = Object.freeze([0xf0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7f, 0xf7]);
export const OTA_REQ = Object.freeze([0xf0, 0x22, 0x24, 0x35, 0x7f, 0xf7]);
/** update traffic, as editor.c tells it (ed_ota_frame, usb.ota_req): an update frame's packed header (wire 00 32, then
 * a byte whose low two bits are 01: ota.c decodes 00 59), or the upgrade command. Other SysEx is not. */
export function isUpdateTraffic(f) {
  if (f.length === OTA_REQ.length && OTA_REQ.every((x, i) => f[i] === x)) return true;
  return f.length >= 5 && f[1] === 0x00 && f[2] === 0x32 && (f[3] & 3) === 1;
}

function mulberry32(a) {
  return () => {
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export function createMockDevice(data, opts = {}) { return new MockDevice(data, opts); }

export class MockDevice {
  constructor(data, opts = {}) {
    this.d = data;
    this.o = { name: "Hortator", editor: true, firmware: "1.0.0", major: PROTO.major, minor: PROTO.minor,
      magic: PROTO.magic, caps: CAP.LIVE | CAP.PRESETS | CAP.PATTERNS | CAP.BACKUP | CAP.RESTORE | CAP.NUDGE | CAP.UPR2 | CAP.SAMPLES, latency: 1,
      seed: 7, ...opts };
    this.uprOld = !(this.o.caps & CAP.UPR2); // an FM-1 before minor 2: "UPR1", 128 voices
    this.nuser = this.uprOld ? LAYOUT.nuserOld : LAYOUT.nuser;
    this.uprMoved = Array(LAYOUT.nuserOld - LAYOUT.nuser).fill(0); // UPR1's slots 97..128: where they moved (+1)
    const P = Object.fromEntries(data.P.map((n, i) => [n, i])), G = Object.fromEntries(data.G.map((n, i) => [n, i]));
    this.P = P;
    this.G = G;
    this.M = Object.fromEntries(data.models.map((m, i) => [m.name, i]));
    this.roG = new Set(RO_G.map((n) => G[n]));
    this.t = 0; // the virtual clock (advance)
    this.faults = { noReply: 0, refuse: {}, flashError: false, cueFull: false, cuePark: -1, noFlash: false, updateAtCommit: false, blocked: [] };
    this.smp = { flash: SLOT_SIZE.map((n) => new Uint8Array(n).fill(255)), up: null, writes: [] }; // the user sample slots (erased)
    this.received = []; // every frame fed (the whitelist test)
    this.silentUntil = -1;
    this.watchUntil = -1;
    this.last = null; // the duplicate rule: { cmd, seq, frame, at }
    this.restore = null;
    this.playing = false;
    this.song = false;
    this.q = { slot: -1, var: -1, now: false };
    this.lastVar = [0, 0, 0, 0];
    this.flash = { upr: null, rec: [null, null, null, null], lk: [null, null, null, null], vr: [null, null, null, null] };
    this.seed();
    this.shadowAll();
    this.inputs = [];
  }

  /* --------------------------------------------------------------- data --- */
  newTrack(model) {
    const p = this.d.tp.map((x) => x.def);
    p[this.P.MODEL] = model;
    this.d.models[model].edit.forEach((e, i) => { p[this.P.E0 + i] = e.def; });
    const pre = this.d.presets.findIndex((x) => x.model === model);
    if (pre >= 0) this.d.presets[pre].k.forEach((k, i) => { p[this.P.E0 + i] = this.clampKnob(model, i, k); });
    return { p, pre: pre >= 0 ? pre : LAYOUT.preNone, preEd: 0 };
  }
  clampKnob(model, i, v) { const e = this.d.models[model].edit[i]; return clamp(v, e.min, e.max); }
  /** a SAMPLE preset's ATK SUS REL LOOP onto t (a factory one, none: the defaults) */
  envPut(t, env) { LAYOUT.envDef.forEach((d, i) => { t.p[this.P.SATK + i] = env ? clamp(env[i] ?? d, 0, LAYOUT.envMax[i]) : d; }); }
  clampFx(k, v) { const t = this.d.tp[LAYOUT.uprFx[k]]; return clamp(v, t.min, t.max); }
  /** preset id's FX (a used user slot that holds them, presets.c upr_fx), else null */
  uprFx(id) {
    const s = id >= this.d.presets.length && id !== LAYOUT.preNone && !this.uprOld ? this.upr[id - this.d.presets.length] : null;
    return s && !s.voiceOnly ? s.fx : null;
  }
  /** reson.c reson_refused: RESON MODEL from -> v on track ti, the caps; the message, or null */
  resonRefused(ti, from, v) {
    const c = this.d.consts, P = this.P, others = (f) => this.tracks.filter((x, i) => i !== ti && f(x.p[P.RMODEL])).length;
    if (v === from) return null;
    if (v !== c.RS_OFF && from === c.RS_OFF && others((m) => m !== c.RS_OFF) >= c.RS_MAXTRK) return "RESON: 4 TRACKS MAX";
    if (v === c.RS_CHORD && from !== c.RS_CHORD && others((m) => m === c.RS_CHORD) >= 2) return "CHORD: 2 TRACKS MAX";
    if (v === c.RS_MODAL && from !== c.RS_MODAL && others((m) => m === c.RS_MODAL) >= c.RS_MAXMODAL) return "MODAL: 2 TRACKS MAX";
    return null;
  }
  emptySteps() { return Array.from({ length: 8 }, () => Array.from({ length: 64 }, emptyStep)); }
  emptyImage(name, models, bpm) {
    const g = this.d.gp.map((x) => x.def);
    g[this.G.BPM] = bpm;
    return { name, g, sel: 0, tracks: models.map((m) => this.newTrack(m)), steps1: this.emptySteps(), vars: Array(7).fill(null),
      locks: [], lockOn: 0, old: null };
  }
  seed() {
    const r = mulberry32(this.o.seed), M = this.M, on = (row, idx, f = 0, cond = 0, note = 60) => {
      for (const i of idx) row[i] = { flags: STEP.ON | f, cond, note, micro: 0 };
    };
    const range = (a, b, s = 1) => { const x = []; for (let i = a; i < b; i += s) x.push(i); return x; };
    /* user presets: about half used, voices near the factory ones, most with their FX (a few changed; not RESON's
     * model), some the voice only */
    this.upr = Array.from({ length: this.nuser }, () => {
      if (r() > 0.45) return null;
      const f = this.d.presets[Math.floor(r() * this.d.presets.length)];
      const knobs = f.k.map((k, i) => this.clampKnob(f.model, i, k + Math.round((r() - 0.5) * 16)));
      if (this.uprOld) return { model: f.model, knobs };
      const fx = LAYOUT.uprFx.map((id, k) => (id !== this.P.RMODEL && r() < 0.3 ? this.clampFx(k, this.d.tp[id].def + Math.round((r() - 0.5) * 60)) : this.d.tp[id].def));
      return { model: f.model, knobs, fx, voiceOnly: r() < 0.2 };
    });
    /* A: TECHNO 1, four variations, locks and motion */
    const a = this.emptyImage("TECHNO 1", [M.BDTRX, M.SDTRX, M.CHTRX, M.OHTRX, M.CPTRX, M.RSTRX, M.TMTRX, M.CYTRX], 128);
    const s = a.steps1;
    on(s[0], [0, 4, 8, 12], STEP.ACC);
    on(s[1], [4, 12]);
    on(s[2], range(0, 16, 2), 0, 0);
    s[2][6].cond = 11; // 50 %
    on(s[3], [2, 6, 10, 14]);
    on(s[4], [12], 0, 57); // FILL
    on(s[5], [3, 11], 1 << STEP.RAT_SHIFT); // 2 hits
    on(s[6], [14, 15], 0, 0, 67);
    on(s[7], [0], 0, 22); // 1/2
    for (const i of [2, 6, 10, 14]) s[2][i].micro = 25; // NUDGE: the off-beat hats late
    s[1][12].micro = -10; // .. the second snare early
    const v2 = { timing: Array.from({ length: 8 }, () => [16, 2, 0, 0]), steps: clone(s) };
    on(v2.steps[0], [0, 4, 8, 10, 12], STEP.ACC);
    on(v2.steps[1], [4, 12, 15], 0, 0, 62);
    const v3 = { timing: Array.from({ length: 8 }, () => [16, 2, 20, 0]), steps: this.emptySteps() };
    on(v3.steps[2], range(0, 16, 1));
    on(v3.steps[3], [8]);
    const v5 = { timing: Array.from({ length: 8 }, (_, k) => [k === 2 ? 12 : 32, 2, 0, 0]), steps: clone(s) };
    on(v5.steps[0], [16, 20, 24, 28, 30]);
    a.vars = [v2, v3, null, v5, null, null, null];
    for (const st of [1, 3, 5, 7, 9, 11, 13, 15]) a.locks.push({ track: 2, step: st, param: this.P.E1, kind: LK.LOCK, value: 20 + st * 4 });
    for (const st of [0, 8]) a.locks.push({ track: 0, step: st, param: this.P.E0, kind: LK.LOCK, value: st ? -3 : 2 });
    a.locks.push({ track: 4, step: 12, param: this.P.REV, kind: LK.LOCK, value: 90 });
    for (const st of [0, 4, 8, 12]) a.locks.push({ track: 3, step: st, param: this.P.FCUT, kind: LK.MOTION, value: 40 + st * 5 });
    a.lockOn = 1 << 3;
    /* B: BREAKS 909, two variations */
    const b = this.emptyImage("BREAKS 909", [M.K909, M.S909, M.HATC, M.HATO, M.C909, M.RIM, M.TOM, M.CYMB], 96);
    b.tracks.forEach((t) => { t.p[this.P.SLEN] = 32; });
    on(b.steps1[0], [0, 10, 16, 26]);
    on(b.steps1[1], [4, 12, 20, 28], STEP.ACC);
    on(b.steps1[2], range(0, 32, 2));
    on(b.steps1[3], [14, 30]);
    b.vars[0] = { timing: Array.from({ length: 8 }, () => [32, 2, 30, 0]), steps: clone(b.steps1) };
    on(b.vars[0].steps[0], [6, 22]);
    for (const st of [2, 6, 18, 22]) b.locks.push({ track: 2, step: st, param: this.P.PAN, kind: LK.LOCK, value: st < 10 ? -30 : 30 });
    for (const st of [12, 28]) b.locks.push({ track: 1, step: st, param: this.P.E2, kind: LK.LOCK, value: 100 });
    /* C: ACID LINE, the 303 on track 1 */
    const c = this.emptyImage("ACID LINE", [M.ACID, M.K808, M.HATC, M.S808, M.HATO, M.CLAVE, M.TOM, M.CYMB], 132);
    const notes = [0, 0, 12, 0, 3, 0, 7, 10, 0, 0, 12, 15, 0, 3, -2, 0];
    notes.forEach((n, i) => { if (i % 4 !== 1) c.steps1[0][i] = { flags: STEP.ON | (i % 3 ? 0 : STEP.ACC) | (i === 6 ? STEP.SLIDE : 0), cond: 0, note: 60 + n - 24, micro: 0 }; });
    c.steps1[0][7].flags |= STEP.TIE;
    on(c.steps1[1], [0, 4, 8, 12]);
    on(c.steps1[2], range(2, 16, 4));
    this.slots = [a, b, c, null];
    this.slots.forEach((img, i) => { if (img) this.writeSlot(i, img); });
    this.writeUpr();
    /* power-on: A loaded (LOAD: its globals), variation 1, stopped */
    this.kit = this.emptyImage("", [M.K808, M.S808, M.HATC, M.HATO, M.C808, M.RIM, M.TOM, M.CYMB], 120);
    if (this.o.powerOnKit) this.loadLive(-1);
    else this.loadLive(0);
  }

  /* objects <-> images ------------------------------------------------------------- */
  preRec(id) {
    if (id === LAYOUT.preNone || id === undefined) return LAYOUT.preNone;
    return id < this.d.presets.length ? this.d.presets[id].uid : LAYOUT.preRecUser | (id - this.d.presets.length);
  }
  preDec(rec) {
    if (rec === LAYOUT.preNone) return LAYOUT.preNone;
    if (rec & LAYOUT.preRecUser) { // (an older bank's slot 97..128: where it moved, presets.c preset_rec_dec_in)
      let u = rec & 0x7fff;
      if (!this.uprOld && u >= LAYOUT.nuser && u < LAYOUT.nuserOld) u = this.uprMoved[u - LAYOUT.nuser] - 1;
      return u >= 0 && u < this.nuser ? this.d.presets.length + u : LAYOUT.preNone;
    }
    const i = this.d.presets.findIndex((p) => p.uid === rec);
    return i < 0 ? LAYOUT.preNone : i;
  }
  objectsOf(img, slot) {
    if (img.old) return { record: img.old, locks: null, vars: null };
    const vars = img.vars.some(Boolean) ? encodeVars({ slot, entries: img.vars }) : null;
    const locks = img.locks.length ? encodeLocks({ slot, on: img.lockOn, events: img.locks }) : null;
    const record = encodeProject({ g: img.g, sel: img.sel, name: img.name, song: img.song,
      tracks: img.tracks.map((t, k) => ({ p: t.p, steps: img.steps1[k], pre: this.preRec(t.pre), preEd: t.preEd })),
      lk_n: locks ? img.locks.length : 0, lk_link: locks ? lockLink(locks) : 0, var_link: vars ? varLink(vars) : 0,
      var_len: vars ? vars.length : 0 });
    return { record, locks, vars };
  }
  imageOf({ record, locks, vars }) {
    const p = decodeProject(record);
    if (!p.full) { // an older record: kept as is in flash; the device converts it at load (not emulated). FDRF: decoded
      // as the firmware converts it (proj_xp_move: NUDGE 0), its own bytes kept in flash
      const img = this.emptyImage("", this.kit.tracks.map((t) => t.p[this.P.MODEL]), 120);
      img.old = Uint8Array.from(record);
      return img;
    }
    return { name: p.name, g: p.g, sel: p.sel, song: [...p.song], old: null,
      tracks: p.tracks.map((t) => ({ p: t.p, pre: this.preDec(t.pre), preEd: t.preEd })),
      steps1: p.tracks.map((t) => t.steps), vars: vars ? decodeVars(vars).entries : Array(7).fill(null),
      locks: locks ? decodeLocks(locks).events : [], lockOn: locks ? decodeLocks(locks).on : 0 };
  }
  flashWrite(key, slot, bytes) {
    const cur = slot < 0 ? this.flash[key] : this.flash[key][slot];
    const rec = { bytes: Uint8Array.from(bytes), seq: (cur ? cur.seq : 0) + 1 };
    if (slot < 0) this.flash[key] = rec; else this.flash[key][slot] = rec;
  }
  writeSlot(s, img) { // the save order: variations, the lock bank, then the record (the commit)
    const o = this.objectsOf(img, s);
    if (o.vars) this.flashWrite("vr", s, o.vars);
    if (o.locks) this.flashWrite("lk", s, o.locks);
    this.flashWrite("rec", s, o.record);
  }
  uprBytes() { return this.uprOld ? encodePresetsV1(this.upr) : encodePresets(this.upr, this.uprMoved); }
  writeUpr() { this.flashWrite("upr", -1, this.uprBytes()); }
  /** the bank from flash, as preset_user_boot reads it: the knobs inside the model's ranges, the FX inside theirs */
  readUpr() {
    const b = this.flash.upr.bytes;
    if (this.uprOld) {
      this.upr = Array.from({ length: LAYOUT.nuserOld }, (_, i) => (b[4 + 10 * i] === 1 && b[5 + 10 * i] < this.d.models.length
        ? { model: b[5 + 10 * i], knobs: [...b.subarray(6 + 10 * i, 14 + 10 * i)].map((x, k) => this.clampKnob(b[5 + 10 * i], k, (x << 24) >> 24)) } : null));
      return;
    }
    const bank = decodePresets(b);
    this.upr = bank.slots.map((s) => s && s.model < this.d.models.length ? { model: s.model, knobs: s.knobs.map((k, i) => this.clampKnob(s.model, i, k)),
      fx: s.fx.map((x, k) => this.clampFx(k, x)), voiceOnly: s.voiceOnly, ...(s.env ? { env: [...s.env] } : {}) } : null);
    this.uprMoved = bank.moved.map((x) => (x > LAYOUT.nuser ? 0 : x));
  }
  flashObjects(s) {
    const r = this.flash.rec[s];
    if (!r) return null;
    const p = decodeProject(r.bytes);
    return { record: r.bytes, locks: p.lk_n && this.flash.lk[s] ? this.flash.lk[s].bytes : null,
      vars: p.var_len && this.flash.vr[s] ? this.flash.vr[s].bytes : null };
  }

  /* the live state --------------------------------------------------------------- */
  img() { return this.loaded < 0 ? this.kit : this.slots[this.loaded]; }
  timingOf(t) { return LAYOUT.timing.map((id) => t.p[id]); }
  loadLive(s) { // LOAD: knobs, globals, variation 1
    this.loaded = s;
    const img = s < 0 ? this.kit : this.slots[s];
    this.g = [...img.g];
    this.tracks = img.tracks.map((t) => ({ p: [...t.p], pre: t.pre, preEd: t.preEd, steps: [] }));
    this.sel = img.sel;
    this.varCur = 0;
    this.loadVar(0);
  }
  fileLive() { // the live steps and knobs back into the loaded slot's image (a write-back)
    const img = this.img();
    this.tracks.forEach((t, k) => {
      const keep = this.varCur ? this.timingOf(img.tracks[k]) : null;
      img.tracks[k] = { p: [...t.p], pre: t.pre, preEd: t.preEd };
      if (keep) LAYOUT.timing.forEach((id, j) => { img.tracks[k].p[id] = keep[j]; });
    });
    if (this.varCur) img.vars[this.varCur - 1] = { timing: this.tracks.map((t) => this.timingOf(t)), steps: this.tracks.map((t) => clone(t.steps)) };
    else this.tracks.forEach((t, k) => { img.steps1[k] = clone(t.steps); });
  }
  loadVar(v) {
    const img = this.img();
    if (v && !img.vars[v - 1]) // never used: a copy of the live one
      img.vars[v - 1] = { timing: this.tracks.map((t) => this.timingOf(t)), steps: this.tracks.map((t) => clone(t.steps)) };
    this.tracks.forEach((t, k) => {
      const tm = v ? img.vars[v - 1].timing[k] : this.timingOf(img.tracks[k]);
      t.steps = clone(v ? img.vars[v - 1].steps[k] : img.steps1[k]);
      LAYOUT.timing.forEach((id, j) => { t.p[id] = tm[j]; });
    });
    this.varCur = v;
  }
  switchTo(s, v) {
    if (s === this.loaded) {
      this.fileLive();
      this.loadVar(v);
      return;
    }
    this.fileLive();
    this.lastVar[this.loaded] = this.varCur;
    this.loaded = s;
    const img = this.slots[s];
    img.tracks.forEach((t, k) => { this.tracks[k].p = [...t.p]; this.tracks[k].pre = t.pre; this.tracks[k].preEd = t.preEd; });
    this.loadVar(0);
    if (v) this.loadVar(v);
  }
  liveImage() { // the loaded slot's image with the live state written back (not stored)
    const save = { img: clone({ ...this.img(), old: null }), old: this.img().old };
    this.fileLive();
    const img = this.img();
    const out = clone({ ...img, old: null });
    out.g = [...this.g];
    out.sel = this.sel;
    out.old = save.old;
    Object.assign(this.img(), save.img, { old: save.old }); // (back as it was)
    return out;
  }
  slotDirty(s) {
    if (!this.slots[s]) return false;
    const f = this.flashObjects(s);
    if (!f) return true;
    const o = this.objectsOf(s === this.loaded ? this.liveImage() : this.slots[s], s), eq = (a, b) => (!a && !b) || (a && b && a.length === b.length && a.every((x, i) => x === b[i]));
    const fp = decodeProject(f.record), fr = fp.old && fp.full ? encodeProject(fp) : f.record; // (FDRF: as converted at load)
    return !eq(o.record, fr) || !eq(o.locks, f.locks) || !eq(o.vars, f.vars);
  }
  vmask(s) {
    const img = s === this.loaded ? this.img() : this.slots[s];
    if (!img) return 1;
    let m = 1;
    img.vars.forEach((e, i) => { if (e) m |= 2 << i; });
    if (s === this.loaded) m |= 1 << this.varCur;
    return m;
  }
  state() {
    const used = this.slots.reduce((m, x, i) => m | (x ? 1 << i : 0), 0);
    const dirty = [0, 1, 2, 3].reduce((m, i) => m | (this.slotDirty(i) ? 1 << i : 0), 0);
    return { playing: this.playing ? 1 : 0, slot: this.loaded < 0 ? NONE : this.loaded, var: this.varCur,
      qslot: this.q.slot < 0 ? NONE : this.q.slot, qvar: this.q.var < 0 ? NONE : this.q.var, sel: this.sel,
      flags: (this.restore ? 1 : 0) | (this.song ? 2 : 0) | (this.faults.noFlash ? 4 : 0), used, dirty, vmask: [0, 1, 2, 3].map((s) => this.vmask(s)) };
  }

  /* knobs ---------------------------------------------------------------------- */
  trackDesc(t, id) {
    const P = this.P;
    if (id >= P.IA && id <= P.IC) {
      const ty = t.p[P.ITYPE];
      return ty <= 0 || ty >= 9 ? this.d.tp[id] : this.d.insDesc[ty < 5 ? 0 : ty === 5 ? 1 : 2][id - P.IA];
    }
    if (id >= P.E0 && id <= P.E7) return this.d.models[t.p[P.MODEL]].edit[id - P.E0];
    if (id === P.RSTRCT && t.p[P.RMODEL] === 3) return this.d.rsChordDesc;
    return this.d.tp[id];
  }
  knob(scope, track, id) { // -> { desc, names, flags, t } or a refusal
    if (scope === SCOPE.TRACK) {
      if (track !== NONE && track > 7) refuse(RC.BAD, DETAIL.RANGE);
      if (id >= this.d.tp.length) refuse(RC.BAD, DETAIL.RANGE);
      const t = this.tracks[track === NONE ? this.sel : track], desc = this.trackDesc(t, id), P = this.P;
      let names = desc.names || [];
      if (this.d.fmt[desc.fmt] === "LDEST") names = Array.from({ length: 19 }, (_, v) => this.ldestName(t, v));
      let flags = (desc.split ? DF.SPLIT : 0) | (desc.order || id === P.MODEL ? DF.ORDER : 0) | (this.d.tp[id].lock ? DF.LOCK : 0);
      if ((id >= P.E0 && id <= P.E7) || (id >= P.IA && id <= P.IC) || id === P.RSTRCT || this.d.fmt[desc.fmt] === "LDEST") flags |= DF.CONTEXT;
      return { desc, names, flags, t, order: id === P.MODEL ? this.d.modelList : desc.order };
    }
    if (scope === SCOPE.GLOBAL) {
      if (id >= this.d.gp.length) refuse(RC.BAD, DETAIL.RANGE);
      const desc = this.d.gp[id];
      return { desc, names: desc.names || [], flags: (desc.split ? DF.SPLIT : 0) | (desc.order ? DF.ORDER : 0) | (this.roG.has(id) ? DF.RO : 0),
        order: desc.order };
    }
    if (scope === SCOPE.MODEL) {
      if (track >= this.d.models.length || id > 7) refuse(RC.BAD, DETAIL.RANGE);
      const desc = this.d.models[track].edit[id];
      return { desc, names: desc.names || [], flags: desc.split ? DF.SPLIT : 0 };
    }
    refuse(RC.BAD, DETAIL.RANGE);
  }
  ldestName(t, v) {
    if (v <= 0) return "OFF";
    const x = this.d.ldest[v - 1];
    if (typeof x === "string") return x;
    const l = this.trackDesc(t, x).label;
    return l && l[0] !== "-" ? l : "--";
  }
  text(k, v) { // the device's text (an approximation of params.c param_format: the page never computes it)
    const d = k.desc, f = this.d.fmt[d.fmt];
    switch (f) {
      case "PCT": { const r = d.max - d.min > 0 ? d.max - d.min : 1; return [String(Math.trunc(((v - d.min) * 100 + Math.trunc(r / 2)) / r)), "%"]; }
      case "BIPCT": { const x = Math.trunc(v * 100 / 64); return [(x > 0 ? "+" : "") + x, "%"]; }
      case "ENUM": return [k.names[v - d.min] ?? String(v), d.unit || ""];
      case "LDEST": return [k.names[v] ?? String(v), ""];
      case "BPM": return [String(v), "BPM"];
      case "STEPS": return [String(v), "STEP"];
      case "SEMI": return [(v > 0 ? "+" : "") + v, "st"];
      case "ONOFF": return [v ? "ON" : "OFF", ""];
      case "NOTE": return [N_NOTE[((v % 12) + 12) % 12], ""];
      case "LPHASE": return [String(Math.trunc(clamp(v, 0, 127) * 360 / 128)), "deg"]; // (the font's degree sign: "deg")
      case "INT":
        if (d.split) return [d.names[Math.floor((v - d.min) * d.names.length / (d.max - d.min + 1))], d.unit || ""];
        return [String(v), d.unit || ""];
      default: return [String(v), ""];
    }
  }
  value(scope, track, id) {
    if (scope === SCOPE.GLOBAL) return this.g[id];
    return this.tracks[track === NONE ? this.sel : track].p[id];
  }
  acidOthers(k) { return this.tracks.filter((t, i) => i !== k && t.p[this.P.MODEL] === this.d.consts.ACID).length; }
  set(scope, track, id, v) { // a knob turned to v (clamped), as the device's setter (editor.c ed_set); the value kept
    const k = this.knob(scope, track, id), P = this.P, c = this.d.consts;
    v = clamp(v, k.desc.min, k.desc.max);
    if (k.flags & DF.RO) refuse(RC.BAD, DETAIL.RANGE, "READ ONLY");
    this.busy();
    if (scope === SCOPE.GLOBAL) {
      if (id === this.G.BPM && this.g[this.G.CLOCK]) return this.g[id]; // following a clock: its tempo
      this.g[id] = v;
      return v;
    }
    const ti = track === NONE ? this.sel : track, t = this.tracks[ti];
    if (id === P.MODEL) {
      if (v === t.p[id]) return v;
      if (v === c.ACID && this.acidOthers(ti) >= c.ACID_MAXTRK) refuse(RC.REFUSED, DETAIL.ACID_CAP);
      const pre = this.d.presets.findIndex((x) => x.model === v); // drum_set_model: the model's first factory preset
      t.p[id] = v;
      for (let i = 0; i < 8; i++) t.p[P.E0 + i] = pre >= 0 ? this.clampKnob(v, i, this.d.presets[pre].k[i]) : this.d.models[v].edit[i].def;
      if (v === c.DM_SMPL) this.envPut(t, null);
      t.pre = pre >= 0 ? pre : LAYOUT.preNone;
      t.preEd = 0;
      return v;
    }
    if (id === P.RMODEL && v !== t.p[id]) { // reson.c reson_refused: the caps
      const why = this.resonRefused(ti, t.p[id], v);
      if (why) refuse(RC.REFUSED, DETAIL.NONE, why);
    }
    if (((id >= P.SLEN && id <= P.SSWING) || id === P.SRC) && this.song) return t.p[id]; // a song's rows set PATTERN
    // presets.c preset_marks: a SOUND knob; an FX knob of a user preset that holds them
    if (((id >= P.E0 && id <= P.E7) || (id >= P.SATK && id <= P.SPLAY) || (LAYOUT.uprFx.includes(id) && this.uprFx(t.pre))) && t.p[id] !== v) t.preEd = 1;
    t.p[id] = v;
    return v;
  }

  /* the transport ------------------------------------------------------------------- */
  feed(frame) {
    const f = Uint8Array.from(frame);
    this.received.push(f);
    if (!this.o.editor) return [];
    if (f[0] !== 0xf0) return [];
    if (f[1] !== 0x7d) { // update traffic silences (editor.c ed_ota_frame, usb.ota_req); other SysEx (F0 7E ...) not
      if (isUpdateTraffic(f)) this.silence();
      return [];
    }
    if (f[2] !== 0x48 || f[3] !== 0x54) return []; // another 7D frame: not ours, freed
    if (this.t < this.silentUntil) return []; // silent: freed, unanswered
    if (f.length < 7) return [];
    if (this.faults.noReply > 0) { this.faults.noReply--; return []; }
    if (this.watchUntil >= this.t) this.watchUntil = this.t + TIMING.watch; // any request keeps WATCH on
    const out = [this.handle(f)].filter(Boolean);
    return out.concat(this.poll());
  }
  silence() {
    this.silentUntil = this.t + TIMING.silence;
    if (this.restore) this.abortRestore();
    this.smp.up = null; // an upload running: dropped (its slot stays empty)
    this.watchUntil = -1;
  }
  updateFrame() { return this.feed(UPDATE_HANDSHAKE); }
  otaReq() { return this.feed(OTA_REQ); }
  advance(ms) {
    this.t += ms;
    if (this.restore && this.t - this.restore.at > TIMING.restoreIdle) this.abortRestore();
    if (this.smp.up && this.t - this.smp.up.at > TIMING.restoreIdle) this.smp.up = null;
    return this.poll();
  }
  get silent() { return this.t < this.silentUntil; }
  request(cmd, args = {}, seq) { // tests: one request, its decoded reply (undefined: none); pushes in this.pushed
    this.seq = seq ?? ((this.seq ?? -1) + 1) % SEQ_PUSH;
    const frames = this.feed(encodeRequest(cmd, this.seq, args)).map(decodeFrame);
    this.pushed = frames.filter((x) => x.push);
    return frames.find((x) => !x.push);
  }
  handle(f) {
    const cmd = f[4], seq = f[5];
    if (seq === SEQ_PUSH) return null;
    if (this.last && this.last.cmd === cmd && this.last.seq === seq && WRITES.has(cmd) && this.t - this.last.at <= TIMING.dup)
      return this.last.frame; // a retry: its first reply again, byte for byte, not done twice
    let reply;
    try {
      let req;
      try { req = decodeRequest(f); } catch (e) { throw new Refusal(RC.BAD, e.detail || DETAIL.LENGTH); }
      if (req.unknown) refuse(RC.BAD, DETAIL.UNKNOWN);
      if ((this.o.caps & CMD_CAP[cmd]) !== CMD_CAP[cmd]) refuse(RC.BAD, DETAIL.NO_CAP);
      const fx = this.faults.refuse[cmd];
      if (fx && fx.after > 0) fx.after--; // (refuse the request after `after` more of this command)
      else if (fx) { delete this.faults.refuse[cmd]; throw new Refusal(fx.rc ?? RC.REFUSED, fx.detail ?? 0, fx.msg); }
      if (cmd === CMD.INFO) this.last = null;
      if (this.restore && cmd !== CMD.RESTORE) this.restore.at = this.t;
      if (this.smp.up) this.smp.up.at = this.t; // any request keeps an upload alive
      const body = this.exec(cmd, req.args);
      reply = this.fit(cmd, seq, body);
    } catch (e) {
      if (!(e instanceof Refusal)) throw e;
      reply = encodeReply(cmd, seq, e.rc, { detail: e.detail, msg: e.msg });
    }
    this.last = { cmd, seq, frame: reply, at: this.t };
    return reply;
  }
  fit(cmd, seq, body) { // a reply's list cut to fit ED_REPLY_MAX (the device answers what fits; the page asks on)
    const key = body.$list;
    if (key && body[key].length > 127) body[key].length = 127; // (a u7 count; what fits is far fewer)
    let f = encodeReply(cmd, seq, RC.OK, body);
    while (f.length > LIMITS.reply && key && body[key].length) { body[key].pop(); f = encodeReply(cmd, seq, RC.OK, body); }
    if (f.length > LIMITS.reply) throw new Error(`mock: reply ${cmd} ${f.length} B`);
    return f;
  }
  busy() { if (this.restore) refuse(RC.BUSY, DETAIL.RESTORING); }
  flashOk() { if (this.faults.noFlash) refuse(RC.REFUSED, DETAIL.NO_FLASH); }
  slotArg(s) {
    if (s === NONE) return this.loaded;
    if (s > 3) refuse(RC.BAD, DETAIL.RANGE);
    return s;
  }

  exec(cmd, a) {
    const P = this.P;
    switch (cmd) {
      case CMD.INFO:
        return { magic: this.o.magic, firmware: this.o.firmware, major: this.o.major, minor: this.o.minor, caps: this.o.caps,
          ntrk: 8, nstep: 64, pcount: this.d.tp.length, gcount: this.d.gp.length, nmodels: this.d.models.length,
          nfact: this.d.presets.length, nuser: this.nuser, nbanks: this.d.banks.length, nslot: 4, nvar: 8,
          maxreq: LIMITS.req, ble: 0, state: this.state() };
      case CMD.PAGES: {
        const pages = this.pages();
        return { total: pages.length, first: a.first, pages: pages.slice(a.first), $list: "pages" };
      }
      case CMD.DESC: {
        const k = this.knob(a.scope, a.track, a.id), d = k.desc;
        let items;
        if (a.first === DESC_ORDER) items = k.order ? [...k.order] : [];
        else items = k.names.slice(a.first);
        return { scope: a.scope, track: a.track, id: a.id, fmt: d.fmt, flags: k.flags, min: d.min, max: d.max, def: d.def,
          label: d.label, unit: d.unit || "", nnames: k.names.length, first: a.first, items, $list: "items" };
      }
      case CMD.GET: {
        if (a.scope > SCOPE.GLOBAL || a.count < 1) refuse(RC.BAD, DETAIL.RANGE);
        this.knob(a.scope, a.track, a.id);
        const n = a.scope === SCOPE.GLOBAL ? this.d.gp.length : this.d.tp.length, values = [];
        for (let id = a.id; id < Math.min(n, a.id + a.count); id++) {
          const k = this.knob(a.scope, a.track, id), v = this.value(a.scope, a.track, id), [text, unit] = this.text(k, v);
          values.push({ value: v, text, unit });
        }
        return { scope: a.scope, track: a.track, id: a.id, values, $list: "values" };
      }
      case CMD.SET: case CMD.TEXT: {
        if (a.scope > SCOPE.GLOBAL) refuse(RC.BAD, DETAIL.RANGE);
        let v;
        if (cmd === CMD.SET) {
          v = this.set(a.scope, a.track, a.id, a.value);
          this.shadowKnob(a.scope, a.track, a.id);
        } else { const k0 = this.knob(a.scope, a.track, a.id); v = clamp(a.value, k0.desc.min, k0.desc.max); }
        const [text, unit] = this.text(this.knob(a.scope, a.track, a.id), v);
        return { scope: a.scope, track: a.track, id: a.id, value: v, text, unit };
      }
      case CMD.DUMP: {
        if (a.track !== NONE && a.track > 7) refuse(RC.BAD, DETAIL.RANGE);
        const ti = a.track === NONE ? this.sel : a.track, t = this.tracks[ti];
        return { track: ti, model: t.p[P.MODEL], preset: t.pre === LAYOUT.preNone ? U14_NONE : t.pre, edited: t.preEd,
          p: [...t.p], g: [...this.g] };
      }
      case CMD.SELECT:
        if (a.track !== NONE) {
          if (a.track > 7) refuse(RC.BAD, DETAIL.RANGE);
          this.sel = a.track;
          this.shadowAll(true);
        }
        return { track: this.sel };
      case CMD.WATCH:
        if (a.on) { if (this.watchUntil < this.t) this.shadowAll(); this.watchUntil = this.t + TIMING.watch; } else this.watchUntil = -1;
        return { on: a.on ? 1 : 0 };
      case CMD.NAMES: {
        const b = this.d.banks[a.bank];
        if (!b) refuse(RC.BAD, DETAIL.RANGE);
        const presets = [];
        for (let i = b.start + a.first; i < b.start + b.count; i++) presets.push({ id: i, model: this.d.presets[i].model, name: this.d.presets[i].name });
        return { bank: a.bank, name: b.name, total: b.count, first: a.first, presets, $list: "presets" };
      }
      case CMD.PRESET_LOAD: {
        this.busy();
        const ti = a.track === NONE ? this.sel : a.track, nf = this.d.presets.length;
        if (ti > 7 || a.id >= nf + this.nuser) refuse(RC.BAD, DETAIL.RANGE);
        const p = a.id < nf ? { model: this.d.presets[a.id].model, knobs: this.d.presets[a.id].k } : this.upr[a.id - nf];
        if (!p) refuse(RC.REFUSED, DETAIL.PRESET_EMPTY);
        const t = this.tracks[ti];
        if (p.model === this.d.consts.ACID && this.acidOthers(ti) >= this.d.consts.ACID_MAXTRK) refuse(RC.REFUSED, DETAIL.ACID_CAP);
        const fx = this.uprFx(a.id);
        t.p[P.MODEL] = p.model;
        p.knobs.forEach((k, i) => { t.p[P.E0 + i] = this.clampKnob(p.model, i, k); });
        if (p.model === this.d.consts.DM_SMPL) this.envPut(t, a.id >= nf ? p.env : null); // presets.c preset_apply
        if (fx) LAYOUT.uprFx.forEach((id, k) => { // presets.c preset_fx_put: RESON within its caps (STRNG, else OFF)
          let v = fx[k];
          if (id === P.RMODEL) {
            const c = this.d.consts, strng = this.d.tp[P.RMODEL].names.indexOf("STRNG");
            if (this.resonRefused(ti, t.p[id], v)) v = strng;
            if (this.resonRefused(ti, t.p[id], v)) v = c.RS_OFF;
          }
          t.p[id] = v;
        });
        t.pre = a.id;
        t.preEd = 0; // (no shadow update, as editor_pattern.c: the selected track's moved knobs are pushed as CHANGED)
        return { track: ti, id: a.id, model: p.model };
      }
      case CMD.STEPS_GET: {
        if (a.var > 7 || a.track > 7) refuse(RC.BAD, DETAIL.RANGE);
        const s = this.slotArg(a.slot), live = s === this.loaded && a.var === this.varCur;
        if (s >= 0 && !this.slots[s]) refuse(RC.REFUSED, DETAIL.EMPTY_SLOT, `SLOT ${"ABCD"[s]} EMPTY`);
        if (s < 0 && a.var !== this.varCur) refuse(RC.REFUSED, DETAIL.NO_SLOT);
        let steps, timing, used = 1;
        if (live) { steps = this.tracks[a.track].steps; timing = this.timingOf(this.tracks[a.track]); } else {
          const img = this.slots[s];
          if (!a.var) { steps = img.steps1[a.track]; timing = this.timingOf(img.tracks[a.track]); } else if (img.vars[a.var - 1]) {
            steps = img.vars[a.var - 1].steps[a.track];
            timing = img.vars[a.var - 1].timing[a.track];
          } else { used = 0; steps = Array.from({ length: 64 }, emptyStep); timing = [0, 0, 0, 0]; }
        }
        const nudge = this.o.caps & CAP.NUDGE; // (minor 1: each step's NUDGE after the steps; an older firmware: none)
        return { slot: s < 0 ? NONE : s, var: a.var, track: a.track, used, live: live ? 1 : 0, timing: [...timing],
          steps: steps.map(({ flags, cond, note, micro }) => (nudge ? { flags, cond, note, micro: micro | 0 } : { flags, cond, note })) };
      }
      case CMD.STEPS_SET: {
        const sent = a.steps.length && a.steps[0].micro !== undefined; // NUDGE sent (minor 1)
        if (sent && !(this.o.caps & CAP.NUDGE)) refuse(RC.BAD, DETAIL.LENGTH); // (an older firmware: 5 + 3 x count only)
        this.busy();
        if (a.track > 7 || a.var > 7 || a.first > 63 || !a.steps.length || a.first + a.steps.length > 64) refuse(RC.BAD, DETAIL.RANGE);
        const s = this.slotArg(a.slot);
        if (this.song) refuse(RC.REFUSED, DETAIL.CUE_SONG, "STOP TO EDIT"); // a song's patterns stay as they are
        if (this.loaded < 0 && a.var !== this.varCur) refuse(RC.REFUSED, DETAIL.NO_SLOT);
        if (s !== this.loaded) refuse(RC.STALE, DETAIL.CUE_FIRST);
        if (a.var !== this.varCur) { // switch-to-edit: the variation's key pressed (stopped: at once; playing: queued)
          if (this.q.slot >= 0) refuse(RC.STALE, DETAIL.CUE_FIRST);
          if (this.q.var !== a.var) this.cueVar(a.var);
          if (a.var !== this.varCur) refuse(RC.STALE, DETAIL.CUE_FIRST);
        }
        const t = this.tracks[a.track];
        a.steps.forEach((x, i) => { // NUDGE sent, or the device's kept (a step written empty: 0, as a tap off leaves it)
          const s = { flags: x.flags & 0x3f, cond: Math.min(x.cond, this.d.consts.COND_MAX),
            note: Math.max(this.d.consts.NT_MIN, Math.min(this.d.consts.NT_MAX, x.note - STEP.NT_OFFSET)) + STEP.NT_OFFSET };
          const empty = !s.flags && !s.cond && s.note === STEP.NT_OFFSET;
          s.micro = sent ? Math.max(-STEP.MICRO_MAX, Math.min(STEP.MICRO_MAX, x.micro)) : empty ? 0 : (t.steps[a.first + i].micro | 0);
          t.steps[a.first + i] = s;
        });
        this.shadowSteps(a.track);
        return { slot: a.slot, var: a.var, track: a.track, first: a.first, count: a.steps.length };
      }
      case CMD.LOCKS_GET: {
        if (a.track > 7) refuse(RC.BAD, DETAIL.RANGE);
        const s = this.slotArg(a.slot);
        if (s >= 0 && !this.slots[s]) refuse(RC.REFUSED, DETAIL.EMPTY_SLOT, `SLOT ${"ABCD"[s]} EMPTY`);
        const ev = (s < 0 ? this.kit : this.slots[s]).locks.filter((e) => e.track === a.track).sort((x, y) => lockOrd(x) - lockOrd(y));
        return { slot: s < 0 ? NONE : s, track: a.track, total: ev.length, first: a.first,
          locks: ev.slice(a.first).map((e) => ({ step: e.step, param: e.param, kind: e.kind, value: e.value })), $list: "locks" };
      }
      case CMD.LOCK_EDIT: return this.lockEdit(a);
      case CMD.CUE: {
        this.busy();
        if (a.kind > CUE.VAR || (a.kind === CUE.SLOT ? a.n > 3 : a.n > 7)) refuse(RC.BAD, DETAIL.RANGE);
        if (a.kind === CUE.SLOT) this.cueSlot(a.n); else this.cueVar(a.n);
        return { kind: a.kind, n: a.n, state: this.state() };
      }
      case CMD.SAVE: {
        this.busy();
        if (a.slot !== NONE && a.slot !== this.loaded) refuse(RC.BAD, DETAIL.RANGE);
        if (this.playing) refuse(RC.STOP, DETAIL.STOP, "STOP TO SAVE");
        if (this.loaded < 0) refuse(RC.REFUSED, DETAIL.NO_SLOT);
        if (this.faults.noFlash) refuse(RC.REFUSED, DETAIL.NO_FLASH);
        if (this.faults.flashError) refuse(RC.FLASH, DETAIL.NONE, "SAVE ERROR");
        const img = this.liveImage();
        this.slots[this.loaded] = img;
        this.writeSlot(this.loaded, img); // (the live state stays: it is now the slot's flash version)
        return { slot: this.loaded, state: this.state() };
      }
      case CMD.BACKUP_LIST: this.flashOk(); return { objects: this.backupList() };
      case CMD.BACKUP_GET: {
        this.flashOk();
        const o = this.backupList()[a.obj];
        if (!o) refuse(RC.BAD, DETAIL.RANGE);
        const b = this.objBytes(a.obj);
        if (!o.present) refuse(RC.REFUSED, DETAIL.NOT_PRESENT);
        if (o.seq !== a.seq) refuse(RC.STALE, DETAIL.CHANGED);
        if (a.offset % LIMITS.piece || a.offset >= o.size) refuse(RC.BAD, DETAIL.RANGE);
        const data = b.slice(a.offset, a.offset + LIMITS.piece);
        return { obj: a.obj, offset: a.offset, len: data.length, data };
      }
      case CMD.RESTORE: this.flashOk(); return this.restoreOp(a);
      case CMD.SMP_INFO: case CMD.SMP_BEGIN: case CMD.SMP_DATA: case CMD.SMP_END: case CMD.SMP_READ: case CMD.SMP_CLEAR:
      case CMD.SMP_RENAME: return this.sampleOp(cmd, a);
      default: refuse(RC.BAD, DETAIL.UNKNOWN);
    }
  }
  pages() {
    const out = [], d = this.d, t = this.tracks[this.sel], P = this.P;
    const snd = [P.MODEL];
    for (let k = 0; k < 8; k++) { const l = d.models[t.p[P.MODEL]].edit[k].label; if (k < 4 || (l && l[0] !== "-")) snd.push(P.E0 + k); }
    snd.push(P.LEVEL, P.PAN, P.NOTE, P.CHOKE, P.PRIO);
    if (t.p[P.MODEL] === d.consts.DM_SMPL) { // pages.c snd_list: SAMPLE's PLAY; ATK SUS REL LOOP, a page of their own
      snd.push(P.SPLAY);
      while (snd.length < 16) snd.push(undefined);
      snd.push(P.SATK, P.SSUS, P.SREL, P.SLOOP);
    }
    for (const pg of d.pages) {
      if (pg.scope > d.sc.GLOBAL || [d.gr.SLOTS, d.gr.MOTION, d.gr.SONG].includes(pg.graph)) continue;
      if (pg.scope === d.sc.GLOBAL && pg.id.some((x) => x !== 255 && x >= d.gp.length)) continue;
      if (pg.graph === d.gr.LDEST) {
        d.lsId.forEach((ids, s) => out.push({ title: `DEST ${s + 1}`, scope: SCOPE.TRACK, graph: pg.graph, ids: [...ids, U14_NONE] }));
        continue;
      }
      const ids = pg.id.map((x) => {
        if (x === 255) return U14_NONE;
        if (pg.scope === d.sc.TRACK && x >= d.sndSlot) return snd[x - d.sndSlot] ?? U14_NONE;
        if (pg.graph === d.gr.GRIDS && this.g[this.G.GMODE] && x >= this.G.GX && x <= this.G.GCHAOS) return this.G.GLEN1 + x - this.G.GX;
        return x;
      });
      if (ids.every((x) => x === U14_NONE)) continue;
      out.push({ title: pg.title, scope: pg.scope === d.sc.TRACK ? SCOPE.TRACK : SCOPE.GLOBAL, graph: pg.graph, ids });
    }
    return out;
  }

  lockEdit(a) {
    this.busy();
    if (a.op > LK.CLEAR_TRACK || a.track > 7 || a.step > 63 || a.param >= this.d.tp.length) refuse(RC.BAD, DETAIL.RANGE);
    const bank = this.img().locks, t = this.tracks[a.track];
    const isLock = (e) => e.kind === LK.LOCK && e.track === a.track;
    let value = a.value;
    if (a.op === LK.SET) {
      if (!this.d.tp[a.param].lock) refuse(RC.REFUSED, DETAIL.NOT_LOCKABLE);
      const d = this.trackDesc(t, a.param);
      value = clamp(a.value, Math.max(-64, d.min), Math.min(127, d.max));
      const e = bank.find((x) => isLock(x) && x.step === a.step && x.param === a.param);
      if (e) e.value = value; else {
        if (bank.length >= LAYOUT.lkMax) refuse(RC.REFUSED, DETAIL.LOCKS_FULL);
        bank.push({ track: a.track, step: a.step, param: a.param, kind: LK.LOCK, value });
        bank.sort((x, y) => lockOrd(x) - lockOrd(y));
      }
    } else {
      const drop = (e) => isLock(e) && (a.op === LK.CLEAR_TRACK || (e.step === a.step && (a.op === LK.CLEAR_STEP || e.param === a.param)));
      for (let i = bank.length - 1; i >= 0; i--) if (drop(bank[i])) bank.splice(i, 1);
    }
    this.shadowLocks(a.track);
    return { op: a.op, track: a.track, step: a.step, param: a.param, value, count: bank.filter(isLock).length };
  }

  /* the cue keys (project.c cue_slot / cue_var) -------------------------------------- */
  cueSlot(s) {
    if (this.song) refuse(RC.REFUSED, DETAIL.CUE_SONG);
    if (this.loaded < 0) refuse(RC.REFUSED, DETAIL.CUE_NOSLOT);
    if (!this.slots[s]) refuse(RC.REFUSED, DETAIL.CUE_EMPTY, `SLOT ${"ABCD"[s]} EMPTY`);
    if (this.q.slot === s) { this.q.now = true; return; }
    if (s === this.loaded) { this.q = { slot: -1, var: -1, now: false }; return; }
    this.cueQueue(s, -1);
  }
  cueVar(v) {
    if (this.song) refuse(RC.REFUSED, DETAIL.CUE_SONG);
    if (this.loaded < 0) refuse(RC.REFUSED, DETAIL.CUE_NOSLOT);
    if (this.q.var === v) { this.q.now = true; return; }
    if (this.q.slot < 0 && v === this.varCur) { this.q.var = -1; this.q.now = false; return; }
    this.cueQueue(this.q.slot, v);
  }
  cueQueue(qs, qv) {
    const s = qs >= 0 ? qs : this.loaded, v = qv >= 0 ? qv : qs >= 0 ? this.lastVar[qs] : this.varCur;
    this.q = { slot: -1, var: -1, now: false };
    if (s === this.loaded && v === this.varCur) return;
    if (this.faults.cueFull) refuse(RC.REFUSED, DETAIL.CUE_FULL);
    if (s !== this.loaded && this.faults.cuePark >= 0 && this.faults.cuePark !== s)
      refuse(RC.REFUSED, DETAIL.CUE_PARK + this.faults.cuePark, `SAVE ${"ABCD"[this.faults.cuePark]} FIRST`);
    this.q = { slot: qs, var: qv, now: false };
    if (!this.playing) this.switchQueued(); // stopped: at once; its STATE push comes from the pass after the reply
  }
  switchQueued() {
    if (this.q.slot < 0 && this.q.var < 0) return;
    const s = this.q.slot >= 0 ? this.q.slot : this.loaded, v = this.q.var >= 0 ? this.q.var : this.q.slot >= 0 ? this.lastVar[s] : this.varCur;
    this.q = { slot: -1, var: -1, now: false };
    this.switchTo(s, v);
  }
  /** the pattern's end (or the next step after the same key again): a queued switch happens; the pass' pushes */
  boundary() {
    this.switchQueued();
    return this.poll();
  }

  /* backup / restore -------------------------------------------------------------- */
  objBytes(obj) {
    if (obj === 0) return this.flash.upr && this.flash.upr.bytes;
    const s = (obj - 1) % 4, f = this.flashObjects(s);
    if (!f) return null;
    return obj < 5 ? f.record : obj < 9 ? f.locks : f.vars;
  }
  backupList() {
    const out = [];
    for (let obj = 0; obj < 13; obj++) {
      const b = this.objBytes(obj), s = (obj - 1) % 4;
      const src = obj === 0 ? this.flash.upr : obj < 5 ? this.flash.rec[s] : obj < 9 ? this.flash.lk[s] : this.flash.vr[s];
      let flags = 0;
      if (obj === 0 ? this.uprBytes().some((x, i) => x !== b[i]) : this.slotDirty(s)) flags |= BF.UNSAVED;
      if (obj > 0 && obj < 5 && b && decodeProject(b).old) flags |= BF.OLD; // (FDRF and older: converted at load)
      out.push(b ? { obj, present: 1, size: b.length, crc: crc32(b), seq: src.seq, flags } : { obj, present: 0, size: 0, crc: 0, seq: 0, flags });
    }
    return out;
  }
  abortRestore() {
    const r = this.restore;
    this.restore = null;
    if (!r) return;
    if (r.kind === RK.PROJECT) { // the RAM homes back from flash (proj_fetch): unsaved edits of that slot are gone
      const f = this.flashObjects(r.slot);
      this.slots[r.slot] = f ? this.imageOf(f) : r.slot === this.loaded ? this.slots[r.slot] : null;
    } else this.readUpr();
  }
  restoreOp(a) {
    const r = this.restore;
    if (a.op === RS.BEGIN) {
      if (this.playing) refuse(RC.STOP, DETAIL.STOP);
      if (r) this.abortRestore();
      if (a.kind > RK.PROJECT || a.slot > 3) refuse(RC.BAD, DETAIL.RANGE);
      const [p0, p1, p2] = a.parts;
      if (a.kind === RK.PRESETS) {
        if (a.magic !== (this.uprOld ? MAGIC.UPR1 : MAGIC.UPR) || p0.size !== (this.uprOld ? SZ.PRESETS_V1 : SZ.PRESETS) || p1.size || p2.size)
          refuse(RC.REFUSED, DETAIL.BAD_OBJECT);
      } else {
        const m = a.magic, txt = String.fromCharCode(m & 255, m >> 8 & 255, m >> 16 & 255);
        if (txt === "FDR" && !OLD_PROJECTS.has(m) && m !== MAGIC.PROJ && (m >>> 24) > 0x47) refuse(RC.REFUSED, DETAIL.NEWER);
        const size = m === MAGIC.PROJ ? SZ.PROJECT : OLD_PROJECTS.get(m);
        if (!size || p0.size !== size || (p1.size && p1.size !== SZ.LOCKS) || (p2.size && (p2.size < SZ.VARS_MIN || p2.size > SZ.VARS_MAX))
          || (!isFullRecord(m) && (p1.size || p2.size))) refuse(RC.REFUSED, DETAIL.BAD_OBJECT);
      }
      this.q = { slot: -1, var: -1, now: false };
      const c = this.faults.cuePark; // the park (var_buf[var_play ^ 1]) holds another slot's unsaved variations
      if (a.kind === RK.PROJECT && p2.size && c >= 0 && c !== a.slot) refuse(RC.REFUSED, DETAIL.CUE_PARK + c, `SAVE ${"ABCD"[c]} FIRST`);
      this.restore = { kind: a.kind, slot: a.kind === RK.PROJECT ? a.slot : 0, magic: a.magic, parts: a.parts.map((p) => ({ ...p, buf: new Uint8Array(p.size), next: 0, last: -1 })), at: this.t };
      return { op: a.op, kind: a.kind, slot: this.restore.slot };
    }
    if (!r) refuse(RC.BAD, DETAIL.NOT_RESTORING);
    r.at = this.t;
    if (a.op === RS.DATA) {
      const p = r.parts[a.part];
      if (!p || !p.size) refuse(RC.BAD, DETAIL.RANGE);
      if (a.offset !== p.next && a.offset !== p.last) refuse(RC.STALE, DETAIL.ORDER);
      if (a.offset % LIMITS.piece || a.data.length !== Math.min(LIMITS.piece, p.size - a.offset)) refuse(RC.BAD, DETAIL.LENGTH);
      p.buf.set(a.data, a.offset);
      p.last = a.offset;
      p.next = Math.max(p.next, a.offset + a.data.length);
      return { op: a.op, part: a.part, offset: a.offset, len: a.data.length };
    }
    if (a.op === RS.ABORT) { this.abortRestore(); return { op: a.op }; }
    /* COMMIT */
    if (this.playing) refuse(RC.STOP, DETAIL.STOP);
    if (r.parts.some((p) => p.next !== p.size)) refuse(RC.REFUSED, DETAIL.INCOMPLETE);
    if (r.parts.some((p) => p.size && crc32(p.buf) !== p.crc)) { this.abortRestore(); refuse(RC.REFUSED, DETAIL.CRC); }
    const bad = () => { this.abortRestore(); refuse(RC.REFUSED, DETAIL.INVALID); };
    const writes = () => { // the update check right before the writes; a failed write leaves flash's version
      if (this.faults.updateAtCommit) { this.faults.updateAtCommit = false; this.abortRestore(); refuse(RC.BUSY, DETAIL.NONE, "UPDATE"); }
      if (this.faults.flashError) { this.abortRestore(); refuse(RC.FLASH, DETAIL.NONE, "SAVE ERROR"); }
    };
    if (r.kind === RK.PRESETS) {
      let bank;
      try { bank = decodePresets(r.parts[0].buf); } catch { bad(); }
      if (bank.bad || bank.old !== this.uprOld || bank.slots.some((s) => s && s.model >= this.d.models.length)) bad();
      writes();
      this.flashWrite("upr", -1, r.parts[0].buf); // (written as sent; the bank then read back as the boot does)
      this.readUpr();
      this.restore = null;
      return { op: a.op, kind: r.kind, slot: 0, state: this.state() };
    }
    const [rec, lk, vr] = r.parts.map((p) => (p.size ? p.buf : null)), p = decodeProject(rec);
    if (!p.ok) bad();
    if (p.full) { // FDRG, FDRF: linked
      if (!!p.lk_n !== !!lk || !!p.var_len !== !!vr || (vr && vr.length !== p.var_len)) bad();
      if (lk) {
        const b = decodeLocks(lk);
        if (b.magic !== MAGIC.LK || b.count !== p.lk_n || lockLink(lk) !== p.lk_link || b.events.some((e) => !this.d.tp[e.param] || !this.d.tp[e.param].lock || e.value < -64)) bad();
      }
      if (vr) { try { if (decodeVars(vr).magic !== MAGIC.VAR || varLink(vr) !== p.var_link) bad(); } catch { bad(); } }
    }
    writes();
    const sealed = sealProject({ record: rec, locks: lk, vars: vr }, r.slot);
    if (sealed.vars) this.flashWrite("vr", r.slot, sealed.vars);
    if (sealed.locks) this.flashWrite("lk", r.slot, sealed.locks);
    this.flashWrite("rec", r.slot, sealed.record);
    this.restore = null;
    this.slots[r.slot] = this.imageOf(this.flashObjects(r.slot));
    if (r.slot === this.loaded) this.loadLive(r.slot); // the loaded slot: loaded again, as LOAD
    return { op: a.op, kind: r.kind, slot: r.slot, state: this.state() };
  }

  /* the user sample slots (§2.7) ------------------------------------------------------ */
  smpErase(k, off) { // one 4 KiB sector of slot k (never past it)
    if (off % SECTOR || off < 0 || off + SECTOR > SLOT_SIZE[k]) throw new Error(`mock: erase outside USR${k + 1}`);
    this.smp.flash[k].fill(255, off, off + SECTOR);
    this.smp.writes.push({ slot: k, off, n: SECTOR });
  }
  smpProg(k, off, b) {
    if (off < 0 || off + b.length > SLOT_SIZE[k]) throw new Error(`mock: write outside USR${k + 1}`);
    const f = this.smp.flash[k];
    for (let i = 0; i < b.length; i++) f[off + i] &= b[i];
    this.smp.writes.push({ slot: k, off, n: b.length });
  }
  smpHeader(k) { const p = parseHeader(this.smp.flash[k].subarray(0, HDR_SIZE)); return headerProblem(p, SLOT_MAX[k]) ? null : p; }
  smpWriteHeader(k, h) { this.smpProg(k, 4, h.subarray(4, HDR_SIZE)); this.smpProg(k, 0, h.subarray(0, 4)); } // the magic last
  /** a slot holding a sample (development, screenshots): flash written as an upload leaves it */
  seedSample(k, header, data) {
    for (let off = 0; off < DATA_OFF + data.length; off += SECTOR) this.smpErase(k, off);
    this.smpProg(k, DATA_OFF, data);
    this.smpWriteHeader(k, header);
  }
  /** power lost: RAM state gone (an upload running, a restore); opts.inHeader: END's header write torn after its bytes
   * 4..479, before the magic */
  powerCut(opts = {}) {
    if (opts.inHeader && this.smp.up) this.smpProg(this.smp.up.slot, 4, opts.inHeader.subarray(4, HDR_SIZE));
    this.smp.up = null;
    this.restore = null;
  }
  sampleOp(cmd, a) {
    const k = a.slot, STOP_UP = "STOP TO UPLOAD";
    if (k > 2) refuse(RC.BAD, DETAIL.RANGE);
    const up = this.smp.up, f = this.smp.flash[k], max = SLOT_MAX[k];
    const drop = () => { this.smp.up = null; };
    const mine = () => { if (!up || up.slot !== k) refuse(RC.BAD, DETAIL.NOT_RESTORING, "NO UPLOAD"); };
    const update = () => {
      if (!this.faults.updateAtCommit) return;
      this.faults.updateAtCommit = false;
      drop();
      refuse(RC.BUSY, DETAIL.NONE, "UPDATE");
    };
    switch (cmd) {
      case CMD.SMP_INFO: {
        const p = this.smpHeader(k);
        return p ? { slot: k, valid: 1, name: p.name, len: p.dataLen, crc: p.crc, max, nz: p.nz, n: p.zones[0].n, rate: p.zones[0].rate }
          : { slot: k, valid: 0, name: "", len: 0, crc: 0, max, nz: 0, n: 0, rate: 0 };
      }
      case CMD.SMP_BEGIN: {
        if (!a.len) refuse(RC.BAD, DETAIL.RANGE);
        if (a.len > max) refuse(RC.REFUSED, DETAIL.TOO_BIG);
        this.flashOk();
        if (this.playing) refuse(RC.STOP, DETAIL.STOP, STOP_UP);
        this.busy();
        update();
        if (this.faults.blocked.includes(k)) refuse(RC.REFUSED, DETAIL.BLOCKED, k === 1 ? "USR2 ERROR: NO SAVE" : "USR3 CLAMP FAILED");
        drop();
        this.smpErase(k, 0); // the header sector: the slot reads empty from here
        this.smp.up = { slot: k, len: a.len, crc: a.crc, next: 0, last: -1, at: this.t };
        return { slot: k, max };
      }
      case CMD.SMP_DATA: {
        mine();
        if (this.playing) refuse(RC.STOP, DETAIL.STOP, STOP_UP);
        const at = DATA_OFF + a.offset, want = Math.min(LIMITS.piece, up.len - a.offset);
        if (a.offset !== up.next && a.offset !== up.last) refuse(RC.STALE, DETAIL.ORDER);
        if (a.offset % LIMITS.piece || a.offset >= up.len || a.data.length !== want) refuse(RC.BAD, DETAIL.LENGTH);
        if (a.offset === up.last) { // a retry of the last piece: answered again when flash holds it
          if (a.data.some((x, i) => f[at + i] !== x)) refuse(RC.STALE, DETAIL.ORDER);
          return { slot: k, offset: a.offset, len: want };
        }
        if (at % SECTOR === WINDOW) { // the update loader's sweep: this piece holds the sector's +0xF00..+0xF4F
          const w = new Uint8Array(WINDOW_N).fill(255);
          w.set(a.data.subarray(0, WINDOW_N));
          const v = w[0] | w[1] << 8;
          if (v && v === crc16(w.subarray(2))) { drop(); refuse(RC.REFUSED, DETAIL.REFUSED_DATA); }
        }
        if (at % SECTOR === 0) this.smpErase(k, at); // the first piece into a sector erases it
        this.smpProg(k, at, a.data);
        up.last = a.offset;
        up.next = a.offset + want;
        return { slot: k, offset: a.offset, len: want };
      }
      case CMD.SMP_END: {
        mine();
        if (this.playing) refuse(RC.STOP, DETAIL.STOP, STOP_UP);
        if (a.header.length !== HDR_SIZE) refuse(RC.BAD, DETAIL.LENGTH);
        if (up.next !== up.len) refuse(RC.REFUSED, DETAIL.INCOMPLETE);
        const p = parseHeader(a.header);
        if (headerProblem(p, max) || p.dataLen !== up.len || p.crc !== up.crc) { drop(); refuse(RC.REFUSED, DETAIL.INVALID); }
        if (crc32(f.subarray(DATA_OFF, DATA_OFF + up.len)) !== up.crc) { drop(); refuse(RC.REFUSED, DETAIL.CRC); }
        update();
        if (this.faults.flashError) { drop(); refuse(RC.FLASH, DETAIL.NONE, "SAVE ERROR"); }
        this.smpWriteHeader(k, a.header);
        drop();
        return { slot: k, valid: this.smpHeader(k) ? 1 : 0 };
      }
      case CMD.SMP_READ: {
        const p = this.smpHeader(k);
        if (!p) refuse(RC.REFUSED, DETAIL.NOT_PRESENT);
        if (a.crc !== p.crc) refuse(RC.STALE, DETAIL.CHANGED);
        const total = DATA_OFF + p.dataLen;
        if (a.offset % LIMITS.piece || a.offset >= total) refuse(RC.BAD, DETAIL.RANGE);
        const data = f.slice(a.offset, Math.min(total, a.offset + LIMITS.piece));
        return { slot: k, offset: a.offset, len: data.length, data };
      }
      case CMD.SMP_CLEAR: {
        this.flashOk();
        if (this.playing) refuse(RC.STOP, DETAIL.STOP);
        this.busy();
        if (up && up.slot === k) drop();
        this.smpErase(k, 0);
        return { slot: k };
      }
      case CMD.SMP_RENAME: {
        if (!a.name.length || a.name.length > 8) refuse(RC.BAD, DETAIL.RANGE);
        this.flashOk();
        if (this.playing) refuse(RC.STOP, DETAIL.STOP);
        this.busy();
        if (!this.smpHeader(k)) refuse(RC.REFUSED, DETAIL.NOT_PRESENT);
        const sec = f.slice(0, SECTOR); // the header sector read, rewritten with the new name (the magic last)
        sec.fill(0, 8, 16);
        for (let i = 0; i < a.name.length; i++) sec[8 + i] = a.name.charCodeAt(i);
        this.smpErase(k, 0);
        this.smpProg(k, DATA_OFF, sec.subarray(DATA_OFF));
        this.smpWriteHeader(k, sec);
        return { slot: k, name: this.smpHeader(k).name };
      }
      default: return refuse(RC.BAD, DETAIL.UNKNOWN);
    }
  }

  /* pushes (WATCH) ----------------------------------------------------------------- */
  hashSteps(k) { return JSON.stringify(this.tracks[k].steps.map((s) => [s.flags, s.cond, s.note, s.micro | 0])); } // (the bytes)
  hashLocks(k) { return JSON.stringify(this.img().locks.filter((e) => e.track === k)); }
  shadowAll(knobsOnly = false) {
    this.sh = this.sh || {};
    this.sh.p = [...this.tracks[this.sel].p];
    this.sh.g = [...this.g];
    if (knobsOnly) return;
    this.sh.state = JSON.stringify(this.state());
    this.sh.steps = this.tracks.map((_, k) => this.hashSteps(k));
    this.sh.locks = this.tracks.map((_, k) => this.hashLocks(k));
  }
  shadowKnob(scope, track, id) {
    if (scope === SCOPE.GLOBAL) this.sh.g[id] = this.g[id];
    else if (track === NONE || track === this.sel) this.sh.p = [...this.tracks[this.sel].p]; // (a MODEL change moves 8 more)
  }
  shadowSteps(k) { this.sh.steps[k] = this.hashSteps(k); }
  shadowLocks(k) { this.sh.locks[k] = this.hashLocks(k); }
  /** one main-loop pass: at most 4 pushes, while watching */
  poll() {
    if (this.silent || this.t > this.watchUntil || !this.o.editor) return [];
    const out = [], sh = this.sh, room = () => out.length < 4;
    const st = JSON.stringify(this.state());
    if (st !== sh.state && room()) {
      const prev = sh.state && JSON.parse(sh.state);
      sh.state = st;
      if (prev && prev.sel !== this.sel) { sh.p = [...this.tracks[this.sel].p]; }
      out.push(encodePush(PUSH.STATE, { state: JSON.parse(st) }));
    }
    const t = this.tracks[this.sel];
    for (let id = 0; id < t.p.length && room(); id++) if (t.p[id] !== sh.p[id]) {
      sh.p[id] = t.p[id];
      const [text, unit] = this.text(this.knob(SCOPE.TRACK, this.sel, id), t.p[id]);
      out.push(encodePush(PUSH.CHANGED, { scope: SCOPE.TRACK, track: this.sel, id, value: t.p[id], text, unit }));
    }
    for (let id = 0; id < this.g.length && room(); id++) if (this.g[id] !== sh.g[id]) {
      sh.g[id] = this.g[id];
      const [text, unit] = this.text(this.knob(SCOPE.GLOBAL, 0, id), this.g[id]);
      out.push(encodePush(PUSH.CHANGED, { scope: SCOPE.GLOBAL, track: 0, id, value: this.g[id], text, unit }));
    }
    // editor_pattern.c ed_pattern_push: one STEPS_CHANGED and one LOCKS_CHANGED at most (track 7F: several)
    const ks = [0, 1, 2, 3, 4, 5, 6, 7].filter((k) => this.hashSteps(k) !== sh.steps[k]);
    if (ks.length && room()) {
      ks.forEach((k) => { sh.steps[k] = this.hashSteps(k); });
      out.push(encodePush(PUSH.STEPS_CHANGED, { slot: this.loaded < 0 ? NONE : this.loaded, var: this.varCur, track: ks.length > 1 ? NONE : ks[0] }));
    }
    const kl = [0, 1, 2, 3, 4, 5, 6, 7].filter((k) => this.hashLocks(k) !== sh.locks[k]);
    if (kl.length && room()) {
      kl.forEach((k) => { sh.locks[k] = this.hashLocks(k); });
      out.push(encodePush(PUSH.LOCKS_CHANGED, { slot: this.loaded < 0 ? NONE : this.loaded, track: kl.length > 1 ? NONE : kl[0] }));
    }
    return out;
  }
  drain() { const out = []; for (let f = this.poll(); f.length; f = this.poll()) out.push(...f); return out; }

  /* the device side (a user on the FM-1) ---------------------------------------------- */
  turn(track, id, v) { const t = this.tracks[track]; t.p[id] = clamp(v, this.trackDesc(t, id).min, this.trackDesc(t, id).max); return this.kick(); }
  turnGlobal(id, v) { this.g[id] = clamp(v, this.d.gp[id].min, this.d.gp[id].max); return this.kick(); }
  select(t) { this.sel = t; return this.kick(); }
  play() { this.playing = true; return this.kick(); }
  stop() { this.playing = false; this.q = { slot: -1, var: -1, now: false }; return this.kick(); }
  setSong(on) { this.song = !!on; return this.kick(); }
  editStep(track, step, s) { this.tracks[track].steps[step] = { ...emptyStep(), ...s }; return this.kick(); }
  lockOnDevice(track, step, param, value) {
    this.img().locks.push({ track, step, param, kind: LK.LOCK, value });
    this.img().locks.sort((x, y) => lockOrd(x) - lockOrd(y));
    return this.kick();
  }
  kick() { // a pass, its pushes delivered to the access' input (if attached)
    const out = this.poll();
    this.deliver(out);
    return out;
  }

  /* Web MIDI -------------------------------------------------------------------- */
  deliver(frames) {
    if (!frames.length || !this.inputs.length) return;
    setTimeout(() => {
      for (const f of frames) for (const inp of this.inputs) inp.onmidimessage && inp.onmidimessage({ data: f, timeStamp: this.t });
    }, this.o.latency);
  }
  get access() {
    if (this._access) return this._access;
    const name = this.o.name, input = { id: "mock-in", name, manufacturer: "", type: "input", state: "connected",
      connection: "open", onmidimessage: null, open: async () => input, close: async () => input };
    const output = { id: "mock-out", name, manufacturer: "", type: "output", state: "connected", connection: "open",
      open: async () => output, close: async () => output,
      send: (data) => { const out = this.feed(data); this.deliver(out); } };
    this.inputs.push(input);
    this._access = { inputs: new Map([[input.id, input]]), outputs: new Map([[output.id, output]]), sysexEnabled: true, onstatechange: null };
    return this._access;
  }
}
