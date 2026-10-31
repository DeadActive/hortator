// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The Projects tab: projects A–D as the FM-1 keeps them in flash (BACKUP_LIST: saved / unsaved changes, size, and a
// summary read from the objects), one project to a `hortator-project` file and back into any slot, "Copy to…"
// another slot (BACKUP_GET the three objects, RESTORE them into the target), and "Back up all" / "Restore all" with a
// `hortator-backup` file (the 128 user presets + the four projects). web/EDITOR_PROTOCOL.md §2.5, §2.6, §5.
// Backups read flash, never RAM: unsaved changes on the FM-1 are flagged and not in the file. Restores run only while
// stopped (the device answers STOP FIRST otherwise), ask first, show progress, can be aborted until the commit, and
// show the device's refusals as it words them. ProjectsModel holds no DOM (node tests drive it against the mock:
// web/editor/test_tab_projects.mjs).
import {
  BF, CAP, CMD, DETAIL, FileError, LIMITS, NONE, OBJ, RC, RK, RS, SCOPE, crc32, decodePresets, encodePresets,
  makeBackupFile, makeProjectFile, readBackupFile, readProjectFile, summarize,
} from "./proto.mjs";
import { BUSY_MSG, clockGuard } from "./session.mjs";
import { SLOTS, chooser, esc } from "./ui.mjs";

const PIECE = LIMITS.piece;
const pieces = (n) => Math.ceil(n / PIECE);
const magicOf = (b) => (b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24) >>> 0;
const date = (d = new Date()) => `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
export const fileName = { project: (slot) => `hortator-project-${SLOTS[slot]}-${date()}.json`, backup: () => `hortator-backup-${date()}.json` };
const objs3 = (s) => [OBJ.PROJECT + s, OBJ.LOCKS + s, OBJ.VARS + s];

class Cancelled extends Error { constructor() { super("Cancelled."); this.cancelled = true; } }
const isSilence = (e) => !!(e && (e.timeout || e.message === BUSY_MSG));

export class ProjectsModel {
  constructor(session) {
    this.s = session;
    this.handlers = new Map();
    this.list = null; // BACKUP_LIST's 13 objects (null: never read: the tab was not shown yet)
    this.cache = new Map(); // obj -> { seq, crc, bytes }: flash's bytes, read once per storage sequence
    this.cards = SLOTS.map((_, slot) => ({ slot, present: false, unsaved: false, size: 0, summary: null }));
    this.presets = { present: false, unsaved: false };
    this.names = null; // the device's model names (DESC of MODEL)
    this.op = null; // the operation running: { what, done, total (pieces), commit, abortable }
    this.aborting = false;
    this.message = ""; // the last outcome or refusal, in words
    this.error = false;
    this.loading = null;
    this.offs = [
      session.on("state", ({ state, prev }) => {
        if (this.list && prev && !this.op && (state.used !== prev.used || state.dirty !== prev.dirty || state.flags !== prev.flags)) this.refresh();
        else this.emit("render");
      }),
      session.on("resync", () => { if (this.list) this.refresh(); }),
      session.on("busy", () => this.emit("render")),
      session.on("restore", () => this.emit("render")),
    ];
  }
  on(ev, fn) { if (!this.handlers.has(ev)) this.handlers.set(ev, new Set()); this.handlers.get(ev).add(fn); return () => this.handlers.get(ev).delete(fn); }
  emit(ev, x) { for (const fn of this.handlers.get(ev) || []) fn(x); }
  destroy() { this.offs.forEach((f) => f()); }
  get playing() { return !!this.s.state?.playing; }
  get canRestore() { return this.s.has(CMD.RESTORE); }
  say(msg, error = false) { this.message = msg; this.error = error; this.emit("render"); }

  /* ------------------------------------------------------------- reading --- */
  async readList() {
    const o = (await this.s.request(CMD.BACKUP_LIST)).objects;
    this.list = o;
    this.presets = { present: !!o[OBJ.PRESETS].present, unsaved: !!(o[OBJ.PRESETS].flags & BF.UNSAVED) };
    this.cards = SLOTS.map((_, s) => {
      const [rec, lk, vr] = objs3(s).map((i) => o[i]), old = this.cards[s];
      const same = old.summary && old.seq === rec.seq && old.crc === rec.crc && rec.present;
      return { slot: s, present: !!rec.present, unsaved: !!(rec.flags & BF.UNSAVED), lost: !!(rec.flags & BF.LOST),
        old: !!(rec.flags & BF.OLD), size: [rec, lk, vr].reduce((n, x) => n + (x.present ? x.size : 0), 0),
        seq: rec.seq, crc: rec.crc, summary: same ? old.summary : null };
    });
    this.emit("render");
    return o;
  }
  /** BACKUP_LIST, then each project's summary (its objects read once per storage sequence) */
  refresh() {
    if (this.loading) { this.again = true; return this.loading; }
    this.loading = (async () => {
      this.emit("loading", true);
      try {
        do {
          this.again = false;
          await this.readList();
          await this.modelNames();
          for (const c of this.cards) if (c.present && !c.summary && !this.op) {
            const p = await this.readProject(c.slot);
            if (p) c.summary = summarize(p, this.names);
            this.emit("render");
          }
        } while (this.again && !this.op);
      } catch (e) {
        if (!isSilence(e) && !this.op) this.say(e.message, true);
      } finally {
        this.loading = null;
        this.emit("loading", false);
      }
    })();
    return this.loading;
  }
  async modelNames() {
    if (this.names) return this.names;
    try {
      const r = await this.s.request(CMD.DESC, { scope: SCOPE.TRACK, track: NONE, id: 0, first: 0 });
      const names = [...r.items];
      while (names.length < r.nnames) {
        const more = await this.s.request(CMD.DESC, { scope: SCOPE.TRACK, track: NONE, id: 0, first: names.length });
        if (!more.items.length) break;
        names.push(...more.items);
      }
      this.names = names;
    } catch (e) { if (isSilence(e)) throw e; this.names = []; }
    return this.names;
  }
  /** one object's flash bytes, piece by piece, its CRC checked (cached per storage sequence) */
  async readObj(obj) {
    const e = this.list[obj];
    if (!e.present) return null;
    const c = this.cache.get(obj);
    if (c && c.seq === e.seq && c.crc === e.crc && c.bytes.length === e.size) { this.tick(pieces(e.size)); return c.bytes; }
    const b = new Uint8Array(e.size);
    for (let off = 0; off < e.size; off += PIECE) {
      const r = await this.s.request(CMD.BACKUP_GET, { obj, offset: off, seq: e.seq });
      if (r.offset !== off || r.len !== Math.min(PIECE, e.size - off)) throw new Error(`The FM-1 sent a wrong piece of object ${obj}.`);
      b.set(r.data.subarray(0, r.len), off);
      this.tick();
    }
    if (crc32(b) !== e.crc) throw Object.assign(new Error("CRC"), { stale: true });
    this.cache.set(obj, { seq: e.seq, crc: e.crc, bytes: b });
    return b;
  }
  /** objects read together; one changed under the read (rc 5: a SAVE meanwhile): LIST again, read again (3 tries) */
  async readFresh(objs) {
    for (let t = 0; ; t++) {
      try {
        const out = [];
        for (const o of objs) out.push(await this.readObj(o));
        return out;
      } catch (e) {
        const stale = e.stale || (e.rc === RC.STALE && e.detail === DETAIL.CHANGED);
        if (!stale || t >= 2) throw stale ? new Error("The FM-1 kept saving while it was read: try again.") : e;
        await this.readList();
      }
    }
  }
  /** a project's three objects from flash: { record, locks, vars, unsaved } (null: the slot is empty) */
  async readProject(slot) {
    if (!this.list) await this.readList();
    if (!this.list[OBJ.PROJECT + slot].present) return null;
    const [record, locks, vars] = await this.readFresh(objs3(slot));
    return { record, locks, vars, unsaved: !!(this.list[OBJ.PROJECT + slot].flags & BF.UNSAVED) };
  }

  /* ------------------------------------------------------- one operation --- */
  tick(n = 1) { if (this.op) { this.op.done = Math.min(this.op.total, this.op.done + n); this.emit("progress", this.op); } }
  /** why a restore cannot start now ("": it can): another tab's restore runs (session.mjs, the restore lock) */
  get restoreBlocked() { return this.s.restoreBlocked("Projects"); }
  /** one operation at a time: progress, abort, its outcome as the message (and thrown again for the caller).
   * restore: the words for the session's restore lock, held for the whole operation (other tabs cannot restore) */
  async run(what, total, fn, restore = "") {
    if (this.op) throw new Error("Another backup or restore is running.");
    const release = restore ? this.s.lockRestore("Projects", restore) : () => {};
    this.op = { what, done: 0, total, commit: false, abortable: false };
    this.aborting = false;
    this.say("");
    this.emit("progress", this.op);
    try {
      return await fn();
    } catch (e) {
      this.say(e.message, true);
      throw e;
    } finally {
      release();
      this.op = null;
      this.aborting = false;
      this.emit("progress", null);
      this.emit("render");
    }
  }
  /** stop the restore running, before its commit: ABORT, nothing written */
  abort() { if (this.op && this.op.abortable) { this.aborting = true; this.emit("render"); } }
  sizeOf(objs) { return objs.reduce((n, o) => n + (this.list[o].present ? pieces(this.list[o].size) : 0), 0); }
  partsPieces(parts) { return parts.reduce((n, b) => n + (b ? pieces(b.length) : 0), 0); }

  /** BEGIN, every part's pieces, COMMIT. A failure before the commit: ABORT (the device reloads the slot from flash) */
  async restoreParts(kind, slot, parts, label) {
    if (!this.canRestore) throw new Error("This firmware cannot restore.");
    const desc = parts.map((b) => (b ? { size: b.length, crc: crc32(b) } : { size: 0, crc: 0 }));
    let begun = false;
    Object.assign(this.op, { abortable: true, commit: false });
    this.emit("progress", this.op);
    try {
      if (this.aborting) throw new Cancelled();
      await this.s.request(CMD.RESTORE, { op: RS.BEGIN, kind, slot, magic: magicOf(parts[0]), parts: desc });
      begun = true;
      for (let p = 0; p < 3; p++) {
        const b = parts[p];
        for (let off = 0; b && off < b.length; off += PIECE) {
          if (this.aborting) throw new Cancelled();
          await this.s.request(CMD.RESTORE, { op: RS.DATA, part: p, offset: off, data: b.subarray(off, off + PIECE) });
          this.tick();
        }
      }
      if (this.aborting) throw new Cancelled();
      Object.assign(this.op, { abortable: false, commit: true });
      this.emit("progress", this.op);
      return await this.s.request(CMD.RESTORE, { op: RS.COMMIT });
    } catch (e) {
      const commit = this.op.commit;
      if (begun && !isSilence(e) && !this.s.busy) await this.s.request(CMD.RESTORE, { op: RS.ABORT }).catch(() => {});
      throw Object.assign(new Error(explain(e, label, commit)), { cause: e });
    } finally {
      Object.assign(this.op, { abortable: false, commit: false });
    }
  }
  restoreProjectObjs(objs, slot, label) {
    return this.restoreParts(RK.PROJECT, slot, [objs.record, objs.locks || null, objs.vars || null], label);
  }
  /** after writes: the slots' cached bytes dropped; the loaded slot was loaded again: every view reads it again */
  after(slots) {
    for (const s of slots) objs3(s).forEach((o) => this.cache.delete(o));
    if (this.s.busy || !this.s.connected) return;
    if (slots.includes(this.s.state?.slot)) this.s.reloaded({ slot: this.s.state.slot });
    this.refresh();
  }

  /* ------------------------------------------------------------ actions --- */
  meta(extra) { return { firmware: this.s.info?.firmware || "", ...extra }; }
  /** "Back up": { file, name, unsaved } — slot's flash copy as a hortator-project file */
  async backupProject(slot) {
    await this.readList();
    if (!this.list[OBJ.PROJECT + slot].present) throw new Error(`${SLOTS[slot]} is empty: nothing to back up.`);
    await this.modelNames();
    return this.run(`Backing up ${SLOTS[slot]}`, this.sizeOf(objs3(slot)), async () => {
      const p = await this.readProject(slot);
      const file = makeProjectFile(p, slot, this.meta({ unsaved: p.unsaved }), this.names);
      this.say(`${SLOTS[slot]} backed up.${p.unsaved ? ` ${SLOTS[slot]}'s unsaved changes are not in the file: SAVE on the FM-1 to include them.` : ""}`);
      return { file, name: fileName.project(slot), unsaved: p.unsaved };
    });
  }
  /** what a restore into slot replaces (for the confirmation) */
  replaces(slot) {
    const c = this.cards[slot], n = SLOTS[slot], st = this.s.state || {};
    let t = `This replaces ${c.present ? `project ${n}${c.summary?.name ? ` (${c.summary.name})` : ""}` : `empty slot ${n}`} on the FM-1.`;
    if (c.unsaved || (st.dirty >> slot & 1)) t += ` ${n}'s unsaved changes on the FM-1 are lost.`;
    if (st.slot === slot) t += ` ${n} is loaded: it is loaded again from the new project.`;
    return t;
  }
  /** "Restore here…": a parsed hortator-project file into slot */
  async restoreProjectFile(json, slot) {
    const p = readProjectFile(json);
    return this.run(`Restoring ${SLOTS[slot]}`, this.partsPieces([p.record, p.locks, p.vars]), async () => {
      try { await this.restoreProjectObjs(p, slot, SLOTS[slot]); } finally { this.after([slot]); }
      this.say(`Restored into ${SLOTS[slot]}${p.summary.name ? ` (${p.summary.name})` : ""}.`);
    }, `project ${SLOTS[slot]}`);
  }
  /** "Copy to…": slot from's flash copy into slot to */
  async copySlot(from, to) {
    if (from === to) throw new Error("Pick another slot.");
    await this.readList();
    if (!this.list[OBJ.PROJECT + from].present) throw new Error(`${SLOTS[from]} is empty: nothing to copy.`);
    return this.run(`Copying ${SLOTS[from]} to ${SLOTS[to]}`, 2 * this.sizeOf(objs3(from)), async () => {
      const p = await this.readProject(from);
      this.op.total = this.op.done + this.partsPieces([p.record, p.locks, p.vars]);
      try { await this.restoreProjectObjs(p, to, SLOTS[to]); } finally { this.after([to]); }
      this.say(`${SLOTS[from]} copied to ${SLOTS[to]}.${p.unsaved ? ` (${SLOTS[from]} as saved: its unsaved changes are not copied.)` : ""}`);
    }, `project ${SLOTS[to]}`);
  }
  /** "Back up all": { file, name, unsaved: [what] } — the user presets and the four projects from flash */
  async backupAll() {
    await this.readList();
    await this.modelNames();
    return this.run("Backing up everything", this.sizeOf([OBJ.PRESETS, ...[0, 1, 2, 3].flatMap(objs3)]), async () => {
      const [upr] = await this.readFresh([OBJ.PRESETS]);
      const presets = upr ? decodePresets(upr) : Array(128).fill(null);
      const projects = [];
      for (let s = 0; s < 4; s++) projects.push(await this.readProject(s));
      const unsaved = [...(this.presets.unsaved ? ["user presets"] : []),
        ...projects.flatMap((p, s) => ((p ? p.unsaved : this.cards[s].unsaved) ? [SLOTS[s]] : []))];
      const file = makeBackupFile(presets, projects, this.meta(), this.names);
      this.say(`Everything backed up.${unsaved.length ? ` Unsaved changes on the FM-1 are not in the file (${unsaved.join(", ")}): SAVE on the FM-1 to include them.` : ""}`);
      return { file, name: fileName.backup(), unsaved };
    });
  }
  /** what "Restore all" replaces (for the confirmation) */
  replacesAll(b) {
    const has = b.projects.flatMap((p, s) => (p ? [SLOTS[s]] : [])), keep = b.projects.flatMap((p, s) => (p ? [] : [SLOTS[s]]));
    const lost = [...(this.presets.unsaved ? ["user presets"] : []), ...b.projects.flatMap((p, s) => (p && this.cards[s].unsaved ? [SLOTS[s]] : []))];
    return `This replaces the 128 user presets${has.length ? ` and projects ${has.join(", ")}` : ""} on the FM-1.`
      + (keep.length ? ` The file has no ${keep.join(", ")}: the FM-1 keeps ${keep.length > 1 ? "them" : "it"}.` : "")
      + (lost.length ? ` Unsaved changes on the FM-1 are lost (${lost.join(", ")}).` : "")
      + (b.projects.some((p, s) => p && this.s.state?.slot === s) ? " The loaded slot is loaded again from the file." : "");
  }
  /** "Restore all": a parsed hortator-backup file; the user presets first, then each project into its slot */
  async restoreAll(json) {
    const b = readBackupFile(json), upr = encodePresets(b.slots);
    const total = pieces(upr.length) + b.projects.reduce((n, p) => n + (p ? this.partsPieces([p.record, p.locks, p.vars]) : 0), 0);
    const done = [], slots = [];
    return this.run("Restoring everything", total, async () => {
      try {
        await this.restoreParts(RK.PRESETS, 0, [upr, null, null], "the user presets");
        this.cache.delete(OBJ.PRESETS);
        done.push("user presets");
        for (let s = 0; s < 4; s++) if (b.projects[s]) {
          slots.push(s);
          await this.restoreProjectObjs(b.projects[s], s, SLOTS[s]);
          done.push(SLOTS[s]);
        }
      } catch (e) {
        throw Object.assign(new Error(`${e.message}${done.length ? ` Already restored: ${done.join(", ")}.` : ""}`), { cause: e.cause || e });
      } finally {
        if (done.length) this.s.reloaded({ presets: true });
        this.after(slots);
      }
      this.say(`Everything restored (${done.join(", ")}).`);
    }, "a backup");
  }
}

/** a failed restore in words: the device's own message, and what flash holds now */
export function explain(e, label, commit) {
  if (e.cancelled) return `Restore of ${label} cancelled: nothing was written.`;
  if (isSilence(e)) return commit
    ? `The FM-1 stopped answering during the commit of ${label} (update?): when it answers again, check ${label}.`
    : `Restore of ${label} dropped: the FM-1 went busy (update?). Nothing was written.`;
  if (e.rc === RC.BUSY && e.detail === DETAIL.NONE) return `Restore of ${label} dropped: an update is starting (${e.message}). Nothing was written.`;
  if (e.rc === RC.BUSY) return `${e.message}: the FM-1 is busy restoring. Try again when it is done.`;
  if (e.rc === RC.STOP) return `${e.message}: stop the FM-1, then restore ${label}. Nothing was written.`;
  if (e.rc === RC.FLASH) return `${e.message}: a flash write failed while restoring ${label}; flash keeps what it held before.`;
  if (e.rc) return `Restore of ${label} refused: ${e.message}. Nothing was written.`;
  return `Restore of ${label} failed: ${e.message}`;
}

/* -------------------------------------------------------------------- the view --- */
const CSS = `
.prj-acts { display: flex; flex-wrap: wrap; gap: 8px; margin: 16px 0 0; }
.prj-btn, .prj-card select { background: var(--k); color: var(--c5); border: 2px solid var(--c2); border-radius: 0; padding: 0 8px; cursor: pointer; }
.prj-btn:hover:not(:disabled), .prj-card select:hover:not(:disabled) { border-color: var(--w); color: var(--w); }
.prj-btn:disabled, .prj-card select:disabled { opacity: .4; cursor: default; }
.prj-btn.main { background: var(--w); border-color: var(--w); color: var(--k); }
.prj-btn.main:hover:not(:disabled) { background: var(--k); color: var(--w); }
.prj-note { color: var(--c3); margin: 8px 0 0; max-width: 90ch; }
.prj-msg { margin: 16px 0 0; padding: 8px; border: 2px solid var(--c2); color: var(--c5); max-width: 90ch; }
.prj-msg.err { border-color: var(--w); color: var(--w); }
.prj-run { display: flex; flex-wrap: wrap; align-items: center; gap: 8px 16px; margin: 16px 0 0; }
.prj-run .bar { flex: 1 1 200px; height: 12px; border: 2px solid var(--c2); max-width: 400px; }
.prj-run .bar i { display: block; height: 100%; background: var(--w); width: 0; }
.prj-run .cnt { color: var(--c3); }
.prj-grid { margin-top: 24px; }
.prj-card h4 { display: flex; justify-content: space-between; gap: 8px; }
.prj-card .st.uns { background: var(--w); color: var(--k); padding: 0 4px; }
.prj-card .body { padding: 8px; }
.prj-card dl { display: grid; grid-template-columns: auto 1fr; gap: 0 16px; margin: 0; }
.prj-card dt { color: var(--c3); }
.prj-card dd { margin: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.prj-card .empty { color: var(--c3); margin: 0; }
`;
function style() {
  if (document.getElementById("prj-style")) return;
  const s = document.createElement("style");
  s.id = "prj-style";
  s.textContent = CSS;
  document.head.append(s);
}
function download(file, name) {
  const url = URL.createObjectURL(new Blob([JSON.stringify(file, null, 1)], { type: "application/json" }));
  const a = Object.assign(document.createElement("a"), { href: url, download: name });
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 10000);
}
function pickJson() { // a file the user picks -> its JSON (null: none picked)
  return new Promise((res, rej) => {
    const i = Object.assign(document.createElement("input"), { type: "file", accept: ".json,application/json" });
    i.addEventListener("change", async () => {
      const f = i.files && i.files[0];
      if (!f) { res(null); return; }
      try { res(JSON.parse(await f.text())); } catch { rej(new FileError("format", `${f.name}: not a JSON file.`)); }
    });
    i.click();
  });
}
const kb = (n) => (n >= 1024 ? `${(n / 1024).toFixed(1)} KB` : `${n} B`);

function cardHtml(m, c, lock, stopLock) {
  const n = SLOTS[c.slot], s = c.summary, loaded = m.s.state?.slot === c.slot;
  const status = !c.present ? (c.unsaved ? "unsaved changes" : "empty") : c.unsaved ? "unsaved changes" : "saved";
  const rows = [["name", s ? s.name || "(no name)" : "…"], ["variations", s ? s.variations.join(" ") : "…"],
    ["locks", s ? String(s.locks) : "…"], ["size", kb(c.size)],
    ...(c.old ? [["format", `${s?.format || "older"}: converted at load`]] : []),
    ...(c.lost ? [["note", "links an object no flash copy holds"]] : [])];
  const restore = m.canRestore;
  return `<section class="page prj-card" data-slot="${c.slot}">
    <h4><span>${n}${loaded ? " · loaded" : ""}</span><span class="st${c.unsaved ? " uns" : ""}">${esc(status)}</span></h4>
    <div class="body">${c.present ? `<dl>${rows.map(([k, v]) => `<dt>${esc(k)}</dt><dd>${esc(v)}</dd>`).join("")}</dl>`
      : '<p class="empty">No project saved here.</p>'}
    ${c.unsaved ? `<p class="prj-note">${n} has unsaved changes: SAVE on the FM-1 to include them in a backup.</p>` : ""}
    <div class="prj-acts">
      <button type="button" class="prj-btn" data-a="backup"${lock || !c.present ? " disabled" : ""}>Back up</button>
      ${restore ? `<button type="button" class="prj-btn" data-a="restore"${stopLock ? " disabled" : ""}>Restore here…</button>
      <select data-a="copy" aria-label="Copy ${n} to another slot"${stopLock || !c.present ? " disabled" : ""}>
        <option value="">Copy to…</option>${SLOTS.map((x, i) => (i === c.slot ? "" : `<option value="${i}">${x}</option>`)).join("")}</select>` : ""}
    </div></div></section>`;
}

export function mountProjects(el, ctx) {
  style();
  const m = new ProjectsModel(ctx.session);
  el.innerHTML = `<div class="snd-head"><h2>Projects</h2><span class="snd-sub">A–D as saved in the FM-1's flash</span>
    <span class="snd-load" hidden>Reading the FM-1…</span></div>
    <div class="prj-acts"><button type="button" class="prj-btn main" data-a="backup-all">Back up all</button>
      <button type="button" class="prj-btn" data-a="restore-all">Restore all…</button>
      <button type="button" class="prj-btn" data-a="reload">Read again</button></div>
    <p class="prj-note">A backup holds what the FM-1 loads after a power cycle: the 128 user presets and the projects with
      their lock banks and variations. Restores and copies need the FM-1 stopped.</p>
    <div class="prj-run" hidden><span class="what"></span><span class="bar"><i></i></span><span class="cnt"></span>
      <button type="button" class="prj-btn" data-a="abort">Abort</button></div>
    <div class="prj-msg" role="status" hidden></div>
    <div class="grid prj-grid"></div>`;
  const $ = (s) => el.querySelector(s), grid = $(".prj-grid"), run = $(".prj-run"), msg = $(".prj-msg");
  const render = () => {
    const other = m.restoreBlocked, lock = !!m.op || m.s.busy || !m.s.connected, stopLock = lock || m.playing || !!other;
    grid.innerHTML = m.cards.map((c) => cardHtml(m, c, lock, stopLock)).join("");
    $('[data-a="backup-all"]').disabled = lock;
    $('[data-a="restore-all"]').hidden = !m.canRestore;
    $('[data-a="restore-all"]').disabled = stopLock;
    $('[data-a="reload"]').disabled = lock;
    const text = m.message || (other && m.canRestore ? `${other} Restores and copies wait for it.`
      : m.playing && m.canRestore ? "The FM-1 is playing: stop it to restore or copy." : "");
    msg.hidden = !text;
    msg.textContent = text;
    msg.classList.toggle("err", !!m.message && m.error);
    $('[data-a="abort"]').disabled = !m.op?.abortable || m.aborting;
  };
  const progress = (op) => {
    run.hidden = !op;
    if (!op) return;
    $(".prj-run .what").textContent = op.commit ? `${op.what}: writing flash…` : `${op.what}…`;
    $(".prj-run .bar i").style.width = `${op.total ? Math.round(100 * op.done / op.total) : 0}%`;
    $(".prj-run .cnt").textContent = `${op.done} / ${op.total}`;
    $('[data-a="abort"]').disabled = !op.abortable || m.aborting;
  };
  const ask = async (t) => !!(await ctx.confirm(t));
  const clockOk = () => clockGuard(m.s, chooser(ctx), (t) => ctx.toast?.(t)); // playing, SYNC out on: stop first?
  const fileOf = async (want) => {
    const j = await pickJson();
    if (j && want === "hortator-project" && j.format === "hortator-backup") throw new FileError("format", "This is a hortator-backup file: use Restore all….");
    return j;
  };
  const act = async (a, slot, val) => {
    try {
      if (a === "backup") { if (!(await clockOk())) return; const r = await m.backupProject(slot); download(r.file, r.name); }
      else if (a === "backup-all") { if (!(await clockOk())) return; const r = await m.backupAll(); download(r.file, r.name); }
      else if (a === "reload") { m.cache.clear(); m.cards.forEach((c) => { c.summary = null; }); m.say(""); await m.refresh(); }
      else if (a === "abort") m.abort();
      else if (a === "restore") {
        const j = await fileOf("hortator-project");
        if (!j) return;
        const p = readProjectFile(j);
        if (await ask(`Restore ${p.summary.name || "the project"} from the file into ${SLOTS[slot]}? ${m.replaces(slot)}`) && await clockOk()) await m.restoreProjectFile(j, slot);
      } else if (a === "restore-all") {
        const j = await fileOf("hortator-backup");
        if (!j) return;
        if (await ask(`Restore everything from the file? ${m.replacesAll(readBackupFile(j))}`) && await clockOk()) await m.restoreAll(j);
      } else if (a === "copy") {
        const to = Number(val);
        if (await ask(`Copy ${SLOTS[slot]} (as saved in flash) to ${SLOTS[to]}? ${m.replaces(to)}`) && await clockOk()) await m.copySlot(slot, to);
      }
    } catch (e) {
      if (m.message !== e.message) m.say(e.message, true);
      ctx.toast?.(e.message);
    }
  };
  el.addEventListener("click", (e) => {
    const b = e.target.closest("button[data-a]");
    if (b && !b.disabled) act(b.dataset.a, Number(b.closest("[data-slot]")?.dataset.slot));
  });
  el.addEventListener("change", (e) => {
    const s = e.target.closest('select[data-a="copy"]');
    if (!s || s.value === "") return;
    const v = s.value;
    s.value = "";
    act("copy", Number(s.closest("[data-slot]").dataset.slot), v);
  });
  m.on("render", render);
  m.on("progress", progress);
  m.on("loading", (on) => { $(".snd-load").hidden = !on; });
  render();
  return {
    model: m,
    show: () => { if (!m.list && !m.loading) m.refresh(); }, // read when first shown; later: STATE / resync / "Read again"
    destroy: () => m.destroy(),
  };
}

export const tab = { id: "projects", title: "Projects", cap: CAP.BACKUP, mount: mountProjects };
