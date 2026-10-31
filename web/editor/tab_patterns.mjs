// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The Patterns tab (web/EDITOR_PROTOCOL.md §2.4): the cue keys (slots A-D, variations 1-8: CUE, exactly the FM-1's
// keys; playing lit, queued blinking, from STATE), an 8 x 64 step grid of a slot's variation (STEPS_GET; edits with
// STEPS_SET; live from STEPS_CHANGED), a step's NOTE / PROB / RATCH / ACC / SLIDE / TIE, the slot's locks (LOCKS_GET,
// LOCK_EDIT; live from LOCKS_CHANGED), "Copy variation" (from a slot on the FM-1, or from a hortator-project /
// hortator-backup file) and the device's SAVE.
// Owner decision 3: editing a variation that is not playing makes the FM-1 switch to it. STEPS_SET itself presses the
// variation's key on the device: stopped, at once (rc 0); playing, queued (rc 5 detail 32): the page waits for the
// STATE push showing that variation playing, then sends the same STEPS_SET again. It never sends CUE for an edit (a
// second press of a queued key means "at the next step").
// PatternsModel holds no DOM (node tests drive it against the mock device: test_tab_patterns.mjs); mountPatterns draws.
import {
  CAP, CMD, CUE, DETAIL, DF, FileError, LAYOUT, LK, NONE, RC, SCOPE, STEP, decodeLocks, decodeProject, decodeVars,
  readBackupFile, readProjectFile,
} from "./proto.mjs";
import { DeviceError } from "./link.mjs";
import { clockGuard } from "./session.mjs";
import { SLOTS, chooser, esc } from "./ui.mjs";

const NTRK = 8, NSTEP = 64, RATCH_MASK = 3 << STEP.RAT_SHIFT;
/** the PROB codes in the knob's order on the device (device_data.json `conds`: the tests compare) */
export const CONDS = (() => {
  const out = [];
  for (let c = 1; c <= 20; c++) out.push({ code: c, name: `${(c - 1) * 5}%` });
  out.push({ code: 0, name: "100%" }, { code: 21, name: "1-SHOT" });
  let c = 22;
  for (let b = 2; b <= 8; b++) for (let a = 1; a <= b; a++) out.push({ code: c++, name: `${a}/${b}` });
  out.push({ code: 57, name: "FILL" }, { code: 58, name: "!FILL" });
  return out;
})();
export const COND = { ALWAYS: 0, FILL: 57, NOT_FILL: 58 };
export const condName = (c) => CONDS.find((x) => x.code === c)?.name ?? String(c);
const NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
/** a step's note byte (60 = C4) as a name */
export const noteName = (n) => NOTES[n % 12] + (Math.floor(n / 12) - 1);
export const ratchOf = (s) => ((s.flags & RATCH_MASK) >> STEP.RAT_SHIFT) + 1;
export const withRatch = (s, n) => ({ ...s, flags: (s.flags & ~RATCH_MASK) | ((Math.max(1, Math.min(4, n)) - 1) << STEP.RAT_SHIFT) });
/** the one character a step cell shows: FILL "F", !FILL "!", a ratchet's hits, a PROB below 100 % "%" */
export function stepGlyph(s) {
  if (!(s.flags & STEP.ON)) return "";
  if (s.cond === COND.FILL) return "F";
  if (s.cond === COND.NOT_FILL) return "!";
  if (ratchOf(s) > 1) return String(ratchOf(s));
  return s.cond ? "%" : "";
}
export function stepTitle(s, step, nlocks = 0) {
  const on = s.flags & STEP.ON;
  return `Step ${step + 1}: ${on ? "on" : "off"}${on ? `, ${noteName(s.note)}, PROB ${condName(s.cond)}, RATCH ${ratchOf(s)}`
    + `${s.flags & STEP.ACC ? ", ACC" : ""}${s.flags & STEP.SLIDE ? ", SLIDE" : ""}${s.flags & STEP.TIE ? ", TIE" : ""}` : ""}`
    + (nlocks ? `, ${nlocks} lock${nlocks > 1 ? "s" : ""}` : "");
}

/** the cue keys as the FM-1 lights them (pure: the tests read it) */
export function cueKeys(st) {
  if (!st) return { slots: [], vars: [] };
  const vm = st.slot === NONE ? 1 : st.vmask[st.slot];
  return {
    slots: SLOTS.map((name, i) => ({ n: i, name, used: !!(st.used >> i & 1), dirty: !!(st.dirty >> i & 1),
      playing: st.slot === i, queued: st.qslot === i })),
    vars: Array.from({ length: 8 }, (_, v) => ({ n: v, name: String(v + 1), used: !!(vm >> v & 1),
      playing: st.var === v, queued: st.qvar === v })),
  };
}

/** a refusal in plain words: the device's message, then what it means */
const HINT = {
  [`${RC.REFUSED}:${DETAIL.CUE_SONG}`]: "a song is playing: stop it to edit or cue",
  [`${RC.REFUSED}:${DETAIL.NO_FLASH}`]: "this FM-1 started without flash: nothing can be saved until it restarts",
  [`${RC.REFUSED}:${DETAIL.NO_SLOT}`]: "the power-on kit is loaded: save it to a slot on the FM-1 first",
  [`${RC.REFUSED}:${DETAIL.VARS_FULL}`]: "the variations do not fit: clear one on the FM-1",
  [`${RC.REFUSED}:${DETAIL.BLOCKED}`]: "saving is blocked this boot",
  [`${RC.REFUSED}:${DETAIL.NOT_LOCKABLE}`]: "this knob cannot be locked",
  [`${RC.REFUSED}:${DETAIL.LOCKS_FULL}`]: "the slot's lock bank is full (1024)",
  [`${RC.STOP}:${DETAIL.STOP}`]: "stop the FM-1 first",
  [`${RC.BUSY}:${DETAIL.RESTORING}`]: "a restore is running: wait for it to finish",
  [`${RC.FLASH}:${DETAIL.NONE}`]: "the flash write failed; the FM-1 still holds what was saved before",
};
export function plain(e) {
  const msg = e?.message || String(e), hint = e?.rc !== undefined ? HINT[`${e.rc}:${e.detail}`] : "";
  return hint ? `${msg}: ${hint}.` : msg;
}

/** a copy's source from a file: the projects of a hortator-project or hortator-backup file (parsed JSON) as
 * [{ label, objs: { record, locks, vars }, vars: [variation indexes used] }]; FileError if it is neither */
export function fileProjects(f) {
  const one = (label, p) => {
    const d = decodeProject(p.record);
    if (d.format !== "FDRF") return { label, objs: p, vars: [], old: d.format };
    const used = [0];
    if (p.vars) decodeVars(p.vars).entries.forEach((e, i) => e && used.push(i + 1));
    return { label: label + (d.name ? ` (${d.name})` : ""), objs: p, vars: used };
  };
  if (f && f.format === "hortator-backup") {
    const b = readBackupFile(f);
    return SLOTS.flatMap((n, s) => (b.projects[s] ? [one(`project ${n}`, b.projects[s])] : []));
  }
  if (f && f.format === "hortator-project") { const p = readProjectFile(f); return [one(`project ${SLOTS[p.slot] ?? "?"}`, p)]; }
  throw new FileError("format", "Not a hortator-project or hortator-backup file.");
}
/** variation v of a file's project: { rows[8] { steps, timing, used }, locks[8] (its LOCKs), motion (MOTION events,
 * not copied: LOCK_EDIT writes LOCKs only) } */
export function fileVariation(objs, v) {
  const p = decodeProject(objs.record);
  if (p.format !== "FDRF") throw new Error(`The file's project is in an older format (${p.format}): restore it into a slot on the FM-1, then copy from that slot.`);
  let rows;
  if (!v) rows = p.tracks.map((t) => ({ steps: t.steps, timing: LAYOUT.timing.map((id) => t.p[id]), used: 1 }));
  else {
    const e = objs.vars ? decodeVars(objs.vars).entries[v - 1] : null;
    if (!e) throw new Error(`VAR ${v + 1} of the file's project is not used: nothing to copy.`);
    rows = e.steps.map((steps, k) => ({ steps, timing: e.timing[k], used: 1 }));
  }
  const ev = objs.locks ? decodeLocks(objs.locks).events : [];
  return { rows, locks: Array.from({ length: NTRK }, (_, k) => ev.filter((e) => e.track === k && e.kind === LK.LOCK)),
    motion: ev.filter((e) => e.kind === LK.MOTION).length };
}

const blank = () => ({ steps: Array.from({ length: NSTEP }, () => ({ flags: 0, cond: 0, note: STEP.NT_OFFSET })), timing: [0, 0, 0, 0], used: 0, live: 0 });
const same = (a, b) => !!a && !!b && a.slot === b.slot && a.var === b.var;
const ALL = [0, 1, 2, 3, 4, 5, 6, 7];

export class PatternsModel {
  constructor(session) {
    this.s = session;
    this.handlers = new Map();
    const st = session.state;
    this.view = { slot: st ? st.slot : NONE, var: st ? st.var : 0 }; // the variation the grid shows
    this.follow = true; // the view moves with what the FM-1 plays
    this.tracks = Array.from({ length: NTRK }, blank);
    this.locks = Array.from({ length: NTRK }, () => []); // the view slot's lock bank, per track
    this.desc = new Map(); // "track:param" -> Promise of DESC
    this.texts = new Map(); // "track:param:value" -> Promise of the device's text
    this.sel = { track: 0, step: 0 };
    this.wait = null; // a switch the page waits for (owner decision 3)
    this.want = { steps: new Set(), locks: new Set() };
    this.loading = null;
    this.working = false; // a copy runs
    this.note = "";
    this.offs = [
      session.on("state", (x) => this.stateChanged(x.state, x.prev)),
      session.on("steps", (p) => this.stepsPushed(p)),
      session.on("locks", (p) => this.locksPushed(p)),
      session.on("changed", (p) => { if (p.scope === SCOPE.TRACK && [0, 48, 71].includes(p.id)) this.forget(p.track); }),
      session.on("resync", () => { this.forget(); this.reload(); }),
      session.on("reload", (w) => { if (w.slot !== undefined) { this.forget(); this.reload(); } else if (w.track !== undefined) this.forget(w.track); }),
      session.on("busy", (on) => on && this.wait?.rej(new Error("FM-1 busy (update?): the edit was not applied."))),
      session.on("closed", () => this.wait?.rej(new Error("Disconnected."))),
    ];
  }
  on(ev, fn) { if (!this.handlers.has(ev)) this.handlers.set(ev, new Set()); this.handlers.get(ev).add(fn); return () => this.handlers.get(ev).delete(fn); }
  emit(ev, x) { for (const fn of this.handlers.get(ev) || []) fn(x); }
  destroy() { this.offs.forEach((f) => f()); this.wait?.rej(new Error("Closed.")); }
  say(msg) { this.note = msg; this.emit("note", msg); }
  get state() { return this.s.state; }
  get playingView() { return { slot: this.state.slot, var: this.state.var }; }
  /** the grid shows the loaded slot: edits go to the FM-1 (other slots are read-only) */
  get editable() { return !!this.state && this.view.slot === this.state.slot; }
  get live() { return !!this.state && same(this.view, this.playingView); }
  stepLocks(track, step) { return this.locks[track].filter((e) => e.step === step); }
  forget(track) {
    for (const m of [this.desc, this.texts]) for (const k of [...m.keys()]) if (track === undefined || k.startsWith(`${track}:`)) m.delete(k);
  }
  cached(map, k, f) {
    if (!map.has(k)) { const p = f(); map.set(k, p); p.catch(() => map.delete(k)); }
    return map.get(k);
  }

  /* ------------------------------------------------------------------ reading --- */
  load() { return this.reload(); }
  /** read again (tracks' steps, tracks' locks): one read at a time; more asked meanwhile is read after it */
  reload(steps = ALL, locks = steps) {
    steps.forEach((k) => this.want.steps.add(k));
    locks.forEach((k) => this.want.locks.add(k));
    if (this.loading) return this.loading;
    this.loading = (async () => {
      this.emit("loading", true);
      try {
        while (this.want.steps.size || this.want.locks.size) {
          const v = { ...this.view }, ws = [...this.want.steps], wl = [...this.want.locks], tr = [], lk = [];
          this.want.steps.clear();
          this.want.locks.clear();
          for (const k of ws) tr[k] = await this.readSteps(v.slot, v.var, k);
          for (const k of wl) lk[k] = await this.readLocks(v.slot, k);
          if (!same(v, this.view)) continue; // the view moved meanwhile: its reload is asked already
          ws.forEach((k) => { this.tracks[k] = tr[k]; });
          wl.forEach((k) => { this.locks[k] = lk[k]; });
          this.emit("render");
        }
      } catch (e) {
        this.want.steps.clear();
        this.want.locks.clear();
        if (this.s.connected && !this.s.busy) this.say(plain(e));
      } finally {
        this.loading = null;
        this.emit("loading", false);
      }
    })();
    return this.loading;
  }
  async readSteps(slot, v, track) {
    const r = await this.s.request(CMD.STEPS_GET, { slot, var: v, track });
    return { steps: r.steps, timing: r.timing, used: r.used, live: r.live };
  }
  async readLocks(slot, track) {
    const out = [];
    for (let first = 0; ;) {
      const r = await this.s.request(CMD.LOCKS_GET, { slot, track, first });
      out.push(...r.locks);
      first = r.first + r.locks.length;
      if (first >= r.total || !r.locks.length) return out;
    }
  }
  /** show another slot / variation (read-only when not the loaded slot); following: what the FM-1 plays */
  setView(slot, v) {
    const nv = { slot, var: v };
    this.follow = !!this.state && same(nv, this.playingView);
    if (same(nv, this.view)) return this.loading || Promise.resolve();
    const slotMoved = nv.slot !== this.view.slot;
    this.view = nv;
    this.emit("view");
    return this.reload(ALL, slotMoved ? ALL : []);
  }
  followNow() { return this.state ? this.setView(this.state.slot, this.state.var) : null; }

  /* ---------------------------------------------------------- the device's side --- */
  stateChanged(st, prev) {
    this.checkWait(st);
    if (prev && !same(st, prev)) {
      const pv = { slot: st.slot, var: st.var }, was = { slot: prev.slot, var: prev.var }, slotMoved = st.slot !== prev.slot;
      if (slotMoved) this.forget();
      if (same(this.view, pv)) this.follow = true;
      if (this.follow && !same(this.view, pv)) {
        const vs = this.view.slot !== pv.slot;
        this.view = pv;
        this.emit("view");
        this.reload(ALL, vs ? ALL : []);
      } else if (same(this.view, pv) || same(this.view, was)) this.reload(ALL, slotMoved ? ALL : []); // live <-> the slot's image
    }
    this.emit("cue");
  }
  stepsPushed(p) {
    if (p.slot !== this.view.slot || p.var !== this.view.var) return;
    this.reload(p.track === NONE ? ALL : [p.track], []);
  }
  locksPushed(p) {
    if (p.slot !== this.view.slot) return;
    this.forget(p.track === NONE ? undefined : p.track);
    this.reload([], p.track === NONE ? ALL : [p.track]);
  }

  /* -------------------------------------------------------------- editing steps --- */
  /** a step's fields (patch: flags / cond / note): shown at once, the FM-1 gets STEPS_SET */
  async setStep(track, step, patch) {
    if (!this.editable) { this.say(`Slot ${SLOTS[this.view.slot] ?? "?"} is shown read-only: cue it to edit.`); return false; }
    const v = { ...this.view }, t = this.tracks[track], nw = { ...t.steps[step], ...patch };
    t.steps[step] = nw;
    this.emit("step", { track, step });
    try {
      await this.writeSteps(v.slot, v.var, track, step, [nw]);
      return true;
    } catch (e) {
      this.say(plain(e));
      if (same(v, this.view)) await this.reload([track], []);
      return false;
    }
  }
  toggle(track, step) { return this.setStep(track, step, { flags: this.tracks[track].steps[step].flags ^ STEP.ON }); }
  /** STEPS_SET; off the playing variation the FM-1 switches (stopped) or queues the switch (playing: rc 5 detail 32,
   * sent again once STATE shows it playing). Never CUE. */
  async writeSteps(slot, v, track, first, steps) {
    const a = { slot, var: v, track, first, steps };
    for (let n = 0; ; n++) {
      if (this.wait && this.wait.key === `${slot}:${v}`) await this.waitSwitch(slot, v);
      const r = await this.s.request(CMD.STEPS_SET, a, { raw: true });
      if (!r.rc) return r; // (off the variation the page knew playing: the FM-1 switched, stopped; a STATE push follows)
      if (r.rc === RC.STALE && r.detail === DETAIL.CUE_FIRST && n < 8) { await this.waitSwitch(slot, v); continue; }
      throw new DeviceError(r);
    }
  }
  /** resolves once STATE shows (slot, v) playing (or the transport stopped: the next STEPS_SET switches at once);
   * rejects when the switch is gone (cancelled while playing, another slot loaded) or on cancelWait() */
  waitSwitch(slot, v) {
    const key = `${slot}:${v}`;
    if (this.wait && this.wait.key === key) return this.wait.done;
    this.wait?.rej(new Error("Another variation was edited: the earlier edit was not applied."));
    const w = { key, slot, v, seen: false };
    w.promise = new Promise((res, rej) => { w.res = res; w.rej = rej; });
    w.done = w.promise.finally(() => { if (this.wait === w) { this.wait = null; this.emit("wait", null); } });
    w.done.catch(() => {});
    this.wait = w;
    this.emit("wait", w);
    this.checkWait(this.state, true);
    return w.done;
  }
  checkWait(st, first = false) {
    const w = this.wait;
    if (!w || !st) return;
    if (st.slot === w.slot && st.var === w.v) { w.res(); return; }
    if (st.slot !== w.slot) { w.rej(new Error(`Slot ${SLOTS[w.slot] ?? "?"} is no longer loaded: the edit was not applied.`)); return; }
    if (st.qvar === w.v || st.qslot !== NONE) { w.seen = true; return; }
    if (first) return; // the STATE push showing the queue is on its way
    if (!st.playing) { w.res(); return; } // stopped: sent again, the switch happens at once
    if (w.seen) w.rej(new Error(`The switch to VAR ${w.v + 1} was cancelled: the edit was not applied.`));
  }
  cancelWait() { this.wait?.rej(new Error("Cancelled: the edit was not applied.")); }

  /* ------------------------------------------------------------------------ cue --- */
  /** a cue key, exactly as on the FM-1 (CUE kind 0 slot / 1 variation); refusals: the device's message */
  async cue(kind, n) {
    try {
      await this.s.request(CMD.CUE, { kind, n });
      return true;
    } catch (e) { this.say(plain(e)); return false; }
  }

  /* ---------------------------------------------------------------------- locks --- */
  descOf(track, param) {
    return this.cached(this.desc, `${track}:${param}`, async () => {
      const r = await this.s.request(CMD.DESC, { scope: SCOPE.TRACK, track, id: param, first: 0 });
      return { label: r.label, unit: r.unit, min: Math.max(-64, r.min), max: Math.min(127, r.max), flags: r.flags };
    });
  }
  textOf(track, param, value) {
    return this.cached(this.texts, `${track}:${param}:${value}`, async () => {
      const r = await this.s.request(CMD.TEXT, { scope: SCOPE.TRACK, track, id: param, value });
      return `${r.text}${r.unit ? " " + r.unit : ""}`;
    });
  }
  /** the knobs LOCK_EDIT accepts on a track (DESC flag LOCK) */
  async lockable(track) {
    const out = [];
    for (let id = 1; id < (this.s.info?.pcount ?? LAYOUT.pcount); id++) {
      const d = await this.descOf(track, id);
      if (d.flags & DF.LOCK && !(d.flags & DF.RO)) out.push({ id, label: d.label || `#${id}` });
    }
    return out;
  }
  async lockEdit(op, track, step = 0, param = 0, value = 0) {
    if (!this.editable) { this.say(`Slot ${SLOTS[this.view.slot] ?? "?"} is shown read-only: cue it to edit its locks.`); return null; }
    try {
      const r = await this.s.request(CMD.LOCK_EDIT, { op, track, step, param, value });
      this.locks[track] = await this.readLocks(this.view.slot, track); // no push echoes the page's own edit
      this.emit("render");
      return r;
    } catch (e) { this.say(plain(e)); return null; }
  }
  lockSet(track, step, param, value) { return this.lockEdit(LK.SET, track, step, param, value); }
  lockClear(track, step, param) { return this.lockEdit(LK.CLEAR, track, step, param); }
  lockClearStep(track, step) { return this.lockEdit(LK.CLEAR_STEP, track, step); }
  lockClearTrack(track) { return this.lockEdit(LK.CLEAR_TRACK, track); }

  /* ---------------------------------------------------------------- copy / save --- */
  /** a variation into variation `to` of the loaded slot: steps and timing (LEN DIV SWG SRC). from: { slot, var } (on
   * the FM-1) or { file: objs (fileProjects), name, var } (a project in a file). Locks belong to the slot: with
   * `locks` (from another slot or a file) the loaded slot's LOCKs are replaced by the source's; MOTION events are never
   * copied (LOCK_EDIT writes LOCKs only). The FM-1 switches to `to` (owner decision 3). Resolves true, or says why not
   * and resolves false. Into another slot: cue it first (copies go into the loaded slot only). */
  async copy(from, to, { locks = false } = {}) {
    const slot = this.state?.slot, file = from.file || null;
    const what = file ? `${from.name || "the file"} VAR ${from.var + 1}` : `${SLOTS[from.slot]} VAR ${from.var + 1}`;
    try {
      if (slot === undefined || slot === NONE) throw new Error("Load a slot on the FM-1 first: a copy goes into the loaded slot.");
      if (!file && from.slot === slot && from.var === to) throw new Error("Source and target are the same variation.");
      this.working = true;
      this.emit("working", true);
      const src = file ? fileVariation(file, from.var) : null, rows = src ? src.rows : [];
      if (!src) for (let k = 0; k < NTRK; k++) {
        const r = await this.readSteps(from.slot, from.var, k);
        if (!r.used) throw new Error(`VAR ${from.var + 1} of slot ${SLOTS[from.slot]} is not used: nothing to copy.`);
        rows.push(r);
      }
      const lks = locks && (src || from.slot !== slot) ? (src ? src.locks : []) : null;
      if (lks && !src) for (let k = 0; k < NTRK; k++) lks.push((await this.readLocks(from.slot, k)).filter((e) => e.kind === LK.LOCK));
      const chk = lks ? await this.checkLocks(slot, lks) : null; // before the first write: refused whole, or it fits
      // the first STEPS_SET makes the FM-1 switch; rc 0 means `to` plays now, so the timing knobs below are its own
      for (let k = 0; k < NTRK; k++) await this.writeSteps(slot, to, k, 0, rows[k].steps);
      for (let k = 0; k < NTRK; k++) for (let j = 0; j < 4; j++)
        await this.s.request(CMD.SET, { scope: SCOPE.TRACK, track: k, id: LAYOUT.timing[j], value: rows[k].timing[j] });
      if (lks) await this.writeLocks(slot, lks, chk.before);
      const motion = src && locks && src.motion ? ` (its ${src.motion} MOTION event${src.motion > 1 ? "s were" : " was"} not copied)` : "";
      const clamped = chk?.clamped ? `; ${chk.clamped} lock value${chk.clamped > 1 ? "s" : ""} kept within the knob's range here` : "";
      this.say(`Copied ${what} to ${SLOTS[slot]} VAR ${to + 1}${lks ? ` with its locks${motion}${clamped}` : ""} (unsaved: SAVE keeps it).`);
      this.forget();
      this.follow = true;
      if (!same(this.view, { slot, var: to })) { this.view = { slot, var: to }; this.emit("view"); }
      await this.reload();
      return true;
    } catch (e) {
      this.say(plain(e));
      this.reload();
      return false;
    } finally {
      this.working = false;
      this.emit("working", false);
    }
  }
  /** a copy's LOCKs (lks[track]: { step, param, value }) checked against this FM-1 before anything is written: every
   * step 0..63, every knob lockable here (DESC flag LOCK, not read-only: as LOCK_EDIT would answer), every value a
   * lock bank holds (-64..127; a value past the knob's range on the target track is kept within it, as the FM-1 does:
   * counted), and the slot's bank (1024 events) holding its MOTION events plus these. Throws a plain Error (nothing
   * written); returns { before (the slot's LOCKs now, per track: the snapshot a failed copy puts back), clamped } */
  async checkLocks(slot, lks) {
    const name = SLOTS[slot], pcount = this.s.info?.pcount ?? LAYOUT.pcount, before = [];
    let motion = 0, n = 0, clamped = 0;
    for (let k = 0; k < NTRK; k++) {
      const ev = await this.readLocks(slot, k);
      motion += ev.filter((e) => e.kind !== LK.LOCK).length;
      before.push(ev.filter((e) => e.kind === LK.LOCK));
    }
    for (let k = 0; k < NTRK; k++) for (const e of lks[k] || []) {
      n++;
      const at = `track ${k + 1} step ${Number(e.step) + 1}`, int = (x) => Number.isInteger(x);
      if (!int(e.step) || e.step < 0 || e.step >= NSTEP || !int(e.param) || e.param < 1 || e.param >= pcount)
        throw new Error(`The source has a lock this FM-1 cannot hold (${at}, knob ${e.param}): nothing was copied.`);
      const d = await this.descOf(k, e.param).catch(() => null);
      if (!d || !(d.flags & DF.LOCK) || d.flags & DF.RO)
        throw new Error(`The source locks ${d?.label || `knob ${e.param}`} (${at}), which this FM-1 cannot lock: nothing was copied.`);
      if (!int(e.value) || e.value < -64 || e.value > 127)
        throw new Error(`The source's lock of ${d.label} (${at}) holds ${e.value}, outside what a lock holds (-64..127): nothing was copied.`);
      if (e.value < d.min || e.value > d.max) clamped++;
    }
    if (motion + n > LAYOUT.lkMax)
      throw new Error(`The source's ${n} locks and slot ${name}'s ${motion} MOTION events do not fit its lock bank (${LAYOUT.lkMax}): nothing was copied.`);
    return { before, clamped };
  }
  /** the LOCKs of every track replaced by lks; a write refused midway: the slot's LOCKs from before (the snapshot)
   * put back, and the error says what happened */
  async writeLocks(slot, lks, before) {
    const put = async (sets) => {
      for (let k = 0; k < NTRK; k++) {
        await this.s.request(CMD.LOCK_EDIT, { op: LK.CLEAR_TRACK, track: k, step: 0, param: 0, value: 0 });
        for (const e of sets[k] || []) await this.s.request(CMD.LOCK_EDIT, { op: LK.SET, track: k, step: e.step, param: e.param, value: e.value });
      }
    };
    try { await put(lks); } catch (e) {
      const back = await put(before).then(() => true, () => false);
      throw new Error(`The steps were copied, but writing the locks failed (${plain(e).replace(/\.$/, "")}): ${back
        ? `slot ${SLOTS[slot]}'s locks were put back as they were`
        : `slot ${SLOTS[slot]}'s locks could not all be put back: check them, or LOAD the slot again on the FM-1 without saving`}.`);
    }
  }
  /** the device's SAVE of the loaded slot (stopped only); its refusals in plain words */
  async save() {
    try {
      const r = await this.s.request(CMD.SAVE, { slot: NONE });
      this.say(`Saved slot ${SLOTS[r.slot]} on the FM-1.`);
      return true;
    } catch (e) { this.say(plain(e)); return false; }
  }
}

/* -------------------------------------------------------------------- the view --- */
const CSS = `
.pt-head { display: flex; flex-wrap: wrap; align-items: baseline; gap: 8px 16px; margin: 16px 0 8px; }
.pt-head h2 { font: 32px/32px var(--mono); margin: 0; color: var(--w); text-transform: uppercase; }
.pt-note { color: var(--c4); min-height: 24px; margin: 0 0 8px; }
.pt-note.alert { color: var(--k); background: var(--w); padding: 0 8px; display: inline-block; }
.pt-btn { background: var(--k); color: var(--c5); border: 2px solid var(--c2); cursor: pointer; font: inherit; padding: 0 8px; }
.pt-btn:hover:not(:disabled) { border-color: var(--w); color: var(--w); }
.pt-btn:disabled { color: var(--c2); cursor: default; }
.pt-btn.main { background: var(--w); border-color: var(--w); color: var(--k); }
.pt-btn.main:hover:not(:disabled) { background: var(--k); }
.pt-row { display: flex; flex-wrap: wrap; align-items: center; gap: 8px 16px; margin: 4px 0; }
.pt-row > .lbl { width: 48px; color: var(--c3); letter-spacing: .1em; }
.pt-keys { display: flex; gap: 4px; }
.pt-key { min-width: 32px; height: 32px; background: var(--k); color: var(--c4); border: 2px solid var(--c2); cursor: pointer; font: inherit; padding: 0 4px; }
.pt-key:hover:not(:disabled) { border-color: var(--w); color: var(--w); }
.pt-key.unused, .pt-key:disabled { color: var(--c2); border-color: var(--c1); }
.pt-key.on { background: var(--w); border-color: var(--w); color: var(--k); }
.pt-key.q { animation: pt-blink .5s steps(1) infinite alternate; }
@keyframes pt-blink { from { background: var(--k); color: var(--w); border-color: var(--w); } to { background: var(--w); color: var(--k); border-color: var(--w); } }
.pt-at { color: var(--c3); }
.pt-at b { color: var(--w); font-weight: normal; }
.pt-gridwrap { overflow-x: auto; margin: 12px 0 0; border: 2px solid var(--c1); padding: 8px; }
.pt-grid { display: grid; grid-template-columns: 88px repeat(64, minmax(10px, 1fr)); row-gap: 4px; min-width: 820px; }
.pt-grid .ruler { color: var(--c2); white-space: nowrap; overflow: visible; height: 24px; }
.pt-trk { color: var(--c4); white-space: nowrap; cursor: pointer; background: none; border: 0; font: inherit; text-align: left; padding: 0; }
.pt-trk small { font-size: inherit; color: var(--c2); }
.pt-trk.sel { color: var(--w); }
.pt-c { position: relative; height: 28px; margin: 0 1px; padding: 0; border: 0; background: var(--c1); color: var(--k); font: inherit;
  cursor: pointer; overflow: hidden; line-height: 28px; text-align: center; min-width: 0; }
.pt-c.beat { margin-left: 3px; }
.pt-c.on { background: var(--c3); }
.pt-c.acc { background: var(--w); }
.pt-c.out { opacity: .3; }
.pt-c.lk::after { content: ""; position: absolute; top: 2px; right: 2px; width: 4px; height: 4px; background: var(--c4); }
.pt-c.on.lk::after { background: var(--k); }
.pt-c.sel { outline: 2px solid var(--w); outline-offset: 1px; z-index: 1; }
.pt-grid.ro .pt-c { cursor: default; }
.pt-cols { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 380px), 1fr)); gap: 16px; margin: 16px 0; }
.pt-box { border: 2px solid var(--c1); min-width: 0; }
.pt-box h4 { margin: 0; padding: 0 8px; background: var(--c1); color: var(--c5); font: inherit; letter-spacing: .1em; display: flex; justify-content: space-between; gap: 8px; }
.pt-box .in { padding: 8px; display: grid; gap: 8px; }
.pt-f { display: grid; grid-template-columns: 64px 1fr 48px; align-items: center; gap: 8px; }
.pt-f > span:first-child { color: var(--c3); }
.pt-f b { font-weight: normal; color: var(--w); }
.pt-tab select, .pt-copy select { background: var(--k); color: var(--c5); border: 2px solid var(--c2); border-radius: 0; font: inherit; padding: 0 2px; }
.pt-tab input[type=range] { width: 100%; accent-color: var(--w); min-width: 0; }
.pt-chk { display: flex; flex-wrap: wrap; gap: 4px 16px; }
.pt-chk label { color: var(--c4); cursor: pointer; }
.pt-lk { display: grid; grid-template-columns: 32px 72px minmax(0, 1fr) 80px 32px; gap: 8px; align-items: center; }
.pt-lk .st { color: var(--c3); cursor: pointer; }
.pt-lk .st:hover { color: var(--w); }
.pt-lk .v { color: var(--w); white-space: nowrap; overflow: hidden; }
.pt-lk.motion, .pt-lk.motion .v { color: var(--c3); }
.pt-list { max-height: 216px; overflow-y: auto; display: grid; gap: 2px; }
.pt-empty { color: var(--c3); }
.pt-copy { border: 2px dashed var(--c2); padding: 8px; margin: 8px 0; display: flex; flex-wrap: wrap; gap: 8px 16px; align-items: center; }
.pt-copy label { color: var(--c4); }
.pt-ro input, .pt-ro select { opacity: .5; }
`;

export function mountPatterns(el, ctx) {
  const m = new PatternsModel(ctx.session), s = ctx.session;
  if (typeof document !== "undefined" && !document.getElementById("pt-style")) {
    const st = document.createElement("style");
    st.id = "pt-style";
    st.textContent = CSS;
    document.head.append(st);
  }
  el.classList.add("pt-tab");
  el.innerHTML = `<div class="pt-head"><h2>Patterns</h2>
      <button type="button" class="pt-btn main" data-act="save" title="The FM-1's SAVE of the loaded slot (stopped only)">Save on FM-1</button>
      <button type="button" class="pt-btn" data-act="copy">Copy variation…</button>
      <span class="pt-load pt-empty" hidden>Reading the FM-1…</span></div>
    <div class="pt-note" role="status"></div>
    <div class="pt-copy" hidden></div>
    <div class="pt-row"><span class="lbl">CUE</span><div class="pt-keys pt-cs" role="group" aria-label="Cue a slot"></div>
      <div class="pt-keys pt-cv" role="group" aria-label="Cue a variation"></div></div>
    <div class="pt-row"><span class="lbl">SHOW</span><div class="pt-keys pt-ss" role="group" aria-label="Show a slot"></div>
      <div class="pt-keys pt-sv" role="group" aria-label="Show a variation"></div><span class="pt-at"></span></div>
    <div class="pt-gridwrap"><div class="pt-grid" role="grid" aria-label="Steps"></div></div>
    <div class="pt-cols"><section class="pt-box pt-step"></section><section class="pt-box pt-locks"></section></div>`;
  const q = (c) => el.querySelector(c);
  const note = q(".pt-note"), grid = q(".pt-grid"), copyEl = q(".pt-copy");
  const vName = (v) => `${v.slot === NONE ? "KIT" : SLOTS[v.slot]} VAR ${v.var + 1}`;

  const drawKeys = () => {
    const st = s.state;
    if (!st) return;
    const k = cueKeys(st), vm = m.view.slot === NONE ? 1 : st.vmask[m.view.slot] ?? 1;
    const key = (x, kind, show) => {
      const isView = show && (kind === CUE.SLOT ? m.view.slot === x.n : m.view.var === x.n);
      const cls = `pt-key${x.used ? "" : " unused"}${show ? (isView ? " on" : "") : `${x.playing ? " on" : ""}${x.queued ? " q" : ""}`}`;
      const what = kind === CUE.SLOT ? `slot ${x.name}` : `variation ${x.name}`;
      const title = show ? `Show ${what} in the grid` : `Cue ${what}, as the FM-1's key${x.playing ? " (playing)" : x.queued ? " (queued)" : ""}${x.dirty ? ", unsaved" : ""}`;
      return `<button type="button" class="${cls}" data-${show ? "show" : "cue"}="${kind}:${x.n}" title="${esc(title)}" aria-pressed="${show ? isView : x.playing}"${
        show && kind === CUE.SLOT && !x.used ? " disabled" : ""}>${esc(x.name)}${!show && x.dirty ? "*" : ""}</button>`;
    };
    q(".pt-cs").innerHTML = k.slots.map((x) => key(x, CUE.SLOT, false)).join("");
    q(".pt-cv").innerHTML = k.vars.map((x) => key(x, CUE.VAR, false)).join("");
    q(".pt-ss").innerHTML = k.slots.map((x) => key(x, CUE.SLOT, true)).join("");
    q(".pt-sv").innerHTML = k.vars.map((x) => key({ ...x, used: !!(vm >> x.n & 1) }, CUE.VAR, true)).join("");
    q(".pt-at").innerHTML = `<b>${esc(vName(m.view))}</b> ${m.live ? "playing: edits are live"
      : m.editable ? "not playing: an edit makes the FM-1 switch to it" : "read-only: not the loaded slot (cue it to edit)"}${
      m.follow ? "" : ` <button type="button" class="pt-btn" data-act="follow">Follow the FM-1</button>`}`;
    q('[data-act="save"]').disabled = m.working || st.slot === NONE;
  };

  const cell = (t, i) => {
    const tr = m.tracks[t], x = tr.steps[i], len = tr.timing[0] || NSTEP, nl = m.stepLocks(t, i).length, on = x.flags & STEP.ON;
    const cls = `pt-c${on ? " on" : ""}${on && x.flags & STEP.ACC ? " acc" : ""}${i >= len ? " out" : ""}${i && i % 4 === 0 ? " beat" : ""}`
      + `${nl ? " lk" : ""}${m.sel.track === t && m.sel.step === i ? " sel" : ""}`;
    return `<button type="button" class="${cls}" data-t="${t}" data-s="${i}" title="${esc(stepTitle(x, i, nl))}" role="gridcell">${stepGlyph(x)}</button>`;
  };
  const drawGrid = () => {
    grid.classList.toggle("ro", !m.editable);
    let h = `<span class="ruler"></span>`;
    for (let i = 0; i < NSTEP; i++) h += `<span class="ruler">${i % 16 === 0 ? i + 1 : ""}</span>`;
    for (let t = 0; t < NTRK; t++) {
      h += `<button type="button" class="pt-trk${m.sel.track === t ? " sel" : ""}" data-trk="${t}" title="Track ${t + 1}">${t + 1} <small>LEN ${m.tracks[t].timing[0] || "-"}</small></button>`;
      for (let i = 0; i < NSTEP; i++) h += cell(t, i);
    }
    grid.innerHTML = h;
  };
  const drawCell = ({ track, step }) => {
    const b = grid.querySelector(`[data-t="${track}"][data-s="${step}"]`);
    if (b) b.outerHTML = cell(track, step);
  };

  const drawStep = () => {
    const { track: t, step: i } = m.sel, x = m.tracks[t].steps[i], ro = !m.editable, dis = ro ? " disabled" : "", box = q(".pt-step");
    const chk = (f, bit, label, title = "") => `<label${title ? ` title="${title}"` : ""}><input type="checkbox" data-f="${f}"${x.flags & bit ? " checked" : ""}${dis}> ${label}</label>`;
    box.classList.toggle("pt-ro", ro);
    box.innerHTML = `<h4><span>TRACK ${t + 1} · STEP ${i + 1}</span><span>${esc(vName(m.view))}</span></h4><div class="in">
      <div class="pt-chk">${chk("on", STEP.ON, "ON")}${chk("acc", STEP.ACC, "ACC")}${chk("slide", STEP.SLIDE, "SLIDE", "ACID")}${chk("tie", STEP.TIE, "TIE", "ACID")}</div>
      <div class="pt-f"><span>NOTE</span><input type="range" min="0" max="127" value="${x.note}" data-f="note" aria-label="NOTE"${dis}><b>${esc(noteName(x.note))}</b></div>
      <div class="pt-f"><span>PROB</span><select data-f="cond" aria-label="PROB"${dis}>${CONDS.map((c) =>
        `<option value="${c.code}"${c.code === x.cond ? " selected" : ""}>${esc(c.name)}</option>`).join("")}</select><span></span></div>
      <div class="pt-f"><span>RATCH</span><select data-f="ratch" aria-label="RATCH"${dis}>${[1, 2, 3, 4].map((n) =>
        `<option value="${n}"${n === ratchOf(x) ? " selected" : ""}>${n}</option>`).join("")}</select><span></span></div>
      <span class="pt-empty">${ro ? "Read-only: cue this slot to edit it." : "Click a step: on / off. Shift-click: select only."}</span></div>`;
  };

  let lockGen = 0;
  const drawLocks = async () => {
    const gen = ++lockGen, { track: t, step: i } = m.sel, ro = !m.editable, box = q(".pt-locks");
    const all = m.locks[t], here = all.filter((e) => e.step === i);
    const head = `<h4><span>LOCKS · TRACK ${t + 1}</span><span>${all.length}</span></h4>`;
    try {
      const rows = async (list, edit) => (await Promise.all(list.map(async (e) => {
        const d = await m.descOf(t, e.param), txt = await m.textOf(t, e.param, e.value), lock = e.kind === LK.LOCK;
        const ctl = edit && lock && !ro ? `<input type="range" min="${d.min}" max="${d.max}" value="${e.value}" data-lv="${e.param}" aria-label="${esc(d.label)}">`
          : `<span class="pt-empty">${lock ? "" : "MOTION"}</span>`;
        return `<div class="pt-lk${lock ? "" : " motion"}"><span class="st" data-go="${e.step}" title="Select step ${e.step + 1}">${String(e.step + 1).padStart(2, "0")}</span>
          <span>${esc(d.label || "#" + e.param)}</span>${ctl}<span class="v">${esc(txt)}</span>${lock && !ro
          ? `<button type="button" class="pt-btn" data-lc="${e.step}:${e.param}" title="Clear this lock" aria-label="Clear">×</button>` : "<span></span>"}</div>`;
      }))).join("");
      const hereRows = await rows(here, true), allRows = await rows(all, false);
      if (gen !== lockGen) return;
      box.innerHTML = head + `<div class="in">
        <div><span class="pt-at">Step ${i + 1}</span>${here.length ? "" : ' <span class="pt-empty">no locks</span>'}</div>${hereRows}
        ${ro ? "" : `<div class="pt-row"><select class="pt-add" aria-label="Lock a knob on this step"><option value="">Add a lock…</option></select>
          <button type="button" class="pt-btn" data-act="clrstep"${here.some((e) => e.kind === LK.LOCK) ? "" : " disabled"}>Clear step</button>
          <button type="button" class="pt-btn" data-act="clrtrk"${all.some((e) => e.kind === LK.LOCK) ? "" : " disabled"}>Clear track</button></div>`}
        <span class="pt-at">All of track ${t + 1}: they apply in every variation of the slot</span>
        <div class="pt-list">${allRows || '<span class="pt-empty">none</span>'}</div></div>`;
    } catch (e) {
      if (gen === lockGen) box.innerHTML = head + `<div class="in"><span class="pt-empty">${esc(plain(e))}</span></div>`;
    }
  };
  const drawAll = () => { drawKeys(); drawGrid(); drawStep(); drawLocks(); };
  const select = (track, step) => { m.sel = { track, step }; drawGrid(); drawStep(); drawLocks(); };

  /* Copy variation: from a slot on the FM-1 or from a project in a file, into a variation of the loaded slot */
  let srcFile = null; // { name, projects: fileProjects() } once a file is picked
  const openCopy = () => {
    const st = s.state;
    if (st.slot === NONE) { m.say("Load a slot on the FM-1 first: a copy goes into the loaded slot."); return; }
    const k = cueKeys(st), L = SLOTS[st.slot], opt = (list, sel) => list.map((x) => `<option value="${x.n}"${x.n === sel ? " selected" : ""}>${esc(x.name)}</option>`).join("");
    copyEl.hidden = false;
    copyEl.innerHTML = `<label>From <select data-c="src"><option value="fm1">the FM-1</option><option value="file">a file…</option></select></label>
      <span class="pt-cfm1"><label>slot <select data-c="fs">${opt(k.slots.filter((x) => x.used), m.view.slot === NONE ? st.slot : m.view.slot)}</select></label>
        <label>VAR <select data-c="fv">${opt(k.vars, m.view.var)}</select></label></span>
      <span class="pt-cfile" hidden><button type="button" class="pt-btn" data-act="pickfile">Choose a file…</button> <span class="pt-cfn pt-empty"></span>
        <label hidden>project <select data-c="fp"></select></label><label hidden>VAR <select data-c="ffv"></select></label></span>
      <label>into ${esc(L)} (loaded) VAR <select data-c="tv">${opt(k.vars, (m.view.var + 1) % 8)}</select></label>
      <label class="pt-cl"><input type="checkbox" data-c="lk"> also replace ${esc(L)}'s locks with the source's</label>
      <button type="button" class="pt-btn main" data-act="docopy">Copy</button><button type="button" class="pt-btn" data-act="nocopy">Cancel</button>
      <span class="pt-empty pt-chint"></span>
      <span class="pt-empty">To copy into another slot, cue that slot first: a copy goes into the loaded slot (${esc(L)}).</span>`;
    copyEl.querySelector('[data-c="src"]').addEventListener("change", syncCopy);
    copyEl.querySelector('[data-c="fs"]').addEventListener("change", syncCopy);
    copyEl.querySelector('[data-c="fp"]').addEventListener("change", () => { fileVars(); syncCopy(); });
    if (srcFile) showFile();
    syncCopy();
  };
  const cg = (c) => copyEl.querySelector(`[data-c="${c}"]`);
  const fromFile = () => cg("src")?.value === "file";
  const syncCopy = () => {
    const st = s.state, ff = fromFile();
    copyEl.querySelector(".pt-cfm1").hidden = ff;
    copyEl.querySelector(".pt-cfile").hidden = !ff;
    copyEl.querySelector(".pt-cl").hidden = !ff && Number(cg("fs").value) === st.slot; // (the same slot: its locks already)
    copyEl.querySelector(".pt-chint").textContent = ff
      ? "From a file: the variation's steps and timing; with the box ticked, the project's LOCKs too. MOTION events are not copied (the editor writes LOCKs only)."
      : "";
    copyEl.querySelector('[data-act="docopy"]').disabled = m.working || (ff && !fileProject());
  };
  const fileProject = () => { const p = srcFile?.projects[Number(cg("fp").value)]; return p && p.vars.length ? p : null; };
  const fileVars = () => {
    const p = srcFile?.projects[Number(cg("fp").value)], sel = cg("ffv");
    sel.innerHTML = (p?.vars || []).map((v) => `<option value="${v}">${v + 1}</option>`).join("");
    sel.parentElement.hidden = !p?.vars.length;
    copyEl.querySelector(".pt-cfn").textContent = srcFile ? `${srcFile.name}${p && p.old ? `: ${p.label} is in an older format (${p.old}): restore it into a slot, then copy from there` : ""}` : "";
  };
  const showFile = () => {
    cg("src").value = "file";
    const fp = cg("fp");
    fp.innerHTML = srcFile.projects.map((p, i) => `<option value="${i}">${esc(p.label)}</option>`).join("");
    fp.parentElement.hidden = srcFile.projects.length < 2;
    fileVars();
  };
  const pickFile = () => {
    const i = Object.assign(document.createElement("input"), { type: "file", accept: ".json,application/json" });
    i.addEventListener("change", async () => {
      const f = i.files && i.files[0];
      if (!f) return;
      try {
        const projects = fileProjects(JSON.parse(await f.text()));
        if (!projects.length) throw new Error(`${f.name}: no project in it.`);
        srcFile = { name: f.name, projects };
        showFile();
      } catch (e) { m.say(e instanceof SyntaxError ? `${f.name}: not a JSON file.` : e.message); }
      syncCopy();
    });
    i.click();
  };
  const doCopy = async () => {
    const st = s.state, ff = fromFile(), p = ff ? fileProject() : null, to = Number(cg("tv").value);
    if (ff && !p) { m.say("Choose a hortator-project or hortator-backup file first."); return; }
    const from = ff ? { file: p.objs, name: `${srcFile.name} ${p.label}`, var: Number(cg("ffv").value) }
      : { slot: Number(cg("fs").value), var: Number(cg("fv").value) };
    const locks = (ff || from.slot !== st.slot) && cg("lk").checked;
    const name = ff ? `${p.label} of ${srcFile.name}` : `slot ${SLOTS[from.slot]}`;
    const msg = `Copy ${ff ? `${p.label} VAR ${from.var + 1} from ${srcFile.name}` : `${SLOTS[from.slot]} VAR ${from.var + 1}`} into ${SLOTS[st.slot]} VAR ${to + 1}?\n\nVAR ${to + 1}'s steps and timing are replaced`
      + (locks ? `, and every LOCK of slot ${SLOTS[st.slot]} (all its variations) is replaced by ${name}'s (MOTION events are not copied)` : "")
      + `. The FM-1 switches to VAR ${to + 1}${st.playing ? " at the pattern's end" : ""}. Nothing is saved until SAVE.`;
    if (!(await ctx.confirm(msg))) return;
    if (!(await clockGuard(s, chooser(ctx), (t) => m.say(t)))) return; // playing, SYNC out on: stop first?
    copyEl.hidden = true;
    await m.copy(from, to, { locks });
  };

  /* events */
  el.addEventListener("click", async (e) => {
    const b = e.target.closest("button, [data-go]");
    if (!b || b.disabled) return;
    const d = b.dataset;
    if (d.cue) { const [kind, n] = d.cue.split(":").map(Number); m.cue(kind, n); return; }
    if (d.show) {
      const [kind, n] = d.show.split(":").map(Number);
      if (kind === CUE.SLOT) m.setView(n, n === m.view.slot ? m.view.var : 0); else m.setView(m.view.slot, n);
      drawKeys();
      return;
    }
    if (d.t !== undefined) {
      const t = Number(d.t), i = Number(d.s), prev = { ...m.sel };
      m.sel = { track: t, step: i };
      drawCell(prev);
      drawCell(m.sel);
      if (prev.track !== t) for (const x of grid.querySelectorAll(".pt-trk")) x.classList.toggle("sel", Number(x.dataset.trk) === t);
      drawStep();
      drawLocks();
      if (!e.shiftKey && m.editable) { await m.toggle(t, i); drawCell({ track: t, step: i }); if (m.sel.track === t && m.sel.step === i) drawStep(); }
      return;
    }
    if (d.trk !== undefined) { select(Number(d.trk), m.sel.step); return; }
    if (d.go !== undefined) { select(m.sel.track, Number(d.go)); return; }
    if (d.lc) { const [st, p] = d.lc.split(":").map(Number); await m.lockClear(m.sel.track, st, p); return; }
    const act = d.act;
    if (act === "save") m.save();
    else if (act === "follow") { m.followNow(); drawKeys(); }
    else if (act === "copy") { if (copyEl.hidden) openCopy(); else copyEl.hidden = true; }
    else if (act === "nocopy") copyEl.hidden = true;
    else if (act === "docopy") doCopy();
    else if (act === "pickfile") pickFile();
    else if (act === "clrstep" && (await ctx.confirm(`Clear the LOCKs of track ${m.sel.track + 1}, step ${m.sel.step + 1}?`))) m.lockClearStep(m.sel.track, m.sel.step);
    else if (act === "clrtrk" && (await ctx.confirm(`Clear every LOCK of track ${m.sel.track + 1}? They apply in all variations of the slot. MOTION stays.`))) m.lockClearTrack(m.sel.track);
    else if (act === "cancelwait") m.cancelWait();
  });
  el.addEventListener("change", async (e) => {
    const f = e.target.dataset.f, { track: t, step: i } = m.sel;
    if (f) {
      const x = m.tracks[t].steps[i], v = e.target.type === "checkbox" ? e.target.checked : Number(e.target.value);
      const bit = { on: STEP.ON, acc: STEP.ACC, slide: STEP.SLIDE, tie: STEP.TIE }[f];
      const patch = bit ? { flags: v ? x.flags | bit : x.flags & ~bit } : f === "ratch" ? { flags: withRatch(x, v).flags } : { [f]: v };
      await m.setStep(t, i, patch);
      drawCell({ track: t, step: i });
      if (m.sel.track === t && m.sel.step === i) drawStep();
      return;
    }
    if (e.target.dataset.lv !== undefined) { await m.lockSet(t, i, Number(e.target.dataset.lv), Number(e.target.value)); return; }
    if (e.target.classList.contains("pt-add") && e.target.value) {
      const p = Number(e.target.value), dsc = await m.descOf(t, p);
      const cur = await s.request(CMD.GET, { scope: SCOPE.TRACK, track: t, id: p, count: 1 }).catch(() => null); // lock what the knob has now
      await m.lockSet(t, i, p, Math.max(dsc.min, Math.min(dsc.max, cur?.values?.[0]?.value ?? dsc.min)));
    }
  });
  el.addEventListener("input", (e) => {
    if (e.target.dataset.f === "note") e.target.nextElementSibling.textContent = noteName(Number(e.target.value));
  });
  el.addEventListener("focusin", async (e) => { // the lockable knobs, read when first needed
    const sel = e.target.closest?.(".pt-add");
    if (!sel || sel.options.length > 1) return;
    const t = m.sel.track, list = await m.lockable(t).catch(() => []);
    if (m.sel.track === t && sel.isConnected) sel.innerHTML = `<option value="">Add a lock…</option>` + list.map((x) => `<option value="${x.id}">${esc(x.label)}</option>`).join("");
  });

  m.on("render", drawAll);
  m.on("view", drawKeys);
  m.on("cue", drawKeys);
  m.on("step", drawCell);
  m.on("note", (msg) => {
    const bad = !/^(Copied|Saved)/.test(msg);
    note.textContent = msg;
    note.classList.toggle("alert", bad);
    if (bad) ctx.toast?.(msg);
  });
  m.on("wait", (w) => {
    if (!w) { if (note.querySelector('[data-act="cancelwait"]')) note.textContent = ""; return; }
    note.classList.remove("alert");
    note.innerHTML = `${esc(`Waiting for VAR ${w.v + 1} to play (queued on the FM-1 till the pattern's end); the edit is sent then.`)}
      <button type="button" class="pt-btn" data-act="cancelwait">Cancel</button>`;
  });
  m.on("loading", (on) => { q(".pt-load").hidden = !on; });
  m.on("working", (on) => { for (const b of el.querySelectorAll('[data-act="copy"], [data-act="docopy"]')) b.disabled = on; drawKeys(); });
  drawAll();
  m.load();
  return { model: m, show: () => { m.forget(); m.reload(); }, destroy: () => m.destroy() };
}

export const tab = { id: "patterns", title: "Patterns", cap: CAP.PATTERNS, mount: mountPatterns };
