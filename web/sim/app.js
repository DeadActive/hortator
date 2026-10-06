// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The FM-1 simulator page: draws the panel (two layouts: the device's landscape, a stacked portrait for phones),
// turns pointer and keyboard input into messages for the worklet (worklet.js runs the firmware), paints the
// frames it sends back, and keeps the flash it writes in IndexedDB (store.js).
import { BTN, B, ENC, KEYS, HeldSet, keyEvent, keyHint, contextMenuLatches } from './controls.js';
import { FlashStore, openFlashDb } from './store.js';
import { needsResume, playbackSession } from './audio.js';
import { parseReel } from './reel.js';

// ---- layouts: design-size boxes [x, y, w, h] (the device is scaled to fit the window)
const WHITE = KEYS.filter(k => !k.black), BLACK = KEYS.filter(k => k.black);
function keybed(x0, y0, width, wW, wH, bW, bH, gapY) {    // whites in a row, the raised keys over their gaps
  const step = width / WHITE.length, out = {};
  WHITE.forEach((k, i) => { out[`key:${k.n}`] = [x0 + i * step + (step - wW) / 2, y0 + bH + gapY, wW, wH]; });
  BLACK.forEach(k => {
    const left = WHITE.findIndex(w => w.n === k.n - 1);                  // the white key below it
    out[`key:${k.n}`] = [x0 + (left + 1) * step - bW / 2, y0, bW, bH];
  });
  return out;
}
const ROWS = [['FX', 'SEL', 'ENV', 'LFO', 'EDIT', 'GLO'], ['HOME', 'SAVE', 'ARP', 'SEQ', 'PLAY', 'REC']];
function buttonRows(rows, x0, y0, size, step, rowStep) {
  const out = {};
  rows.forEach((r, j) => r.forEach((l, i) => { out[`btn:${B[l]}`] = [x0 + i * step, y0 + j * rowStep, size, size]; }));
  return out;
}
const knob = (cx, cy, d = 46) => [cx - d / 2, cy - d / 2, d, d];
const LAYOUTS = {
  landscape: {
    W: 1000, H: 620,
    deco: [['bezel', 270, 36, 268, 268], ['tray', 52, 262, 196, 64], ['tray', 572, 158, 394, 156],
           ['tray keybed', 40, 336, 920, 262]],
    lcd: [284, 50, 240, 240],
    enc: { master: knob(95, 108), 0: knob(195, 108), 2: knob(95, 214), 1: knob(195, 214),
           3: knob(632, 108), 4: knob(731, 108), 5: knob(830, 108), 6: knob(929, 108) },
    ctl: { ...buttonRows(ROWS, 588, 174, 52, 62, 68),
           [`btn:${B['OCT-']}`]: [66, 275, 76, 38], [`btn:${B['OCT+']}`]: [158, 275, 76, 38],
           ...keybed(60, 350, 880, 47, 122, 44, 100, 14) },
  },
  portrait: {                                       // phones: the screen on top, OCT- / OCT+ end the button rows
    W: 420, H: 772,
    deco: [['bezel', 76, 20, 268, 268], ['tray', 14, 450, 392, 128], ['tray keybed', 10, 590, 400, 172]],
    lcd: [90, 34, 240, 240],
    enc: { master: knob(60, 340), 0: knob(160, 340), 1: knob(260, 340), 2: knob(360, 340),
           3: knob(60, 416), 4: knob(160, 416), 5: knob(260, 416), 6: knob(360, 416) },
    ctl: { ...buttonRows([[...ROWS[0], 'OCT-'], [...ROWS[1], 'OCT+']], 25, 462, 46, 54, 58),
           ...keybed(16, 598, 388, 20, 76, 20, 72, 6) },
  },
};

// ---- the panel's elements (built once, placed per layout)
const device = document.getElementById('device');
const fit = document.getElementById('fit');
const note = document.getElementById('note');
const els = { deco: [], ctl: {}, enc: {}, lbl: {} };
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
  for (let i = 0; i < 4; i++) { const d = document.createElement('div'); device.append(d); els.deco.push(d); }
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
  const glare = document.createElement('div');
  glare.className = 'glare';
  device.append(glare);
}
function addHint(el, ctl) {
  if (!keyHint[ctl]) return;
  const h = document.createElement('span');
  h.className = 'hint';
  h.textContent = keyHint[ctl];
  el.append(h);
}

let layout = null;
function relayout() {
  const name = window.innerWidth < 700 && window.innerHeight > window.innerWidth ? 'portrait' : 'landscape';
  const L = LAYOUTS[name];
  if (layout !== L) {
    layout = L;
    els.deco.forEach((d, i) => { d.hidden = !L.deco[i]; });
    L.deco.forEach(([cls, x, y, w, h], i) => { els.deco[i].className = cls; place(els.deco[i], [x, y, w, h]); });
    place(lcd, L.lcd);
    for (const [ctl, box] of Object.entries(L.ctl)) place(els.ctl[ctl], box);
    for (const [id, box] of Object.entries(L.enc)) {
      place(els.enc[id], box);
      place(els.lbl[id], [box[0] - 30, box[1] - 20, box[2] + 60, 14]);
    }
    device.style.width = `${L.W}px`;
    device.style.height = `${L.H}px`;
  }
  const availW = Math.min(document.documentElement.clientWidth - 32, 1000);
  const availH = window.innerHeight - 56 - 118 - 8;   // the hero scene's padding: nav above, Switch on and its bar below
  const scale = Math.max(0.3, Math.min(availW / L.W, availH / L.H, 1.2));
  device.style.setProperty('--scale', scale);
  fit.style.width = `${L.W * scale}px`;
  fit.style.height = `${L.H * scale}px`;
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
function paint(now) {
  reelTick(now);
  if (frame) {
    const { fb, leds, keyLeds } = frame;
    frame = null;
    for (let i = 0; i < fb.length; i++) {
      const c = fb[i];
      const r = (((c >> 11) & 31) * 527 + 23) >> 6, g = (((c >> 5) & 63) * 259 + 33) >> 6, b = ((c & 31) * 527 + 23) >> 6;
      px[i] = 0xff000000 | (b << 16) | (g << 8) | r;
    }
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
let reel = null, reelPos = -1, reelT0 = 0;
const reelFb = new Uint16Array(240 * 240);
if (window.DecompressionStream)
  fetch('reel.bin.gz')
    .then(r => r.ok ? new Response(r.body.pipeThrough(new DecompressionStream('gzip'))).arrayBuffer() : Promise.reject())
    .then(b => { reel = parseReel(b); reelT0 = performance.now(); })
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

// ---- the hero: the scroll through #hero moves p 0 .. 1: the device far back, tilted and blurred .. the simulator
const hero = document.getElementById('hero'), stage = document.getElementById('stage');
const tilt = document.getElementById('tilt');
const still = window.matchMedia('(prefers-reduced-motion: reduce)');
let arrived = false, heroQueued = false;
const clamp01 = x => Math.min(1, Math.max(0, x));
const ease = x => { x = clamp01(x); return x * x * (3 - 2 * x); };
const mix = (a, b, t) => a + (b - a) * t;
function heroFrame() {
  heroQueued = false;
  const r = hero.getBoundingClientRect(), track = r.height - window.innerHeight;
  const p = still.matches || track <= 0 ? 1 : clamp01(-r.top / track);
  const m = ease(p / 0.9);                          // lands a little before the track ends
  const tall = layout === LAYOUTS.portrait;          // phones: a gentler start, so it stays in the frame
  tilt.style.transform = m >= 1 ? 'none'
    : `translate3d(0, ${mix(tall ? 4 : 10, 0, m)}vh, ${mix(tall ? -700 : -1150, 0, m)}px) rotateX(${mix(tall ? 50 : 57, 0, m)}deg) `
      + `rotateY(${mix(tall ? 0 : 7, 0, m)}deg) rotateZ(${mix(tall ? -9 : -17, 0, m)}deg)`;
  const set = (k, v) => stage.style.setProperty(k, v);
  set('--blur', mix(9, 0, ease(p / 0.75)).toFixed(2));
  set('--dim', mix(0.6, 1, m).toFixed(3));
  const copy = 1 - ease(p / 0.28);
  set('--copy', copy.toFixed(3));
  set('--copy-events', copy > 0.5 ? 'auto' : 'none');
  set('--sweep', ease((p - 0.3) / 0.6).toFixed(3));
  set('--glare', Math.sin(Math.PI * clamp01((p - 0.3) / 0.6)).toFixed(3));
  set('--shadow', mix(0.8, 0.3, m).toFixed(3));
  if ((m >= 1) !== arrived) {
    arrived = m >= 1;
    set('--arrived', arrived ? 1 : 0);
    set('--arrived-events', arrived ? 'auto' : 'none');
    device.classList.toggle('waiting', !arrived);
    if (!arrived) releaseAll();                     // nothing stays held while it flies away
  }
}
function queueHero() { if (!heroQueued) { heroQueued = true; requestAnimationFrame(heroFrame); } }

build();
relayout();
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
