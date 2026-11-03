// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The Presets tab (web/EDITOR_PROTOCOL.md §2.3, §2.5, §2.6, §5): left, the factory banks (MACHINEDRUM / MODERN /
// CLASSIC, NAMES) loaded onto the selected track (PRESET_LOAD); right, the 96 user slots as a list the page edits
// (rename, delete, reorder, "Save track N here" from DUMP, each preset's FX shown and edited or dropped), read from the
// FM-1 (BACKUP_LIST / BACKUP_GET of object 0), kept in "hortator-presets" files and written back whole (RESTORE kind 0:
// BEGIN, DATA, COMMIT; ABORT on cancel). An FM-1 before UPR2 (128 voices) is read, not written.
// Owner decision 2: the names live only in the page and the files; the FM-1 keeps showing "MODEL NN".
// PresetsModel holds no DOM (web/editor/test_tab_presets.mjs drives it against the mock device); mountPresets draws it.
// A tab module exports `tab = { id, title, cap, mount(el, ctx) }` (app.mjs); mount returns { show, destroy }.
import {
  BF, CAP, CMD, DETAIL, FileError, LAYOUT, LIMITS, MAGIC, NONE, RC, RK, RS, SCOPE, SZ, UPR_FX_LABELS, crc32, crcHex,
  decodePresets, encodePresets, makePresetsFile, readPresetsFile, userPresetName,
} from "./proto.mjs";
import { clockGuard } from "./session.mjs";
import { esc, chooser } from "./ui.mjs";

const P_MODEL = 0, P_E0 = 1; // core.h: P_MODEL, P_E0..P_E7 (DUMP's p list)
const NAME_MAX = 24;
const NAMES_KEY = "hortator-preset-names"; // the page's memory of the names given, by sound (this browser only)
const NFX = LAYOUT.uprFx.length;
const noMoves = () => Array(LAYOUT.nuserOld - LAYOUT.nuser).fill(0);
const sameFx = (a, b) => !!a.voiceOnly === !!b.voiceOnly && (!!a.voiceOnly || a.fx.every((x, i) => x === b.fx[i]));
/** a SAMPLE preset's ATK SUS REL LOOP PLAY as the FX panel shows them ("" none) */
export function envText(env) {
  if (!Array.isArray(env)) return "";
  const [a, su, r, lp, pl, ct] = LAYOUT.envDef.map((d, i) => env[i] ?? d);
  return `ATK ${a} · SUS ${su} · REL ${r} · LOOP ${["OFF", "FWD", "PING", "LOOPI", "ONE"][lp] ?? lp} · PLAY ${pl ? "CHOP" : "KEY"} · CUT ${ct ? "ON" : "OFF"}`;
}
const sameEnv = (a, b) => String(a.env ?? "") === String(b.env ?? "");   // (SAMPLE: ATK SUS REL LOOP)
const same = (a, b) => (!a && !b) || (!!a && !!b && a.model === b.model && a.knobs.every((k, i) => k === b.knobs[i]) && sameFx(a, b) && sameEnv(a, b));
// (the names given, by sound: the voice, and the FX when the preset holds them)
const soundKey = (s) => `${s.model}:${s.knobs.join(",")}${s.voiceOnly ? "" : `:${s.fx.join(",")}`}${s.env ? `:${s.env.join(",")}` : ""}`;
const slotOf = (s, name) => s && { name, model: s.model, knobs: [...s.knobs], fx: [...s.fx], voiceOnly: !!s.voiceOnly, ...(s.env ? { env: [...s.env] } : {}) };
const pad3 = (n) => String(n).padStart(3, "0");

/** an error to show: the device's own message and what it means */
export function explain(e) {
  if (!e) return "";
  if (e instanceof FileError || e.rc === undefined) return e.message;
  const m = `FM-1: ${e.message}`;
  if (e.rc === RC.STOP) return `${m}. Stop the FM-1 (PLAY), then try again. Nothing was written.`;
  if (e.rc === RC.BUSY) return e.detail === DETAIL.RESTORING ? `${m}. Another restore is running: wait for it.`
    : `${m}. An update started: nothing was written.`;
  if (e.rc === RC.FLASH) return `${m}. The FM-1 kept its old presets.`;
  if (e.rc === RC.REFUSED && e.detail === DETAIL.NO_FLASH) return `${m}. No flash this boot: power-cycle the FM-1.`;
  if (e.rc === RC.REFUSED && e.detail >= DETAIL.BAD_OBJECT && e.detail <= DETAIL.INCOMPLETE) return `${m}. Nothing was written.`;
  return m;
}
export class Cancelled extends Error { constructor() { super("Cancelled: the FM-1 keeps its presets."); this.cancelled = true; } }

export class PresetsModel {
  /** opts.storage: localStorage (the default) or null: where the names given are remembered */
  constructor(session, opts = {}) {
    this.s = session;
    this.storage = opts.storage === undefined ? safeStorage() : opts.storage;
    this.handlers = new Map();
    this.modelNames = [];
    this.banks = []; // [{ bank, name, presets: [{ id, model, name }] }]
    this.slots = Array(LAYOUT.nuser).fill(null); // the list: null | { name, model, knobs[8], fx[23], voiceOnly }
    this.moved = noMoves(); // UPR1's slots 97..128: where they moved (the record's, kept as read; a file's)
    this.lost = 0; // presets an older file (128 slots) held that did not fit the 96
    this.fileNote = ""; // what opening a file says beyond the list (what was moved, lost)
    this.fxDesc = UPR_FX_LABELS.map((label) => ({ label, min: -128, max: 127, names: [] })); // DESC of each FX
    this.device = null; // the FM-1's 96 slots as last read / written (null: not read)
    this.deviceOld = false; // the FM-1 runs a firmware before UPR2 (128 voices): read (moved in), never written
    this.deviceInfo = null; // BACKUP_LIST's entry of object 0
    this.source = ""; // "FM-1" or a file's name
    this.job = null; // a write running: { cancelled, committing }
    this.loaded = false;
  }
  on(ev, fn) { if (!this.handlers.has(ev)) this.handlers.set(ev, new Set()); this.handlers.get(ev).add(fn); return () => this.handlers.get(ev).delete(fn); }
  emit(ev, x) { for (const fn of this.handlers.get(ev) || []) fn(x); }
  get canRead() { return this.s.has(CMD.BACKUP_GET); }
  get canWrite() { return this.s.has(CMD.RESTORE) && !!((this.s.info?.caps ?? 0) & CAP.UPR2); }
  get track() { return this.s.state?.sel ?? 0; }
  get nfact() { return this.s.info?.nfact ?? 151; }

  /** what the tab shows: the model names, the factory banks, the FM-1's user slots */
  async load() {
    await this.readModelNames();
    await this.readFxDescs().catch(() => {});
    await this.readFactory();
    if (this.canRead) await this.readDevice();
    this.loaded = true;
    this.emit("render");
  }
  async readModelNames() {
    const a = { scope: SCOPE.TRACK, track: NONE, id: P_MODEL };
    const r = await this.s.request(CMD.DESC, { ...a, first: 0 }), names = [...r.items];
    while (names.length < r.nnames) {
      const more = await this.s.request(CMD.DESC, { ...a, first: names.length });
      if (!more.items.length) break;
      names.push(...more.items);
    }
    this.modelNames = names;
  }
  /** each FX's range and names (DESC of the selected track's knob: INSERT A..C's labels follow its TYPE) */
  async readFxDescs() {
    const out = [];
    for (let k = 0; k < NFX; k++) {
      const a = { scope: SCOPE.TRACK, track: NONE, id: LAYOUT.uprFx[k] };
      const r = await this.s.request(CMD.DESC, { ...a, first: 0 }), names = [...r.items];
      while (names.length < r.nnames) {
        const more = await this.s.request(CMD.DESC, { ...a, first: names.length });
        if (!more.items.length) break;
        names.push(...more.items);
      }
      out.push({ label: UPR_FX_LABELS[k], min: r.min, max: r.max, names });
    }
    this.fxDesc = out;
  }
  /** FX k's value as the FM-1 shows it: an enum's name, else the number */
  fxText(k, v) { const d = this.fxDesc[k]; return d && d.names.length && v >= d.min && v - d.min < d.names.length ? d.names[v - d.min] : String(v); }
  async readFactory() {
    const banks = [];
    for (let bank = 0; bank < (this.s.info?.nbanks ?? 3); bank++) {
      const b = { bank, name: "", presets: [] };
      for (let first = 0; ;) { // as many as fit a reply: ask on from there
        const r = await this.s.request(CMD.NAMES, { bank, first });
        b.name = r.name;
        b.presets.push(...r.presets);
        first = r.first + r.presets.length;
        if (first >= r.total || !r.presets.length) break;
      }
      banks.push(b);
    }
    this.banks = banks;
  }
  /** the factory presets of a bank (-1: all) whose name or model name contains q */
  factory(q = "", bank = -1) {
    const t = q.trim().toUpperCase();
    return this.banks.filter((b) => bank < 0 || b.bank === bank).flatMap((b) => b.presets.map((p) => ({ ...p, bank: b.bank })))
      .filter((p) => !t || p.name.toUpperCase().includes(t) || (this.modelNames[p.model] || "").toUpperCase().includes(t));
  }
  /** PRESET_LOAD onto a track (the selected one): MODEL and the 8 SOUND knobs (a user preset: its FX too), as the FM-1's PRESETS list */
  async loadPreset(id, track = this.track) {
    const r = await this.s.request(CMD.PRESET_LOAD, { track, id });
    // the selected track's knobs that moved come as CHANGED pushes (as a knob turned on the FM-1); the preset id and
    // another track do not: the views showing that track read it again
    this.s.reloaded({ track: r.track });
    return r;
  }

  /* ------------------------------------------------------------------ the user list --- */
  /** the FM-1's 96 user slots (flash: object 0) into the list (an FM-1 before UPR2: its 128 moved in, as it will) */
  async readDevice() {
    for (let tries = 0; ; tries++) {
      const list = await this.s.request(CMD.BACKUP_LIST);
      const o = list.objects.find((x) => x.obj === 0);
      let slots, bank = null;
      if (!o || !o.present) slots = Array(LAYOUT.nuser).fill(null); // no user bank saved yet
      else {
        const b = new Uint8Array(o.size);
        try {
          for (let off = 0; off < o.size; off += LIMITS.piece) {
            const r = await this.s.request(CMD.BACKUP_GET, { obj: 0, offset: off, seq: o.seq });
            b.set(r.data.subarray(0, Math.min(r.data.length, o.size - off)), off);
          }
        } catch (e) {
          if (e.rc === RC.STALE && tries < 3) continue; // saved in between: LIST and read it again
          throw e;
        }
        if (crc32(b) !== o.crc) { if (tries < 3) continue; throw new Error("The FM-1's user presets read back damaged (CRC)."); }
        bank = decodePresets(b);
        slots = bank.slots;
      }
      this.deviceInfo = o || null;
      this.deviceOld = !((this.s.info?.caps ?? 0) & CAP.UPR2);
      this.device = slots;
      this.moved = bank ? [...bank.moved] : noMoves();
      this.deviceFrom = bank && bank.from ? bank.from : []; // (an FM-1 before UPR2: the older slot each came from)
      this.lost = bank ? bank.lost : 0;
      this.fileNote = "";
      this.slots = slots.map((s, i) => slotOf(s, s && this.nameOf(s, i)));
      this.source = "FM-1";
      this.emit("render");
      return this.slots;
    }
  }
  defaultName(s, i) { return userPresetName(i, this.modelNames[s.model] || "USER"); }
  nameOf(s, i) { return this.memory()[soundKey(s)] || this.defaultName(s, i); }
  memory() { try { return JSON.parse(this.storage?.getItem(NAMES_KEY) || "{}") || {}; } catch { return {}; } }
  remember() {
    if (!this.storage) return;
    const m = this.memory();
    this.slots.forEach((s, i) => { if (s && s.name && s.name !== this.defaultName(s, i)) m[soundKey(s)] = s.name; });
    try { this.storage.setItem(NAMES_KEY, JSON.stringify(m)); } catch { /* storage off or full */ }
  }
  /** per slot: the list's sound differs from the FM-1's (names do not count); null: the FM-1 not read */
  changed() { return this.device ? this.slots.map((s, i) => !same(s, this.device[i])) : null; }
  get differs() { const c = this.changed(); return !c || c.some(Boolean); }
  /** user slot i can be loaded onto a track: the FM-1 holds the same sound there */
  canLoadUser(i) {
    return !!this.slots[i] && !!this.device && same(this.slots[i], this.device[i]) && (!this.deviceOld || this.deviceFrom[i] === i);
  }
  loadUser(i, track = this.track) { return this.loadPreset(this.nfact + i, track); }

  rename(i, name) {
    const s = this.slots[i];
    if (!s) return;
    s.name = String(name ?? "").replace(/[\u0000-\u001f\u007f]/g, "").trim().slice(0, NAME_MAX) || this.defaultName(s, i);
    this.remember();
    this.emit("render");
  }
  remove(i) { this.slots[i] = null; this.emit("render"); }
  /** drag to reorder: the slot at `from` goes to `to`, the ones between shift by one */
  move(from, to) {
    const n = LAYOUT.nuser;
    if (from === to || from < 0 || to < 0 || from >= n || to >= n) return;
    const [x] = this.slots.splice(from, 1);
    this.slots.splice(to, 0, x);
    this.emit("render");
  }
  /** FX k of slot i (a preset that holds its FX), inside the FX's range */
  setFx(i, k, v) {
    const s = this.slots[i], d = this.fxDesc[k];
    if (!s || s.voiceOnly || !d || !Number.isFinite(Number(v))) return;
    s.fx[k] = Math.max(d.min, Math.min(d.max, Math.round(Number(v))));
    this.emit("render");
  }
  /** slot i keeps its FX (loading it sets them) or is the voice only (loading it leaves the track's FX) */
  keepFx(i, on) {
    const s = this.slots[i];
    if (!s) return;
    s.voiceOnly = !on;
    this.emit("render");
  }
  /** "Save track N here": the track's MODEL, SOUND knobs and FX (DUMP) into the list's slot i (sent by Write to FM-1) */
  async saveTrack(i, track = this.track) {
    const d = await this.s.request(CMD.DUMP, { track }), c8 = (k) => Math.max(-128, Math.min(127, k));
    const s = { model: d.model, knobs: d.p.slice(P_E0, P_E0 + 8).map(c8), fx: LAYOUT.uprFx.map((id) => c8(d.p[id])), voiceOnly: false };
    if (d.model === LAYOUT.smpl) s.env = d.p.slice(LAYOUT.pStored + LAYOUT.xpDef.length, LAYOUT.pcount); // ATK SUS REL LOOP PLAY CUT
    s.name = this.memory()[soundKey(s)] || `${(this.modelNames[d.model] || "USER").slice(0, 5)} T${d.track + 1}`;
    this.slots[i] = s;
    this.emit("render");
    return s;
  }
  /** the UPR2 record the list makes (what Write to FM-1 sends; the moves as read) */
  record() { return encodePresets(this.slots, this.moved); }

  /* ------------------------------------------------------------------------ files --- */
  /** a "hortator-presets" file (§5) of the list, plus `upr2`: size and CRC-32 of the record its slots make (checked
   * on reading when present) */
  toFile(meta = {}) {
    const f = makePresetsFile({ slots: this.slots, moved: this.moved }, this.modelNames, { firmware: this.s.info?.firmware || "", ...meta });
    const rec = this.record();
    f.upr2 = { size: rec.length, crc32: crcHex(crc32(rec)) };
    return f;
  }
  fileName(d = new Date()) {
    return `hortator-presets-${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}.json`;
  }
  /** a file (text or parsed: hortator-presets, or the presets of a hortator-backup) into the list; FileError if not.
   * A version 1 file (128 voices) is moved into the 96 slots as the FM-1 moves its own (fileNote says what) */
  fromFile(text, name = "file") {
    let f;
    try { f = typeof text === "string" ? JSON.parse(text) : text; } catch { throw new FileError("format", "Not a JSON file."); }
    if (f && f.format === "hortator-backup" && f.presets) f = { ...f, format: "hortator-presets", slots: f.presets.slots, moved: f.presets.moved };
    const { slots, moved, lost } = readPresetsFile(f); // (its checksum checked: upr2, or a version 1 file's upr1)
    this.slots = slots.map((s, i) => s && { ...s, name: s.name || this.defaultName(s, i) });
    this.moved = moved;
    this.lost = lost;
    this.fileNote = f.version < 2 ? `An older file: its presets past 96 moved into empty slots, the sound only${lost
      ? `; ${lost} preset${lost === 1 ? "" : "s"} did not fit the 96 slots (lost)` : ""}.` : "";
    this.source = name;
    this.remember();
    this.emit("render");
    return this.slots;
  }

  /* ------------------------------------------------------------- write to the FM-1 --- */
  /** what the confirmation says before the FM-1's 96 slots are replaced */
  confirmText() {
    const used = this.slots.filter(Boolean).length, c = this.changed(), n = c ? c.filter(Boolean).length : 0;
    let t = `Replace the FM-1's ${LAYOUT.nuser} user presets with this list (${used} used)?${c ? ` ${n} slot${n === 1 ? "" : "s"} change.` : ""}`;
    if (this.deviceInfo && this.deviceInfo.flags & BF.UNSAVED) t += " The FM-1's user preset changes not saved to flash are replaced too.";
    return `${t} Names stay in this page and its files; the FM-1 shows "MODEL NN".`;
  }
  /** what the FM-1 would refuse in the list (a model it does not have): [] = fine */
  check() {
    const n = this.modelNames.length || this.s.info?.nmodels || 0;
    return this.slots.map((s, i) => (s && n && s.model >= n ? `Slot ${i + 1}: model ${s.model} is not in this firmware.` : null)).filter(Boolean);
  }
  cancel() { if (this.job && !this.job.committing) this.job.cancelled = true; }
  /** RESTORE of object 0 (stopped only): BEGIN, the pieces, COMMIT. cancel() between pieces sends ABORT: nothing is
   * written. onProgress({ done, total }) in bytes. The FM-1's refusals reject with its message (explain()). */
  async write(onProgress = () => {}) {
    if (this.job) throw new Error("A write is already running.");
    if (!this.canWrite) throw new Error(this.s.has(CMD.RESTORE) ? "Update the FM-1 first: its firmware keeps 128 presets "
      + "without FX (the new one moves them into 96 at its first start)." : "Not in this firmware.");
    const bad = this.check();
    if (bad.length) throw new Error(bad[0]);
    const release = this.s.lockRestore("Presets", "the user presets"); // (another tab's restore: throws, nothing sent)
    const rec = this.record(), total = rec.length, job = this.job = { cancelled: false, committing: false };
    let begun = false;
    try {
      await this.s.request(CMD.RESTORE, { op: RS.BEGIN, kind: RK.PRESETS, slot: 0, magic: MAGIC.UPR,
        parts: [{ size: SZ.PRESETS, crc: crc32(rec) }, { size: 0, crc: 0 }, { size: 0, crc: 0 }] });
      begun = true;
      onProgress({ done: 0, total });
      for (let off = 0; off < total; off += LIMITS.piece) {
        if (job.cancelled) throw new Cancelled();
        await this.s.request(CMD.RESTORE, { op: RS.DATA, part: 0, offset: off, data: rec.subarray(off, off + LIMITS.piece) });
        onProgress({ done: Math.min(total, off + LIMITS.piece), total });
      }
      if (job.cancelled) throw new Cancelled();
      job.committing = true;
      await this.s.request(CMD.RESTORE, { op: RS.COMMIT });
      begun = false;
      this.remember();
      this.device = decodePresets(rec).slots;
      const names = this.slots.map((s) => s && s.name);
      await this.readDevice().catch(() => {}); // what flash holds now
      this.slots.forEach((s, i) => { if (s && names[i] && same(s, this.device[i])) s.name = names[i]; });
      return true;
    } catch (e) {
      if (begun) await this.s.request(CMD.RESTORE, { op: RS.ABORT }).catch(() => {}); // the FM-1 reloads flash's
      throw e;
    } finally {
      this.job = null;
      release();
      this.emit("render");
    }
  }
}

function safeStorage() { try { return globalThis.localStorage || null; } catch { return null; } }

/* -------------------------------------------------------------------------- the view --- */
const CSS = `
.pr { display: grid; grid-template-columns: minmax(0, 2fr) minmax(0, 3fr); gap: 16px; margin-top: 16px; }
.pr-col { border: 2px solid var(--c1); min-width: 0; display: flex; flex-direction: column; }
.pr-col > h3 { margin: 0; padding: 0 8px; background: var(--c1); color: var(--c5); font: inherit; letter-spacing: .1em; text-transform: uppercase; }
.pr-bar { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; padding: 8px; border-bottom: 2px solid var(--c1); }
.pr-bar input[type=search] { flex: 1; min-width: 10ch; background: var(--k); color: var(--c5); border: 2px solid var(--c2); border-radius: 0; font: inherit; padding: 0 4px; }
.pr button { background: var(--k); color: var(--c5); border: 2px solid var(--c2); border-radius: 0; cursor: pointer; padding: 0 8px; }
.pr button:hover:not(:disabled) { border-color: var(--w); color: var(--w); }
.pr button.main { background: var(--w); border-color: var(--w); color: var(--k); }
.pr button.main:hover:not(:disabled) { background: var(--k); color: var(--w); }
.pr button:disabled { opacity: .4; cursor: default; }
.pr button[aria-pressed=true] { background: var(--c2); color: var(--w); }
.pr-list { list-style: none; margin: 0; padding: 0; overflow-y: auto; max-height: 70vh; }
.pr-list li { display: flex; align-items: center; gap: 8px; padding: 0 8px; border-bottom: 1px solid var(--c1); min-height: 28px; }
.pr-list li .n { color: var(--c2); width: 4ch; flex: none; }
.pr-list li .nm { flex: 1; min-width: 0; color: var(--c5); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.pr-list li .md { color: var(--c3); width: 6ch; flex: none; white-space: nowrap; overflow: hidden; }
.pr-list li .bk { color: var(--c2); width: 3ch; flex: none; }
.pr-fac li { cursor: pointer; }
.pr-fac li:hover .nm { color: var(--w); }
.pr-fac li.on { background: var(--w); }
.pr-fac li.on span { color: var(--k); }
.pr-usr li.empty .nm { color: var(--c2); }
.pr-usr li.chg .n { color: var(--w); }
.pr-usr li.chg .n::after { content: "*"; }
.pr-usr li.over { outline: 2px dashed var(--w); outline-offset: -2px; }
.pr-usr li .grip { cursor: grab; color: var(--c2); flex: none; user-select: none; }
.pr-usr li input { flex: 1; min-width: 0; background: transparent; color: var(--c5); border: 0; border-bottom: 1px dashed var(--c2); font: inherit; padding: 0; }
.pr-usr li input:focus { color: var(--w); border-bottom-color: var(--w); outline: none; }
.pr-usr li button { padding: 0 4px; border-width: 1px; }
.pr-st { padding: 4px 8px; color: var(--c3); min-height: 32px; }
.pr-st.alert { color: var(--k); background: var(--w); }
.pr-prog { display: flex; gap: 8px; align-items: center; padding: 4px 8px; border-bottom: 2px solid var(--c1); }
.pr-prog progress { flex: 1; height: 8px; accent-color: var(--w); }
.pr-usr li .fxb { color: var(--c3); width: 5ch; flex: none; font-size: .85em; }
.pr-usr li.pr-fx { display: block; padding: 4px 8px 8px 5ch; background: var(--k); }
.pr-fx .keep { display: flex; gap: 8px; align-items: center; color: var(--c5); margin-bottom: 4px; }
.pr-fx .keep input { flex: none; width: auto; border: 0; }
.pr-fx .grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(11ch, 1fr)); gap: 4px 8px; }
.pr-fx .grid label { display: flex; flex-direction: column; color: var(--c3); font-size: .85em; min-width: 0; }
.pr-fx .grid input, .pr-fx .grid select { width: 100%; background: var(--k); color: var(--c5); border: 1px solid var(--c2); border-radius: 0; font: inherit; padding: 0 2px; }
.pr-fx .grid :disabled { opacity: .5; }
@media (max-width: 760px) { .pr { grid-template-columns: 1fr; } .pr-list { max-height: 50vh; } }
`;

function download(name, text) {
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([text], { type: "application/json" }));
  a.download = name;
  document.body.append(a);
  a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
}
const ask = async (ctx, msg) => (ctx.confirm ? !!(await ctx.confirm(msg)) : true);

export function mountPresets(el, ctx) {
  const s = ctx.session, m = new PresetsModel(s), say = (msg) => ctx.toast?.(msg);
  if (!document.getElementById("pr-css")) document.head.append(Object.assign(document.createElement("style"), { id: "pr-css", textContent: CSS }));
  let bank = -1, query = "", pick = null, drag = -1;
  const open = new Set(); // the user slots whose FX are shown
  el.innerHTML = `<div class="snd-head"><h2>Presets</h2><span class="snd-sub pr-sub"></span>
      <span class="snd-load" hidden>Reading the FM-1…</span></div>
    <div class="pr">
      <section class="pr-col" aria-label="Factory presets"><h3>Factory</h3>
        <div class="pr-bar"><span role="group" aria-label="Bank" class="pr-banks"></span>
          <input type="search" placeholder="Search" aria-label="Search factory presets"></div>
        <div class="pr-bar"><button type="button" class="main pr-load" disabled>Load onto track 1</button>
          <span class="snd-sub">pick one, or double-click</span></div>
        <ul class="pr-list pr-fac" role="listbox" aria-label="Factory presets"></ul></section>
      <section class="pr-col" aria-label="User presets"><h3>User · ${LAYOUT.nuser} slots</h3>
        <div class="pr-bar"><button type="button" data-a="read">Read FM-1</button><button type="button" data-a="open">Open file…</button>
          <button type="button" data-a="backup">Back up</button><button type="button" class="main" data-a="write">Write to FM-1</button>
          <input type="file" accept=".json,application/json" hidden></div>
        <div class="pr-prog" hidden><progress max="1" value="0"></progress><span></span><button type="button" data-a="cancel">Cancel</button></div>
        <div class="pr-st" role="status"></div>
        <ul class="pr-list pr-usr" aria-label="User preset slots"></ul></section>
    </div>`;
  const q = (c) => el.querySelector(c), fac = q(".pr-fac"), usr = q(".pr-usr"), st = q(".pr-st"), file = q("input[type=file]");
  const prog = q(".pr-prog"), busy = () => !!m.job;
  const clockOk = () => clockGuard(s, chooser(ctx), say); // playing with SYNC out on: Stop / Continue / Cancel
  const fail = (e) => { if (!e?.cancelled) say(explain(e)); };

  const drawBanks = () => {
    q(".pr-banks").innerHTML = [[-1, "ALL"], ...m.banks.map((b) => [b.bank, b.name === "MACHINEDRUM" ? "MD" : b.name])]
      .map(([b, n]) => `<button type="button" data-b="${b}" aria-pressed="${b === bank}">${esc(n)}</button>`).join(" ");
  };
  const drawLoad = () => {
    const b = q(".pr-load");
    b.textContent = `Load onto track ${m.track + 1}`;
    b.disabled = pick === null || busy();
    q(".pr-sub").textContent = `track ${m.track + 1} selected`;
  };
  const drawFactory = () => {
    fac.innerHTML = m.factory(query, bank).map((p) => `<li role="option" data-id="${p.id}" class="${p.id === pick ? "on" : ""}"
      aria-selected="${p.id === pick}"><span class="bk">${["MD", "MOD", "CLS"][p.bank] || ""}</span><span class="nm">${esc(p.name)}</span>
      <span class="md">${esc(m.modelNames[p.model] || "")}</span></li>`).join("")
      || `<li><span class="nm">${m.loaded ? "No preset matches." : "…"}</span></li>`;
    drawLoad();
  };
  const drawStatus = () => {
    const chg = m.changed(), n = chg ? chg.filter(Boolean).length : 0, used = m.slots.filter(Boolean).length;
    let t = `${used} of ${LAYOUT.nuser} used · from ${m.source || "—"}. `;
    if (m.deviceOld && m.device) t += `This FM-1's firmware keeps 128 presets without FX: the list shows them as the new firmware moves them${m.lost && m.source === "FM-1" ? ` (${m.lost} would not fit)` : ""}; update the FM-1 to write presets. `;
    if (m.fileNote) t += `${m.fileNote} `;
    if (!m.device) t += "The FM-1's list is not read yet.";
    else if (n) t += `${n} slot${n === 1 ? " differs" : "s differ"} from the FM-1 (*): Write to FM-1 to send ${n === 1 ? "it" : "them"}.`;
    else t += "Same as the FM-1.";
    if (m.deviceInfo && m.deviceInfo.flags & BF.UNSAVED) t += " The FM-1 has user preset changes not saved to flash.";
    if (s.state?.playing && m.canWrite) t += " Stop the FM-1 to write.";
    const other = s.restoreBlocked("Presets");
    if (other && m.canWrite) t += ` ${other} Write to FM-1 waits for it.`;
    st.textContent = t;
    st.classList.remove("alert");
    for (const b of el.querySelectorAll(".pr-bar [data-a]")) {
      const a = b.dataset.a;
      b.disabled = busy() || (a === "read" && !m.canRead) || (a === "write" && (!m.canWrite || !m.loaded || !!other));
      if (a === "write") b.title = other || "";
    }
  };
  const fxPanel = (x, i, w, dis) => {
    const cells = m.fxDesc.map((d, k) => {
      const v = x.fx[k], off = dis(w || x.voiceOnly);
      const ctl = d.names.length
        ? `<select data-k="${k}" aria-label="${esc(d.label)} of slot ${i + 1}"${off}>${d.names.map((n, j) => `<option value="${d.min + j}"${d.min + j === v ? " selected" : ""}>${esc(n)}</option>`).join("")}</select>`
        : `<input type="number" data-k="${k}" min="${d.min}" max="${d.max}" value="${v}" aria-label="${esc(d.label)} of slot ${i + 1}"${off}>`;
      return `<label><span>${esc(d.label)}</span>${ctl}</label>`;
    }).join("");
    return `<li class="pr-fx" data-i="${i}"><label class="keep"><input type="checkbox" data-k="keep"${x.voiceOnly ? "" : " checked"}${dis(w)}>
      Loading it sets these FX (off: the sound only, the track keeps its own FX)</label><div class="grid">${cells}</div>${x.env
      ? `<p class="env">SAMPLE: ${esc(envText(x.env))} (set by the sound when it loads)</p>` : ""}</li>`;
  };
  const drawUser = () => {
    const chg = m.changed(), t = m.track + 1, w = busy(), dis = (c) => (c ? " disabled" : "");
    usr.innerHTML = m.slots.map((x, i) => `<li data-i="${i}" draggable="${!!x && !w}" class="${x ? "" : "empty"}${chg?.[i] ? " chg" : ""}">
      <span class="n">${pad3(i + 1)}</span>${x ? `<span class="grip" title="Drag to reorder" aria-hidden="true">::</span>
      <input value="${esc(x.name)}" maxlength="${NAME_MAX}" aria-label="Name of slot ${i + 1}"${dis(w)}>
      <span class="md">${esc(m.modelNames[x.model] || x.model)}</span>
      <span class="fxb" title="${x.voiceOnly ? "The sound only: loading it leaves the track's FX" : "The sound and its FX"}">${x.voiceOnly ? "SOUND" : "+FX"}</span>
      <button type="button" data-a="fx" aria-expanded="${open.has(i)}" title="Show / edit the FX this preset holds">FX</button>
      <button type="button" data-a="up" title="Move up"${dis(w || !i)}>↑</button><button type="button" data-a="down" title="Move down"${dis(w || i === LAYOUT.nuser - 1)}>↓</button>
      <button type="button" data-a="load" title="Load onto track ${t}"${dis(w || !m.canLoadUser(i))}>Load</button>`
      : '<span class="nm">— empty —</span>'}
      <button type="button" data-a="save" title="Save track ${t}'s sound and FX here"${dis(w)}>Save T${t}</button>
      ${x ? `<button type="button" data-a="del" title="Delete" aria-label="Delete slot ${i + 1}"${dis(w)}>×</button>` : ""}</li>${x && open.has(i) ? fxPanel(x, i, w, dis) : ""}`).join("");
    drawStatus();
  };
  const render = () => { drawBanks(); drawFactory(); drawUser(); };

  const load = async (id) => {
    try {
      const r = await m.loadPreset(id);
      const name = id < m.nfact ? m.factory().find((p) => p.id === id)?.name : m.slots[id - m.nfact]?.name;
      say(`Track ${r.track + 1}: ${name || ""} (${m.modelNames[r.model] || r.model}).`);
    } catch (e) { fail(e); }
  };
  q(".pr-banks").addEventListener("click", (e) => { const b = e.target.closest("[data-b]"); if (b) { bank = Number(b.dataset.b); drawBanks(); drawFactory(); } });
  q("input[type=search]").addEventListener("input", (e) => { query = e.target.value; drawFactory(); });
  fac.addEventListener("click", (e) => { const li = e.target.closest("[data-id]"); if (li) { pick = Number(li.dataset.id); drawFactory(); } });
  fac.addEventListener("dblclick", (e) => { const li = e.target.closest("[data-id]"); if (li) load(Number(li.dataset.id)); });
  q(".pr-load").addEventListener("click", () => pick !== null && load(pick));

  const actions = {
    read: async () => {
      if (m.device && m.differs && !(await ask(ctx, "Read the FM-1's user presets again? This list's changes are dropped."))) return;
      if (!(await clockOk())) return;
      try { await m.readDevice(); } catch (e) { fail(e); }
    },
    open: () => file.click(),
    backup: () => { download(m.fileName(), JSON.stringify(m.toFile(), null, 1)); say("Backed up: the names are in the file."); },
    write: async () => {
      const bad = m.check();
      if (bad.length) { say(bad[0]); return; }
      if (!(await ask(ctx, m.confirmText()))) return;
      if (!(await clockOk())) return;
      const bar = prog.querySelector("progress"), lbl = prog.querySelector("span");
      bar.value = 0;
      lbl.textContent = "";
      prog.hidden = false;
      try {
        await m.write(({ done, total }) => { bar.value = done / total; lbl.textContent = `${done} / ${total} B`; });
        say("Written: the FM-1 holds this list.");
      } catch (e) {
        if (!e.cancelled) say(explain(e));
        st.textContent = explain(e);
        st.classList.add("alert");
      } finally { prog.hidden = true; }
    },
    cancel: () => { m.cancel(); prog.querySelector("span").textContent = "Cancelling…"; },
  };
  q(".pr").addEventListener("click", (e) => { const b = e.target.closest(".pr-bar [data-a], .pr-prog [data-a]"); if (b) actions[b.dataset.a]?.(); });
  file.addEventListener("change", async () => {
    const f = file.files[0];
    file.value = "";
    if (!f) return;
    try { m.fromFile(await f.text(), f.name); say(`Opened ${f.name}: Write to FM-1 to send it.`); } catch (e) { say(explain(e)); }
  });
  usr.addEventListener("click", async (e) => {
    const b = e.target.closest("button[data-a]"), li = e.target.closest("li[data-i]");
    if (!b || !li) return;
    const i = Number(li.dataset.i), a = b.dataset.a;
    try {
      if (a === "fx") {
        if (open.has(i)) open.delete(i); else open.add(i);
        drawUser();
      } else if (a === "up" || a === "down") { open.clear(); m.move(i, a === "up" ? i - 1 : i + 1); } else if (a === "del") { open.delete(i); m.remove(i); }
      else if (a === "load") await load(m.nfact + i);
      else if (a === "save") {
        if (m.slots[i] && !(await ask(ctx, `Replace slot ${i + 1} (${m.slots[i].name}) in the list with track ${m.track + 1}'s sound and FX?`))) return;
        await m.saveTrack(i);
        say(`Slot ${i + 1}: track ${m.track + 1}'s sound and FX. Write to FM-1 to send it.`);
      }
    } catch (err) { fail(err); }
  });
  usr.addEventListener("change", (e) => {
    const li = e.target.closest("li[data-i]"), k = e.target.dataset.k;
    if (!li) return;
    const i = Number(li.dataset.i);
    if (k === "keep") m.keepFx(i, e.target.checked);
    else if (k !== undefined) m.setFx(i, Number(k), e.target.value);
    else if (e.target.matches("input")) m.rename(i, e.target.value);
  });
  usr.addEventListener("keydown", (e) => { if (e.key === "Enter" && e.target.matches("input")) e.target.blur(); });
  usr.addEventListener("dragstart", (e) => { const li = e.target.closest("li[data-i]"); if (!li) return; drag = Number(li.dataset.i); e.dataTransfer.effectAllowed = "move"; });
  usr.addEventListener("dragover", (e) => {
    const li = e.target.closest("li[data-i]");
    if (drag < 0 || !li) return;
    e.preventDefault();
    for (const x of usr.querySelectorAll(".over")) if (x !== li) x.classList.remove("over");
    li.classList.add("over");
  });
  usr.addEventListener("drop", (e) => { const li = e.target.closest("li[data-i]"); if (drag >= 0 && li) { e.preventDefault(); open.clear(); m.move(drag, Number(li.dataset.i)); } drag = -1; });
  usr.addEventListener("dragend", () => { drag = -1; for (const x of usr.querySelectorAll(".over")) x.classList.remove("over"); });

  m.on("render", render);
  const offs = [
    s.on("state", ({ state, prev }) => { if (!prev || state.sel !== prev.sel || state.playing !== prev.playing) { drawLoad(); if (!busy()) drawUser(); } }),
    s.on("resync", () => { if (m.loaded && !m.differs && !busy()) m.readDevice().catch(() => {}); }),
    s.on("restore", () => { if (!busy()) drawStatus(); }),
    s.on("reload", (w) => { if (w.presets && m.loaded && !m.differs && !busy()) m.readDevice().catch(() => {}); }),
  ];
  let started = false;
  const show = () => { // read the FM-1 when the tab is first shown (not at connect, while the Sound tab reads)
    if (started) return;
    started = true;
    q(".snd-load").hidden = false;
    m.load().catch(fail).finally(() => { q(".snd-load").hidden = true; render(); });
  };
  render();
  return { model: m, show, destroy: () => offs.forEach((f) => f()) };
}

export const tab = { id: "presets", title: "Presets", cap: CAP.PRESETS, mount: mountPresets };
