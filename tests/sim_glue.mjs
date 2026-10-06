// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The web simulator's page glue on the built wasm (tools/build_sim.sh): node tests/sim_glue.mjs [build/sim]
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { Fm1Sim, FS } from '../web/sim/engine.js';

const DIR = process.argv[2] || 'build/sim';
const WASM = readFileSync(join(DIR, 'fm1sim.wasm'));
const B_EDIT = 4, B_PLAY = 10;                      // panel.c's label order (controls.js checks it against panel.c)

async function boot(sectors = null) {
  const sim = await Fm1Sim.create(WASM);
  sim.boot(sectors);
  return sim;
}
function run(sim, frames) {
  let ui = 0;
  for (; frames > 0; frames -= 128) ui += sim.render(128).uiFrames;
  return ui;
}
function peak(sim, frames) {
  let m = 0;
  for (; frames > 0; frames -= 128) {
    const { left, right } = sim.render(128);
    for (let i = 0; i < 128; i++) m = Math.max(m, Math.abs(left[i]), Math.abs(right[i]));
  }
  return m;
}
function hash(fb) {
  let h = 2166136261;
  for (const v of fb) h = Math.imul(h ^ v, 16777619) >>> 0;
  return h;
}
function tap(sim, label) {
  sim.btn(label, true);
  run(sim, 1024);
  sim.btn(label, false);
  run(sim, 1024);
}

test('a fresh boot draws the screen and runs UI frames', async () => {
  const sim = await boot();
  const ui = run(sim, 4096);
  assert.ok(ui >= 4, `UI frames in 93 ms: ${ui}`);
  assert.ok(sim.fb().filter(v => v !== 0).length > 500);
});

test('a button press changes the screen', async () => {
  const sim = await boot();
  run(sim, 4096);
  const h = hash(sim.fb());
  tap(sim, B_EDIT);
  assert.notEqual(hash(sim.fb()), h);
});

test('PLAY on the demo is not silent; the transport runs', async () => {
  const sim = await boot();
  run(sim, 4096);
  assert.ok(peak(sim, FS / 2) < 4 / 32768, 'silent before PLAY');
  tap(sim, B_PLAY);
  assert.ok(sim.playing());
  assert.ok(peak(sim, FS) > 0.01);
});

test('the fresh boot leaves dirty flash sectors; a new instance boots from them with the demos', async () => {
  const sim = await boot();
  run(sim, 4096);
  const dirty = sim.takeDirty();
  assert.ok(dirty.length > 0);
  assert.equal(sim.takeDirty().length, 0, 'taken once');
  for (const { off, bytes } of dirty) {
    assert.equal(off % 4096, 0);
    assert.equal(bytes.length, 4096);
  }
  const again = await boot(new Map(dirty.map(d => [d.off, d.bytes])));
  run(again, 4096);
  tap(again, B_PLAY);
  assert.ok(peak(again, FS) > 0.01, 'the demo came back from flash');
});

test('releaseAll lets go of held keys and buttons', async () => {
  const sim = await boot();
  run(sim, 4096);
  sim.key(5, true);
  run(sim, 2048);
  assert.ok((sim.keyLeds() >>> 5) & 1, 'held key lit');
  sim.releaseAll();
  run(sim, 2048);
  assert.equal(sim.keyLeds(), 0, 'released');
});

// ---- controls.js: the panel's data, checked against the firmware
import { BTN, ENC, KEYS, KEYBOARD, HeldSet, keyEvent } from '../web/sim/controls.js';

function cEnum(first) {                             // panel.c: enum { B_FX, ..., NB } -> ['B_FX', ...]
  const src = readFileSync('firmware/src/panel.c', 'utf8');
  const m = src.match(new RegExp(`enum\\s*\\{\\s*(${first}[^}]*)\\}`));
  return m[1].split(',').map(s => s.trim()).filter(s => s && !/^N[BE]$/.test(s));
}

test('BTN and ENC follow panel.c (label ids are array indexes)', () => {
  assert.deepEqual(BTN.map(b => b.c), cEnum('B_FX'));
  assert.deepEqual(ENC.map(e => e.c), cEnum('EN_SELECT'));
  assert.equal(BTN[B_EDIT].label, 'EDIT');
  assert.equal(BTN[B_PLAY].label, 'PLAY');
});

test('27 keys F3..G5, the white ones are the step keys (seq.c STEP_KEY)', () => {
  assert.equal(KEYS.length, 27);
  assert.equal(KEYS[0].name, 'F3');
  assert.equal(KEYS[26].name, 'G5');
  assert.deepEqual(KEYS.filter(k => !k.black).map(k => k.n), [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]);
});

test('the computer keyboard plays every key and PLAY / REC / HOME / OCT- / OCT+, no code twice', () => {
  const codes = Object.keys(KEYBOARD);
  assert.equal(new Set(codes).size, codes.length);
  const keys = new Set(codes.filter(c => KEYBOARD[c].kind === 'key').map(c => KEYBOARD[c].id));
  assert.equal(keys.size, 27);
  const btns = new Set(codes.filter(c => KEYBOARD[c].kind === 'btn').map(c => BTN[KEYBOARD[c].id].label));
  for (const l of ['PLAY', 'REC', 'HOME', 'OCT-', 'OCT+']) assert.ok(btns.has(l), l);
  const ctl = codes.map(c => `${KEYBOARD[c].kind}:${KEYBOARD[c].id}`);
  assert.equal(new Set(ctl).size, ctl.length, 'one code per control');
});

test('HeldSet: a control stays held until every source lets go', () => {
  const h = new HeldSet();
  assert.equal(h.press('btn:10', 'kbd'), true, 'first source: down');
  assert.equal(h.press('btn:10', 'ptr1'), false, 'second source: already down');
  assert.equal(h.release('btn:10', 'kbd'), false, 'one left: still held');
  assert.equal(h.release('btn:10', 'ptr1'), true, 'last one: up');
  assert.equal(h.release('btn:10', 'ptr1'), false, 'an extra release is ignored');
  h.press('key:3', 'ptr2');
  h.press('btn:6', 'kbd');
  assert.deepEqual(h.releaseAll().sort(), ['btn:6', 'key:3']);
  assert.deepEqual(h.releaseAll(), []);
});

test('keyEvent: mapped codes, autorepeat and shortcuts with modifiers ignored', () => {
  assert.deepEqual(keyEvent({ code: 'KeyZ', type: 'keydown' }), { ctl: 'key:0', down: true });
  assert.deepEqual(keyEvent({ code: 'KeyZ', type: 'keyup' }), { ctl: 'key:0', down: false });
  assert.deepEqual(keyEvent({ code: 'Space', type: 'keydown' }), { ctl: `btn:${B_PLAY}`, down: true });
  assert.equal(keyEvent({ code: 'KeyZ', type: 'keydown', repeat: true }), null);
  assert.equal(keyEvent({ code: 'KeyR', type: 'keydown', metaKey: true }), null);
  assert.equal(keyEvent({ code: 'KeyR', type: 'keydown', ctrlKey: true }), null);
  assert.equal(keyEvent({ code: 'F5', type: 'keydown' }), null);
});

// ---- the page offers the source of what it runs (GPL-3.0 §6): build/sim/source.tar.gz, linked from the footer
import { execFileSync } from 'node:child_process';

test('the page links to the source archive it ships, and the archive holds the firmware and the sim', () => {
  const html = readFileSync(join(DIR, 'index.html'), 'utf8');
  assert.match(html, /<a [^>]*href="source\.tar\.gz"/);
  const list = execFileSync('tar', ['-tzf', join(DIR, 'source.tar.gz')], { encoding: 'utf8' }).split('\n');
  for (const f of ['firmware/src/felucca.c', 'tests/sim_core.c', 'web/sim/app.js', 'tools/build_sim.sh', 'LICENSE'])
    assert.ok(list.some(l => l.endsWith(f)), f);
});
