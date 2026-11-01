// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The Samples tab: the FM-1's three user sample slots USR1–USR3 (the SAMPLE model's and the LAYER's own sets;
// web/EDITOR_PROTOCOL.md §2.7, §5). Each slot: its name, length, the bytes used of its room, Play (READ, decoded as the
// FM-1 decodes), Back up (.fsmp), Restore…, Rename…, Clear. A sound file (WAV, AIFF — whatever the browser decodes)
// becomes one sample: mono, trimmed on its waveform, optionally normalized, at its own rate (44.1 kHz at most) or a
// lower one when the slot needs it, IMA ADPCM exactly as the FM-1 decodes it (sample.mjs); Preview plays those bytes
// decoded; Upload sends it (SMP_BEGIN, the pieces, SMP_END) with progress and Cancel. Uploads and restores need the
// FM-1 stopped; a failed or cancelled one leaves the slot empty, never half a sample. SamplesModel holds no DOM
// (web/editor/test_tab_samples.mjs drives it against the mock device); mountSamples draws it.
import { CAP, CMD, DETAIL, LIMITS, RC, crc32 } from "./proto.mjs";
import * as S from "./sample.mjs";
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
  /** a decoded file: { name, rate, channels: [Float32Array] } */
  setSource(src) {
    const mono = S.mixdown(src.channels);
    this.source = { name: src.name, rate: src.rate, channels: src.channels, mono, length: mono.length };
    const empty = this.slots.findIndex((x) => x && !x.valid);
    this.edit = { start: 0, end: mono.length, normalize: false, slot: empty >= 0 ? empty : 0, name: S.cleanName(src.name), hz: 0 };
    this.edit.hz = S.pickRate(this.choices()).hz;
    this.enc = null;
    this.say("");
  }
  /** the rates offered for the trimmed source into the target slot */
  choices() {
    const e = this.edit;
    return S.rateChoices(this.source.rate, Math.max(1, e.end - e.start), S.SLOT_MAX[e.slot]);
  }
  /** change the trim, normalize, slot, name or rate. The rate follows (the highest that fits) unless one is given */
  setEdit(patch) {
    const e = this.edit, n = this.source.length;
    Object.assign(e, patch);
    e.start = Math.max(0, Math.min(n - 1, Math.round(e.start)));
    e.end = Math.max(e.start + 1, Math.min(n, Math.round(e.end)));
    if (patch.name !== undefined) e.name = S.cleanName(patch.name);
    if (patch.hz === undefined && (patch.start !== undefined || patch.end !== undefined || patch.slot !== undefined || !e.hz)) e.hz = S.pickRate(this.choices()).hz;
    this.enc = null;
    this.emit("render");
  }
  /** what the edit makes: { hz, n, bytes, fits, seconds, label, max } */
  plan() {
    const e = this.edit, n = Math.max(1, S.lengthAt(e.end - e.start, this.source.rate, e.hz)), bytes = Math.ceil(n / 2);
    return { hz: e.hz, n, bytes, fits: bytes <= S.SLOT_MAX[e.slot], seconds: n / e.hz, label: S.qualityOf(e.hz), max: S.SLOT_MAX[e.slot] };
  }
  /** the encoded sample (made once per edit) */
  prepare() {
    if (this.enc) return this.enc;
    const p = this.plan();
    if (!p.fits) throw new S.SampleError(`Too long for ${S.SLOT_NAMES[this.edit.slot]} (${kib(p.bytes)} of ${kib(p.max)}): trim it or pick a lower rate.`);
    this.enc = S.encodeSample(S.render(this.source, this.edit), this.edit.hz, this.edit.name);
    return this.enc;
  }
  /** what the FM-1 will play: the encoded bytes decoded as the device decodes them */
  preview() { const e = this.prepare(); return { pcm: S.adpcmDecode(e.data, e.n), hz: e.hz }; }

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
      this.say(`Restored ${f.info.name || "the sample"} into ${S.SLOT_NAMES[k]}.`);
    } catch (e) { this.say(e.message, !e.cancelled); throw e; }
  }
  async clear(k) {
    try {
      this.stopped("clear a slot");
      await this.s.request(CMD.SMP_CLEAR, { slot: k });
      this.cache.delete(k);
      await this.info(k);
      this.say(`${S.SLOT_NAMES[k]} cleared.`);
    } catch (e) { this.say(e.message, true); throw e; }
  }
  async rename(k, name) {
    try {
      this.stopped("rename a sample");
      await this.s.request(CMD.SMP_RENAME, { slot: k, name: S.cleanName(name) });
      this.cache.delete(k);
      await this.info(k);
      this.say(`${S.SLOT_NAMES[k]} is now ${this.slots[k].name}.`);
    } catch (e) { this.say(e.message, true); throw e; }
  }
}

/* ------------------------------------------------------------------------ the DOM --- */
const CSS = `
.smp-acts { display: flex; flex-wrap: wrap; gap: 8px; margin: 16px 0 8px; }
.smp-card .bar, .smp-run .bar { display: inline-block; width: 100%; height: 8px; background: var(--c1); vertical-align: middle; }
.smp-card .bar i, .smp-run .bar i { display: block; height: 100%; background: var(--w); }
.smp-card .bar.full i { background: var(--c4); }
.smp-card .body { padding: 8px; }
.smp-card dl { display: grid; grid-template-columns: auto 1fr; gap: 0 16px; margin: 0 0 8px; }
.smp-card dt { color: var(--c3); }
.smp-card dd { margin: 0; color: var(--c5); }
.smp-card .empty { color: var(--c3); margin: 0 0 8px; }
.smp-card .st { float: right; color: var(--c3); }
.smp-card .st.on { color: var(--w); }
.smp-edit { margin: 16px 0; }
.smp-edit .body { padding: 8px; display: grid; gap: 8px; }
.smp-wave { width: 100%; height: 120px; background: var(--k); border: 2px solid var(--c1); cursor: ew-resize; touch-action: none; display: block; }
.smp-trim { display: grid; grid-template-columns: auto 1fr auto; gap: 4px 8px; align-items: center; }
.smp-trim input[type=range] { width: 100%; }
.smp-row { display: flex; flex-wrap: wrap; gap: 8px 16px; align-items: center; }
.smp-row label { color: var(--c3); }
.smp-row select, .smp-row input[type=text] { background: var(--k); color: var(--c5); border: 2px solid var(--c2); border-radius: 0; font: inherit; padding: 0 4px; }
.smp-row input[type=text] { width: 10ch; text-transform: uppercase; }
.smp-plan { color: var(--c5); }
.smp-plan.bad { background: var(--w); color: var(--k); padding: 0 8px; }
.smp-run { display: grid; grid-template-columns: auto 1fr auto auto; gap: 8px; align-items: center; margin: 8px 0; }
.smp-btn { background: var(--k); color: var(--c5); border: 2px solid var(--c2); cursor: pointer; padding: 0 8px; }
.smp-btn:hover { border-color: var(--w); color: var(--w); }
.smp-btn.main { background: var(--w); color: var(--k); border-color: var(--w); }
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
function drawWave(cv, mono, e) {
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
}

function cardHtml(m, k, lock) {
  const x = m.slots[k], name = S.SLOT_NAMES[k], max = S.SLOT_MAX[k];
  if (!x) return `<section class="page smp-card" data-slot="${k}"><h4>${name}</h4><div class="body"><p class="empty">…</p></div></section>`;
  const used = x.valid ? x.len : 0, pct = Math.min(100, Math.round(100 * used / max));
  return `<section class="page smp-card" data-slot="${k}">
    <h4>${name}<span class="st${x.valid ? " on" : ""}">${x.valid ? "sample" : "empty"}</span></h4>
    <div class="body">${x.valid ? `<dl><dt>name</dt><dd>${esc(x.name || "(no name)")}</dd><dt>length</dt><dd>${secText(x.seconds)}</dd>
      <dt>rate</dt><dd>${hzText(S.hzOfQ16(x.rate))}${x.nz > 1 ? ` · ${x.nz} zones` : ""}</dd></dl>` : '<p class="empty">No sample here.</p>'}
      <p class="smp-used"><span class="bar${pct > 95 ? " full" : ""}"><i style="width:${pct}%"></i></span><br>${kib(used)} of ${kib(max)}</p>
      <div class="smp-acts">
        <button type="button" class="smp-btn" data-a="play"${!x.valid || lock ? " disabled" : ""}>Play</button>
        <button type="button" class="smp-btn" data-a="backup"${!x.valid || lock ? " disabled" : ""}>Back up</button>
        <button type="button" class="smp-btn" data-a="restore"${lock ? " disabled" : ""}>Restore…</button>
        <button type="button" class="smp-btn" data-a="rename"${!x.valid || lock ? " disabled" : ""}>Rename…</button>
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
    <p class="smp-note">One sample per slot, over all keys (TUNE sets its pitch). USR1 holds 80 KiB, USR2 and USR3 48 KiB each: about
      3.7 s and 2.2 s at 44.1 kHz, twice that at 22 kHz. Writing a slot needs the FM-1 stopped; a cancelled or failed upload leaves the slot empty.</p>
    <div class="smp-run" hidden><span class="what"></span><span class="bar"><i></i></span><span class="cnt"></span>
      <button type="button" class="smp-btn" data-a="cancel">Cancel</button></div>
    <div class="smp-msg" role="status" hidden></div>
    <section class="page smp-edit" hidden><h4>New sample</h4><div class="body">
      <canvas class="smp-wave" aria-label="Waveform: drag to trim"></canvas>
      <div class="smp-trim"><label for="smp-s">Start</label><input type="range" id="smp-s" data-e="start" min="0" step="1"><output data-o="start"></output>
        <label for="smp-e">End</label><input type="range" id="smp-e" data-e="end" min="1" step="1"><output data-o="end"></output></div>
      <div class="smp-row">
        <label>Slot <select data-e="slot">${S.SLOT_NAMES.map((n, k) => `<option value="${k}">${n}</option>`).join("")}</select></label>
        <label>Name <input type="text" data-e="name" maxlength="8" spellcheck="false"></label>
        <label><input type="checkbox" data-e="normalize"> Normalize</label>
        <label>Rate <select data-e="hz"></select></label></div>
      <div class="smp-row"><span class="smp-plan"></span></div>
      <div class="smp-row"><button type="button" class="smp-btn" data-a="preview">Preview</button>
        <button type="button" class="smp-btn main" data-a="upload">Upload</button>
        <button type="button" class="smp-btn" data-a="close">Close</button></div>
    </div></section>
    <div class="grid smp-grid"></div>`;
  const $ = (s) => el.querySelector(s), grid = $(".smp-grid"), run = $(".smp-run"), msg = $(".smp-msg"), ed = $(".smp-edit");
  const wave = $(".smp-wave");
  const fmtAt = (i) => (m.source ? secText(i / m.source.rate) : "");
  const renderEdit = () => {
    ed.hidden = !m.source;
    if (!m.source) return;
    const e = m.edit, p = m.plan(), n = m.source.length;
    for (const [k, v] of [["start", e.start], ["end", e.end]]) {
      const r = ed.querySelector(`[data-e="${k}"]`);
      r.max = String(n);
      r.value = String(v);
      ed.querySelector(`[data-o="${k}"]`).textContent = fmtAt(v);
    }
    ed.querySelector('[data-e="slot"]').value = String(e.slot);
    const nm = ed.querySelector('[data-e="name"]');
    if (document.activeElement !== nm) nm.value = e.name;
    ed.querySelector('[data-e="normalize"]').checked = !!e.normalize;
    const hz = ed.querySelector('[data-e="hz"]');
    hz.innerHTML = m.choices().map((c) => `<option value="${c.hz}"${c.hz === e.hz ? " selected" : ""}>${hzText(c.hz)} · ${c.label} · ${secText(c.seconds)}${c.fits ? "" : " · too long"}</option>`).join("");
    const pl = ed.querySelector(".smp-plan");
    pl.textContent = `${secText(p.seconds)} at ${hzText(p.hz)} (${p.label}) · ${kib(p.bytes)} of ${kib(p.max)} in ${S.SLOT_NAMES[e.slot]}`
      + (p.fits ? "" : " · too long: trim it or pick a lower rate")
      + `${m.source.channels.length > 1 ? " · mixed to mono" : ""} · source ${hzText(m.source.rate)}`;
    pl.classList.toggle("bad", !p.fits);
    const lock = !!m.op || m.s.busy || !m.s.connected;
    ed.querySelector('[data-a="upload"]').disabled = lock || !p.fits || m.playing || !!m.restoreBlocked;
    ed.querySelector('[data-a="preview"]').disabled = !p.fits;
    drawWave(wave, m.source.mono, e);
  };
  const render = () => {
    const lock = !!m.op || m.s.busy || !m.s.connected || !m.available;
    grid.innerHTML = [0, 1, 2].map((k) => cardHtml(m, k, lock)).join("");
    $('[data-a="open"]').disabled = !m.available;
    $('[data-a="reload"]').disabled = lock;
    const text = m.message || (m.restoreBlocked ? `${m.restoreBlocked} Sample writes wait for it.` : m.playing ? "The FM-1 is playing: stop it to upload, restore, rename or clear." : "");
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
  const act = async (a, k) => {
    try {
      if (a === "open") {
        const f = await pickFile("audio/*,.wav,.aif,.aiff,.aifc");
        if (!f) return;
        m.say(`Decoding ${f.name}…`);
        m.setSource(await decodeFile(f));
      } else if (a === "reload") { m.cache.clear(); m.say(""); await m.refresh(); }
      else if (a === "close") { m.source = null; m.edit = null; m.enc = null; render(); }
      else if (a === "preview") { const p = m.preview(); play(p.pcm, p.hz); }
      else if (a === "upload") {
        const k2 = m.edit.slot, x = m.slots[k2];
        if (x?.valid && !(await ctx.confirm(`Replace ${x.name || "the sample"} in ${S.SLOT_NAMES[k2]}?`))) return;
        await m.upload();
      } else if (a === "cancel") m.cancel();
      else if (a === "play") { const p = await m.slotPreview(k); play(p.pcm, p.hz); }
      else if (a === "backup") { const r = await m.backup(k); download(r.bytes, r.name); }
      else if (a === "restore") {
        const f = await pickFile(".fsmp,application/octet-stream");
        if (!f) return;
        const bytes = new Uint8Array(await f.arrayBuffer());
        const info = S.parseBackup(bytes, k).info, x = m.slots[k];
        if (x?.valid && !(await ctx.confirm(`Replace ${x.name || "the sample"} in ${S.SLOT_NAMES[k]} with ${info.name || "the file's sample"}?`))) return;
        await m.restore(k, bytes);
      } else if (a === "rename") {
        const nm = globalThis.prompt?.(`New name for ${S.SLOT_NAMES[k]} (8 characters):`, m.slots[k].name);
        if (nm) await m.rename(k, nm);
      } else if (a === "clear") {
        if (await ctx.confirm(`Clear ${S.SLOT_NAMES[k]} (${m.slots[k].name})? Back it up first if you want to keep it.`)) await m.clear(k);
      }
    } catch (e) {
      if (m.message !== e.message) m.say(e.message, !e.cancelled);
      ctx.toast?.(e.message);
    }
  };
  el.addEventListener("click", (e) => {
    const b = e.target.closest("button[data-a]");
    if (b && !b.disabled) act(b.dataset.a, Number(b.closest("[data-slot]")?.dataset.slot));
  });
  ed.addEventListener("input", (e) => {
    const t = e.target.closest("[data-e]");
    if (!t || !m.source) return;
    const k = t.dataset.e;
    if (k === "start" || k === "end") m.setEdit({ [k]: Number(t.value) });
    else if (k === "slot") m.setEdit({ slot: Number(t.value) });
    else if (k === "hz") m.setEdit({ hz: Number(t.value) });
    else if (k === "normalize") m.setEdit({ normalize: t.checked });
  });
  ed.addEventListener("change", (e) => {
    const t = e.target.closest('[data-e="name"]');
    if (t && m.source) m.setEdit({ name: t.value });
  });
  let drag = null; // the trim handle nearest to where the waveform was pressed
  const atX = (ev) => { const r = wave.getBoundingClientRect(); return Math.round(Math.max(0, Math.min(1, (ev.clientX - r.left) / r.width)) * m.source.length); };
  wave.addEventListener("pointerdown", (ev) => {
    if (!m.source) return;
    const i = atX(ev);
    drag = Math.abs(i - m.edit.start) <= Math.abs(i - m.edit.end) ? "start" : "end";
    wave.setPointerCapture(ev.pointerId);
    m.setEdit({ [drag]: i });
  });
  wave.addEventListener("pointermove", (ev) => { if (drag) m.setEdit({ [drag]: atX(ev) }); });
  wave.addEventListener("pointerup", () => { drag = null; });
  m.on("render", render);
  m.on("progress", progress);
  m.on("loading", (on) => { $(".snd-load").hidden = !on; });
  render();
  return {
    model: m,
    show: () => { if (!m.slots[0] && !m.loading) m.refresh(); },
    destroy: () => m.destroy(),
  };
}

export const tab = { id: "samples", title: "Samples", cap: CAP.LIVE, mount: mountSamples };
