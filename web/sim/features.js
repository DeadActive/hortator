// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The features section (docs/superpowers/specs/2026-10-09-websim-features-lab-design.md): eight chapters, each a
// piece on the firmware's own DSP. The analyser (lab.js Lab, silent, in the page) starts when the section nears the
// screen and draws every piece; the player (LabPlayer, an AudioWorklet) starts on the first Play and sounds one piece
// at a time. A piece that leaves the screen, or a hidden tab, stops.
import { Lab, LabPlayer } from './lab.js';
import { el } from './lab_ui.js';
import { B, ENC } from './controls.js';
import { parseReel, snapshots } from './reel.js';

const PIECES = {                                     // data-piece -> its module, loaded once the analyser runs
  voices: () => import('./lab_voices.js'),
  reson: () => import('./lab_reson.js'),
  comp: () => import('./lab_comp.js'),
  lfo: () => import('./lab_lfo.js'),
  seq: () => import('./lab_seq.js'),
  grids: () => import('./lab_grids.js'),
  fx: () => import('./lab_fx.js'),
  ble: () => import('./lab_ble.js'),
};

// each chapter's page on the FM-1's own screen: the panel moves that open it from the power-on screen (HOME's TRACKS
// mode), as the chapter's "On the panel"
const PAGES = {
  voices: [{ sel: 0 }, { tap: 'EDIT' }],                       // SOUND 1/3
  reson: [{ sel: 0 }, ...Array(5).fill({ tap: 'FX' })],       // FILTER 1, 2, FX, SLICER, RESON 1
  comp: [{ tap: 'HOME' }, { enc: 'SELECT' }],                   // the HOME screen, SELECT on: COMP 2/3
  lfo: [{ sel: 0 }, { tap: 'LFO' }],                           // LFO 1
  seq: [{ sel: 1 }, { tap: 'SEQ' }],                           // STEP 1/4
  grids: [{ tap: 'ARP' }],                                      // GRIDS 1/2
  fx: [{ sel: 1 }, { hold: 'FX' }],                            // the PERFORM layer, FX held
  ble: [],                                                      // no page: TRACKS, as it stays
};
const panel = ops => ops.map(o => ('tap' in o ? { tap: B[o.tap] } : 'hold' in o ? { hold: B[o.hold] } :
  'enc' in o ? { enc: ENC.findIndex(e => e.label === o.enc), steps: o.steps ?? 1 } : o));

function paint(canvas, fb) {                          // RGB565 -> the canvas, 1:1
  const g = canvas.getContext('2d'), img = g.createImageData(240, 240), px = img.data;
  for (let i = 0; i < fb.length; i++) {
    const v = fb[i];
    px[4 * i] = (v >> 11) << 3;
    px[4 * i + 1] = ((v >> 5) & 63) << 2;
    px[4 * i + 2] = (v & 31) << 3;
    px[4 * i + 3] = 255;
  }
  g.putImageData(img, 0, 0);
}

// ---- the screens' clips (tests/sim_feature_reel.c): each chapter's page recorded with its knobs turned to their ends,
// looped while on screen; a change in the piece shows the live screen for a while, then the clip comes back
const LIVE_MS = 5000;
const still = matchMedia('(prefers-reduced-motion: reduce)');
let reel = null, clips = null, snaps = null, ticking = false;
const screens = new Map();                            // chapter -> { lcd, clip, fb, pos, t0, live, seen }

function loadClips() {
  if (!window.DecompressionStream || still.matches) return Promise.resolve();
  return Promise.all([
    fetch('features.bin.gz').then(r => (r.ok ? new Response(r.body.pipeThrough(new DecompressionStream('gzip'))).arrayBuffer()
      : Promise.reject(new Error(`features.bin.gz: HTTP ${r.status}`)))),
    fetch('features.json').then(r => (r.ok ? r.json() : Promise.reject(new Error(`features.json: HTTP ${r.status}`)))),
  ]).then(([buf, list]) => {
    reel = parseReel(buf);
    clips = Object.fromEntries(list.map(c => [c.name, c]));
    snaps = snapshots(reel, list.map(c => c.from));
  }).catch(err => console.error(err));                // no clips: the screens show the live state only
}

function restart(s) {                                 // the clip from its first frame
  s.fb.set(snaps.get(s.clip.from).fb);
  s.pos = s.clip.from;
  s.t0 = performance.now();
  paint(s.lcd, s.fb);
}

function tick(now) {
  ticking = false;
  for (const s of screens.values()) {
    if (!s.seen || !s.clip || now < s.live) continue;
    if (s.live) {                                     // the live hold is over: the clip again
      s.live = 0;
      restart(s);
      continue;
    }
    const len = s.clip.to - s.clip.from, target = s.clip.from + Math.floor((now - s.t0) / 1000 * reel.fps) % len;
    if (target === s.pos) continue;
    if (target < s.pos) {                             // looped
      restart(s);
      continue;
    }
    while (s.pos < target) reel.apply(++s.pos, s.fb);
    paint(s.lcd, s.fb);
  }
  if ([...screens.values()].some(s => s.seen && s.clip)) {
    ticking = true;
    requestAnimationFrame(tick);
  }
}

function watch(chapter, lcd, name) {                  // the chapter's clip plays while its screen is in view
  const s = { lcd, clip: clips?.[name] ?? null, fb: new Uint16Array(240 * 240), pos: -1, t0: 0, live: 0, seen: false };
  screens.set(chapter, s);
  if (s.clip) restart(s);
  new IntersectionObserver(([e]) => {
    s.seen = e.isIntersecting;
    if (s.seen && !ticking) {
      ticking = true;
      requestAnimationFrame(tick);
    }
  }).observe(lcd);
  return s;
}

const DIAGRAMS = new Set(['ble']);                   // no firmware to run: mounted without the analyser

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
  const lcd = chapter.querySelector('.device-screen canvas'), page = PAGES[stage.dataset.piece] ?? [];
  const shown = lcd ? watch(chapter, lcd, stage.dataset.piece) : null;
  let ready = false;
  // screen(more): the FM-1's screen for the analyser's state now (a piece calls it at the end of a render): with a
  // clip, only for a change made here (then held LIVE_MS); without one, always
  const screen = (more = []) => {
    if (!lab || !lcd || (shown?.clip && !ready)) return;
    paint(lcd, lab.screen(panel([...page, ...more])));
    if (shown?.clip) shown.live = performance.now() + LIVE_MS;
  };
  let piece = null;
  // audition(): the piece's sound, started if it is not on (a pad, a held key); -> the player or null
  piece = make(stage, { lab, ids, screen, audition: () => play(chapter, piece, button, false) });
  ready = true;
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
    const needsLab = c => !DIAGRAMS.has(c.querySelector('.plate')?.dataset.piece);
    chapters.filter(c => !needsLab(c)).forEach(mount);
    Promise.all([boot(), loadClips()]).then(() => {
      for (const c of chapters.filter(c => !needsLab(c))) {   // a diagram's screen: the page it leaves as it is
        const lcd = c.querySelector('.device-screen canvas');
        lab.reset();
        if (lcd) paint(lcd, lab.screen(panel(PAGES[c.querySelector('.plate').dataset.piece] ?? [])));
      }
      return Promise.all(chapters.filter(needsLab).map(mount));
    }).catch(err => {
      section.classList.add('lab-failed');
      console.error(err);
    });
  }, { rootMargin: '100% 0px' }).observe(section);
  document.addEventListener('visibilitychange', () => { if (document.hidden) stop(); });
}
