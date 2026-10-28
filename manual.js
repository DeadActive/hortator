// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The manual's panel map: a small FM-1 (mini.js) beside the list of its controls. Pointing at a control, or at its
// line in the list (hover, tap, focus), lights both and shows the line under the FM-1. The list is the content; the
// drawing is a picture of it (aria-hidden).
import { KEYS } from './controls.js?v=9d7508cdf2';
import { buildMini } from './mini.js?v=9d7508cdf2';

const GROUPS = {                                   // data-ctl names for several parts at once
  white: KEYS.filter(k => !k.black).map(k => `key:${k.n}`),
  black: KEYS.filter(k => k.black).map(k => `key:${k.n}`),
};

export function initManual() {
  const host = document.getElementById('manual-mini'), list = document.getElementById('map-list');
  const caption = document.getElementById('map-caption');
  if (!host || !list || !caption) return;
  const parts = buildMini(host);
  const items = [...list.querySelectorAll('li[data-ctl]')];
  const partsOf = li => li.dataset.ctl.split(' ').flatMap(c => GROUPS[c] || [c]).map(k => parts[k]).filter(Boolean);
  const idle = [...caption.childNodes];
  let shown = null;
  const show = li => {
    if (li === shown) return;
    if (shown) { shown.classList.remove('on'); for (const p of partsOf(shown)) p.classList.remove('on'); }
    shown = li;
    li.classList.add('on');
    for (const p of partsOf(li)) p.classList.add('on');
    caption.replaceChildren(...[...li.childNodes].map(n => n.cloneNode(true)));
  };
  const clear = () => {
    if (!shown) return;
    shown.classList.remove('on');
    for (const p of partsOf(shown)) p.classList.remove('on');
    shown = null;
    caption.replaceChildren(...idle);
  };
  for (const li of items) {
    li.tabIndex = 0;
    for (const ev of ['pointerenter', 'click', 'focus']) li.addEventListener(ev, () => show(li));
    for (const p of partsOf(li)) {
      p.classList.add('hit');
      for (const ev of ['pointerenter', 'click']) p.addEventListener(ev, () => show(li));
    }
  }
  list.addEventListener('focusout', e => { if (!list.contains(e.relatedTarget)) clear(); });
}
