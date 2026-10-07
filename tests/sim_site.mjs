// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The site around the simulator: the landing page (web/sim/index.html) and the restyled English installer
// (web/index_pkg.html). Run from the repo root: node tests/sim_site.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';

// ---- the installer: restyled and rebranded, its install logic untouched
const ORIG_REV = '12a1ba0';                         // the installer's last change on main (upstream 1.0 installer)
const NOW = readFileSync('web/index_pkg.html', 'utf8');
let ORIG = null;
try { ORIG = execFileSync('git', ['show', `${ORIG_REV}:web/index_pkg.html`], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }); }
catch { /* a source archive without git history: the guard is skipped */ }

const script = html => html.match(/<script type="module">([\s\S]*?)<\/script>/)[1];
const TEXT_RE = /const TEXT = (\{[\s\S]*?\n\});\n/;
const masked = s => s.replace(TEXT_RE, 'const TEXT = /*TEXT*/;\n');
const table = html => Function(`return ${script(html).match(TEXT_RE)[1]}`)();
// the only script changes allowed (spec): English only, and the status label's name
const CHANGES = [
  ['let lang = (navigator.language || "en").toLowerCase().startsWith("ja") ? "ja" : "en";', 'const lang = "en";'],
  ['  $("lang").textContent = t("other");\n', ''],
  ['$("lang").addEventListener("click", () => { lang = lang === "ja" ? "en" : "ja"; applyLang(); });\n', ''],
  ['say(`Felucca ${meta.version}`);', 'say(`Hortator ${meta.version}`);'],
];

test('installer: the script is main\'s, changed only in its text table, the language lines and the label', { skip: !ORIG && 'no git history' }, () => {
  let want = masked(script(ORIG));
  for (const [from, to] of CHANGES) {
    assert.ok(want.includes(from), `main's installer no longer has: ${from.trim()} (update CHANGES)`);
    want = want.replace(from, to);
  }
  assert.equal(masked(script(NOW)), want);
});

test('installer: English only, every message the logic can show is still there', { skip: !ORIG && 'no git history' }, () => {
  const old = table(ORIG), now = table(NOW);
  assert.deepEqual(Object.keys(now), ['en'], 'one language');
  for (const k of Object.keys(old.en)) if (k !== 'other' && k !== 'editor') assert.equal(typeof now.en[k], 'string', k);
  for (const [, k] of NOW.matchAll(/data-t="(\w+)"/g)) assert.equal(typeof now.en[k], 'string', `data-t ${k}`);
});

test('installer: named Hortator, no language switch, /*LIB*/ and /*META*/ kept for make_site.py', () => {
  assert.match(NOW, /<html lang="en">/);
  assert.match(NOW.match(/<title>(.*)<\/title>/)[1], /Hortator/);
  assert.match(NOW.match(/<h1[^>]*>([\s\S]*?)<\/h1>/)[1], /Hortator/);
  assert.doesNotMatch(NOW, /FM-1 Drums|FM-1 DRUMS/, 'the old name is gone');
  assert.doesNotMatch(NOW, /id="lang"/);
  assert.equal(NOW.split('/*LIB*/').length, 2);
  assert.equal(NOW.split('/*META*/').length, 2);
  for (const id of ['go', 'bar', 'status', 'log']) assert.match(NOW, new RegExp(`id="${id}"`), id);
});

// ---- the landing page: the simulator as the hero, what it does, install
const LANDING = readFileSync('web/sim/index.html', 'utf8');

test('landing: hero (with the device and the #play end), features, install; install goes to the installer; the source is linked', () => {
  for (const id of ['hero', 'features', 'install']) assert.match(LANDING, new RegExp(`<section[^>]*id="${id}"`), id);
  const hero = LANDING.slice(LANDING.indexOf('id="hero"'), LANDING.indexOf('id="features"'));
  assert.match(hero, /id="device"/, 'the device lives in the hero');
  assert.match(hero, /id="play"/, 'the end of the hero track is #play');
  assert.match(hero, /id="tilt"/, 'the 3D transform wrapper');
  assert.match(LANDING, /href="webapp\/installer\/"/);
  assert.match(LANDING, /href="#play"/);
  assert.match(LANDING, /href="source\.tar\.gz"/);
  assert.match(LANDING.match(/<title>(.*)<\/title>/)[1], /Hortator/);
  assert.doesNotMatch(LANDING, /FM-1 Drums|FM-1 DRUMS/, 'the old name is gone');
  assert.match(LANDING, /galley/, 'the name\'s origin is told');
  assert.match(LANDING, /id="device"/, 'the simulator is on the page');
});

// ---- layout.js: the panel's two layouts, the morph between them, the hero's choreography (pure, no DOM)
import { LAYOUTS, blend, heroPose, outline } from '../web/sim/layout.js';

test('layouts: both have every control, a 240 px LCD, the same knobs', () => {
  const { landscape: L, portrait: P } = LAYOUTS;
  assert.deepEqual(Object.keys(L.ctl).sort(), Object.keys(P.ctl).sort());
  assert.equal(Object.keys(L.ctl).length, 14 + 27);
  assert.deepEqual(Object.keys(L.enc).sort(), Object.keys(P.enc).sort());
  assert.equal(L.lcd[2], 240);
  assert.equal(P.lcd[2], 240);
});

test('blend: exactly the landscape at 0 and the portrait at 1; the OCT tray, which the phone has not, fades out', () => {
  const { landscape: L, portrait: P } = LAYOUTS;
  const a = blend(L, P, 0), b = blend(L, P, 1), h = blend(L, P, 0.5);
  assert.deepEqual([a.W, a.H, a.lcd, a.ctl, a.enc], [L.W, L.H, L.lcd, L.ctl, L.enc]);
  assert.deepEqual([b.W, b.H, b.lcd, b.ctl, b.enc], [P.W, P.H, P.lcd, P.ctl, P.enc]);
  for (const name of Object.keys(P.deco)) assert.deepEqual(b.deco[name].box, P.deco[name], name);
  assert.equal(a.deco.oct.opacity, 1);
  assert.equal(b.deco.oct.opacity, 0);
  assert.ok(h.W < L.W && h.W > P.W, 'halfway is between the two');
  assert.deepEqual(blend(L, L, 0.7).ctl, L.ctl, 'desktop: nothing moves');
});

test('hero pose: far, tilted and blurred at the top; flat, sharp and arrived at the end, desktop and phone', () => {
  for (const phone of [false, true]) {
    const s = heroPose(0, phone), e = heroPose(1, phone);
    assert.ok(s.blur >= 8 && s.transform !== 'none' && !s.arrived && s.copy === 1, `start (${phone})`);
    assert.ok(e.blur === 0 && e.transform === 'none' && e.arrived && e.copy === 0, `end (${phone})`);
    assert.equal(e.morph, phone ? 1 : 0);
  }
});

test('hero pose: the shine never plays while the scene is blurred (a blur filter re-renders the whole scene)', () => {
  for (const phone of [false, true])
    for (let i = 0; i <= 1000; i++) {
      const q = heroPose(i / 1000, phone);
      assert.ok(!(q.blur > 0 && q.glare > 0), `p=${i / 1000} phone=${phone}: blur ${q.blur}, glare ${q.glare}`);
      if (phone && q.morph > 0) assert.equal(q.transform, 'none', `p=${i / 1000}: the phone morphs only once flat`);
    }
});

// ---- the features: a sticky screen plays each feature's clip; the panel locator lights the controls to press
import { BTN, ENC } from '../web/sim/controls.js';

test('features: each names a recorded clip and real panel controls', () => {
  const sec = LANDING.slice(LANDING.indexOf('id="features"'), LANDING.indexOf('id="install"'));
  const feats = [...sec.matchAll(/<li class="feat"[^>]*data-clip="(\w+)"[^>]*data-press="([^"]+)"/g)];
  assert.ok(feats.length >= 6, `${feats.length} features`);
  const clips = new Set(['home', 'seq', 'sound', 'fx', 'grids', 'comp', 'tracks', 'lfo', 'end']);   // tests/sim_record.c
  const labels = new Set([...BTN.map(b => b.label), ...ENC.map(e => e.label)]);
  for (const [, clip, press] of feats) {
    assert.ok(clips.has(clip), `clip ${clip}`);
    for (const l of press.split(' ')) assert.ok(labels.has(l), `${clip}: ${l} is not on the panel`);
  }
  assert.match(sec, /id="manual-screen"/, 'the sticky screen');
  assert.match(sec, /id="locator"/, 'the panel locator');
});

test('install: the version and what is new, the connection diagram, the installer, the way back and the recovery', () => {
  const sec = LANDING.slice(LANDING.indexOf('id="install"'), LANDING.indexOf('<footer'));
  for (const id of ['fw-version', 'whats-new', 'cable']) assert.match(sec, new RegExp(`id="${id}"`), id);
  assert.match(sec, /href="webapp\/installer\/"/);
  assert.match(sec, /<details[^>]*>\s*<summary>Back to the official firmware<\/summary>/);
  assert.match(sec, /<details[^>]*>\s*<summary>If an install fails<\/summary>/);
  assert.match(sec, /beta/i);
});

test('Controls opens its help over the hero stage, next to the button (not below the whole scroll track)', () => {
  const stage = LANDING.slice(LANDING.indexOf('id="stage"'), LANDING.indexOf('id="play"'));
  assert.match(stage, /id="help-toggle"/);
  assert.match(stage, /id="help"/, 'the help panel is inside the sticky stage');
});

test('footer: the name and its origin, the ways around the page, the credits and the licence', () => {
  const foot = LANDING.slice(LANDING.indexOf('<footer'), LANDING.indexOf('</footer>'));
  assert.match(foot, /HORTATOR/);
  for (const href of ['#play', 'webapp/installer/', '#install', 'source.tar.gz']) assert.ok(foot.includes(`href="${href}"`), href);
  for (const credit of ['Felucca', 'Leo Kuroshita', 'DEADACTIVE', 'Terminus', 'CC0', 'GPL-3.0', 'not affiliated'])
    assert.ok(foot.includes(credit), credit);
});

test('hero pose: the device lands early and holds: the last 40 % of the scroll track is the simulator, flat and usable', () => {
  for (const phone of [false, true])
    for (let i = 600; i <= 1000; i++) {
      const q = heroPose(i / 1000, phone);
      assert.ok(q.arrived && q.transform === 'none' && q.blur === 0 && q.glare === 0, `p=${i / 1000} phone=${phone}`);
    }
});

test('the simulator bar: the power caption, Controls and Reset in one row; the reset confirm is a pop-over in it', () => {
  const bar = LANDING.slice(LANDING.indexOf('id="play-bar"'), LANDING.indexOf('id="help"'));
  for (const id of ['power-note', 'help-toggle', 'reset', 'reset-confirm']) assert.match(bar, new RegExp(`id="${id}"`), id);
  assert.doesNotMatch(bar, /id="power-on"/, 'the power switch is on the device now');
  assert.match(LANDING, /class="hero-end"[^>]*id="play"|id="play"[^>]*class="hero-end"/);
});

// ---- web/make_site.py: the site's front page is the landing page with the simulator (when build/sim is built)
import { existsSync, rmSync } from 'node:fs';

test('make_site: the front page is the landing page with the simulator beside it; the installer stays at webapp/installer', {
  skip: !(existsSync('build/sim/package.fwsc') && existsSync('build/sim/fm1sim.wasm')) && 'needs a build_sim --package build',
}, () => {
  const out = 'build/sim/site-test';
  rmSync(out, { recursive: true, force: true });
  const py = process.env.PYTHON || 'python3';
  execFileSync(py, ['-c', 'import sys; sys.path.insert(0, "web"); import make_site; make_site.main(*sys.argv[1:])',
    'build/sim/package.fwsc', 'drum-0.0.0+test', out], { stdio: 'pipe' });
  const front = readFileSync(`${out}/index.html`, 'utf8');
  assert.match(front, /HORTATOR/, 'the landing page, not the old redirect');
  assert.match(front, /app\.js\?v=[0-9a-f]+/, 'the stamped build');
  for (const f of ['fm1sim.wasm', 'app.js', 'worklet.js', 'reel.bin.gz', 'reel.json', 'version.json', 'source.tar.gz'])
    assert.ok(existsSync(`${out}/${f}`), f);
  for (const f of ['reel.bin', 'reel.hash', 'package.fwsc']) assert.ok(!existsSync(`${out}/${f}`), `${f} is not published`);
  assert.match(readFileSync(`${out}/webapp/installer/index.html`, 'utf8'), /Install Hortator/);
  rmSync(out, { recursive: true, force: true });
});

test('outline: the body\'s edge as wall strips, 4 sides and N per corner, following the rounded outline', () => {
  const W = 1000, H = 620, R = 44, N = 6;
  const strips = outline(W, H, R, N);
  assert.equal(strips.length, 4 + 4 * N);
  const len = strips.reduce((a, s) => a + Math.hypot(s.x1 - s.x0, s.y1 - s.y0), 0);
  const perimeter = 2 * (W + H) - 8 * R + 2 * Math.PI * R;
  assert.ok(Math.abs(len - perimeter) / perimeter < 0.01, `${len} vs ${perimeter}`);
  for (let i = 0; i < strips.length; i++) {          // a closed loop: each strip starts where the last one ended
    const a = strips[i], b = strips[(i + 1) % strips.length];
    assert.ok(Math.hypot(a.x1 - b.x0, a.y1 - b.y0) < 1e-6, `gap after strip ${i}`);
  }
  for (const s of strips) assert.ok(s.x0 >= -1e-9 && s.x0 <= W + 1e-9 && s.y0 >= -1e-9 && s.y0 <= H + 1e-9);
});

test('sim.css parses: no comment left open, braces balanced (an open comment once swallowed half the page\'s styles)', () => {
  const css = readFileSync('web/sim/sim.css', 'utf8');
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  assert.ok(!bare.includes('/*'), 'a comment is never closed');
  assert.equal((bare.match(/{/g) || []).length, (bare.match(/}/g) || []).length, 'braces');
});

test('the power switch is page UI over the stage, a leader line to where the FM-1 has it (the top edge, above the screen\'s right corner)', () => {
  const stage = LANDING.slice(LANDING.indexOf('id="stage"'), LANDING.indexOf('id="play"'));
  assert.match(stage, /<button[^>]*id="power-on"[^>]*role="switch"|<button[^>]*role="switch"[^>]*id="power-on"/, 'a switch in the page');
  assert.match(stage, /id="power-lead"/, 'the leader line');
  const app = readFileSync('web/sim/app.js', 'utf8');
  assert.doesNotMatch(app, /sw\.id = 'power-on'/, 'no longer built into the device');
  assert.doesNotMatch(app, /power-mark|powerMark/, 'nothing on the device: no mark either');
  assert.doesNotMatch(readFileSync('web/sim/sim.css', 'utf8'), /power-mark/);
  for (const [name, L] of Object.entries(LAYOUTS)) {                 // the mark on the device: where the real switch is
    const [x, y, w, h] = L.power;
    assert.equal(y + h / 2, 0, `${name}: centred on the top edge`);
    const bezelRight = L.deco.bezel[0] + L.deco.bezel[2];
    assert.ok(x + w <= bezelRight + 4 && x + w >= bezelRight - 60, `${name}: over the screen's right corner`);
  }
  assert.deepEqual(blend(LAYOUTS.landscape, LAYOUTS.portrait, 1).power, LAYOUTS.portrait.power);
  const css = readFileSync('web/sim/sim.css', 'utf8');
  assert.match(css, /@keyframes nudge/, 'the knob nudges toward on to draw the eye');
});

test('while the simulator is on screen, a cue says the page goes on', () => {
  const stage = LANDING.slice(LANDING.indexOf('id="stage"'), LANDING.indexOf('id="play"'));
  assert.match(stage, /<a [^>]*class="more-cue"[^>]*href="#features"|<a [^>]*href="#features"[^>]*class="more-cue"/);
});

test('Controls opens beside the device on wide screens: the stage gives the panel its width and the device moves left', () => {
  const css = readFileSync('web/sim/sim.css', 'utf8'), app = readFileSync('web/sim/app.js', 'utf8');
  assert.match(css, /\.hero-stage\.help-open \.help\s*{[^}]*right:/, 'the panel on the right');
  assert.match(css, /\.hero-stage\.help-open \.scene\s*{[^}]*padding-right:/, 'the device keeps out of its way');
  assert.match(app, /HELP_W/, 'the device is fitted to the width left beside the panel');
});
