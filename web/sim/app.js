// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The FM-1 simulator page: draws the panel (two layouts: the device's landscape, a stacked portrait for phones),
// turns pointer and keyboard input into messages for the worklet (worklet.js runs the firmware), paints the
// frames it sends back, and keeps the flash it writes in IndexedDB (store.js).
import { BTN, ENC, KEYS, HeldSet, keyEvent, keyHint, contextMenuLatches } from './controls.js';
import { FlashStore, openFlashDb } from './store.js';
import { needsResume, playbackSession } from './audio.js';
import { parseReel, snapshots } from './reel.js';
import { LAYOUTS, DECO_CLASS, blend, heroPose } from './layout.js';

// ---- the panel's elements (built once, placed per layout)
const device = document.getElementById('device');
const fit = document.getElementById('fit');
const note = document.getElementById('note');
const els = { deco: {}, ctl: {}, enc: {}, lbl: {} };
const lcd = document.createElement('canvas');
lcd.width = lcd.height = 240;
lcd.className = 'lcd';
lcd.setAttribute('aria-label', 'screen');
const lcdCtx = lcd.getContext('2d');
lcdCtx.fillStyle = '#000';
lcdCtx.fillRect(0, 0, 240, 240);

function place(el, [x, y, w, h]) {
  el.style.left = `${x}px`; el.style.top = `${y}px`; el.style.width = `${w}px`; el.style.height = `${h}px`;
}

const SLABS = 14;                                 // the body extruded behind the face, 1.6 px a layer
function build() {
  for (let i = SLABS; i >= 1; i--) {
    const s = document.createElement('div'), t = i / SLABS;
    s.className = 'slab';
    s.style.inset = '0';
    s.style.translate = `0 0 ${-i * 1.6}px`;
    s.style.background = `rgb(${Math.round(78 - 40 * t)}, ${Math.round(82 - 41 * t)}, ${Math.round(88 - 43 * t)})`;
    device.append(s);
  }
  for (const name of Object.keys(DECO_CLASS)) {
    const d = document.createElement('div');
    d.className = DECO_CLASS[name];
    device.append(d);
    els.deco[name] = d;
  }
  device.append(lcd);
  BTN.forEach((b, id) => {
    const el = document.createElement('div');
    el.className = 'btn';
    el.dataset.ctl = `btn:${id}`;
    el.setAttribute('role', 'button');
    el.setAttribute('aria-label', b.label);
    el.innerHTML = b.label === 'PLAY' ? '<span class="two">PLAY<br>STOP</span>' : `<span>${b.label}</span>`;
    addHint(el, `btn:${id}`);
    device.append(el);
    els.ctl[`btn:${id}`] = el;
  });
  KEYS.forEach(k => {
    const el = document.createElement('div');
    el.className = `key${k.black ? ' black' : ''}`;
    el.dataset.ctl = `key:${k.n}`;
    el.setAttribute('role', 'button');
    el.setAttribute('aria-label', k.name);
    el.innerHTML = '<span class="stripe"></span>';
    addHint(el, `key:${k.n}`);
    device.append(el);
    els.ctl[`key:${k.n}`] = el;
  });
  [...ENC.map((e, i) => [String(i), e.label]), ['master', 'MASTER']].forEach(([id, label]) => {
    const el = document.createElement('div');
    el.className = `enc${id === 'master' ? ' master' : ''}`;
    el.tabIndex = 0;
    el.setAttribute('role', 'slider');
    el.setAttribute('aria-label', label);
    el.innerHTML = '<span class="skirt"></span><span class="cap"></span>';
    device.append(el);
    els.enc[id] = el;
    const l = document.createElement('div');
    l.className = 'lbl';
    l.textContent = label;
    device.append(l);
    els.lbl[id] = l;
  });
  const glare = document.createElement('div');           // the shine: a fixed stripe moved by transform (no repaint)
  glare.className = 'glare';
  glare.innerHTML = '<span class="glare-band"></span>';
  device.append(glare);
  els.glare = glare.firstChild;
}
function addHint(el, ctl) {
  if (!keyHint[ctl]) return;
  const h = document.createElement('span');
  h.className = 'hint';
  h.textContent = keyHint[ctl];
  el.append(h);
}

// geometry: phones show the FM-1's own landscape form while it flies in, then morph into the portrait simulator
let phone = false, availW = 1000, availH = 600, morphNow = -1;
function fitScale(L) { return Math.max(0.2, Math.min(availW / L.W, availH / L.H, 1.2)); }
function relayout() {
  phone = window.innerWidth < 700 && window.innerHeight > window.innerWidth;
  availW = Math.min(document.documentElement.clientWidth - 32, 1000);
  availH = window.innerHeight - 56 - 118 - 8;       // the hero scene's padding: nav above, Switch on and its bar below
  geometry(phone ? Math.max(0, morphNow) : 0, true);
}
function geometry(morph, force) {                  // place every part for this morph (0 landscape .. 1 portrait)
  if (!force && morph === morphNow) return;
  morphNow = morph;
  const G = blend(LAYOUTS.landscape, phone ? LAYOUTS.portrait : LAYOUTS.landscape, morph);
  for (const [name, d] of Object.entries(G.deco)) {
    place(els.deco[name], d.box);
    els.deco[name].style.opacity = d.opacity;
  }
  place(lcd, G.lcd);
  for (const [ctl, b] of Object.entries(G.ctl)) place(els.ctl[ctl], b);
  for (const [id, b] of Object.entries(G.enc)) {
    place(els.enc[id], b);
    place(els.lbl[id], [b[0] - 30, b[1] - 20, b[2] + 60, 14]);
  }
  const scale = fitScale(G);                        // fits at every step of the morph
  device.style.width = `${G.W}px`;
  device.style.height = `${G.H}px`;
  device.style.setProperty('--scale', scale);
  fit.style.width = `${G.W * scale}px`;
  fit.style.height = `${G.H * scale}px`;
}

// ---- input -> worklet
let node = null;
const held = new HeldSet();
const pending = [];
function send(m) { if (node) node.port.postMessage(m); else pending.push(m); }
function ctlMsg(ctl, down) {
  const [kind, id] = ctl.split(':');
  return { t: kind, id: Number(id), down };
}
function press(ctl, src) {
  if (held.press(ctl, src)) { send(ctlMsg(ctl, true)); els.ctl[ctl]?.classList.add('down'); }
}
function release(ctl, src) {
  if (held.release(ctl, src)) { send(ctlMsg(ctl, false)); els.ctl[ctl]?.classList.remove('down'); }
  if (src === 'latch') els.ctl[ctl]?.classList.remove('latched');
}
function releaseAll() {
  for (const ctl of held.releaseAll()) {
    send(ctlMsg(ctl, false));
    els.ctl[ctl]?.classList.remove('down', 'latched');
  }
  send({ t: 'release' });
}
function toggleLatch(ctl) {
  if (held.holds(ctl, 'latch')) release(ctl, 'latch');
  else { press(ctl, 'latch'); els.ctl[ctl].classList.add('latched'); }
}

function bindControls() {
  let lastPointerType;
  device.addEventListener('contextmenu', e => {          // right-click a button: hold it (combinations)
    const el = e.target.closest('[data-ctl]');
    e.preventDefault();
    if (el && el.classList.contains('btn') && contextMenuLatches(lastPointerType)) toggleLatch(el.dataset.ctl);
  });
  device.addEventListener('pointerdown', e => {
    const el = e.target.closest('[data-ctl]');
    lastPointerType = e.pointerType;
    if (!el || e.button !== 0) return;
    const ctl = el.dataset.ctl, src = `p${e.pointerId}`;
    e.preventDefault();
    el.setPointerCapture(e.pointerId);
    if (held.holds(ctl, 'latch')) { release(ctl, 'latch'); return; }   // a tap lets a held button go
    press(ctl, src);
    if (el.classList.contains('btn') && e.pointerType !== 'mouse') {
      const timer = setTimeout(() => { if (held.holds(ctl, src)) toggleLatch(ctl); }, 600);
      el.addEventListener('pointerup', () => clearTimeout(timer), { once: true });
    }
  });
  const up = e => {
    const el = e.target.closest?.('[data-ctl]');
    if (el) release(el.dataset.ctl, `p${e.pointerId}`);
  };
  device.addEventListener('pointerup', up);
  device.addEventListener('pointercancel', up);
  device.addEventListener('lostpointercapture', up);

  for (const [id, el] of Object.entries(els.enc)) bindEncoder(id, el);

  window.addEventListener('keydown', e => {
    if (!arrived || e.target.closest?.('.help, .confirm')) return;
    if ((e.code === 'ArrowUp' || e.code === 'ArrowDown') && hovered) {
      e.preventDefault();
      turn(hovered, e.code === 'ArrowUp' ? 1 : -1);
      return;
    }
    const k = keyEvent(e);
    if (k) { e.preventDefault(); press(k.ctl, 'kbd'); }
  });
  window.addEventListener('keyup', e => {
    const k = keyEvent(e);
    if (k) { e.preventDefault(); release(k.ctl, 'kbd'); }
  });
  window.addEventListener('blur', releaseAll);
  document.addEventListener('visibilitychange', () => { if (document.hidden) releaseAll(); else wake(); });
  device.addEventListener('pointerdown', wake, true);
  window.addEventListener('keydown', wake, true);
}

// encoders: 12 px of drag = one detent, up = clockwise; MASTER is a pot (0..1023 over 270 degrees)
let hovered = null, masterValue = 800;
const angle = { };
function turn(id, steps) {
  if (id === 'master') {
    masterValue = Math.max(0, Math.min(1023, masterValue + steps * 32));
    showMaster();
    send({ t: 'master', value: masterValue });
    return;
  }
  spin(id, steps);
  send({ t: 'enc', id: Number(id), steps });
}
function spin(id, steps) {                         // the cap only (the reel turns knobs too)
  angle[id] = (angle[id] ?? 0) + steps * 15;
  els.enc[id].style.setProperty('--turn', `${angle[id]}deg`);
}
function showMaster() { els.enc.master.style.setProperty('--turn', `${-135 + masterValue / 1023 * 270}deg`); }
function bindEncoder(id, el) {
  let acc = 0, lastY = 0;
  el.addEventListener('pointerenter', () => { hovered = id; });
  el.addEventListener('pointerleave', () => { if (hovered === id) hovered = null; });
  el.addEventListener('focus', () => { hovered = id; });
  el.addEventListener('pointerdown', e => {
    e.preventDefault();
    el.setPointerCapture(e.pointerId);
    lastY = e.clientY;
    acc = 0;
  });
  el.addEventListener('pointermove', e => {
    if (!el.hasPointerCapture(e.pointerId)) return;
    const scale = parseFloat(getComputedStyle(device).getPropertyValue('--scale')) || 1;
    acc += (lastY - e.clientY) / scale;
    lastY = e.clientY;
    const px = id === 'master' ? 4 : 12, steps = Math.trunc(acc / px);
    if (steps) { acc -= steps * px; turn(id, id === 'master' ? steps / 8 : steps); }
  });
  let wheel = 0;
  el.addEventListener('wheel', e => {
    e.preventDefault();
    wheel -= e.deltaY;
    const steps = Math.trunc(wheel / 40);
    if (steps) { wheel -= steps * 40; turn(id, steps); }
  }, { passive: false });
}

// ---- frames from the worklet
let frame = null;
const img = lcdCtx.createImageData(240, 240);
const px = new Uint32Array(img.data.buffer);
function rgb565(fb, out) {                         // the LCD's RGB565 -> canvas RGBA
  for (let i = 0; i < fb.length; i++) {
    const c = fb[i];
    const r = (((c >> 11) & 31) * 527 + 23) >> 6, g = (((c >> 5) & 63) * 259 + 33) >> 6, b = ((c & 31) * 527 + 23) >> 6;
    out[i] = 0xff000000 | (b << 16) | (g << 8) | r;
  }
}
function paint(now) {
  reelTick(now);
  manualTick(now);
  if (frame) {
    const { fb, leds, keyLeds } = frame;
    frame = null;
    rgb565(fb, px);
    lcdCtx.putImageData(img, 0, 0);
    for (let b = 0; b < 14; b++) els.ctl[`btn:${b}`].classList.toggle('lit', ((leds >>> b) & 1) === 1);
    for (let n = 0; n < 27; n++) els.ctl[`key:${n}`].classList.toggle('lit', ((keyLeds >>> n) & 1) === 1);
  }
  requestAnimationFrame(paint);
}

// ---- flash in IndexedDB (store.js), the audio clock (audio.js)
const store = new FlashStore(openFlashDb, msg => { note.textContent = msg; });
let ctx = null;
function wake() {                                 // a tap or a key brings a stopped context back
  if (!ctx || !needsResume(ctx.state)) return;
  ctx.resume().catch(() => {});
}

// ---- power on
const wasmBytes = fetch('fm1sim.wasm').then(r => {
  if (!r.ok) throw new Error(`fm1sim.wasm: HTTP ${r.status}`);
  return r.arrayBuffer();
});
async function powerOn() {
  const btn = document.getElementById('power-on'), pnote = document.getElementById('power-note');
  btn.disabled = true;
  pnote.textContent = 'Starting…';
  try {
    if (!window.AudioWorkletNode) throw new Error('This browser has no AudioWorklet. Use a current Chrome, Firefox or Safari.');
    playbackSession(navigator);
    ctx = new AudioContext({ sampleRate: 44100, latencyHint: 'interactive' });
    const resumed = ctx.resume();                         // inside the tap: browsers allow sound from here on
    await ctx.audioWorklet.addModule('worklet.js');
    const [wasm, sectors] = await Promise.all([wasmBytes, store.load()]);
    node = new AudioWorkletNode(ctx, 'fm1', { numberOfInputs: 0, outputChannelCount: [2], processorOptions: { wasm, sectors } });
    node.port.onmessage = e => {
      const m = e.data;
      if (m.t === 'frame') frame = m;
      else if (m.t === 'flash') store.put(m.sectors);
      else if (m.t === 'error') note.textContent = `The firmware failed to start: ${m.message}`;
    };
    node.connect(ctx.destination);
    await resumed;
    send({ t: 'master', value: masterValue });
    for (const m of pending.splice(0)) node.port.postMessage(m);
    if (ctx.sampleRate !== 44100) note.textContent = `Audio runs at ${ctx.sampleRate} Hz here, not 44100 Hz: pitch and tempo are off.`;
    ctx.addEventListener('statechange', () => {
      if (needsResume(ctx.state) && !document.hidden) note.textContent = 'Sound stopped. Tap the panel to start it again.';
      else if (ctx.state === 'running' && note.textContent.startsWith('Sound stopped')) note.textContent = '';
    });
    document.getElementById('power').hidden = true;
  } catch (err) {
    btn.disabled = false;
    pnote.textContent = err.message || String(err);
  }
}

// ---- page controls
function bindPage() {
  const toggle = document.getElementById('help-toggle'), help = document.getElementById('help');
  toggle.addEventListener('click', () => {
    const open = help.hidden;
    help.hidden = !open;
    toggle.setAttribute('aria-expanded', String(open));
    device.classList.toggle('show-hints', open);
  });
  const confirm = document.getElementById('reset-confirm');
  document.getElementById('reset').addEventListener('click', () => { confirm.hidden = false; });
  document.getElementById('reset-no').addEventListener('click', () => { confirm.hidden = true; });
  document.getElementById('reset-yes').addEventListener('click', async () => {
    await store.clear();
    location.reload();
  });
  const on = document.getElementById('power-on');
  on.addEventListener('click', powerOn);
  on.disabled = false;
  document.getElementById('power-note').textContent = 'Sound starts with this tap.';
}

// ---- the reel: recorded pages (tests/sim_record.c) on the screen, LEDs and knobs until the firmware is switched on
let reel = null, reelPos = -1, reelT0 = 0, clips = null, snaps = null;
const reelFb = new Uint16Array(240 * 240);
if (window.DecompressionStream)
  fetch('reel.bin.gz')
    .then(r => r.ok ? new Response(r.body.pipeThrough(new DecompressionStream('gzip'))).arrayBuffer() : Promise.reject())
    .then(b => { reel = parseReel(b); reelT0 = performance.now(); return fetch('reel.json'); })
    .then(r => r.json())
    .then(list => { clips = Object.fromEntries(list.map(c => [c.name, c])); snaps = snapshots(reel, list.map(c => c.from)); manualShow(); })
    .catch(() => { /* no reel: the screen stays dark until Switch on */ });
function reelTick(now) {
  if (!reel || node) return;
  const target = Math.floor((now - reelT0) / 1000 * reel.fps) % reel.frames;
  if (target === reelPos) return;
  if (target < reelPos) reelPos = -1;               // looped: frame 0 is a whole screen
  let f;
  while (reelPos < target) {
    f = reel.apply(++reelPos, reelFb);
    for (let k = 0; k < 7; k++) if (f.enc[k]) spin(String(k), f.enc[k]);
  }
  frame = { fb: reelFb, leds: f.leds, keyLeds: f.keyLeds };
}

// ---- the hero: the scroll through #hero moves p 0 .. 1 (layout.js heroPose: far back, tilted and blurred ..
// the simulator)
const hero = document.getElementById('hero'), stage = document.getElementById('stage');
const tilt = document.getElementById('tilt'), scene = document.getElementById('scene');
const still = window.matchMedia('(prefers-reduced-motion: reduce)');
let arrived = false, heroQueued = false, lastFilter = '';
function heroFrame() {
  heroQueued = false;
  const r = hero.getBoundingClientRect(), track = r.height - window.innerHeight;
  const p = still.matches || track <= 0 ? 1 : Math.min(1, Math.max(0, -r.top / track));
  const q = heroPose(p, phone);
  tilt.style.transform = q.transform;
  const filter = q.blur > 0 ? `blur(${q.blur}px)` : 'none';   // no filter at all once sharp
  if (filter !== lastFilter) { scene.style.filter = filter; lastFilter = filter; }
  geometry(phone ? q.morph : 0, false);
  stage.style.setProperty('--dim', q.dim);
  stage.style.setProperty('--copy', q.copy.toFixed(3));
  stage.style.setProperty('--copy-events', q.copy > 0.5 ? 'auto' : 'none');
  stage.style.setProperty('--shadow', q.shadow);
  els.glare.parentNode.style.opacity = q.glare;
  if (q.glare > 0) els.glare.style.transform = `translate3d(${-60 + q.sweep * 220}%, 0, 0) rotate(18deg)`;
  if (q.arrived !== arrived) {
    arrived = q.arrived;
    stage.style.setProperty('--arrived', arrived ? 1 : 0);
    stage.style.setProperty('--arrived-events', arrived ? 'auto' : 'none');
    device.classList.toggle('waiting', !arrived);
    if (!arrived) releaseAll();                     // nothing stays held while it flies away
  }
}
function queueHero() { if (!heroQueued) { heroQueued = true; requestAnimationFrame(heroFrame); } }

// ---- the features: a sticky screen plays the clip of the feature in the middle of the window; a small FM-1 lights
// the controls to press (the same layout table as the simulator)
const manualCanvas = document.getElementById('manual-screen');
const manualCtx = manualCanvas?.getContext('2d');
const manualImg = manualCtx?.createImageData(240, 240);
const manualPx = manualImg && new Uint32Array(manualImg.data.buffer);
const manualFb = new Uint16Array(240 * 240);
const mini = {};
let manualOn = false, manualClip = null, manualPos = -1, manualT0 = 0, manualFeat = null;
function buildLocator() {                          // the features' locator: its parts by label, to light
  Object.assign(mini, buildMini(document.getElementById('locator')));
  BTN.forEach((b, id) => { if (mini[`btn:${id}`]) mini[b.label] = mini[`btn:${id}`]; });
}
function buildMini(host) {                         // a small FM-1 from the layout table, scaled to its host
  const parts = {};
  if (!host) return parts;
  const L = LAYOUTS.landscape, inner = document.createElement('div');
  inner.className = 'mini';
  const part = (cls, b, key) => {
    const d = document.createElement('div');
    d.className = cls;
    d.style.cssText = `left:${b[0]}px;top:${b[1]}px;width:${b[2]}px;height:${b[3]}px`;
    inner.append(d);
    if (key) parts[key] = d;
  };
  for (const [name, b] of Object.entries(L.deco)) part(`m-${name}`, b);
  part('m-lcd', L.lcd);
  for (const [k, b] of Object.entries(L.ctl)) part(k.startsWith('key') ? 'm-key' : 'm-btn', b, k);
  for (const [id, b] of Object.entries(L.enc)) part('m-enc', b, id === 'master' ? 'MASTER' : ENC[id].label);
  host.append(inner);
  const fitMini = () => { inner.style.transform = `scale(${host.clientWidth / L.W})`; };
  new ResizeObserver(fitMini).observe(host);
  fitMini();
  return parts;
}

// ---- install: the firmware this site ships and what is new in it (version.json, written by the build)
const MONTHS = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October',
  'November', 'December'];
function plainNote(n) {                            // the changelog's line for players: no upstream credits or baselines
  let t = n.text.replace(/\s*\((?:upstream|adapted)[^()]*(?:\([^()]*\)[^()]*)*\)/gi, '')
    .replace(/\s*Frozen baseline[^.]*\./gi, '');
  if (t.startsWith(n.title)) t = t.slice(n.title.length).replace(/^[\s:,]+/, '');
  return t.charAt(0).toUpperCase() + t.slice(1);
}
fetch('version.json').then(r => r.ok ? r.json() : Promise.reject()).then(v => {
  const [y, m, d] = v.date.split('-').map(Number);
  const newer = v.since.length ? ` and ${v.since.length} newer change${v.since.length > 1 ? 's' : ''}` : '';
  document.getElementById('fw-version').textContent =
    `Version ${v.version}${newer}, built ${d} ${MONTHS[m - 1]} ${y} from commit ${v.commit}`;
  const ul = document.getElementById('whats-new');
  for (const n of [...v.since, ...v.release.notes]) {
    const li = document.createElement('li'), span = document.createElement('span'), b = document.createElement('b');
    b.textContent = n.title;
    span.append(b, ' ', plainNote(n));                // clamped to three lines inside the item's padding
    li.append(span);
    ul.append(li);
  }
}).catch(() => { document.querySelector('.whats-new')?.remove(); });
function manualShow() {                            // the feature now in the middle of the window
  if (!manualFeat || !clips || !manualCtx) return;
  const c = clips[manualFeat.dataset.clip];
  if (!c) return;
  manualClip = c;
  manualFb.set(snaps.get(c.from).fb);
  manualPos = c.from;
  manualT0 = performance.now();
  rgb565(manualFb, manualPx);
  manualCtx.putImageData(manualImg, 0, 0);
  const press = manualFeat.dataset.press.split(' ');
  for (const el of Object.values(mini)) el.classList.remove('lit');
  for (const l of press) mini[l]?.classList.add('lit');
  document.getElementById('press').textContent = `PRESS ${press[0]}`;
}
function manualTick(now) {
  if (!manualOn || !manualClip) return;
  const len = manualClip.to - manualClip.from;
  const target = manualClip.from + Math.floor((now - manualT0) / 1000 * reel.fps) % len;
  if (target === manualPos) return;
  if (target < manualPos) { manualFb.set(snaps.get(manualClip.from).fb); manualPos = manualClip.from; }   // looped
  while (manualPos < target) reel.apply(++manualPos, manualFb);
  rgb565(manualFb, manualPx);
  manualCtx.putImageData(manualImg, 0, 0);
}

build();
relayout();
buildLocator();
buildMini(document.getElementById('install-mini'));
const manualObserver = new IntersectionObserver(entries => {
  for (const e of entries) if (e.isIntersecting && e.target !== manualFeat) {
    manualFeat?.classList.remove('active');
    manualFeat = e.target;
    manualFeat.classList.add('active');
    manualShow();
  }
}, { rootMargin: '-45% 0px -45% 0px' });
{
  const feats = [...document.querySelectorAll('.feat')];
  manualFeat = feats[0] ?? null;
  manualFeat?.classList.add('active');
  feats.forEach(f => manualObserver.observe(f));
  const manual = document.querySelector('.manual');
  if (manual) new IntersectionObserver(([e]) => { manualOn = e.isIntersecting; }).observe(manual);
}
device.classList.add('waiting');
heroFrame();
window.addEventListener('scroll', queueHero, { passive: true });
window.addEventListener('resize', queueHero);
still.addEventListener?.('change', queueHero);
showMaster();
bindControls();
bindPage();
window.addEventListener('resize', relayout);
document.fonts?.ready.then(relayout);
requestAnimationFrame(paint);
