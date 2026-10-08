// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The features section (docs/superpowers/specs/2026-10-09-websim-features-lab-design.md): eight chapters, each a
// piece on the firmware's own DSP. The analyser (lab.js Lab, silent, in the page) starts when the section nears the
// screen and draws every piece; the player (LabPlayer, an AudioWorklet) starts on the first Play and sounds one piece
// at a time. A piece that leaves the screen, or a hidden tab, stops.
import { Lab, LabPlayer } from './lab.js';
import { el } from './lab_ui.js';

const PIECES = {                                     // data-piece -> its module, loaded once the analyser runs
  voices: () => import('./lab_voices.js'),
  reson: () => import('./lab_reson.js'),
  comp: () => import('./lab_comp.js'),
};

let lab = null, ids = null, wasm = null, player = null, starting = null, playing = null;

async function boot() {
  const [w, i] = await Promise.all([
    fetch('fm1sim.wasm').then(r => (r.ok ? r.arrayBuffer() : Promise.reject(new Error(`fm1sim.wasm: HTTP ${r.status}`)))),
    fetch('lab_ids.json').then(r => (r.ok ? r.json() : Promise.reject(new Error(`lab_ids.json: HTTP ${r.status}`)))),
  ]);
  wasm = w;
  ids = i;
  lab = await Lab.create(wasm, ids);
}

function playerReady() {                              // the first Play: an AudioContext from inside the click
  starting ??= LabPlayer.start(wasm, ids).then(p => (player = p));
  return starting;
}

function stop() {
  if (!playing) return;
  const { piece, button } = playing;
  playing = null;
  player?.off();
  piece.stop?.();
  button.setAttribute('aria-pressed', 'false');
  button.textContent = 'Play';
}

// the piece's sound on: the player started (the first time, inside this click), reset, the piece's own setup; another
// piece playing stops first; loop false: set up, nothing started (a pad, a held key). Resolves to the player, or null when the piece was stopped meanwhile.
async function play(chapter, piece, button, loop = true) {
  if (playing?.piece === piece) return playing.ready;
  stop();
  button.setAttribute('aria-pressed', 'true');
  button.textContent = 'Stop';
  const me = { chapter, piece, button };
  playing = me;
  me.ready = (async () => {
    const p = await playerReady();
    if (playing !== me) return null;               // stopped while the player started
    p.call('reset');
    piece.start(p, loop);
    await p.on();
    return p;
  })().catch(err => {
    if (playing === me) stop();
    button.textContent = 'No sound in this browser';
    button.disabled = true;
    console.error(err);
    return null;
  });
  return me.ready;
}

async function mount(chapter) {
  const stage = chapter.querySelector('.plate');
  const load = PIECES[stage?.dataset.piece];
  if (!load) return;
  const { default: make } = await load();
  const button = el('button', 'lab-play', 'Play');
  let piece = null;
  // audition(): the piece's sound, started if it is not on (a pad, a held key); -> the player or null
  piece = make(stage, { lab, ids, audition: () => play(chapter, piece, button, false) });
  if (!piece.start) return;                           // a diagram: nothing to hear
  button.type = 'button';
  button.setAttribute('aria-pressed', 'false');
  button.addEventListener('click', () => (playing?.piece === piece ? stop() : play(chapter, piece, button)));
  const slot = stage.querySelector('.play-slot');   // the piece's place for it, else the plate's top
  if (slot) slot.prepend(button);
  else stage.prepend(button);
  new IntersectionObserver(([e]) => { if (!e.isIntersecting && playing?.chapter === chapter) stop(); })
    .observe(chapter);
}

export function initFeatures() {
  const section = document.getElementById('features');
  if (!section) return;
  const chapters = [...section.querySelectorAll('.chapter')];
  new IntersectionObserver(([e], obs) => {
    if (!e.isIntersecting) return;
    obs.disconnect();
    boot().then(() => Promise.all(chapters.map(mount))).catch(err => {
      section.classList.add('lab-failed');
      console.error(err);
    });
  }, { rootMargin: '100% 0px' }).observe(section);
  document.addEventListener('visibilitychange', () => { if (document.hidden) stop(); });
}
