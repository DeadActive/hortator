// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// Piece 8, Bluetooth MIDI (the Bluetooth version, README "Bluetooth MIDI"): an animated diagram, no sound. The
// Bluetooth page's rows in the order they are used (MENU > BLUETOOTH: ON, SCAN, a device picked), the states it shows,
// then an SMC-Mixer in its DAW mode: a fader moved, its message flies to the FM-1, that track's level follows. It
// runs while on screen; with reduced motion it shows the connected state, still.
import { el } from './lab_ui.js?v=4da7514222';

const SCRIPT = [                                     // [the page's row, what it does, the state after, ms]
  ['BLUETOOTH  ON', 'Bluetooth starts (off at every power-on)', 'IDLE', 1800],
  ['SCAN', 'a 10 s scan; the BLE-MIDI devices listed first', 'SCANNING', 2200],
  ['1  SMC-Mixer', 'picked with OCT+: remembered, and connected to', 'CONNECTED', 1800],
  [null, 'faders moved: the track levels follow', 'CONNECTED', 5200],
];
const STATES = ['OFF', 'STARTING', 'IDLE', 'SCANNING', 'CONNECTED'];

export default function ble(plate) {
  plate.classList.add('ble');
  const stage = el('div', 'ble-stage');
  const fm1 = el('div', 'ble-node fm1');
  const levels = el('div', 'ble-levels'), bars = [];
  for (let i = 0; i < 4; i++) {                       // tracks 1 .. 4: their LEVEL, as the faders set it
    const slot = el('span', 'ble-level'), bar = el('i');
    slot.append(bar);
    levels.append(slot);
    bars.push(bar);
  }
  fm1.append(levels, el('span', 'ble-name', 'FM-1'), el('span', 'ble-sub', 'track levels 1 to 4'));
  const air = el('div', 'ble-air');
  const rings = el('div', 'ble-rings');
  for (let i = 0; i < 3; i++) rings.append(el('span'));
  air.append(rings, el('div', 'ble-link'));
  const ctl = el('div', 'ble-node ctl');
  const faders = el('div', 'ble-faders');
  const caps = [];
  for (let i = 0; i < 4; i++) {
    const slot = el('span', 'ble-fader'), cap = el('i');
    slot.append(cap);
    faders.append(slot);
    caps.push(cap);
  }
  ctl.append(faders, el('span', 'ble-name', 'SMC-Mixer'), el('span', 'ble-sub', 'DAW mode: faders 1 to 4'));
  stage.append(fm1, air, ctl);

  const next = el('div', 'ble-next');
  next.append(el('span', 'ble-name', 'MIDI mode'), el('span', 'ble-sub', 'any other controller: its notes play the tracks, CLK BLE follows its clock'));
  const statesEl = el('div', 'ble-states');
  const chipsEl = STATES.map(s => { const c = el('span', 'ble-state', s); statesEl.append(c); return c; });
  const status = el('div', 'ble-status');
  status.append(statesEl);

  const term = el('ol', 'ble-console');
  term.setAttribute('aria-label', 'The Bluetooth page, step by step');
  const lines = SCRIPT.map(([cmd, what]) => {
    const li = el('li');
    if (cmd) li.append(el('code', null, cmd));
    li.append(el('span', null, what));
    term.append(li);
    return li;
  });

  const side = el('div', 'ble-side');
  side.append(status, term);
  const row = el('div', 'ble-row');
  row.append(side, next);
  plate.append(stage, row);

  // ---- the loop: the steps one after the other, then fader moves as packets
  let step = -1, timer = 0, packets = 0, running = false, gen = 0;
  const state = s => chipsEl.forEach(c => c.classList.toggle('on', c.textContent === s));
  const show = i => {
    step = i;
    lines.forEach((li, k) => { li.classList.toggle('done', k < i); li.classList.toggle('now', k === i); });
    plate.dataset.step = String(i);
    state(i === 0 ? 'STARTING' : SCRIPT[i][2]);
    if (i === 0) setTimeout(() => step === 0 && state('IDLE'), 900);
  };
  const packet = () => {                             // a fader moves: its message flies to the FM-1, that level follows
    const f = Math.floor(Math.random() * 4), v = Math.random();
    caps[f].style.transform = `translateY(${Math.round((1 - v) * 30)}px)`;
    const dot = el('span', 'ble-packet'), mine = gen;
    air.append(dot);
    setTimeout(() => {                               // the flight's length (sim.css ble-fly)
      dot.remove();
      if (mine === gen) bars[f].style.height = `${Math.round(10 + v * 90)}%`;
    }, 900);
  };
  const run = () => {
    clearTimeout(timer);
    if (!running) return;
    const i = (step + 1) % SCRIPT.length;
    if (i === 0) {
      gen++;                                         // a new round: the packets still flying are not counted
      for (const d of air.querySelectorAll('.ble-packet')) d.remove();
      state('OFF');
    }
    show(i);
    if (i === SCRIPT.length - 1) {
      packets = setInterval(packet, 160);
      setTimeout(() => clearInterval(packets), SCRIPT[i][3] - 900);
    }
    timer = setTimeout(run, SCRIPT[i][3]);
  };
  const still = matchMedia('(prefers-reduced-motion: reduce)');
  const rest = () => {                               // reduced motion: the connected state, still
    show(SCRIPT.length - 1);
  };
  new IntersectionObserver(([e]) => {
    if (still.matches) return rest();
    running = e.isIntersecting;
    if (running) run();
    else {
      clearTimeout(timer);
      clearInterval(packets);
    }
  }).observe(plate);
  state('OFF');
  return {};                                         // a diagram: no Play
}
