// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// Piece 3, the pump compressor: a bar of an MD kick (BDTRX), offbeat open hats (OHTRX) and ACID's 303-style line on
// the 16ths, the hats and the line ducked under the kick (Streams' compressor keyed by track 1). The firmware renders
// the bar twice, bypassed and compressed, after a bar of warm-up; the compressor's gain reduction per audio half (the
// lab's probes) is drawn under it. Playing, a playhead follows the sequencer's step and a meter shows the reduction now.
import { el, knob, chips, scope, INK } from './lab_ui.js';
import * as draw from './lab_draw.js';
import { ditherPattern } from './dither.js';

const DB = gr => gr * 241 / 327680;                  // comp.gr -> dB, as the FM-1's COMP page (ui_draw.c)
const FLOOR = -24;                                   // the trace's bottom, dB
const LINE = [                                       // ACID's line: [NOTE from C4, accent, slide into the next]
  [-24, 1], [-24, 0], [-12, 0], [-24, 0], [-21, 0], [-24, 1], [-17, 0, 1], [-12, 0],
  [-24, 1], [-24, 0], [-14, 0], [-24, 0], [-19, 0], [-24, 1, 1], [-17, 0], [-12, 0],
];

export default function comp(plate, { lab, ids, screen }) {
  const { fs, P, G, DM, gp, names } = ids;
  const state = { on: true, kickMuted: false, ghost: gp.CGHOST.def, thr: 20, rat: 90, rel: 20 };
  const bar = Math.round(fs * 60 / 120 * 4);         // 120 BPM (lab_reset)
  let player = null, comp = new Float32Array(0), dry = comp, gr = [], bypassKey = '', queued = false;
  let now = null;                                    // playing: { pos (steps), db }

  const kit = t => {                                 // the loop, on an instance (both calls go through t.call)
    t.call('model', 0, DM.BDTRX);
    t.call('model', 1, DM.OHTRX);
    t.call('model', 2, DM.ACID);
    LINE.forEach(([nt, acc, slide], s) => {
      t.call('step', 0, s, s % 4 === 0, false);
      t.call('step', 1, s, s % 4 === 2, false);
      t.call('step', 2, s, true, acc);
      t.call('note', 2, s, nt, slide, false);
    });
    t.call('param', 1, P.LEVEL, 80);
    t.call('param', 1, P.DUCK, 1);
    t.call('param', 2, P.DUCK, 1);
  };
  const settings = (t, on = state.on) => {
    t.call('global', G.CSRC, on ? 1 : 0);
    t.call('global', G.CTHR, state.thr);
    t.call('global', G.CRAT, state.rat);
    t.call('global', G.CREL, state.rel);
    t.call('global', G.CGHOST, state.ghost);
    t.call('param', 0, P.MUTE, state.kickMuted ? 1 : 0);
  };
  const local = { call: (fn, ...a) => lab[fn](...a) };

  const take = on => {                               // a bar after a bar of warm-up -> the mix, the probes
    lab.reset();
    kit(local);
    settings(local, on);
    lab.param(0, P.LEVEL, 0);                        // the picture: the ducked tracks alone (the key is taken before
    lab.play(true);                                  // the level, fx.c mix_part); the kick is the dots
    lab.renderMono(bar);
    lab.probeClear();
    const a = lab.renderMono(bar);
    return [a, lab.probes()];
  };
  const render = () => {
    queued = false;
    const key = `${state.kickMuted} ${state.ghost}`;   // the bypassed bar does not depend on the knobs
    if (key !== bypassKey) {
      [dry] = take(false);
      bypassKey = key;
    }
    let probes;
    if (state.on) [comp, probes] = take(true);
    else {
      take(false);                                   // (the state the screen shows)
      comp = dry;
      probes = [];
    }
    gr = probes.map(p => DB(p.gr));
    waveScope.redraw();
    grScope.redraw();
    screen();
  };
  const queue = () => { if (!queued) { queued = true; requestAnimationFrame(render); } };

  const beats = (g, w, h) => {
    g.save();
    g.strokeStyle = INK.grid;
    for (let b = 1; b < 4; b++) {
      const x = Math.round(b / 4 * w) + 0.5;
      g.beginPath();
      g.moveTo(x, 0);
      g.lineTo(x, h);
      g.stroke();
    }
    g.restore();
  };
  const playhead = (g, w, h) => {
    if (!now) return;
    const x = Math.round(now.pos / 16 * w) + 0.5;
    g.save();
    g.strokeStyle = INK.led;
    g.shadowColor = INK.glow;
    g.shadowBlur = 6;
    g.beginPath();
    g.moveTo(x, 0);
    g.lineTo(x, h);
    g.stroke();
    g.restore();
  };
  const kickDots = (g, w) => {                      // heard: filled; a silent key (GHOST): hollow; no key: none
    const keys = state.on && !(state.kickMuted && state.ghost === names.ghost.indexOf('MUTE'));
    const heard = !state.kickMuted && state.ghost !== names.ghost.indexOf('HIDE');
    if (!heard && !keys) return;
    g.save();
    g.strokeStyle = g.fillStyle = INK.led;
    g.lineWidth = 1.5;
    for (let b = 0; b < 4; b++) {
      g.beginPath();
      g.arc(b / 4 * w + 6, 7, 4, 0, 2 * Math.PI);
      if (heard) g.fill(); else g.stroke();
    }
    g.font = '16px "Hortator Terminus", monospace';
    g.fillStyle = INK.dry;
    g.textBaseline = 'middle';
    g.fillText(heard ? 'kick' : 'kick, unheard', 15, 7);
    g.restore();
  };
  const waveScope = scope('wave', (g, w, h) => {
    beats(g, w, h);
    kickDots(g, w);
    let pk = 0;
    for (let i = 0; i < dry.length; i++) pk = Math.max(pk, Math.abs(dry[i]));
    const scale = 0.92 / (pk || 1);
    g.save();
    g.translate(0, 16);                              // under the kick's dots
    draw.wave(g, w, h - 16, dry, bar, { color: INK.dim, glow: null, scale });
    if (state.on) draw.wave(g, w, h - 16, comp, bar, { scale });
    g.restore();
    playhead(g, w, h);
  });
  const grScope = scope('gr', (g, w, h) => {
    const y = d => 2 + Math.min(1, d / FLOOR) * (h - 18);
    beats(g, w, h);
    g.save();
    g.font = '16px "Hortator Terminus", monospace';
    g.fillStyle = INK.dim;
    g.textBaseline = 'bottom';
    g.fillText(`${FLOOR} dB`, 2, h);
    g.textBaseline = 'top';
    g.fillText('0 dB', 2, 4);
    g.strokeStyle = INK.warm;
    g.fillStyle = ditherPattern(g, INK.warm, 0.25);   // the reduction's area, dithered
    g.lineWidth = 1.5;
    g.shadowColor = 'rgba(0,0,0,0)';
    g.shadowBlur = 8;
    g.beginPath();
    g.moveTo(0, y(0));
    gr.forEach((d, i) => g.lineTo((i + 0.5) / gr.length * w, y(d)));
    g.lineTo(w, y(0));
    g.fill();
    g.stroke();
    g.restore();
    playhead(g, w, h);
  });

  const live = t => {                                // a change heard now and drawn when the frame comes
    if (player) settings(player);
    queue();
  };
  const toggle = (name, items, value, fn) => {
    const fam = el('div', 'family'), c = chips(items, value, v => { fn(v); live(); }, name);
    fam.append(el('span', 'family-name', name), c.root);
    return fam;
  };
  const pickers = el('div', 'families inline');
  pickers.append(
    toggle('Compressor', [{ label: 'ON', value: true }, { label: 'BYPASS', value: false }], state.on, v => { state.on = v; }),
    toggle('Kick', [{ label: 'HEARD', value: false }, { label: 'MUTED', value: true }], state.kickMuted, v => { state.kickMuted = v; }),
    toggle('GHOST', names.ghost.map((n, v) => ({ label: n, value: v })), state.ghost, v => { state.ghost = v; }));

  const knobsRow = el('div', 'knobs');
  for (const [key, d] of [['thr', gp.CTHR], ['rat', gp.CRAT], ['rel', gp.CREL]])
    knobsRow.append(knob(d, v => { state[key] = v; live(); }, { value: state[key] }).root);

  const legend = el('p', 'legend');
  const grKey = el('span', 'key-warm', 'Gain reduction');
  legend.append(el('span', 'key-dim', 'Hats and ACID, bypassed'), el('span', 'key-led', 'Compressed'), grKey);
  const readout = () => {                            // playing: the reduction now, in the legend
    grKey.textContent = now ? `Gain reduction, now ${now.db < -0.05 ? '\u2212' : ''}${Math.abs(now.db).toFixed(1)} dB`
      : 'Gain reduction';
  };
  const scopes = el('div', 'scopes');
  scopes.append(waveScope.canvas, grScope.canvas);
  const controls = el('div', 'controls-row');
  controls.append(knobsRow, el('div', 'play-slot'));
  plate.append(pickers, legend, scopes, controls);
  render();

  let lastIdx = -1, since = 0;
  const perStep = fs * 60 / 120 / 4 / 256;           // audio halves per step
  return {
    start(p, loop) {
      player = p;
      kit(p);
      settings(p);
      p.onProbes(list => {
        for (const q of list) {
          if (q.idx[0] !== lastIdx) { lastIdx = q.idx[0]; since = 0; } else since++;
        }
        const last = list[list.length - 1];
        now = { pos: (lastIdx % 16) + Math.min(0.99, since / perStep), db: state.on ? DB(last.gr) : 0 };
        readout();
        waveScope.redraw();
        grScope.redraw();
      });
      if (loop) p.call('play', true);
    },
    stop() {
      player = null;
      now = null;
      lastIdx = -1;
      readout();
      waveScope.redraw();
      grScope.redraw();
    },
  };
}
