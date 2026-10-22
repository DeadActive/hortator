// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// Piece 2, the resonators: an exciter (a short drum hit) through RESON. The firmware renders the hit twice, dry and
// through its inserts (lab_voice), and both are drawn on one scale: the dry hit dim, the ring bright, in time and as
// spectra, where the string's or the chord's partials stand up as a comb. Play loops a figure; the pad hits it.
import { el, knob, chips, scope, INK, hitPad } from './lab_ui.js';
import { tailLength, spectrum } from './lab_dsp.js';
import * as draw from './lab_draw.js';

const EXCITERS = ['RSTRX', 'CLTRX', 'SDTRX', 'CHTRX', 'TMTRX'];   // MD voices: rim, claves, snare, hat, tom
const NOTES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const note = n => `${NOTES[n % 12]}${Math.floor(n / 12) - 1}`;   // params.c F_NOTEO: 60 = C4
const FIGURE = [0, 3, 6, 10, 12];                    // the loop's hits in a 16-step bar

export default function reson(plate, { lab, ids, audition, screen }) {
  const { fs, P, RS, DM, tp, names } = ids;
  const state = { ex: DM.RSTRX, model: RS.STRNG, tune: 52, decay: 96, mix: 100, strct: 40 };
  let player = null, dry = new Float32Array(0), wet = dry, view = 0, ddb = new Float32Array(1), wdb = ddb, queued = false;

  const sets = () => [                               // the state as the firmware's track parameters
    [P.RMODEL, state.model], [P.RTUNE, state.tune], [P.RDECAY, state.decay], [P.RMIX, state.mix], [P.RSTRCT, state.strct],
  ];
  const apply = t => {
    t.call('model', 0, state.ex);
    for (const [p, v] of sets()) t.call('param', 0, p, v);
  };
  const local = { call: (fn, ...a) => lab[fn](...a) };

  const render = () => {
    queued = false;
    const n = fs * 2;
    lab.reset();
    lab.model(0, state.ex);
    dry = lab.voice(n, true);                        // RESON off: the exciter alone
    apply(local);
    wet = lab.voice(n, true);
    view = Math.min(n, Math.max(fs / 4, Math.max(tailLength(wet), tailLength(dry)) * 1.1));
    ddb = spectrum(dry);
    wdb = spectrum(wet);
    waveScope.redraw();
    specScope.redraw();
    screen();
  };
  const queue = () => { if (!queued) { queued = true; requestAnimationFrame(render); } };

  const waveScope = scope('wave', (g, w, h) => {
    const pk = Math.max(draw.peakDb(dry, view), draw.peakDb(wet, view)), scale = 0.92 / 10 ** (pk / 20);
    draw.wave(g, w, h - 16, wet, view, { scale });   // the ring, then the dry hit over it: where they part, it rings
    draw.wave(g, w, h - 16, dry, view, { color: INK.dry, glow: null, scale });
    const end = tailLength(dry);
    draw.marker(g, w, h - 16, end, view, `dry hit ends, ${Math.round(end / fs * 1000)} ms`);
    draw.timeAxis(g, w, h, view / fs);
  });
  const specScope = scope('spec', (g, w, h) => {
    draw.spectrum(g, w, h, wdb, fs, { norm: true });
    draw.spectrum(g, w, h, ddb, fs, { color: INK.dry, glow: null, fill: false, norm: true });
  });

  const set = (key, p) => v => {
    state[key] = v;
    player?.call('param', 0, p, v);
    queue();
  };
  const strctKnob = () => knob(
    { ...tp.RSTRCT, label: state.model === RS.CHORD ? 'CHORD' : 'STRCT' }, set('strct', P.RSTRCT), {
      value: state.strct,
      show: state.model === RS.CHORD ? v => names.rchord[Math.floor(v * names.rchord.length / 128)] : undefined,
    });
  const knobsRow = el('div', 'knobs');
  let strct = strctKnob();
  knobsRow.append(
    knob(tp.RTUNE, set('tune', P.RTUNE), { value: state.tune, show: note }).root,
    knob(tp.RDECAY, set('decay', P.RDECAY), { value: state.decay }).root,
    knob(tp.RMIX, set('mix', P.RMIX), { value: state.mix }).root,
    strct.root);

  const models = names.rmodel.map((n, v) => ({ label: n, value: v })).filter(m => m.value !== RS.OFF);
  const modelChips = chips(models, state.model, m => {
    state.model = m;
    const next = strctKnob();                        // STRCT reads as the chord on CHORD
    strct.root.replaceWith(next.root);
    strct = next;
    player?.call('param', 0, P.RMODEL, m);
    queue();
  }, 'Resonator');
  const exChips = chips(EXCITERS.map(n => ({ label: n, value: DM[n] })), state.ex, m => {
    state.ex = m;
    if (player) apply(player);
    queue();
  }, 'Exciter');
  const pickers = el('div', 'families inline');
  for (const [name, c] of [['Exciter', exChips], ['Resonator', modelChips]]) {
    const fam = el('div', 'family');
    fam.append(el('span', 'family-name', name), c.root);
    pickers.append(fam);
  }

  const pad = hitPad(audition);

  const legend = el('p', 'legend');
  legend.append(el('span', 'key-dim', 'Dry hit'), el('span', 'key-led', 'Through RESON'));
  const scopes = el('div', 'scopes two');
  scopes.append(waveScope.canvas, specScope.canvas);
  const controls = el('div', 'controls-row'), hand = el('div', 'play-slot');
  hand.append(pad);
  controls.append(knobsRow, hand);
  plate.append(pickers, legend, scopes, controls);
  render();

  return {
    start(p, loop) {
      player = p;
      apply(p);
      for (let s = 0; s < 16; s++) p.call('step', 0, s, FIGURE.includes(s), s === 0);
      if (loop) p.call('play', true);
    },
    stop() { player = null; },
  };
}
