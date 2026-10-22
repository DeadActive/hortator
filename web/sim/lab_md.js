// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// Piece 2, the MD bank: the 21 engines measured from the Machinedrum's TRX and EFM machines, one tile each in the
// MODEL list's order (ids.list), with the machine it was measured from, what it plays, and one hit at its default
// sound drawn by the firmware (lab_voice). A tile picks the engine (the FM-1's screen shows its SOUND page) and hits
// it; Play loops the picked one on the quarters.
import { el, scope } from './lab_ui.js';
import { tailLength } from './lab_dsp.js';
import * as draw from './lab_draw.js';

// each MD engine: the Machinedrum machine it was measured from, what it plays, and a note where the model is partial
// (md-re/docs/TRX.md, EFM.md; CHANGELOG.md 0.19.0, 0.20.0)
export const MACHINES = {
  BDTRX: ['TRX-BD', 'kick'], SDTRX: ['TRX-SD', 'snare'], TMTRX: ['TRX-XT', 'tom'], CGTRX: ['TRX-XC', 'conga'],
  CPTRX: ['TRX-CP', 'clap'], RSTRX: ['TRX-RS', 'rim shot'], CBTRX: ['TRX-CB', 'cowbell'],
  CHTRX: ['TRX-CH', 'closed hat'], OHTRX: ['TRX-OH', 'open hat'],
  CYTRX: ['TRX-CY', 'cymbal', 'partial: its filters are not settled yet'],
  SHTRX: ['TRX-MA', 'maracas'], CLTRX: ['TRX-CL', 'claves'], B2TRX: ['TRX-B2', 'second kick'],
  BDEFM: ['EFM-BD', 'FM kick'], SDEFM: ['EFM-SD', 'FM snare'],
  TMEFM: ['EFM-XT', 'FM tom', 'partial: its FM is a stand-in (with MOD at 0 it follows the machine)'],
  CPEFM: ['EFM-CP', 'FM clap'], RSEFM: ['EFM-RS', 'FM rim shot'], CBEFM: ['EFM-CB', 'FM cowbell'],
  HHEFM: ['EFM-HH', 'FM hat'], CYEFM: ['EFM-CY', 'FM cymbal'],
};

export default function md(plate, { lab, ids, audition, screen }) {
  const { fs, models, banks } = ids;
  const list = ids.list.map(id => models[id]).filter(m => banks[m.bank] === 'MACHINEDRUM');
  const state = { model: list[0].id };
  let player = null;

  const tiles = new Map();                           // model id -> { button, draw }
  const families = el('div', 'md-families');
  for (const fam of ['TRX', 'EFM']) {
    const group = list.filter(m => m.name.endsWith(fam));
    const grid = el('div', 'md-grid');
    grid.setAttribute('role', 'radiogroup');
    grid.setAttribute('aria-label', `MD ${fam}`);
    for (const m of group) {
      const [machine, what, partial] = MACHINES[m.name] ?? ['', ''];
      const b = el('button', 'md-tile');
      b.type = 'button';
      b.setAttribute('role', 'radio');
      const len = el('span', 'md-len', '');
      let hit = new Float32Array(0), view = 1;
      const thumb = scope('md-thumb', (g, w, h) => {
        if (!hit.length) return;
        const pk = draw.peakDb(hit, view);
        draw.wave(g, w, h, hit, view, { scale: 0.9 / 10 ** (pk / 20), glow: null });
      });
      const head = el('span', 'md-head');
      head.append(el('span', 'md-name', m.name));
      if (partial) head.append(el('span', 'md-tag', 'PARTIAL'));
      b.append(head, el('span', 'md-what', `${machine}, ${what}`), thumb.canvas, len);
      if (partial) b.title = `${m.name}: ${partial}`;
      b.addEventListener('click', () => pick(m.id));
      grid.append(b);
      tiles.set(m.id, {
        button: b,
        render() {                                   // one hit at the default sound, from silence
          lab.reset();
          lab.model(0, m.id);
          hit = lab.voice(Math.round(fs * 1.5), false);
          const tail = tailLength(hit), all = tail >= hit.length * 0.98;   // still ringing at the window's end
          view = Math.min(hit.length, Math.max(fs / 10, tail * 1.1));
          len.textContent = all ? `over ${Math.round(hit.length / fs * 1000)} ms` : `${Math.round(tail / fs * 1000)} ms`;
          thumb.redraw();
        },
      });
    }
    const name = el('p', 'family-name', `MD ${fam} (${group.length})`);
    families.append(name, grid);
  }

  const mark = () => { for (const [id, t] of tiles) t.button.setAttribute('aria-checked', String(id === state.model)); };
  const showScreen = () => {                         // the screen: the picked engine's SOUND page
    lab.reset();
    lab.model(0, state.model);
    screen();
  };
  async function pick(id) {
    state.model = id;
    mark();
    showScreen();
    player?.call('model', 0, id);
    (await audition())?.call('hit', 0, 127);
  }

  const queue = [...tiles.values()];                 // the thumbnails, one a frame: the page stays responsive
  const next = () => {
    const t = queue.shift();
    if (!t) return;
    t.render();
    requestAnimationFrame(next);
  };

  const legend = el('p', 'legend');
  legend.append(el('span', 'key-led', 'One hit at each engine’s default sound, drawn by the firmware, and its length to −60 dB'));
  const controls = el('div', 'controls-row');
  controls.append(el('div', 'md-hint', 'Pick an engine to hear it.'), el('div', 'play-slot'));
  plate.append(legend, families, controls);
  mark();
  showScreen();
  requestAnimationFrame(next);

  return {
    start(p, loop) {                                 // Play: the picked engine on every quarter of a 16-step bar
      player = p;
      p.call('model', 0, state.model);
      for (let s = 0; s < 16; s++) p.call('step', 0, s, s % 4 === 0, s === 0);
      if (loop) p.call('play', true);
    },
    stop() { player = null; },
  };
}
