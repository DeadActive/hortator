// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// Piece 4, two LFOs on a track: closed hats on the 16ths, LFO 1 on their TONE, LFO 2 on their DECAY. The firmware
// renders two bars; the lab's probes give both LFOs' outputs per audio half, drawn over the hats' waveform (whose
// tails stretch and shrink with the DECAY), and the knob each hit gets, as lfo.c computes it, per step below.
import { el, knob, scope, INK } from './lab_ui.js';
import * as draw from './lab_draw.js';

const DEST = { TONE: 3, DECAY: 2 };                  // lfo.c: DEST 1 .. 8 = the model's knobs E0 .. E7
const STEPS = 32;                                    // two bars of 16ths

export default function lfo(plate, { lab, ids, screen }) {
  const { fs, P, LF, DM, tp, names, models } = ids;
  const edit = models[DM.HATC].edit;
  const base = { TONE: edit[2].def, DECAY: edit[1].def };
  const lfos = [
    { dest: 'TONE', wave: names.lwave.indexOf('SINE'), rate: 23, morph: 0, depth: 30 },      // 1 bar
    { dest: 'DECAY', wave: names.lwave.indexOf('S&H'), rate: 46, morph: 0, depth: 44 },      // 1/4
  ];
  const span = fs * 4;                               // two bars at 120 BPM
  let player = null, mix = new Float32Array(0), outs = [[], []], hits = [[], []], queued = false, now = null;

  const kit = t => {
    t.call('model', 0, DM.HATC);
    for (let s = 0; s < 16; s++) t.call('step', 0, s, true, s % 4 === 0);
  };
  const settings = t => lfos.forEach((l, k) => {
    const b = k ? P.LFO2 : P.LFO1;
    t.call('param', 0, b + LF.DEST, DEST[l.dest]);
    t.call('param', 0, b + LF.WAVE, l.wave);
    t.call('param', 0, b + LF.RATE, l.rate);
    t.call('param', 0, b + LF.MORPH, l.morph);
    t.call('param', 0, b + LF.DEPTH, l.depth);
  });
  const local = { call: (fn, ...a) => lab[fn](...a) };

  const knobValue = (k, out) => {                    // lfo.c lfo_live: the knob plus the scaled output, clamped
    const d = edit[k === 0 ? 2 : 1], l = lfos[k];
    const v = base[l.dest] + Math.trunc(Math.trunc(out * l.depth / 64) * (d.max - d.min) / 32767);
    return Math.max(d.min, Math.min(d.max, v));
  };
  const render = () => {
    queued = false;
    lab.reset();
    kit(local);
    settings(local);
    lab.play(true);
    lab.probeClear();
    mix = lab.renderMono(span);
    const pr = lab.probes();
    outs = [pr.map(p => p.lfo[0][0] / 32767), pr.map(p => p.lfo[0][1] / 32767)];
    hits = [[], []];                                 // the LFOs where each step starts
    let last = -1, step = -1;
    for (const p of pr) {
      if (p.idx[0] !== last) {
        last = p.idx[0];
        if (++step >= STEPS) break;
        for (const k of [0, 1]) hits[k][step] = knobValue(k, p.lfo[0][k]);
      }
    }
    waveScope.redraw();
    stepScope.redraw();
    screen();
  };
  const queue = () => { if (!queued) { queued = true; requestAnimationFrame(render); } };

  const playhead = (g, w, h) => {
    if (!now) return;
    const x = Math.round(now.pos / STEPS * w) + 0.5;
    g.save();
    g.strokeStyle = INK.led;
    g.globalAlpha = 0.8;
    g.beginPath();
    g.moveTo(x, 0);
    g.lineTo(x, h);
    g.stroke();
    g.restore();
  };
  const trace = (g, w, h, v, color, glow) => {
    g.save();
    g.strokeStyle = color;
    g.lineWidth = 2;
    g.shadowColor = glow;
    g.shadowBlur = 8;
    g.beginPath();
    v.forEach((y, i) => g[i ? 'lineTo' : 'moveTo']((i + 0.5) / v.length * w, h / 2 - y * h * 0.44));
    g.stroke();
    g.restore();
  };
  const waveScope = scope('wave', (g, w, h) => {
    g.save();
    g.strokeStyle = INK.grid;
    for (let b = 1; b < 8; b++) {                    // the beats; the bar line brighter
      const x = Math.round(b / 8 * w) + 0.5;
      g.globalAlpha = b === 4 ? 1 : 0.5;
      g.beginPath();
      g.moveTo(x, 0);
      g.lineTo(x, h);
      g.stroke();
    }
    g.restore();
    let pk = 0;
    for (let i = 0; i < mix.length; i++) pk = Math.max(pk, Math.abs(mix[i]));
    draw.wave(g, w, h, mix, span, { color: '#505050', glow: null, scale: 0.9 / (pk || 1) });
    trace(g, w, h, outs[1], INK.warm, 'rgba(0,0,0,0)');
    trace(g, w, h, outs[0], INK.led, INK.glow);
    playhead(g, w, h);
  });
  const stepScope = scope('per-step', (g, w, h) => {   // per step: the knob the hit got, TONE above, DECAY below
    const row = (h - 6) / 2, cw = w / STEPS;
    g.save();
    g.font = '16px "Hortator Terminus", monospace';
    for (const k of [0, 1]) {
      const top = k * (row + 6), d = edit[k === 0 ? 2 : 1];
      g.fillStyle = INK.grid;
      g.fillRect(0, top, w, row);
      g.fillStyle = k ? INK.warm : INK.led;
      g.shadowColor = k ? 'rgba(0,0,0,0)' : INK.glow;
      g.shadowBlur = 6;
      hits[k].forEach((v, s) => {
        const f = (v - d.min) / (d.max - d.min), bh = Math.max(2, f * (row - 4));
        g.fillRect(s * cw + 2, top + row - 2 - bh, Math.max(2, cw - 4), bh);
      });
      g.shadowBlur = 0;
      g.fillStyle = INK.dry;
      g.textBaseline = 'top';
      g.fillText(k ? 'DECAY' : 'TONE', 4, top + 3);
    }
    g.restore();
    playhead(g, w, h);
  });

  const live = () => {
    if (player) settings(player);
    queue();
  };
  const groups = el('div', 'lfo-groups');
  lfos.forEach((l, k) => {
    const group = el('div', 'lfo-group'), row = el('div', 'knobs');
    const head = el('p', `lfo-head ${k ? 'key-warm' : 'key-led'}`, `LFO ${k + 1} on the hats' ${l.dest === 'TONE' ? 'tone' : 'decay'}`);
    const pct = v => `${v > 0 ? '+' : v < 0 ? '−' : ''}${Math.round(Math.abs(v) * 100 / 64)}%`;   // F_BIPCT
    row.append(
      knob(tp.LFO_WAVE, v => { l.wave = v; live(); }, { value: l.wave }).root,
      knob(tp.LFO_RATE, v => { l.rate = v; live(); }, { value: l.rate, show: v => names.lsync[Math.floor(v * 17 / 128)] }).root,
      knob(tp.LFO_MORPH, v => { l.morph = v; live(); }, { value: l.morph }).root,
      knob(tp.LFO_DEPTH, v => { l.depth = v; live(); }, { value: l.depth, show: pct }).root);
    group.append(head, row);
    groups.append(group);
  });
  const scopes = el('div', 'scopes');
  scopes.append(waveScope.canvas, stepScope.canvas);
  const controls = el('div', 'controls-row');
  controls.append(groups, el('div', 'play-slot'));
  plate.append(scopes, controls);
  render();

  let lastIdx = -1, bar = 0, since = 0;
  const perStep = fs * 60 / 120 / 4 / 256;
  return {
    start(p, loop) {
      player = p;
      kit(p);
      settings(p);
      p.onProbes(list => {
        for (const q of list) {
          if (q.idx[0] !== lastIdx) {
            if (q.idx[0] < lastIdx) bar ^= 1;
            lastIdx = q.idx[0];
            since = 0;
          } else since++;
        }
        now = { pos: bar * 16 + (lastIdx % 16) + Math.min(0.99, since / perStep) };
        waveScope.redraw();
        stepScope.redraw();
      });
      if (loop) p.call('play', true);
    },
    stop() {
      player = null;
      now = null;
      lastIdx = -1;
      bar = 0;
      waveScope.redraw();
      stepScope.redraw();
    },
  };
}
