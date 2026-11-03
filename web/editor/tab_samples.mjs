// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The Samples tab: the FM-1's three user sample slots USR1–USR3 (the SAMPLE model's and the LAYER's own sets;
// web/EDITOR_PROTOCOL.md §2.7, §5). Each slot holds a set of up to 16 samples (sampset.mjs): its name, the bytes used
// of its room, its samples (each: name, length, size, Play decoded as the FM-1 decodes it, Edit, up / down, Delete,
// Rename), Back up (.fsmp), Restore…, Rename…, Clear. A sound file (WAV, AIFF — whatever the browser decodes) becomes
// a sample: mono, trimmed on its waveform, optionally normalized, at its own rate (44.1 kHz at most) or a lower one when
// the room needs it, IMA ADPCM exactly as the FM-1 decodes it (sample.mjs); added to a set as a new slot, put in place
// of one, or replacing the whole set; Chop cuts it into slices first (chop.mjs: by beats, equal, transients). Every
// write sends the whole set (SMP_BEGIN, the pieces, SMP_END) with progress and Cancel; slot changes wait for Write.
// Writes need the FM-1 stopped; a failed or cancelled one leaves the slot empty, never half a set, and the page keeps
// the set to write again or save as a file. SamplesModel holds no DOM (web/editor/test_tab_samples.mjs drives it
// against the mock device); mountSamples draws it.
import { CAP, CMD, DETAIL, LIMITS, RC, crc32 } from "./proto.mjs";
import * as S from "./sample.mjs";
import * as T from "./sampset.mjs";
import * as C from "./chop.mjs";
import { BUSY_MSG } from "./session.mjs";
import { esc } from "./ui.mjs";

const PIECE = LIMITS.piece;
class Cancelled extends Error { constructor(m = "Cancelled.") { super(m); this.cancelled = true; } }
const isSilence = (e) => !!(e && (e.timeout || e.message === BUSY_MSG));
const kib = (n) => `${(n / 1024).toFixed(1)} KiB`;
const hzText = (hz) => `${Math.round(hz).toLocaleString("en-US")} Hz`;
const secText = (s) => `${s.toFixed(s < 10 ? 2 : 1)} s`;

export class SamplesModel {
  constructor(session) {
    this.s = session;
    this.handlers = new Map();
    this.slots = [null, null, null]; // SMP_INFO's fields + seconds (null: not read yet)
    this.source = null; // { name, rate, channels, mono, length }
    this.edit = null; // { start, end, normalize, hz, slot, name }
    this.enc = null; // the encoded sample for this.edit (encodeSample's), null: not made yet
    this.op = null; // { what, done, total, slot }
    this.cancelling = false;
    this.cache = new Map(); // slot -> { crc, header, data }: READ once per CRC
    this.sets = [null, null, null]; // each slot's set as the page holds it: { name, slots, plain, nz, dirty, failed, crc }
    this.chop = null; // { mode auto / beats / equal, sens, bpm, div, count, offset, cuts, edited }
    this.message = "";
    this.error = false;
    this.loading = null;
    this.offs = [
      session.on("resync", () => { if (this.slots[0]) this.refresh(); }),
      session.on("busy", () => this.emit("render")),
      session.on("state", () => this.emit("render")),
      session.on("restore", () => this.emit("render")),
    ];
  }
  on(ev, fn) { if (!this.handlers.has(ev)) this.handlers.set(ev, new Set()); this.handlers.get(ev).add(fn); return () => this.handlers.get(ev).delete(fn); }
  emit(ev, x) { for (const fn of this.handlers.get(ev) || []) fn(x); }
  destroy() { this.offs.forEach((f) => f()); }
  get available() { return this.s.has(CMD.SMP_INFO); }
  get playing() { return !!this.s.state?.playing; }
  get restoreBlocked() { return this.s.restoreBlocked("samples"); }
  say(msg, error = false) { this.message = msg; this.error = error; this.emit("render"); }

  /* ----------------------------------------------------------------- the slots --- */
  async refresh() {
    if (!this.available) { this.say("This FM-1's firmware has no sample slots for the editor: Update the FM-1 to 0.28.0 or later.", true); return; }
    if (this.loading) return this.loading;
    this.loading = (async () => {
      this.emit("loading", true);
      try {
        for (let k = 0; k < 3; k++) {
          const r = await this.s.request(CMD.SMP_INFO, { slot: k });
          this.slots[k] = { ...r, seconds: S.seconds(r.n, r.rate) };
          this.emit("render");
        }
      } catch (e) { if (!isSilence(e)) this.say(e.message, true); } finally { this.loading = null; this.emit("loading", false); }
    })();
    return this.loading;
  }
  async info(k) {
    const r = await this.s.request(CMD.SMP_INFO, { slot: k });
    this.slots[k] = { ...r, seconds: S.seconds(r.n, r.rate) };
    this.emit("render");
    return this.slots[k];
  }

  /* ---------------------------------------------------------------- a sound file --- */
  /** a decoded file: { name, rate, channels: [Float32Array] }; into the first empty slot (as a new slot of it) */
  setSource(src) {
    const mono = S.mixdown(src.channels);
    this.source = { name: src.name, rate: src.rate, channels: src.channels, mono, length: mono.length };
    const empty = this.slots.findIndex((x) => x && !x.valid);
    this.edit = { start: 0, end: mono.length, normalize: false, slot: empty >= 0 ? empty : 0, name: S.cleanName(src.name), hz: 0, into: "add", index: -1 };
    this.edit.hz = S.pickRate(this.choices()).hz;
    this.enc = null;
    this.chop = null;
    this.say("");
  }
  /** the bytes the edited sample may take: the whole slot ("set": it replaces the set), the room left ("add"), the
   * room with slot index's bytes ("replace") */
  maxFor(into = this.edit.into) {
    const k = this.edit.slot, set = this.sets[k], max = S.SLOT_MAX[k];
    if (into === "set") return max;
    if (this.chop && into === "add") { // the slices: each its name, and a byte each for odd lengths
      const r = this.room(k), have = set ? set.slots.reduce((a, x) => a + T.slotBytes(x), 0) : r.used, n = Math.min(this.chop.cuts.length + 1, r.room);
      return Math.max(0, max - have - T.namesBytes(r.count + n) - n);
    }
    if (!set) return this.room(k).free;
    if (into === "replace") {
      const others = set.slots.filter((_, i) => i !== this.edit.index);
      return Math.max(0, max - others.reduce((a, x) => a + T.slotBytes(x), 0) - T.namesBytes(others.length + 1));
    }
    return T.roomFor(set.slots, max).free;
  }
  /** the rates offered for the trimmed source into the target */
  choices() {
    const e = this.edit;
    return S.rateChoices(this.source.rate, Math.max(1, e.end - e.start), this.maxFor());
  }
  /** change the trim, normalize, slot, target (into, index), name or rate. The rate follows (the highest that fits)
   * unless one is given */
  setEdit(patch) {
    const e = this.edit, n = this.source.length;
    Object.assign(e, patch);
    e.start = Math.max(0, Math.min(n - 1, Math.round(e.start)));
    e.end = Math.max(e.start + 1, Math.min(n, Math.round(e.end)));
    if (patch.slot !== undefined && patch.into === undefined && e.into === "replace") { e.into = "add"; e.index = -1; }
    if (patch.name !== undefined) e.name = S.cleanName(patch.name);
    const moved = ["start", "end", "slot", "into", "index"].some((k) => patch[k] !== undefined);
    if (patch.hz !== undefined) e.hzUser = true;
    else if (moved || !e.hz) { e.hzUser = false; e.hz = S.pickRate(this.choices()).hz; }
    this.enc = null;
    if (this.chop && (patch.start !== undefined || patch.end !== undefined)) { this.chopCompute(); this.chopRate(); }
    this.emit("render");
  }
  /** chopping: the rate follows the slices (their names take room) unless one was chosen */
  chopRate() { if (this.chop && !this.edit.hzUser) { this.edit.hz = S.pickRate(this.choices()).hz; this.enc = null; } }
  /** what the edit makes: { hz, n, bytes, fits, seconds, label, max } (into: the target, else the edit's) */
  plan(into = this.edit.into) {
    const e = this.edit, n = Math.max(1, S.lengthAt(e.end - e.start, this.source.rate, e.hz)), bytes = Math.ceil(n / 2), max = this.maxFor(into);
    return { hz: e.hz, n, bytes, fits: bytes <= max, seconds: n / e.hz, label: S.qualityOf(e.hz), max };
  }
  /** the encoded sample replacing a whole slot (made once per edit) */
  prepare() {
    if (this.enc) return this.enc;
    const p = this.plan("set");
    if (!p.fits) throw new S.SampleError(`Too long for ${S.SLOT_NAMES[this.edit.slot]} (${kib(p.bytes)} of ${kib(p.max)}): trim it or pick a lower rate.`);
    this.enc = S.encodeSample(S.render(this.source, this.edit), this.edit.hz, this.edit.name);
    return this.enc;
  }
  /** what the FM-1 will play: the encoded bytes decoded as the device decodes them */
  preview() {
    if (this.edit.into === "set") { const e = this.prepare(); return { pcm: S.adpcmDecode(e.data, e.n), hz: e.hz }; }
    const s = this.encodeSlot();
    return { pcm: S.adpcmDecode(s.data, s.n), hz: this.edit.hz };
  }
  /** the edit as a slot of a set */
  encodeSlot() { return T.makeSlot(S.render(this.source, this.edit), this.edit.hz, this.edit.name); }

  /* ------------------------------------------------------------------- the sets --- */
  /** slot k's set (READ when it holds one; once per CRC, and never over changes not written yet) */
  async openSet(k, force = false) {
    const x = this.slots[k] || await this.info(k), cur = this.sets[k];
    if (cur && !force && (cur.dirty || cur.crc === (x.valid ? x.crc : 0))) return cur;
    let set = { name: S.SLOT_NAMES[k], slots: [], plain: true, nz: 0, dirty: false, failed: false, crc: 0 };
    if (x.valid) {
      const r = await this.readSlot(k), p = T.parseSet(r.header, r.data);
      set = { name: p.name || S.SLOT_NAMES[k], slots: p.slots, plain: p.plain, nz: p.nz, dirty: false, failed: false, crc: r.crc };
    }
    this.sets[k] = set;
    this.emit("render");
    return set;
  }
  /** slot k's room: { used, max, free (for one more sample), room (samples left), count } (a set not read yet: from
   * SMP_INFO) */
  room(k) {
    const set = this.sets[k], max = S.SLOT_MAX[k];
    if (set) return { ...T.roomFor(set.slots, max), used: T.setBytes(set.slots), max, count: set.slots.length };
    const x = this.slots[k], used = x?.valid ? x.len : 0, count = x?.valid ? x.nz : 0;
    return { used, max, count, room: T.MAX_SLOTS - count, free: Math.max(0, max - used - T.namesBytes(count + 1) + T.namesBytes(count)) };
  }
  /** the set, if the page may change it (a set made elsewhere: Error .relayout until relayout(k)) */
  mutable(k) {
    const set = this.sets[k], name = S.SLOT_NAMES[k];
    if (!set) throw new Error(`${name} is not read yet.`);
    if (!set.plain) {
      throw Object.assign(new Error(`${name} holds a set made elsewhere (${set.nz} zones, ${set.slots.length} samples): changing it lays it out again as ${set.slots.length} plain slots, one key each.`), { relayout: true });
    }
    return set;
  }
  relayout(k) { const set = this.sets[k]; if (set) { set.plain = true; set.dirty = true; this.emit("render"); } }
  changed(set) { set.dirty = true; this.emit("render"); }
  renameSlot(k, i, name) { const set = this.mutable(k); set.slots[i] = { ...set.slots[i], name: S.cleanName(name) }; this.changed(set); }
  moveSlot(k, i, to) {
    const set = this.mutable(k), n = set.slots.length;
    if (to < 0 || to >= n || i === to) return;
    const [x] = set.slots.splice(i, 1);
    set.slots.splice(to, 0, x);
    this.changed(set);
  }
  deleteSlot(k, i) { const set = this.mutable(k); set.slots.splice(i, 1); this.changed(set); }
  /** changes not written: the set as the FM-1 has it (READ again) */
  async revertSet(k) { this.sets[k] = null; this.say(""); return this.openSet(k, true); }
  /** slot i of set k as the FM-1 plays it: { pcm, hz } */
  slotSound(k, i) { return T.decodeSlot(this.sets[k].slots[i]); }
  /** the set as the page holds it, as a file (.fsmp: restores like a backup) */
  setFile(k) {
    const set = this.sets[k], img = T.composeSet(set.slots, set.slots.length === 1 ? set.slots[0].name : set.name, S.SLOT_MAX[k]);
    return { bytes: S.backupBytes(img.header, img.data), name: S.backupName(k, set.name) };
  }
  /** the set to the FM-1, whole (header last); an empty set clears the slot. Failing, the set stays as edited (failed:
   * the slot is empty now) */
  async writeSet(k) {
    const set = this.sets[k], name = S.SLOT_NAMES[k];
    try {
      if (!set) throw new Error(`${name} is not read yet.`);
      this.stopped("write a set");
      if (!set.slots.length) {
        await this.s.request(CMD.SMP_CLEAR, { slot: k });
        this.cache.delete(k);
        await this.info(k);
        Object.assign(set, { dirty: false, failed: false, crc: 0, plain: true, nz: 0 });
        this.say(`${name} is empty now.`);
        return;
      }
      if (set.slots.length === 1) set.name = set.slots[0].name; // one sample: the set's name is its name
      const img = T.composeSet(set.slots, set.name, S.SLOT_MAX[k]);
      await this.write(k, img.header, img.data, `Writing ${name} (${set.slots.length} sample${set.slots.length > 1 ? "s" : ""})`);
      const crc = crc32(img.data);
      this.cache.set(k, { crc, header: img.header, data: img.data, info: S.parseHeader(img.header), raw: S.backupBytes(img.header, img.data) });
      Object.assign(set, { dirty: false, failed: false, crc, plain: true, nz: set.slots.length, name: img.name });
      this.say(`${name} written: ${set.slots.length} sample${set.slots.length > 1 ? "s" : ""}, ${kib(img.data.length)} of ${kib(S.SLOT_MAX[k])}.`);
    } catch (e) {
      if (set) { set.dirty = true; set.failed = !this.slots[k]?.valid; }
      this.say(e.cancelled ? `${e.message} The set is kept here: Write it again or save it as a file.` : e.message, !e.cancelled);
      throw e;
    }
  }
  /** the edited sample as a new slot of its set, written at once */
  async addToSet() {
    try {
      this.stopped("add a sample");
      const k = this.edit.slot, name = S.SLOT_NAMES[k], set = await this.openSet(k);
      this.mutable(k);
      if (set.slots.length >= T.MAX_SLOTS) throw new S.SampleError(`${name} holds ${T.MAX_SLOTS} samples already: delete one first.`);
      const p = this.plan("add");
      if (!p.fits) throw new S.SampleError(`Too long for the room left in ${name} (${kib(p.bytes)} of ${kib(p.max)}): trim it or pick a lower rate.`);
      const slot = this.encodeSlot();
      if (!set.slots.length) set.name = slot.name; // a new set: named after its first sample
      set.slots.push(slot);
      this.changed(set);
      await this.writeSet(k);
      this.say(`Added ${slot.name} to ${name} as sample ${set.slots.length}: ${secText(slot.n / this.edit.hz)} at ${hzText(this.edit.hz)}, ${kib(T.slotBytes(slot))}.`);
      return slot;
    } catch (e) { if (this.message !== e.message && !/kept here/.test(this.message)) this.say(e.message, !e.cancelled); throw e; }
  }
  /** a slot of its set opened in the editor: its sound (as the FM-1 plays it), to trim / normalize / resample it */
  editSlot(k, i) {
    const x = this.sets[k].slots[i];
    this.setSource(T.slotSource(x));
    this.setEdit({ slot: k, into: "replace", index: i, name: x.name, hz: Math.round(S.hzOfQ16(x.rate)) });
  }
  /** the edited sample in place of slot edit.index, written at once */
  async replaceInSet() {
    try {
      this.stopped("replace a sample");
      const k = this.edit.slot, i = this.edit.index, set = await this.openSet(k);
      this.mutable(k);
      if (!(i >= 0 && i < set.slots.length)) throw new Error("That sample is not in the set any more.");
      const p = this.plan("replace");
      if (!p.fits) throw new S.SampleError(`Too long for ${S.SLOT_NAMES[k]} (${kib(p.bytes)} of ${kib(p.max)}): trim it or pick a lower rate.`);
      const slot = this.encodeSlot();
      set.slots[i] = slot;
      this.changed(set);
      await this.writeSet(k);
      this.say(`Sample ${i + 1} of ${S.SLOT_NAMES[k]} is now ${slot.name}.`);
      return slot;
    } catch (e) { if (this.message !== e.message && !/kept here/.test(this.message)) this.say(e.message, !e.cancelled); throw e; }
  }

  /* ----------------------------------------------------------------------- chop --- */
  chopStart() {
    this.chop = { mode: "auto", sens: 50, bpm: 120, div: 16, count: 8, offset: 0, cuts: [], edited: false, tempo: null };
    this.chopCompute();
    this.chopRate();
    this.emit("render");
    return this.chop;
  }
  chopStop() { this.chop = null; if (!this.edit.hzUser) this.edit.hz = S.pickRate(this.choices()).hz; this.emit("render"); }
  /** the cuts of the chop's mode over the trimmed source (absolute positions); manual edits dropped */
  chopCompute() {
    const c = this.chop, e = this.edit, x = this.source.mono.subarray(e.start, e.end), n = x.length, r = this.source.rate;
    const rel = c.mode === "beats" ? C.beatCuts(n, r, c.bpm, c.div, c.offset) : c.mode === "equal" ? C.equalCuts(n, c.count) : C.detectTransients(x, r, c.sens);
    c.cuts = rel.map((v) => v + e.start);
    c.edited = false;
  }
  chopSet(patch) {
    Object.assign(this.chop, patch);
    this.chopCompute();
    this.chopRate();
    this.emit("render");
  }
  /** the trimmed source's tempo (detected once per trim): { bpm, whole } */
  chopTempo() {
    const e = this.edit, key = `${e.start}:${e.end}`, c = this.chop;
    if (!c.tempo || c.tempo.key !== key) c.tempo = { key, ...C.detectTempo(this.source.mono.subarray(e.start, e.end), this.source.rate) };
    return c.tempo;
  }
  /** a cut by hand: "add" at a; "move" cut a to b; "remove" cut a */
  chopCut(op, a, b) {
    const c = this.chop, e = this.edit, inside = (v) => v - e.start, n = e.end - e.start;
    const rel = c.cuts.map(inside);
    const out = op === "add" ? C.addCut(rel, inside(a), n) : op === "move" ? C.moveCut(rel, a, inside(b), n) : C.removeCut(rel, a);
    c.cuts = out.map((v) => v + e.start);
    c.edited = true;
    this.chopRate();
    this.emit("render");
  }
  /** the trimmed source as the slices will hold it (normalized as a whole, at the edit's rate), made once per edit */
  chopPcm() {
    const e = this.edit, key = `${e.start}:${e.end}:${e.normalize}:${e.hz}`;
    if (!this.chopCache || this.chopCache.key !== key) this.chopCache = { key, pcm: S.render(this.source, e) };
    return this.chopCache.pcm;
  }
  /** the slices: { slices [{ start, end (source samples), a, b (at the rate), n, bytes }], fit, lost, room } */
  chopPlan() {
    const e = this.edit, k = e.slot, f = e.hz / this.source.rate, len = Math.max(1, S.lengthAt(e.end - e.start, this.source.rate, e.hz));
    const slices = C.slices(e.end - e.start, this.chop.cuts.map((v) => v - e.start)).map(([s0, s1]) => {
      const a = Math.min(len, Math.round(s0 * f)), b = s1 === e.end - e.start ? len : Math.min(len, Math.round(s1 * f));
      return { start: s0 + e.start, end: s1 + e.start, a, b, n: Math.max(0, b - a), bytes: Math.ceil(Math.max(0, b - a) / 2) };
    }).filter((x) => x.n > 0);
    const set = this.sets[k], have = set ? set.slots : [], max = S.SLOT_MAX[k];
    const used = set ? have.reduce((a, x) => a + T.slotBytes(x), 0) : this.room(k).used, count = set ? have.length : this.room(k).count;
    const fit = C.fitting(slices.map((x) => x.bytes + T.SLOT_TRAILER), max - used - T.SLOT_TRAILER * count - 5, T.MAX_SLOTS - count);
    return { slices, fit: fit.n, lost: slices.length - fit.n, bytes: fit.bytes, room: T.MAX_SLOTS - count };
  }
  /** slice i as the FM-1 will play it */
  chopPreview(i) {
    const sl = this.chopPlan().slices[i], pcm = this.chopPcm().subarray(sl.a, sl.b);
    return { pcm: S.adpcmDecode(S.adpcmEncode(pcm), pcm.length), hz: this.edit.hz };
  }
  /** the slices that fit added to the set as slots (named after the file: "BREAK 1" ..), written at once */
  async chopAdd() {
    try {
      this.stopped("add the slices");
      const k = this.edit.slot, name = S.SLOT_NAMES[k], set = await this.openSet(k);
      this.mutable(k);
      const pl = this.chopPlan();
      if (!pl.fit) throw new S.SampleError(pl.room ? `No slice fits the room left in ${name}: pick a lower rate or a shorter part.` : `${name} holds ${T.MAX_SLOTS} samples already.`);
      const pcm = this.chopPcm(), full = S.cleanName(this.source.name), base = full.replace(/[\d\s._-]+$/, "") || full;
      if (!set.slots.length) set.name = full;
      pl.slices.slice(0, pl.fit).forEach((x, i) => {
        const num = String(i + 1), nm = `${base.slice(0, T.NAME_LEN - 1 - num.length).trim()} ${num}`;
        set.slots.push(T.makeSlot(pcm.slice(x.a, x.b), this.edit.hz, nm));
      });
      this.changed(set);
      await this.writeSet(k);
      this.chop = null;
      this.say(`Added ${pl.fit} slice${pl.fit > 1 ? "s" : ""} to ${name}${pl.lost ? `; ${pl.lost} did not fit (${pl.room - pl.fit <= 0 ? `${T.MAX_SLOTS} samples at most` : "no room left"})` : ""}.`);
    } catch (e) { if (this.message !== e.message && !/kept here/.test(this.message)) this.say(e.message, !e.cancelled); throw e; }
  }

  /* ------------------------------------------------------------ writing a slot --- */
  stopped(what) {
    if (this.playing) throw new Error(`The FM-1 is playing: stop it to ${what}.`);
    const other = this.restoreBlocked;
    if (other) throw new Error(`${other} Wait for it to finish.`);
  }
  tick() { if (this.op) { this.op.done++; this.emit("progress", this.op); } }
  /** BEGIN, the pieces, END (the session's restore lock held): the slot's new sample; Cancel: CLEAR */
  async write(k, header, data, what) {
    const name = S.SLOT_NAMES[k];
    this.stopped("write a sample");
    const unlock = this.s.lockRestore("samples", `a sample into ${name}`);
    this.op = { what, done: 0, total: Math.ceil(data.length / PIECE) + 2, slot: k };
    this.cancelling = false;
    this.cache.delete(k);
    this.emit("progress", this.op);
    this.emit("render"); // (Upload, the cards' actions: off while it runs)
    let begun = false;
    try {
      await this.s.request(CMD.SMP_BEGIN, { slot: k, len: data.length, crc: crc32(data) });
      begun = true;
      this.tick();
      for (let off = 0; off < data.length; off += PIECE) {
        if (this.cancelling) throw new Cancelled();
        await this.s.request(CMD.SMP_DATA, { slot: k, offset: off, data: data.subarray(off, off + PIECE) });
        this.tick();
      }
      if (this.cancelling) throw new Cancelled();
      await this.s.request(CMD.SMP_END, { slot: k, header });
      this.tick();
    } catch (e) {
      if (e.cancelled) {
        await this.s.request(CMD.SMP_CLEAR, { slot: k }).catch(() => {});
        throw new Cancelled(`Cancelled: ${name} is empty.`);
      }
      const empty = begun ? ` ${name} is empty.` : " Nothing was written.";
      if (e.rc === RC.STOP) throw new Error(begun ? `The FM-1 started playing: the upload stopped and ${name} is empty. Stop it and try again.`
        : `The FM-1 is playing: stop it to write a sample. Nothing was written.`);
      if (e.rc === RC.REFUSED && e.detail === DETAIL.REFUSED_DATA) throw new Error(`The FM-1 refused this sample's data (it could look like an update record).${empty} Change the trim a little and try again.`);
      throw new Error(`${e.message}${/\.$/.test(e.message) ? "" : "."}${empty}`);
    } finally {
      this.op = null;
      this.emit("progress", null);
      unlock();
      await this.info(k).catch(() => {});
    }
  }
  async upload() {
    try {
      this.stopped("upload");
      const enc = this.prepare(), k = this.edit.slot;
      await this.write(k, enc.header, enc.data, `Uploading ${enc.name} to ${S.SLOT_NAMES[k]}`);
      this.sets[k] = null;
      this.say(`Uploaded ${enc.name} to ${S.SLOT_NAMES[k]}: ${secText(enc.n / enc.hz)} at ${hzText(enc.hz)}, ${kib(enc.data.length)}.`);
      return enc;
    } catch (e) { this.say(e.message, !e.cancelled); throw e; }
  }
  cancel() { if (this.op) { this.cancelling = true; this.emit("render"); } }

  /* ------------------------------------------------------------ reading a slot --- */
  /** the slot's raw bytes (READ, piece by piece): { header, data, info } */
  async readSlot(k) {
    const i = await this.info(k);
    if (!i.valid) throw new Error(`${S.SLOT_NAMES[k]} is empty.`);
    const c = this.cache.get(k);
    if (c && c.crc === i.crc) return c;
    const total = S.DATA_OFF + i.len, b = new Uint8Array(total);
    for (let off = 0; off < total; off += PIECE) {
      const r = await this.s.request(CMD.SMP_READ, { slot: k, offset: off, crc: i.crc });
      b.set(r.data, off);
    }
    const header = b.slice(0, S.HDR_SIZE), data = b.slice(S.DATA_OFF), info = S.parseHeader(header);
    if (crc32(data) !== i.crc) throw new Error(`${S.SLOT_NAMES[k]} read back damaged: read it again.`);
    const out = { crc: i.crc, header, data, info, raw: b };
    this.cache.set(k, out);
    return out;
  }
  /** the slot's first zone decoded as the FM-1 decodes it */
  async slotPreview(k) {
    const { data, info } = await this.readSlot(k), z = info.zones[0];
    return { pcm: S.adpcmDecode(data.subarray(z.off), z.n), hz: z.hz };
  }
  async backup(k) {
    try {
      const r = await this.readSlot(k);
      this.say(`${S.SLOT_NAMES[k]} backed up.`);
      return { bytes: S.backupBytes(r.header, r.data), name: S.backupName(k, r.info.name) };
    } catch (e) { this.say(e.message, true); throw e; }
  }
  /** a backup file (.fsmp) into slot k: checked first (nothing sent when it fails) */
  async restore(k, bytes) {
    try {
      const f = S.parseBackup(bytes, k);
      this.stopped("restore a sample");
      await this.write(k, f.header, f.data, `Restoring ${f.info.name || "the sample"} into ${S.SLOT_NAMES[k]}`);
      this.sets[k] = null;
      this.say(`Restored ${f.info.name || "the sample"} into ${S.SLOT_NAMES[k]}.`);
    } catch (e) { this.say(e.message, !e.cancelled); throw e; }
  }
  async clear(k) {
    try {
      this.stopped("clear a slot");
      await this.s.request(CMD.SMP_CLEAR, { slot: k });
      this.cache.delete(k);
      this.sets[k] = null;
      await this.info(k);
      this.say(`${S.SLOT_NAMES[k]} cleared.`);
    } catch (e) { this.say(e.message, true); throw e; }
  }
  async rename(k, name) {
    try {
      this.stopped("rename a sample");
      await this.s.request(CMD.SMP_RENAME, { slot: k, name: S.cleanName(name) });
      this.cache.delete(k);
      if (this.sets[k] && !this.sets[k].dirty) this.sets[k] = null;
      else if (this.sets[k]) this.sets[k].name = S.cleanName(name);
      await this.info(k);
      this.say(`${S.SLOT_NAMES[k]} is now ${this.slots[k].name}.`);
    } catch (e) { this.say(e.message, true); throw e; }
  }
}

/* ------------------------------------------------------------------------ the DOM --- */
const CSS = `
.smp-acts { display: flex; flex-wrap: wrap; gap: 8px; margin: 16px 0 8px; }
.smp-grid { grid-template-columns: 1fr; }
.smp-list td.acts { text-align: right; }
.smp-card .bar, .smp-run .bar { display: inline-block; width: 100%; height: 8px; background: var(--c1); vertical-align: middle; }
.smp-card .bar i, .smp-run .bar i { display: block; height: 100%; background: var(--w); }
.smp-card .bar.full i { background: var(--c4); }
.smp-card .body { padding: 8px; }
.smp-card .empty { color: var(--c3); margin: 0 0 8px; }
.smp-card .st { float: right; color: var(--c3); }
.smp-card .st.on { color: var(--w); }
.smp-card .st.dirty { background: var(--w); color: var(--k); padding: 0 8px; }
.smp-list { width: 100%; border-collapse: collapse; margin: 0 0 8px; }
.smp-list td { padding: 0 4px; border-bottom: 2px solid var(--c1); white-space: nowrap; }
.smp-list td.n { color: var(--c3); text-align: right; }
.smp-list td.nm { color: var(--w); width: 100%; }
.smp-list td.dim { color: var(--c3); }
.smp-list button { background: none; color: var(--c4); border: 0; cursor: pointer; padding: 0 4px; font: inherit; }
.smp-list button:hover { color: var(--w); }
.smp-list button:disabled { opacity: .3; cursor: default; }
.smp-note.warn { color: var(--w); }
.smp-edit { margin: 16px 0; }
.smp-edit .body { padding: 8px; display: grid; gap: 8px; }
.smp-wave { width: 100%; height: 120px; background: var(--k); border: 2px solid var(--c1); cursor: ew-resize; touch-action: none; display: block; }
.smp-wave.chop { cursor: crosshair; }
.smp-trim { display: grid; grid-template-columns: auto 1fr auto; gap: 4px 8px; align-items: center; }
.smp-trim input[type=range] { width: 100%; }
.smp-row { display: flex; flex-wrap: wrap; gap: 8px 16px; align-items: center; }
.smp-row label { color: var(--c3); }
.smp-row select, .smp-row input[type=text], .smp-row input[type=number] { background: var(--k); color: var(--c5); border: 2px solid var(--c2); border-radius: 0; font: inherit; padding: 0 4px; }
.smp-row input[type=text] { width: 10ch; text-transform: uppercase; }
.smp-row input[type=number] { width: 8ch; }
.smp-plan { color: var(--c5); }
.smp-plan.bad { background: var(--w); color: var(--k); padding: 0 8px; }
.smp-chop { border-top: 2px solid var(--c1); padding-top: 8px; display: grid; gap: 8px; }
.smp-slices { display: flex; flex-wrap: wrap; gap: 4px; }
.smp-slices button { min-width: 4ch; }
.smp-slices button.lost { opacity: .4; text-decoration: line-through; }
.smp-run { display: grid; grid-template-columns: auto 1fr auto auto; gap: 8px; align-items: center; margin: 8px 0; }
.smp-btn { background: var(--k); color: var(--c5); border: 2px solid var(--c2); cursor: pointer; padding: 0 8px; }
.smp-btn:hover { border-color: var(--w); color: var(--w); }
.smp-btn.main { background: var(--w); color: var(--k); border-color: var(--w); }
.smp-btn.on { border-color: var(--w); color: var(--w); }
.smp-btn:disabled { opacity: .4; cursor: default; }
.smp-msg { margin: 8px 0; color: var(--c5); }
.smp-msg.err { background: var(--w); color: var(--k); padding: 0 8px; }
.smp-note { color: var(--c3); max-width: 90ch; }
`;
function style() {
  if (document.getElementById("smp-style")) return;
  const s = document.createElement("style");
  s.id = "smp-style";
  s.textContent = CSS;
  document.head.append(s);
}
function download(bytes, name) {
  const url = URL.createObjectURL(new Blob([bytes], { type: "application/octet-stream" }));
  const a = Object.assign(document.createElement("a"), { href: url, download: name });
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 10000);
}
function pickFile(accept) {
  return new Promise((res) => {
    const i = Object.assign(document.createElement("input"), { type: "file", accept });
    i.addEventListener("change", () => res(i.files && i.files[0] ? i.files[0] : null));
    i.click();
  });
}
/** a sound file -> { name, rate, channels }: decoded at the file's own rate when its header says it (WAV, AIFF) */
export async function decodeFile(file) {
  const buf = await file.arrayBuffer(), rate = S.sniffRate(new Uint8Array(buf.slice(0, 4096)));
  const Ctx = globalThis.OfflineAudioContext || globalThis.webkitOfflineAudioContext;
  let ab;
  try {
    const ctx = rate >= 3000 && rate <= 384000 ? new Ctx(1, 1, rate) : new (globalThis.AudioContext || globalThis.webkitAudioContext)();
    ab = await ctx.decodeAudioData(buf);
    ctx.close?.();
  } catch { throw new Error(`${file.name}: this browser cannot decode it (use WAV or AIFF).`); }
  return { name: file.name, rate: ab.sampleRate, channels: Array.from({ length: ab.numberOfChannels }, (_, c) => ab.getChannelData(c)) };
}
let audio = null, playing = null;
function play(pcm, hz) {
  const Ctx = globalThis.AudioContext || globalThis.webkitAudioContext;
  if (!Ctx || !pcm.length) return;
  audio = audio || new Ctx();
  try { playing?.stop(); } catch { /* ended */ }
  const b = audio.createBuffer(1, pcm.length, Math.max(3000, Math.round(hz))), ch = b.getChannelData(0);
  for (let i = 0; i < pcm.length; i++) ch[i] = pcm[i] / 32768;
  const src = audio.createBufferSource();
  src.buffer = b;
  src.connect(audio.destination);
  src.start();
  playing = src;
}
function drawWave(cv, mono, e, cuts) {
  const w = cv.clientWidth || 600, hgt = cv.clientHeight || 120, dpr = globalThis.devicePixelRatio || 1;
  cv.width = Math.round(w * dpr);
  cv.height = Math.round(hgt * dpr);
  const g = cv.getContext("2d"), css = getComputedStyle(document.documentElement), col = (v) => css.getPropertyValue(v).trim() || "#fff";
  g.scale(dpr, dpr);
  g.fillStyle = col("--k");
  g.fillRect(0, 0, w, hgt);
  const n = mono.length, per = n / w, mid = hgt / 2;
  for (let x = 0; x < w; x++) {
    let lo = 0, hi = 0;
    const a = Math.floor(x * per), b = Math.max(a + 1, Math.floor((x + 1) * per));
    for (let i = a; i < b && i < n; i += Math.max(1, Math.floor((b - a) / 64))) { const v = mono[i]; if (v < lo) lo = v; if (v > hi) hi = v; }
    const inside = a >= e.start && a < e.end;
    g.fillStyle = inside ? col("--w") : col("--c2");
    g.fillRect(x, mid - hi * mid, 1, Math.max(1, (hi - lo) * mid));
  }
  g.fillStyle = col("--c4");
  for (const at of [e.start, e.end]) g.fillRect(Math.min(w - 2, Math.round(at / per)), 0, 2, hgt);
  if (cuts) {
    g.font = "12px monospace";
    cuts.forEach((c, i) => {
      const x = Math.min(w - 1, Math.round(c.at / per));
      g.fillStyle = c.lost ? col("--c2") : col("--c5");
      g.fillRect(x, 0, 1, hgt);
      g.fillRect(x - 3, 0, 7, 3);
      if (c.label) { g.fillStyle = c.lost ? col("--c2") : col("--c3"); g.fillText(c.label, x + 3, hgt - 4); }
      void i;
    });
  }
}

const sizeText = (b) => (b < 1024 ? `${b} B` : kib(b));
function setRows(m, k, lock) {
  const set = m.sets[k];
  if (!set || !set.slots.length) return "";
  const n = set.slots.length, ro = lock || !set.plain;
  return `<table class="smp-list"><tbody>${set.slots.map((x, i) => `<tr data-i="${i}">
    <td class="n">${i + 1}</td><td class="nm">${esc(x.name)}</td><td class="dim">${secText(x.n / S.hzOfQ16(x.rate))}</td><td class="dim">${sizeText(T.slotBytes(x))}</td>
    <td class="acts"><button type="button" data-a="splay" title="Play (as the FM-1 plays it)"${lock ? " disabled" : ""}>Play</button><button type="button" data-a="sedit" title="Trim, normalize or resample it"${ro ? " disabled" : ""}>Edit</button><button type="button" data-a="sup" title="Move up"${ro || !i ? " disabled" : ""}>↑</button><button type="button" data-a="sdown" title="Move down"${ro || i === n - 1 ? " disabled" : ""}>↓</button><button type="button" data-a="sname" title="Rename"${ro ? " disabled" : ""}>Name</button><button type="button" data-a="sdel" title="Delete"${ro ? " disabled" : ""}>Del</button></td></tr>`).join("")}</tbody></table>`;
}
function cardHtml(m, k, lock) {
  const x = m.slots[k], name = S.SLOT_NAMES[k], max = S.SLOT_MAX[k], set = m.sets[k];
  if (!x) return `<section class="page smp-card" data-slot="${k}"><h4>${name}</h4><div class="body"><p class="empty">…</p></div></section>`;
  const r = m.room(k), used = set ? r.used : x.valid ? x.len : 0, pct = Math.min(100, Math.round(100 * used / max)), count = set ? set.slots.length : x.valid ? x.nz : 0;
  const dirty = set?.dirty, st = dirty ? `<span class="st dirty">${set.failed ? "not on the FM-1: write again" : "changed: not written"}</span>`
    : `<span class="st${count ? " on" : ""}">${count ? `${count} sample${count > 1 ? "s" : ""}` : "empty"}</span>`;
  const list = set ? setRows(m, k, lock) : x.valid ? '<p class="empty">Reading its samples…</p>' : "";
  return `<section class="page smp-card" data-slot="${k}">
    <h4>${name}${set && count ? ` · ${esc(set.slots.length === 1 ? set.slots[0].name : set.name)}` : x.valid && x.name ? ` · ${esc(x.name)}` : ""}${st}</h4>
    <div class="body">${count || dirty ? "" : '<p class="empty">No sample here.</p>'}${list}
      ${set && !set.plain ? `<p class="smp-note warn">Made elsewhere: ${set.nz} zones over ${set.slots.length} samples. Changing it lays it out again as plain samples, one key each.</p>` : ""}
      <p class="smp-used"><span class="bar${pct > 95 ? " full" : ""}"><i style="width:${pct}%"></i></span><br>${kib(used)} of ${kib(max)} · ${kib(r.free)} free · ${count} of ${T.MAX_SLOTS} samples</p>
      <div class="smp-acts">
        <button type="button" class="smp-btn${dirty ? " main" : ""}" data-a="write"${!dirty || lock ? " disabled" : ""}>Write ${name}</button>
        ${dirty ? `<button type="button" class="smp-btn" data-a="revert"${lock ? " disabled" : ""}>Undo changes</button>
        <button type="button" class="smp-btn" data-a="setfile"${lock || !set.slots.length ? " disabled" : ""}>Save as file</button>` : ""}
        ${set && !set.plain ? `<button type="button" class="smp-btn" data-a="relayout"${lock ? " disabled" : ""}>Lay out as samples</button>` : ""}
        <button type="button" class="smp-btn" data-a="addto"${lock || r.room <= 0 || (set && !set.plain) ? " disabled" : ""}>Add a sound…</button>
        <button type="button" class="smp-btn" data-a="chopto"${lock || r.room <= 0 || (set && !set.plain) ? " disabled" : ""}>Chop a sound…</button>
        <button type="button" class="smp-btn" data-a="backup"${!x.valid || lock ? " disabled" : ""}>Back up</button>
        <button type="button" class="smp-btn" data-a="restore"${lock ? " disabled" : ""}>Restore…</button>
        <button type="button" class="smp-btn" data-a="rename"${!x.valid || lock ? " disabled" : ""}>Rename set…</button>
        <button type="button" class="smp-btn" data-a="clear"${!x.valid || lock ? " disabled" : ""}>Clear</button>
      </div></div></section>`;
}

export function mountSamples(el, ctx) {
  style();
  const m = new SamplesModel(ctx.session);
  el.innerHTML = `<div class="snd-head"><h2>Samples</h2><span class="snd-sub">USR1–USR3: the SAMPLE model's and the LAYER's own sets</span>
    <span class="snd-load" hidden>Reading the FM-1…</span></div>
    <div class="smp-acts"><button type="button" class="smp-btn main" data-a="open">Load a sound file…</button>
      <button type="button" class="smp-btn" data-a="reload">Read again</button></div>
    <p class="smp-note">Each set holds up to 16 samples: SLOT 1, 2, 3 … on the FM-1 play them in this order (TUNE sets the pitch;
      a MIDI note plays the selected one transposed). USR1 holds 80 KiB, USR2 and USR3 48 KiB each: about 3.7 s and 2.2 s
      at 44.1 kHz, twice that at 22 kHz. Adding a sample, replacing one or chopping writes the whole set; moving, renaming
      and deleting wait for Write. Writing needs the FM-1 stopped; a cancelled or failed write leaves the set empty on the
      FM-1, and the page keeps it to write again or to save as a file. The samples' names are kept in the set (the FM-1
      shows the set's name).</p>
    <div class="smp-run" hidden><span class="what"></span><span class="bar"><i></i></span><span class="cnt"></span>
      <button type="button" class="smp-btn" data-a="cancel">Cancel</button></div>
    <div class="smp-msg" role="status" hidden></div>
    <section class="page smp-edit" hidden><h4 class="ttl">New sample</h4><div class="body">
      <canvas class="smp-wave" aria-label="Waveform: drag to trim"></canvas>
      <div class="smp-trim"><label for="smp-s">Start</label><input type="range" id="smp-s" data-e="start" min="0" step="1"><output data-o="start"></output>
        <label for="smp-e">End</label><input type="range" id="smp-e" data-e="end" min="1" step="1"><output data-o="end"></output></div>
      <div class="smp-row">
        <label>Set <select data-e="slot">${S.SLOT_NAMES.map((n, k) => `<option value="${k}">${n}</option>`).join("")}</select></label>
        <label>Into <select data-e="into"></select></label>
        <label>Name <input type="text" data-e="name" maxlength="8" spellcheck="false"></label>
        <label><input type="checkbox" data-e="normalize"> Normalize</label>
        <label>Rate <select data-e="hz"></select></label></div>
      <div class="smp-row"><span class="smp-plan"></span></div>
      <div class="smp-chop" hidden>
        <div class="smp-row"><label>Chop</label>
          <button type="button" class="smp-btn" data-c="auto">Auto</button><button type="button" class="smp-btn" data-c="beats">Beats</button>
          <button type="button" class="smp-btn" data-c="equal">Equal</button>
          <span class="c-auto"><label>Sensitivity <input type="range" data-ch="sens" min="0" max="100" step="1"></label></span>
          <span class="c-beats"><label>Tempo <input type="number" data-ch="bpm" min="30" max="300" step="0.1"></label>
            <button type="button" class="smp-btn" data-a="cdetect">Detect</button><button type="button" class="smp-btn" data-a="chalf">÷2</button><button type="button" class="smp-btn" data-a="cdouble">×2</button>
            <label>Every <select data-ch="div"><option value="4">1/4</option><option value="8">1/8</option><option value="16">1/16</option></select></label></span>
          <span class="c-equal"><label>Slices <input type="number" data-ch="count" min="2" max="64" step="1"></label></span></div>
        <div class="smp-row"><span class="smp-note">On the waveform: click to add a cut, drag one to move it, double-click it to remove it. Click a slice below to hear it.</span></div>
        <div class="smp-slices"></div>
        <div class="smp-row"><span class="smp-cplan"></span></div>
      </div>
      <div class="smp-row"><button type="button" class="smp-btn" data-a="preview">Preview</button>
        <button type="button" class="smp-btn main" data-a="go">Add</button>
        <button type="button" class="smp-btn" data-a="chop">Chop…</button>
        <button type="button" class="smp-btn" data-a="close">Close</button></div>
    </div></section>
    <div class="grid smp-grid"></div>`;
  const $ = (s) => el.querySelector(s), grid = $(".smp-grid"), run = $(".smp-run"), msg = $(".smp-msg"), ed = $(".smp-edit");
  const wave = $(".smp-wave"), chopEl = $(".smp-chop");
  const fmtAt = (i) => (m.source ? secText(i / m.source.rate) : "");
  const lockNow = () => !!m.op || m.s.busy || !m.s.connected;
  const intoOpts = () => {
    const e = m.edit, k = e.slot, nm = S.SLOT_NAMES[k], set = m.sets[k], r = m.room(k), o = [];
    o.push(["add", `a new sample (${r.count + 1} of ${T.MAX_SLOTS}, ${kib(r.free)} free)`]);
    if (e.into === "replace" && set && e.index >= 0 && e.index < set.slots.length) o.push(["replace", `sample ${e.index + 1} (${set.slots[e.index].name})`]);
    o.push(["set", `the whole of ${nm} (replaces its samples)`]);
    return o;
  };
  const chopRender = () => {
    chopEl.hidden = !m.chop;
    $('[data-a="chop"]').textContent = m.chop ? "No chop" : "Chop…";
    if (!m.chop) return null;
    const c = m.chop, pl = m.chopPlan();
    for (const b of chopEl.querySelectorAll("[data-c]")) b.classList.toggle("on", b.dataset.c === c.mode);
    chopEl.querySelector(".c-auto").hidden = c.mode !== "auto";
    chopEl.querySelector(".c-beats").hidden = c.mode !== "beats";
    chopEl.querySelector(".c-equal").hidden = c.mode !== "equal";
    for (const [k, v] of [["sens", c.sens], ["bpm", c.bpm], ["div", c.div], ["count", c.count]]) {
      const i = chopEl.querySelector(`[data-ch="${k}"]`);
      if (document.activeElement !== i) i.value = String(v);
    }
    chopEl.querySelector(".smp-slices").innerHTML = pl.slices.map((x, i) =>
      `<button type="button" class="smp-btn${i >= pl.fit ? " lost" : ""}" data-a="cplay" data-s="${i}" title="${secText(x.n / m.edit.hz)}, ${sizeText(x.bytes)}">${i + 1}</button>`).join("");
    const nm = S.SLOT_NAMES[m.edit.slot];
    chopEl.querySelector(".smp-cplan").textContent = `${pl.slices.length} slice${pl.slices.length === 1 ? "" : "s"}`
      + (pl.lost ? ` · ${pl.fit} fit ${nm}, ${pl.lost} do not (${pl.fit >= pl.room ? `a set holds ${T.MAX_SLOTS} samples` : "no room left: a lower rate fits more"})` : ` · all fit ${nm} (${kib(pl.bytes)})`)
      + (c.edited ? " · cuts edited by hand" : "");
    return pl;
  };
  const renderEdit = () => {
    ed.hidden = !m.source;
    if (!m.source) return;
    const e = m.edit, p = m.plan(), n = m.source.length, lock = lockNow();
    for (const [k, v] of [["start", e.start], ["end", e.end]]) {
      const r = ed.querySelector(`[data-e="${k}"]`);
      r.max = String(n);
      r.value = String(v);
      ed.querySelector(`[data-o="${k}"]`).textContent = fmtAt(v);
    }
    ed.querySelector('[data-e="slot"]').value = String(e.slot);
    ed.querySelector('[data-e="into"]').innerHTML = intoOpts().map(([v, t]) => `<option value="${v}"${v === e.into ? " selected" : ""}>${esc(t)}</option>`).join("");
    const nm = ed.querySelector('[data-e="name"]');
    if (document.activeElement !== nm) nm.value = e.name;
    ed.querySelector('[data-e="normalize"]').checked = !!e.normalize;
    const hz = ed.querySelector('[data-e="hz"]');
    hz.innerHTML = m.choices().map((c) => `<option value="${c.hz}"${c.hz === e.hz ? " selected" : ""}>${hzText(c.hz)} · ${c.label} · ${secText(c.seconds)}${c.fits ? "" : " · too long"}</option>`).join("");
    const pl = chopRender(), name = S.SLOT_NAMES[e.slot], set = m.sets[e.slot];
    const full = e.into === "add" && m.room(e.slot).room <= 0, plain = !set || set.plain || e.into === "set";
    const fits = pl ? pl.fit > 0 : p.fits && !full;
    const plan = ed.querySelector(".smp-plan");
    plan.textContent = `${secText(p.seconds)} at ${hzText(p.hz)} (${p.label}) · ${kib(p.bytes)} of ${kib(p.max)} ${e.into === "set" ? `in ${name}` : `free in ${name}`}`
      + (full ? ` · ${name} holds ${T.MAX_SLOTS} samples already` : p.fits || pl ? "" : " · too long: trim it or pick a lower rate")
      + `${m.source.channels.length > 1 ? " · mixed to mono" : ""} · source ${hzText(m.source.rate)}`;
    plan.classList.toggle("bad", !fits && !pl);
    ed.querySelector(".ttl").textContent = e.into === "replace" ? `Sample ${e.index + 1} of ${name}` : "New sample";
    const go = ed.querySelector('[data-a="go"]');
    go.textContent = pl ? `Add ${pl.fit} slice${pl.fit === 1 ? "" : "s"} to ${name}` : e.into === "add" ? `Add to ${name}` : e.into === "replace" ? `Replace sample ${e.index + 1}` : `Upload (replaces ${name})`;
    go.disabled = lock || !fits || m.playing || !!m.restoreBlocked || !plain;
    ed.querySelector('[data-a="preview"]').disabled = !p.fits && !pl;
    ed.querySelector('[data-a="chop"]').disabled = e.into === "replace";
    wave.classList.toggle("chop", !!m.chop);
    const cuts = pl ? pl.slices.map((x, i) => ({ at: x.start, lost: i >= pl.fit, label: String(i + 1) })) : null;
    drawWave(wave, m.source.mono, e, cuts);
  };
  const render = () => {
    const lock = lockNow() || !m.available;
    grid.innerHTML = [0, 1, 2].map((k) => cardHtml(m, k, lock)).join("");
    $('[data-a="open"]').disabled = !m.available;
    $('[data-a="reload"]').disabled = lock;
    const text = m.message || (m.restoreBlocked ? `${m.restoreBlocked} Sample writes wait for it.` : m.playing ? "The FM-1 is playing: stop it to write a set, restore, rename or clear." : "");
    msg.hidden = !text;
    msg.textContent = text;
    msg.classList.toggle("err", !!m.message && m.error);
    renderEdit();
  };
  const progress = (op) => {
    run.hidden = !op;
    if (!op) return;
    $(".smp-run .what").textContent = `${op.what}…`;
    $(".smp-run .bar i").style.width = `${op.total ? Math.round(100 * op.done / op.total) : 0}%`;
    $(".smp-run .cnt").textContent = `${Math.round(100 * op.done / op.total)} %`;
    $('[data-a="cancel"]').disabled = m.cancelling;
  };
  const openSets = async () => { for (let k = 0; k < 3; k++) if (m.slots[k]?.valid && (!m.sets[k] || (!m.sets[k].dirty && m.sets[k].crc !== m.slots[k].crc))) await m.openSet(k).catch((e) => m.say(e.message, true)); };
  /** a change to a set made elsewhere: asked first, then laid out again */
  const changing = async (k, fn) => {
    try { return await fn(); } catch (e) {
      if (!e.relayout) throw e;
      if (!(await ctx.confirm(`${e.message} Continue?`))) return null;
      m.relayout(k);
      return fn();
    }
  };
  const loadFile = async (k, chop) => {
    const f = await pickFile("audio/*,.wav,.aif,.aiff,.aifc");
    if (!f) return;
    m.say(`Decoding ${f.name}…`);
    const src = await decodeFile(f);
    if (k >= 0) await m.openSet(k);
    m.setSource(src);
    if (k >= 0) m.setEdit({ slot: k, into: "add" });
    if (chop) m.chopStart();
    m.say("");
  };
  const act = async (a, k, i, btn) => {
    try {
      if (a === "open") await loadFile(-1, false);
      else if (a === "addto") await loadFile(k, false);
      else if (a === "chopto") await loadFile(k, true);
      else if (a === "reload") { m.cache.clear(); m.sets = m.sets.map((x) => (x?.dirty ? x : null)); m.say(""); await m.refresh(); await openSets(); }
      else if (a === "close") { m.source = null; m.edit = null; m.enc = null; m.chop = null; render(); }
      else if (a === "preview") { const p = m.preview(); play(p.pcm, p.hz); }
      else if (a === "chop") { if (m.chop) m.chopStop(); else m.chopStart(); }
      else if (a === "cdetect") { const t = m.chopTempo(); m.chopSet({ bpm: t.bpm }); m.say(`Tempo: ${t.bpm} BPM${t.whole ? ` (${t.whole} beats long)` : ""}.`); }
      else if (a === "chalf") m.chopSet({ bpm: Math.round(m.chop.bpm * 5) / 10 });
      else if (a === "cdouble") m.chopSet({ bpm: Math.round(m.chop.bpm * 20) / 10 });
      else if (a === "cplay") { const p = m.chopPreview(Number(btn.dataset.s)); play(p.pcm, p.hz); }
      else if (a === "go") {
        const e = m.edit, k2 = e.slot, x = m.slots[k2];
        await changing(k2, async () => {
          if (m.chop) return m.chopAdd();
          if (e.into === "add") return m.addToSet();
          if (e.into === "replace") return m.replaceInSet();
          if (x?.valid && !(await ctx.confirm(`Replace all of ${S.SLOT_NAMES[k2]} (${x.name || "its samples"}) with ${e.name}?`))) return null;
          return m.upload();
        });
        await openSets();
      } else if (a === "cancel") m.cancel();
      else if (a === "write") await m.writeSet(k);
      else if (a === "revert") { if (await ctx.confirm(`Undo the changes to ${S.SLOT_NAMES[k]} (read it again from the FM-1)?`)) await m.revertSet(k); }
      else if (a === "setfile") { const r = m.setFile(k); download(r.bytes, r.name); }
      else if (a === "relayout") { if (await ctx.confirm(`Lay out ${S.SLOT_NAMES[k]} again as plain samples, one key each? (Write sends it.)`)) m.relayout(k); }
      else if (a === "splay") { const p = m.slotSound(k, i); play(p.pcm, p.hz); }
      else if (a === "sedit") await changing(k, () => m.editSlot(k, i));
      else if (a === "sup") await changing(k, () => m.moveSlot(k, i, i - 1));
      else if (a === "sdown") await changing(k, () => m.moveSlot(k, i, i + 1));
      else if (a === "sname") {
        const nm = globalThis.prompt?.(`New name for sample ${i + 1} of ${S.SLOT_NAMES[k]} (8 characters):`, m.sets[k].slots[i].name);
        if (nm) await changing(k, () => m.renameSlot(k, i, nm));
      } else if (a === "sdel") {
        if (await ctx.confirm(`Delete sample ${i + 1} (${m.sets[k].slots[i].name}) from ${S.SLOT_NAMES[k]}? (Write sends it.)`)) await changing(k, () => m.deleteSlot(k, i));
      } else if (a === "backup") { const r = await m.backup(k); download(r.bytes, r.name); }
      else if (a === "restore") {
        const f = await pickFile(".fsmp,application/octet-stream");
        if (!f) return;
        const bytes = new Uint8Array(await f.arrayBuffer());
        const info = S.parseBackup(bytes, k).info, x = m.slots[k];
        if (x?.valid && !(await ctx.confirm(`Replace ${x.name || "the set"} in ${S.SLOT_NAMES[k]} with ${info.name || "the file's set"}?`))) return;
        await m.restore(k, bytes);
        await openSets();
      } else if (a === "rename") {
        const nm = globalThis.prompt?.(`New name for ${S.SLOT_NAMES[k]} (8 characters):`, m.slots[k].name);
        if (nm) await m.rename(k, nm);
        await openSets();
      } else if (a === "clear") {
        if (await ctx.confirm(`Clear ${S.SLOT_NAMES[k]} (${m.slots[k].name}) and all its samples? Back it up first if you want to keep it.`)) await m.clear(k);
      }
    } catch (e) {
      if (m.message !== e.message && !/kept here/.test(m.message)) m.say(e.message, !e.cancelled);
      ctx.toast?.(e.message);
    }
  };
  el.addEventListener("click", (e) => {
    const b = e.target.closest("button[data-a]");
    if (b && !b.disabled) act(b.dataset.a, Number(b.closest("[data-slot]")?.dataset.slot), Number(b.closest("[data-i]")?.dataset.i), b);
    const c = e.target.closest("button[data-c]");
    if (c && m.chop) m.chopSet({ mode: c.dataset.c });
  });
  ed.addEventListener("input", (e) => {
    const t = e.target.closest("[data-e]");
    if (t && m.source) {
      const k = t.dataset.e;
      if (k === "start" || k === "end") m.setEdit({ [k]: Number(t.value) });
      else if (k === "slot") { m.setEdit({ slot: Number(t.value) }); m.openSet(Number(t.value)).then(render, () => {}); }
      else if (k === "hz") m.setEdit({ hz: Number(t.value) });
      else if (k === "into") m.setEdit({ into: t.value });
      else if (k === "normalize") m.setEdit({ normalize: t.checked });
    }
    const c = e.target.closest("[data-ch]");
    if (c && m.chop) {
      const v = Number(c.value);
      if (c.dataset.ch === "bpm" && !(v >= 30 && v <= 300)) return;
      if (c.dataset.ch === "count" && !(v >= 2 && v <= 64)) return;
      m.chopSet({ [c.dataset.ch]: v });
    }
  });
  ed.addEventListener("change", (e) => {
    const t = e.target.closest('[data-e="name"]');
    if (t && m.source) m.setEdit({ name: t.value });
  });
  /* the waveform: the trim handles; chopping, the cuts (click adds, drag moves, double-click removes) */
  let drag = null;
  const atX = (ev) => { const r = wave.getBoundingClientRect(); return Math.round(Math.max(0, Math.min(1, (ev.clientX - r.left) / r.width)) * m.source.length); };
  const cutNear = (ev) => {
    const i = C.nearestCut(m.chop.cuts, atX(ev)), r = wave.getBoundingClientRect();
    return i >= 0 && Math.abs(m.chop.cuts[i] - atX(ev)) / m.source.length * r.width <= 6 ? i : -1;
  };
  wave.addEventListener("pointerdown", (ev) => {
    if (!m.source) return;
    const at = atX(ev);
    wave.setPointerCapture(ev.pointerId);
    if (m.chop) {
      const i = cutNear(ev);
      if (i >= 0) drag = { cut: m.chop.cuts[i] };
      else if (at > m.edit.start && at < m.edit.end) { m.chopCut("add", at); drag = { cut: at }; }
      return;
    }
    drag = Math.abs(at - m.edit.start) <= Math.abs(at - m.edit.end) ? "start" : "end";
    m.setEdit({ [drag]: at });
  });
  wave.addEventListener("pointermove", (ev) => {
    if (!drag) return;
    if (typeof drag === "string") { m.setEdit({ [drag]: atX(ev) }); return; }
    const i = m.chop.cuts.indexOf(drag.cut), to = Math.max(m.edit.start + 1, Math.min(m.edit.end - 1, atX(ev)));
    if (i >= 0 && to !== drag.cut) { m.chopCut("move", i, to); drag.cut = m.chop.cuts.includes(to) ? to : drag.cut; }
  });
  wave.addEventListener("pointerup", () => { drag = null; });
  wave.addEventListener("dblclick", (ev) => { if (m.chop) { const i = cutNear(ev); if (i >= 0) m.chopCut("remove", i); } });
  m.on("render", render);
  m.on("progress", progress);
  m.on("loading", (on) => { $(".snd-load").hidden = !on; });
  render();
  return {
    model: m,
    show: () => { if (!m.slots[0] && !m.loading) m.refresh().then(openSets); },
    destroy: () => m.destroy(),
  };
}

export const tab = { id: "samples", title: "Samples", cap: CAP.LIVE, mount: mountSamples };
