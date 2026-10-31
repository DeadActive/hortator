// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The Sound tab: the selected track's pages and the globals pages as the FM-1 has them, built from PAGES and DESC
// (new pages and knobs in the firmware appear here by themselves), each value with the device's own text, edited
// live both ways: SET from the page, CHANGED / STATE pushes from the device (web/EDITOR_PROTOCOL.md §2.2, §4 rule 5).
// SoundModel holds no DOM (node tests drive it against the mock device); mountSound draws it.
import { CAP, CMD, DF, GR, ID, NONE, SCOPE, U14_NONE } from "./proto.mjs";
import { esc } from "./ui.mjs";

export { ID }; // (proto.mjs: the core.h ids the page names, checked against device_data.json)
const FMT_ENUM = 8, FMT_LDEST = 21;
/** a change of these re-reads the track: pages, the CONTEXT knobs' descriptions, the values */
const TRACK_CONTEXT = new Set([ID.MODEL, ID.RMODEL, ID.ITYPE]);
const VALUES = 1, TRACK = 2, ALL = 3; // what a reload reads

export class SoundModel {
  constructor(session) {
    this.s = session;
    this.handlers = new Map();
    this.groups = []; // [{ title, scope, ids }]: the device's pages, consecutive pages of one title merged
    this.desc = new Map(); // "scope:id" -> { fmt, flags, min, max, def, label, unit, names, order }
    this.val = [new Map(), new Map()]; // [scope] id -> { value, text, unit }
    this.track = null; // the track shown (the selection when read)
    this.model = null;
    this.preset = U14_NONE;
    this.edited = 0;
    this.pending = 0;
    this.loading = null;
    this.offs = [
      session.on("changed", (p) => this.changed(p)),
      session.on("state", ({ state, prev }) => this.stateChanged(state, prev)),
      session.on("resync", () => this.reload(ALL)),
      // another tab loaded a preset onto a track, or restored the loaded slot: the device pushes only some of it
      session.on("reload", (w) => { if (w.slot !== undefined || (w.track !== undefined && (this.track === null || w.track === this.track))) this.reload(TRACK); }),
    ];
  }
  on(ev, fn) { if (!this.handlers.has(ev)) this.handlers.set(ev, new Set()); this.handlers.get(ev).add(fn); return () => this.handlers.get(ev).delete(fn); }
  emit(ev, x) { for (const fn of this.handlers.get(ev) || []) fn(x); }
  destroy() { this.offs.forEach((f) => f()); }
  static key(scope, id) { return `${scope}:${id}`; }
  value(scope, id) { return this.val[scope].get(id); }
  descOf(scope, id) { return this.desc.get(SoundModel.key(scope, id)); }

  load() { return this.reload(ALL); }
  /** reads coalesced: one at a time; a request for more while one runs reads once more after it */
  reload(what = TRACK) {
    this.pending = Math.max(this.pending, what);
    if (this.loading) return this.loading;
    this.loading = (async () => {
      this.emit("loading", true);
      try {
        while (this.pending) {
          const w = this.pending;
          this.pending = 0;
          if (w >= TRACK) { await this.readPages(); await this.readDescs(w === ALL); }
          await this.readValues();
          this.emit("render");
        }
      } catch (e) {
        this.pending = 0;
        if (this.s.connected && !this.s.busy) this.emit("error", e.message);
      } finally {
        this.loading = null;
        this.emit("loading", false);
      }
    })();
    return this.loading;
  }
  async readPages() {
    const pages = [];
    for (let first = 0; ;) {
      const r = await this.s.request(CMD.PAGES, { first });
      pages.push(...r.pages);
      first = r.first + r.pages.length;
      if (first >= r.total || !r.pages.length) break;
    }
    const groups = [], add = (title, scope, cols, any) => {
      const ids = cols.filter((x) => x !== U14_NONE);
      if (!ids.length) return;
      const g = any ? groups.find((x) => x.title === title && x.scope === scope) : groups[groups.length - 1];
      if (g && g.title === title && g.scope === scope) g.ids.push(...ids);
      else groups.push({ title, scope, ids });
    };
    for (const p of pages) {
      if (p.graph === GR.LFO) { // the device draws an LFO page in two halves: LFO 1's two knobs, then LFO 2's
        add(`${p.title} 1`, p.scope, p.ids.slice(0, 2), true);
        add(`${p.title} 2`, p.scope, p.ids.slice(2), true);
      } else add(p.title, p.scope, p.ids, false);
    }
    this.groups = groups;
  }
  async readDescs(all) {
    if (all) this.desc.clear();
    for (const g of this.groups) for (const id of g.ids) {
      const d = this.descOf(g.scope, id);
      if (!d || (g.scope === SCOPE.TRACK && d.flags & DF.CONTEXT)) await this.readDesc(g.scope, id);
    }
  }
  async readDesc(scope, id) {
    const track = scope === SCOPE.TRACK ? NONE : 0;
    const r = await this.s.request(CMD.DESC, { scope, track, id, first: 0 });
    const names = [...r.items];
    while (names.length < r.nnames) {
      const more = await this.s.request(CMD.DESC, { scope, track, id, first: names.length });
      if (!more.items.length) break;
      names.push(...more.items);
    }
    const order = r.flags & DF.ORDER ? (await this.s.request(CMD.DESC, { scope, track, id, first: 0x7f })).items : null;
    const d = { fmt: r.fmt, flags: r.flags, min: r.min, max: r.max, def: r.def, label: r.label, unit: r.unit, names, order };
    this.desc.set(SoundModel.key(scope, id), d);
    return d;
  }
  async readValues() {
    const d = await this.s.request(CMD.DUMP, { track: NONE });
    this.track = d.track;
    this.model = d.model;
    this.preset = d.preset;
    this.edited = d.edited;
    for (const [scope, n] of [[SCOPE.TRACK, d.p.length], [SCOPE.GLOBAL, d.g.length]]) {
      const vals = scope === SCOPE.TRACK ? d.p : d.g;
      for (let id = 0; id < n;) { // the texts: GET as many as fit, ask on from there
        const r = await this.s.request(CMD.GET, { scope, track: scope === SCOPE.TRACK ? NONE : 0, id, count: Math.min(127, n - id) });
        if (!r.values.length) break;
        r.values.forEach((v, i) => this.val[scope].set(id + i, { value: v.value, text: v.text, unit: v.unit }));
        id += r.values.length;
      }
      vals.forEach((v, id) => { if (!this.val[scope].has(id)) this.val[scope].set(id, { value: v, text: String(v), unit: "" }); });
    }
  }
  /** one knob read again (after a refusal: show what the device kept) */
  async refresh(scope, id) {
    const r = await this.s.request(CMD.GET, { scope, track: scope === SCOPE.TRACK ? NONE : 0, id, count: 1 });
    if (r.values[0]) this.put(scope, id, r.values[0]);
  }
  put(scope, id, v) {
    this.val[scope].set(id, { value: v.value, text: v.text, unit: v.unit });
    if (scope === SCOPE.TRACK && id === ID.MODEL) this.model = v.value;
    this.emit("value", { scope, id });
  }
  needsTrack(scope, id) { return scope === SCOPE.TRACK ? TRACK_CONTEXT.has(id) : id === ID.GMODE; }

  /** the page turns a knob: SET (a queued SET of the same knob takes the newer value), the device's text back */
  async set(scope, id, value) {
    try {
      const r = await this.s.request(CMD.SET, { scope, track: scope === SCOPE.TRACK ? NONE : 0, id, value },
        { key: `set:${scope}:${id}` });
      this.put(scope, id, r);
      if (scope === SCOPE.TRACK && id >= 1 && id <= 8) this.edited = 1;
      if (this.needsTrack(scope, id)) await this.reload(TRACK);
      return r;
    } catch (e) {
      this.emit("error", e.message);
      await this.refresh(scope, id).catch(() => {});
      return null;
    }
  }
  /** a push: a knob moved on the FM-1 */
  changed(p) {
    if (p.scope === SCOPE.TRACK && this.track !== null && p.track !== this.track) return; // a reload is coming
    this.put(p.scope, p.id, p);
    if (this.needsTrack(p.scope, p.id)) this.reload(TRACK);
  }
  stateChanged(st, prev) {
    if (!prev || this.track === null) return;
    if (st.sel !== prev.sel || st.slot !== prev.slot || st.var !== prev.var) this.reload(TRACK);
  }
}

/* -------------------------------------------------------------------- the view --- */
const isMenu = (d) => (d.fmt === FMT_ENUM || d.fmt === FMT_LDEST) && !(d.flags & DF.SPLIT) && d.names.length >= d.max - d.min + 1;
const kAttr = (scope, id) => `${scope}:${id}`;

function knobHtml(m, scope, id) {
  const d = m.descOf(scope, id), v = m.value(scope, id) || { value: 0, text: "", unit: "" };
  if (!d) return "";
  const ro = d.flags & DF.RO, k = kAttr(scope, id), lab = esc(d.label || "");
  let ctl = "";
  if (ro) ctl = "";
  else if (isMenu(d)) {
    const idx = d.order ? d.order : d.names.map((_, i) => i);
    ctl = `<select aria-label="${lab}">${idx.filter((i) => i <= d.max - d.min).map((i) =>
      `<option value="${d.min + i}"${d.min + i === v.value ? " selected" : ""}>${esc(d.names[i])}</option>`).join("")}</select>`;
  } else ctl = `<input type="range" min="${d.min}" max="${d.max}" step="1" value="${v.value}" aria-label="${lab}">`;
  return `<div class="knob${ro ? " ro" : ""}${d.flags & DF.LOCK ? " lockable" : ""}" data-k="${k}">
    <span class="lbl">${lab}</span><output><b>${esc(v.text)}</b><small>${esc(v.unit)}</small></output>${ctl}</div>`;
}

export function mountSound(el, ctx) {
  const m = new SoundModel(ctx.session);
  el.innerHTML = `<div class="snd-head"><h2 class="snd-title">Sound</h2><span class="snd-sub"></span>
    <span class="snd-load" hidden>Reading the FM-1…</span></div><div class="snd-pages"></div>`;
  const pagesEl = el.querySelector(".snd-pages"), sub = el.querySelector(".snd-sub");
  const head = () => {
    const md = m.descOf(SCOPE.TRACK, ID.MODEL), mv = m.value(SCOPE.TRACK, ID.MODEL);
    el.querySelector(".snd-title").textContent = m.track === null ? "Sound" : `Track ${m.track + 1}`;
    sub.textContent = (mv ? mv.text : md?.names?.[m.model] || "") + (m.preset !== U14_NONE && m.edited ? " (edited)" : "");
  };
  const render = () => {
    const sec = (scope, title) => {
      const gs = m.groups.filter((g) => g.scope === scope);
      if (!gs.length) return "";
      return `<h3 class="snd-sec">${esc(title)}</h3><div class="grid">${gs.map((g) => `<section class="page">
        <h4>${esc(g.title)}</h4><div class="knobs">${g.ids.map((id) => knobHtml(m, scope, id)).join("")}</div></section>`).join("")}</div>`;
    };
    pagesEl.innerHTML = sec(SCOPE.TRACK, "Track") + sec(SCOPE.GLOBAL, "Global");
    head();
  };
  const drawValue = ({ scope, id }) => {
    const box = pagesEl.querySelector(`[data-k="${kAttr(scope, id)}"]`), v = m.value(scope, id);
    if (scope === SCOPE.TRACK && id === ID.MODEL) head();
    if (!box || !v) return;
    box.querySelector("output b").textContent = v.text;
    box.querySelector("output small").textContent = v.unit;
    const c = box.querySelector("input, select");
    if (c && !(c === document.activeElement && box.classList.contains("drag"))) c.value = String(v.value);
    box.classList.remove("flash");
    void box.offsetWidth; // restart the touch mark
    box.classList.add("flash");
  };
  const edit = (e) => {
    const box = e.target.closest(".knob");
    if (!box || !e.target.matches("input, select")) return;
    const [scope, id] = box.dataset.k.split(":").map(Number);
    m.set(scope, id, Number(e.target.value));
  };
  pagesEl.addEventListener("input", edit);
  pagesEl.addEventListener("pointerdown", (e) => e.target.closest(".knob")?.classList.add("drag"));
  pagesEl.addEventListener("pointerup", (e) => e.target.closest(".knob")?.classList.remove("drag"));
  m.on("render", render);
  m.on("value", drawValue);
  m.on("error", (msg) => ctx.toast?.(msg));
  m.on("loading", (on) => { el.querySelector(".snd-load").hidden = !on; });
  m.load();
  return { model: m, destroy: () => m.destroy() };
}

export const tab = { id: "sound", title: "Sound", cap: CAP.LIVE, mount: mountSound };
