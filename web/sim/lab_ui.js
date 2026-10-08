// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The lab pieces' controls (features.js): knobs and chips labelled like the FM-1's silkscreen, a scope canvas that
// draws at the device's pixel ratio. No sound here: a piece wires them to the analyser and the player.

export function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text != null) e.textContent = text;
  return e;
}

// a knob: drag up / down (or left / right), arrows, Page keys, Home / End. d: the firmware's descriptor
// { label, min, max, def, names?, unit? }; show(v) formats the readout (default: the name or the number)
export function knob(d, onInput, { value = d.def, show } = {}) {
  const root = el('div', 'knob'), dial = el('div', 'dial'), cap = el('span', 'cap'), val = el('span', 'val');
  const name = el('span', 'name', d.label);
  dial.append(cap);
  root.append(dial, name, val);
  dial.tabIndex = 0;
  dial.setAttribute('role', 'slider');
  dial.setAttribute('aria-label', d.label);
  dial.setAttribute('aria-valuemin', d.min);
  dial.setAttribute('aria-valuemax', d.max);
  const fmt = show ?? (v => (d.names ? d.names[v] : `${v}${d.unit ?? ''}`));
  let v = value;
  const draw = () => {
    const f = (v - d.min) / ((d.max - d.min) || 1);
    cap.style.transform = `rotate(${-135 + 270 * f}deg)`;
    dial.style.setProperty('--f', f.toFixed(3));
    val.textContent = fmt(v);
    dial.setAttribute('aria-valuenow', v);
    dial.setAttribute('aria-valuetext', fmt(v));
  };
  const set = (n, fire = true) => {
    n = Math.max(d.min, Math.min(d.max, Math.round(n)));
    if (n === v) return;
    v = n;
    draw();
    if (fire) onInput(v);
  };
  const span = d.max - d.min, coarse = Math.max(1, Math.round(span / 8));
  let drag = null;
  dial.addEventListener('pointerdown', e => {
    dial.setPointerCapture(e.pointerId);
    drag = { x: e.clientX, y: e.clientY, v };
    e.preventDefault();
  });
  dial.addEventListener('pointermove', e => {
    if (!drag) return;                               // 160 px of travel covers the range
    const dist = (drag.y - e.clientY) + (e.clientX - drag.x);
    set(drag.v + dist * span / 160);
  });
  const end = () => { drag = null; };
  dial.addEventListener('pointerup', end);
  dial.addEventListener('pointercancel', end);
  dial.addEventListener('keydown', e => {
    const step = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1, PageUp: coarse, PageDown: -coarse }[e.key];
    if (step) set(v + step);
    else if (e.key === 'Home') set(d.min);
    else if (e.key === 'End') set(d.max);
    else return;
    e.preventDefault();
  });
  draw();
  return { root, get value() { return v; }, set: n => set(n, false) };
}

// a row of chips, one picked (a radio group): items [{ label, value }]
export function chips(items, value, onPick, label) {
  const root = el('div', 'chips');
  root.setAttribute('role', 'radiogroup');
  if (label) root.setAttribute('aria-label', label);
  const buttons = items.map(it => {
    const b = el('button', 'chip', it.label);
    b.type = 'button';
    b.setAttribute('role', 'radio');
    b.addEventListener('click', () => { pick(it.value); onPick(it.value); });
    root.append(b);
    return [it.value, b];
  });
  const pick = v => { for (const [k, b] of buttons) b.setAttribute('aria-checked', String(k === v)); };
  pick(value);
  return { root, pick };
}

// a canvas sized to its box at the device pixel ratio; draw(g, w, h) in CSS pixels, again on resize
export function scope(cls, draw) {
  const c = el('canvas', `scope ${cls ?? ''}`.trim());
  const g = c.getContext('2d');
  let w = 0, h = 0, queued = false;
  const paint = () => {
    queued = false;
    if (!w || !h) return;
    g.setTransform(devicePixelRatio, 0, 0, devicePixelRatio, 0, 0);
    g.clearRect(0, 0, w, h);
    draw(g, w, h);
  };
  new ResizeObserver(([e]) => {
    w = e.contentRect.width;
    h = e.contentRect.height;
    c.width = Math.round(w * devicePixelRatio);
    c.height = Math.round(h * devicePixelRatio);
    paint();
  }).observe(c);
  return { canvas: c, redraw() { if (!queued) { queued = true; requestAnimationFrame(paint); } } };
}

export const INK = { led: '#f2f7ff', glow: 'rgba(215,232,255,.55)', dim: '#5d636b', grid: '#22262b', warm: '#ffb347' };
