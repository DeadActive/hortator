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
  assert.ok((sim.leds() >>> 19) & 1, 'held key lit');
  sim.releaseAll();
  run(sim, 2048);
  assert.equal((sim.leds() >>> 19) & 1, 0, 'released');
});
