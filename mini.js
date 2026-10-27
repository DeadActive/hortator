// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// A small FM-1 from the layout table (the install diagram, the manual's panel map), scaled to its host. Returns its
// parts by control: a button by its label (HOME, OCT-), a knob by its label (SELECT, KNOB1, MASTER), a key as key:n.
import { BTN, ENC } from './controls.js?v=4da7514222';
import { LAYOUTS } from './layout.js?v=4da7514222';

export function buildMini(host) {
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
  for (const [k, b] of Object.entries(L.ctl))
    if (k.startsWith('key')) part('m-key', b, k);
    else part('m-btn', b, BTN[Number(k.slice(4))].label);
  for (const [id, b] of Object.entries(L.enc)) part('m-enc', b, id === 'master' ? 'MASTER' : ENC[id].label);
  host.append(inner);
  const fitMini = () => { inner.style.transform = `scale(${host.clientWidth / L.W})`; };
  new ResizeObserver(fitMini).observe(host);
  fitMini();
  return parts;
}
