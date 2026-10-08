// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// Piece 1, the drum voices: a model, its four HOME knobs; one hit rendered by the firmware (lab_voice) drawn as its
// waveform (the window fits the hit's length) and its spectrum. Play loops the hit on the quarters; the pad hits it.
import { el, knob, chips, scope, hitPad } from './lab_ui.js';
import { tailLength, spectrum } from './lab_dsp.js';
import * as draw from './lab_draw.js';

const FAMILIES = [                                    // the chips, grouped as a drummer looks for them
  ['Kicks', ['K808', 'K909', 'KBOOM', 'KPUNC']],
  ['Snares and claps', ['S808', 'S909', 'SSNAP', 'SCRAK', 'C808', 'C909']],
  ['Hats and cymbals', ['HATC', 'HATO', 'HMETL', 'HNOIS', 'CYMB']],
  ['Percussion', ['TOM', 'CONGA', 'MEMB', 'RIM', 'CLAVE', 'COWB']],
  ['FM and samples', ['FM', 'SMPL']],
];

export default function voices(plate, { lab, ids, audition }) {
  const fs = ids.fs, models = ids.models, byName = Object.fromEntries(models.map(m => [m.name, m]));
  const P = ids.P;
  const state = { model: ids.DM.K909, e: models[ids.DM.K909].edit.slice(0, 4).map(d => d.def) };
  let player = null, buf = new Float32Array(0), view = 0, db = new Float32Array(1), queued = false;

  const apply = t => {                                // the state onto an instance: the analyser or the player
    t.call('model', 0, state.model);
    state.e.forEach((v, k) => t.call('param', 0, P.E0 + k, v));
  };
  const local = { call: (fn, ...a) => lab[fn](...a) };

  const render = () => {                              // one hit from silence, coalesced to a frame
    queued = false;
    lab.reset();
    apply(local);
    buf = lab.voice(Math.round(fs * 1.5), false);
    view = Math.min(buf.length, Math.max(fs / 8, tailLength(buf) * 1.1));
    db = spectrum(buf);
    waveScope.redraw();
    specScope.redraw();
  };
  const queue = () => { if (!queued) { queued = true; requestAnimationFrame(render); } };

  const waveScope = scope('wave', (g, w, h) => {
    const pk = draw.peakDb(buf, view);                // drawn to its peak; the level in numbers
    draw.wave(g, w, h - 16, buf, view, { scale: 0.92 / 10 ** (pk / 20) });
    draw.timeAxis(g, w, h, view / fs, `peak ${pk < -0.05 ? '\u2212' : ''}${Math.abs(pk).toFixed(1)} dB`);
  });
  const specScope = scope('spec', (g, w, h) => draw.spectrum(g, w, h, db, fs));

  const groups = el('div', 'families');
  const pickers = FAMILIES.map(([name, list]) => {
    const fam = el('div', 'family');
    const items = list.filter(n => byName[n]).map(n => ({ label: n, value: byName[n].id }));
    const c = chips(items, state.model, setModel, name);
    fam.append(el('span', 'family-name', name), c.root);
    groups.append(fam);
    return c;
  });
  const knobsRow = el('div', 'knobs');
  let knobs = [];
  function buildKnobs() {
    knobsRow.replaceChildren();
    knobs = models[state.model].edit.slice(0, 4).map((d, k) => {
      const kn = knob(d, v => {
        state.e[k] = v;
        player?.call('param', 0, P.E0 + k, v);
        queue();
      }, { value: state.e[k] });
      knobsRow.append(kn.root);
      return kn;
    });
  }
  function setModel(m) {
    state.model = m;
    state.e = models[m].edit.slice(0, 4).map(d => d.def);
    for (const c of pickers) c.pick(m);
    buildKnobs();
    if (player) apply(player);
    queue();
  }

  const pad = hitPad(audition);

  const scopes = el('div', 'scopes two');
  scopes.append(waveScope.canvas, specScope.canvas);
  const controls = el('div', 'controls-row'), hand = el('div', 'play-slot');
  hand.append(pad);
  controls.append(knobsRow, hand);
  plate.append(groups, scopes, controls);
  buildKnobs();
  render();

  return {
    start(p, loop) {                                // Play: the hit on every quarter of a 16-step bar
      player = p;
      apply(p);
      for (let s = 0; s < 16; s++) p.call('step', 0, s, s % 4 === 0, s === 0);
      if (loop) p.call('play', true);
    },
    stop() { player = null; },
  };
}
