// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The editor page's shell (web/editor.html): Connect, the header bar, the tabs. Nothing is sent before Connect; the
// session (session.mjs) asks only "Hortator" ports, only INFO, until one identifies itself. `?mock` runs the page
// against the mock device (mock_device.mjs) for development; `?mock=auto` also connects at once.
// The tabs, one module each: tab_sound.mjs (live sound), tab_presets.mjs, tab_projects.mjs, tab_patterns.mjs,
// tab_samples.mjs (the user sample slots).
// One editor per browser (instance.mjs): a second tab says "The editor is open in another tab" and offers "Use here".
import { connect, refusal } from "./session.mjs";
import { SingleTab } from "./instance.mjs";
import { mountHeader } from "./header.mjs";
import { esc, h } from "./ui.mjs";
import { tab as sound } from "./tab_sound.mjs";
import { tab as presets } from "./tab_presets.mjs";
import { tab as projects } from "./tab_projects.mjs";
import { tab as patterns } from "./tab_patterns.mjs";
import { tab as samples } from "./tab_samples.mjs";

export const TABS = [sound, presets, projects, patterns, samples];
const TAB_KEY = "hortator-editor-tab";
const $ = (s) => document.querySelector(s);
const params = new URLSearchParams(location.search);
const MOCK = params.has("mock");
let session = null, views = [], header = null, mock = null;
const TAKEN = "The editor is open in another tab. Use it there, or use it here (the other tab disconnects).";
const single = new SingleTab({ name: MOCK ? "hortator-editor-mock" : undefined, onLost: () => {
  if (session) session.close("taken"); // its views torn down by "closed"; then the lock goes to the other tab
  else taken(TAKEN);
} });
function taken(msg) { // another tab has the editor: Connect waits for "Use here"
  $("#taken").hidden = !msg;
  $("#taken p").textContent = msg || "";
  $("#connect").disabled = !!msg;
}
async function useHere() {
  const b = $("#usehere");
  b.disabled = true;
  try { await single.takeOver(); taken(null); say(null); } finally { b.disabled = false; }
}

function say(r) { // a message under Connect, with its links
  const el = $("#msg");
  el.hidden = !r;
  if (!r) return;
  el.innerHTML = `<p>${esc(r.message)}</p>${(r.links || []).map((l) =>
    `<a class="cta ghost" href="${esc(l.href)}"${/^https?:/.test(l.href) ? ' rel="noopener"' : ""}>${esc(l.text)}</a>`).join(" ")}`;
  if (r.search) el.append(" ", h("button", { type: "button", class: "cta ghost", onclick: () => onConnect({ all: true }) },
    "Search other MIDI ports"));
}
let toastT;
function toast(msg) {
  const el = $("#toast");
  el.textContent = msg;
  el.hidden = false;
  clearTimeout(toastT);
  toastT = setTimeout(() => { el.hidden = true; }, 4000);
}

/** a question with a button per option (the clock-out warning: Stop / Continue / Cancel): the option chosen; Escape or
 * closing it: the last option */
function choose(msg, opts) {
  const d = $("#choose");
  if (!d || typeof d.showModal !== "function") return Promise.resolve(window.confirm(`${msg}\n\nOK: ${opts[1]}. Cancel: ${opts[2]}.`) ? opts[1] : opts[2]);
  d.querySelector("p").textContent = msg;
  const acts = d.querySelector(".acts");
  acts.replaceChildren(...opts.map((o, i) => h("button", { type: "button", class: i === opts.length - 1 ? "cta ghost" : "cta",
    onclick: () => d.close(o) }, o)));
  return new Promise((res) => {
    d.addEventListener("close", () => res(opts.includes(d.returnValue) ? d.returnValue : opts[opts.length - 1]), { once: true });
    d.returnValue = "";
    d.showModal();
  });
}

async function access() {
  if (MOCK) {
    if (!mock) {
      const { createMockDevice, loadDeviceData } = await import("./mock_device.mjs");
      mock = createMockDevice(await loadDeviceData(), { latency: 2 });
      await seedMockSample(mock);
      setInterval(() => mock.deliver(mock.advance(100)), 100); // the device's clock runs (WATCH, the update silence)
      mockBar(mock);
    }
    return mock.access;
  }
  if (!navigator.requestMIDIAccess) throw refusal("nomidi");
  try { return await navigator.requestMIDIAccess({ sysex: true }); } catch { throw refusal("denied"); }
}

/** opts.all: the user asked to search every MIDI port (INFO only, until one answers as a Hortator) */
async function onConnect(opts = {}) {
  const btn = $("#connect");
  if (!single.held && !(await single.claim())) { taken(TAKEN); return; }
  btn.disabled = true;
  say({ message: opts.all ? "Asking every MIDI port for a Hortator FM-1…" : "Looking for a Hortator FM-1…" });
  try {
    const r = await connect(await access(), { all: !!opts.all });
    if (!r.ok) { say(r); return; }
    if (!single.held) { r.session.close(); say(null); return; } // another tab took the editor meanwhile
    say(null);
    started(r.session);
  } catch (e) {
    say(e.why ? e : { message: e.message, links: [] });
  } finally { btn.disabled = !single.held; }
}

function started(s) {
  session = s;
  $("#start").hidden = true;
  $("#app").hidden = false;
  header = mountHeader($("#hb"), s, { onError: toast });
  const ctx = { session: s, toast, confirm: (m) => window.confirm(m), choose };
  const nav = $("#tabs"), panels = $("#panels");
  nav.innerHTML = "";
  panels.innerHTML = "";
  views = [];
  for (const t of TABS) {
    if (!(s.info.caps & t.cap)) continue; // a feature this firmware lacks: no tab
    const b = document.createElement("button");
    Object.assign(b, { type: "button", textContent: t.title });
    b.setAttribute("role", "tab");
    b.dataset.tab = t.id;
    b.addEventListener("click", () => show(t.id));
    nav.append(b);
    const p = document.createElement("section");
    p.className = "panel";
    p.id = `p-${t.id}`;
    p.dataset.tab = t.id;
    p.setAttribute("role", "tabpanel");
    panels.append(p);
    views.push({ id: t.id, view: t.mount(p, ctx) || {} });
  }
  if (MOCK) globalThis.hortatorMock = { device: mock, views }; // ?mock only: the views' models for development
  let last = location.hash.slice(1);
  try { last = last || localStorage.getItem(TAB_KEY) || ""; } catch { /* storage off */ }
  show(views.some((v) => v.id === last) ? last : views[0]?.id);
  s.on("busy", (on) => on && toast("FM-1 busy (update?): the editor waits and reconnects by itself."));
  s.on("asleep", (on) => on && toast("The FM-1 stayed busy (an update?): the editor stopped asking. Click Reconnect when it is done."));
  s.on("refused", (r) => stopped(r));
  s.on("closed", (why) => {
    if (why === "taken") { stopped(null); taken("The editor was opened in another tab: this one disconnected. Use here takes it back."); return; }
    if (why !== "refused") stopped({ message: why === "unplugged" ? "The FM-1 was unplugged." : "Disconnected.", links: [] });
  });
}

function show(id) {
  if (!id) return;
  for (const b of document.querySelectorAll("#tabs [data-tab]")) b.setAttribute("aria-selected", b.dataset.tab === id ? "true" : "false");
  for (const p of document.querySelectorAll("#panels .panel")) p.hidden = p.dataset.tab !== id;
  views.find((v) => v.id === id)?.view.show?.();
  try { localStorage.setItem(TAB_KEY, id); } catch { /* storage off */ }
  history.replaceState(null, "", location.pathname + location.search + "#" + id);
}

function stopped(r) {
  views.forEach((v) => v.view.destroy?.());
  header?.destroy();
  views = [];
  session = null;
  $("#app").hidden = true;
  $("#start").hidden = false;
  say(r);
}

/* ?mock: the FM-1's side, by hand -------------------------------------------------------- */
async function seedMockSample(m) { // USR1 holds a short decaying tone, USR2 a kit of four, so the Samples tab has something to show
  const S = await import("./sample.mjs"), T = await import("./sampset.mjs"), hz = 22050;
  const x = Float32Array.from({ length: hz }, (_, i) => Math.exp(-i / 4000) * Math.sin(2 * Math.PI * 110 * i / hz * (1 + 2 * Math.exp(-i / 800))));
  const e = S.encodeSample(S.toPcm16(S.normalize(x)), hz, "MOCK BD");
  m.seedSample(0, e.header, e.data);
  let seed = 7;
  const noise = () => { seed = (seed * 1103515245 + 12345) >>> 0; return seed / 2147483648 - 1; };
  const kit = [["KICK", 0.35, (i) => Math.exp(-i / 3000) * Math.sin(2 * Math.PI * 55 * i / hz * (1 + 3 * Math.exp(-i / 600)))],
    ["SNARE", 0.25, (i) => Math.exp(-i / 1800) * (0.7 * noise() + 0.4 * Math.sin(i / 6))], ["CL HAT", 0.08, (i) => Math.exp(-i / 300) * noise()],
    ["CLAP", 0.3, (i) => Math.exp(-(i % 2200) / 250) * (i < 6600 ? 1 : Math.exp(-(i - 6600) / 1500)) * noise()]];
  const slots = kit.map(([n, sec, f]) => T.makeSlot(S.toPcm16(S.normalize(Float32Array.from({ length: Math.round(sec * hz) }, (_, i) => f(i)))), hz, n));
  const set = T.composeSet(slots, "MOCK KIT", S.SLOT_MAX[1]);
  m.seedSample(1, set.header, set.data);
}
function mockBar(m) {
  const bar = $("#mockbar");
  bar.hidden = false;
  const rnd = (a, b) => a + Math.floor(Math.random() * (b - a + 1));
  const acts = {
    knob: () => { const id = m.P.E0 + rnd(0, 3), d = m.trackDesc(m.tracks[m.sel], id); m.turn(m.sel, id, rnd(d.min, d.max)); },
    bpm: () => m.turnGlobal(m.G.BPM, rnd(90, 160)),
    track: () => m.select((m.sel + 1) % 8),
    play: () => (m.playing ? m.stop() : m.play()),
    update: () => m.updateFrame(),
  };
  bar.addEventListener("click", (e) => { const a = e.target.closest("button[data-a]"); if (a) acts[a.dataset.a](); });
}

$("#connect").addEventListener("click", () => onConnect());
$("#usehere").addEventListener("click", useHere);
if (!MOCK && !navigator.requestMIDIAccess) say(refusal("nomidi"));
if (MOCK) $("#mode").textContent = "Mock device: no FM-1 needed.";
single.claim().then((mine) => { // the editor is this tab's from the start, unless another tab has it
  if (!mine) taken(TAKEN);
  else if (params.get("mock") === "auto") onConnect();
});
addEventListener("pagehide", () => single.release());
