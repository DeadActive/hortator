// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// Piece 6, Grids: the X / Y pad over Mutable Instruments' map and the three densities; the firmware's lanes (Kick,
// Snare, Hats: one bar of 32nds, grids_preview) redraw as the point moves. Playing, three tracks follow the channels;
// the cells light as they fire, amber where CHAOS added a hit the map did not have.
import { el, knob, scope, INK } from './lab_ui.js?v=7bf91331b2';
import { ditherPattern } from './dither.js?v=7bf91331b2';

const LANES = ['Kick', 'Snare', 'Hats'];

export default function grids(plate, { lab, ids, screen }) {
  const { fs, G, P, DM, gp } = ids;
  const state = { x: 40, y: 90, chaos: 0, fill: [90, 90, 90] };
  let player = null, lanes = new Uint8Array(96), queued = false, now = -1, played = [[], [], []];

  const globals = t => {
    t.call('global', G.GMODE, 0);
    t.call('global', G.GX, state.x);
    t.call('global', G.GY, state.y);
    t.call('global', G.GCHAOS, state.chaos);
    state.fill.forEach((f, ch) => t.call('global', G.GFILL1 + ch, f));
  };
  const kit = t => [DM.K909, DM.S909, DM.HATC].forEach((m, k) => {
    t.call('model', k, m);
    t.call('param', k, P.SRC, k + 1);                // the track follows Grids' channel k + 1 (SEQ > PATTERN SRC)
  });
  const local = { call: (fn, ...a) => lab[fn](...a) };
  const render = () => {
    queued = false;
    lab.reset();
    globals(local);
    lanes = lab.grids();
    laneScope.redraw();
    padScope.redraw();
    screen();
  };
  const queue = () => { if (!queued) { queued = true; requestAnimationFrame(render); } };
  const changed = () => {
    if (player) globals(player);
    queue();
  };

  // ---- the pad: X / Y (0..127), Grids' 5 x 5 nodes behind
  const padScope = scope('xy', (g, w, h) => {
    g.save();
    g.fillStyle = '#000000';
    g.fillRect(0, 0, w, h);
    g.fillStyle = '#505050';
    for (let i = 0; i < 5; i++)
      for (let j = 0; j < 5; j++) g.fillRect(i / 4 * (w - 12) + 4, j / 4 * (h - 12) + 4, 4, 4);
    const px = state.x / 127 * w, py = (1 - state.y / 127) * h;
    g.strokeStyle = '#282828';
    g.beginPath();
    g.moveTo(px, 0); g.lineTo(px, h);
    g.moveTo(0, py); g.lineTo(w, py);
    g.stroke();
    g.fillStyle = INK.led;
    g.shadowColor = INK.glow;
    g.shadowBlur = 14;
    g.beginPath();
    g.arc(px, py, 8, 0, 2 * Math.PI);
    g.fill();
    g.restore();
  });
  const pad = padScope.canvas;
  pad.tabIndex = 0;
  pad.setAttribute('role', 'slider');
  pad.setAttribute('aria-label', 'Grids map: X and Y (arrow keys)');
  const label = () => pad.setAttribute('aria-valuetext', `X ${state.x}, Y ${state.y}`);
  const moveTo = e => {
    const r = pad.getBoundingClientRect();
    state.x = Math.round(Math.max(0, Math.min(1, (e.clientX - r.left) / r.width)) * 127);
    state.y = Math.round(Math.max(0, Math.min(1, 1 - (e.clientY - r.top) / r.height)) * 127);
    label();
    changed();
  };
  pad.addEventListener('pointerdown', e => { pad.setPointerCapture(e.pointerId); moveTo(e); e.preventDefault(); });
  pad.addEventListener('pointermove', e => { if (pad.hasPointerCapture(e.pointerId)) moveTo(e); });
  pad.addEventListener('keydown', e => {
    const d = { ArrowLeft: [-4, 0], ArrowRight: [4, 0], ArrowUp: [0, 4], ArrowDown: [0, -4] }[e.key];
    if (!d) return;
    e.preventDefault();
    state.x = Math.max(0, Math.min(127, state.x + d[0]));
    state.y = Math.max(0, Math.min(127, state.y + d[1]));
    label();
    changed();
  });
  label();

  // ---- the lanes: 32 steps each; accent tall; playing, what fired lights (amber: added by CHAOS)
  const laneScope = scope('lanes', (g, w, h) => {
    const left = 52, cw = (w - left) / 32, rh = h / 3;
    g.save();
    g.font = '16px "Hortator Terminus", monospace';
    g.textBaseline = 'middle';
    for (let ch = 0; ch < 3; ch++) {
      g.fillStyle = INK.dry;
      g.fillText(LANES[ch], 0, ch * rh + rh / 2);
      for (let i = 0; i < 32; i++) {
        const b = lanes[ch * 32 + i], x = left + i * cw + 1.5, full = rh - 10, y0 = ch * rh + 5;
        const fired = played[ch][i];
        if (i % 8 === 0) {
          g.fillStyle = INK.grid;
          g.fillRect(x - 1.5, y0, 1, full);
        }
        if (b & 1) {
          const hh = b & 2 ? full : full * 0.55;
          g.fillStyle = fired ? INK.led : INK.dry;
          if (fired) { g.shadowColor = INK.glow; g.shadowBlur = 8; }
          g.fillRect(x, y0 + full - hh, cw - 3, hh);
          g.shadowBlur = 0;
        } else if (fired) {
          g.fillStyle = ditherPattern(g, INK.led, 0.5);   // CHAOS: a hit the map does not have, dithered
          g.shadowColor = 'rgba(0,0,0,0)';
          g.shadowBlur = 8;
          g.fillRect(x, y0 + full * 0.45, cw - 3, full * 0.55);
          g.shadowBlur = 0;
        } else {
          g.fillStyle = '#282828';
          g.fillRect(x, y0 + full - 3, cw - 3, 3);
        }
      }
    }
    if (now >= 0) {
      g.fillStyle = '#282828';
      g.fillRect(left + now * cw, 0, cw, h);
    }
    g.restore();
  });

  const knobsRow = el('div', 'knobs');
  knobsRow.append(knob(gp.GCHAOS, v => { state.chaos = v; changed(); }, { value: state.chaos }).root);
  [gp.GFILL1, gp.GFILL2, gp.GFILL3].forEach((d, ch) =>
    knobsRow.append(knob(d, v => { state.fill[ch] = v; changed(); }, { value: state.fill[ch] }).root));
  const stage = el('div', 'grids-stage');
  stage.append(pad, laneScope.canvas);
  const legend = el('p', 'legend');
  legend.append(el('span', 'key-dim', 'The map'), el('span', 'key-led', 'Played'), el('span', 'key-warm', 'Added by CHAOS'));
  const controls = el('div', 'controls-row');
  controls.append(knobsRow, el('div', 'play-slot'));
  plate.append(stage, legend, controls);
  render();

  let lastIdx = -1, since = 0;
  const per32 = fs * 60 / 120 / 8 / 256;             // audio halves per 32nd
  return {
    start(p, loop) {
      player = p;
      kit(p);
      globals(p);
      p.onProbes(list => {
        for (const q of list) {
          if (q.idx[0] !== lastIdx) { lastIdx = q.idx[0]; since = 0; } else since++;
          const at = (lastIdx % 16) * 2 * per32 + since;   // halves into the bar
          const step = Math.floor(at / per32) % 32;
          if (step < now) played = [[], [], []];      // a new bar
          now = step;
          const hit = Math.floor((at + 1) / per32) % 32;   // a hit is set up a block ahead of its sound
          for (let ch = 0; ch < 3; ch++) {
            if (!((q.fired >> ch) & 1)) continue;
            const on = i => lanes[ch * 32 + ((i + 32) % 32)] & 1;
            let at = hit;                             // a step's edge: the map's unplayed neighbour, not CHAOS
            if (!on(at) && on(at - 1) && !played[ch][(at + 31) % 32]) at = (at + 31) % 32;
            else if (!on(at) && on(at + 1) && !played[ch][(at + 1) % 32]) at = (at + 1) % 32;
            played[ch][at] = true;
          }
        }
        laneScope.redraw();
      });
      if (loop) p.call('play', true);
    },
    stop() {
      player = null;
      now = lastIdx = -1;
      played = [[], [], []];
      laneScope.redraw();
    },
  };
}
