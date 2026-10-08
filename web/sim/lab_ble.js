// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// Piece 8, Bluetooth MIDI (alpha, a separate FELUCCA_BLE build; docs/ble/DEVICE_STEPS.md): an animated diagram, no
// sound. The console's commands, as typed, and what each does: start, scan, the controller found offering BLE-MIDI,
// connect; then fader moves arrive as messages the FM-1 counts (part 1). Playing the tracks from them is part 2,
// drawn as what comes next. It runs while on screen; with reduced motion it shows the connected state, still.
import { el } from './lab_ui.js';

const SCRIPT = [                                     // [command, what it does, the state after, ms]
  ['ble start', 'Bluetooth on, until the next power-off', 'IDLE', 1800],
  ['ble scan', 'a 10 s scan for devices', 'IDLE', 2200],
  ['ble list', 'the controller is found, offering BLE-MIDI', 'IDLE', 1600],
  ['ble connect 1', 'connected and subscribed to its MIDI', 'RECEIVING', 1400],
  [null, 'faders moved: messages arrive and are counted', 'RECEIVING', 5200],
];
const STATES = ['OFF', 'STARTING', 'IDLE', 'RECEIVING'];

export default function ble(plate) {
  plate.classList.add('ble');
  const stage = el('div', 'ble-stage');
  const fm1 = el('div', 'ble-node fm1');
  fm1.append(el('span', 'ble-name', 'FM-1'), el('span', 'ble-sub', 'Hortator, BLE build'));
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
  ctl.append(faders, el('span', 'ble-name', 'BLE-MIDI controller'), el('span', 'ble-sub', 'tested: SMC-Mixer'));
  stage.append(fm1, air, ctl);

  const next = el('div', 'ble-next');
  next.append(el('span', 'ble-name', 'Next'), el('span', 'ble-sub', 'the messages play the tracks'));
  const statesEl = el('div', 'ble-states');
  const chipsEl = STATES.map(s => { const c = el('span', 'ble-state', s); statesEl.append(c); return c; });
  const counters = el('div', 'ble-counters'), msgs = el('span', 'ble-count', 'msgs 0'), errs = el('span', 'ble-count', 'errs 0');
  counters.append(msgs, errs);
  const status = el('div', 'ble-status');
  status.append(statesEl, counters);

  const term = el('ol', 'ble-console');
  term.setAttribute('aria-label', 'The FM-1’s serial console');
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
  let step = -1, timer = 0, count = 0, packets = 0, running = false, gen = 0;
  const state = s => chipsEl.forEach(c => c.classList.toggle('on', c.textContent === s));
  const show = i => {
    step = i;
    lines.forEach((li, k) => { li.classList.toggle('done', k < i); li.classList.toggle('now', k === i); });
    plate.dataset.step = String(i);
    state(i === 0 ? 'STARTING' : SCRIPT[i][2]);
    if (i === 0) setTimeout(() => step === 0 && state('IDLE'), 900);
  };
  const packet = () => {                             // a fader moves: a message flies to the FM-1, counted there
    const f = Math.floor(Math.random() * 4);
    caps[f].style.transform = `translateY(${Math.round(Math.random() * 30)}px)`;
    const dot = el('span', 'ble-packet'), mine = gen;
    air.append(dot);
    setTimeout(() => {                               // the flight's length (sim.css ble-fly)
      dot.remove();
      if (mine === gen) msgs.textContent = `msgs ${++count}`;
    }, 900);
  };
  const run = () => {
    clearTimeout(timer);
    if (!running) return;
    const i = (step + 1) % SCRIPT.length;
    if (i === 0) {
      gen++;                                         // a new round: the packets still flying are not counted
      for (const d of air.querySelectorAll('.ble-packet')) d.remove();
      count = 0;
      msgs.textContent = 'msgs 0';
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
    msgs.textContent = 'msgs counting';
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
