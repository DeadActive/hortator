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
import { BTN, ENC, KEYS, KEYBOARD, HeldSet, keyEvent, contextMenuLatches } from '../web/sim/controls.js';

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
  assert.equal(BTN[1].label, 'SEL', 'B_SCL is printed SEL on the device');
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

test('a right-click latches; the contextmenu a touch or pen long-press also fires does not (the timer does)', () => {
  assert.equal(contextMenuLatches('mouse'), true);
  assert.equal(contextMenuLatches('touch'), false);
  assert.equal(contextMenuLatches('pen'), false);
  assert.equal(contextMenuLatches(undefined), true, 'no pointerdown seen (keyboard context-menu key): a mouse-like latch');
});

// ---- store.js: the flash in IndexedDB, against a fake database (node has none)
import { FlashStore } from '../web/sim/store.js';

function fakeDb(data, mode = {}) {                  // mode.throwTx: transaction() throws; mode.abort: writes abort
  return {
    closed: false,
    transaction() {
      if (mode.throwTx) throw new Error('InvalidStateError: connection lost');
      const tx = { oncomplete: null, onabort: null, onerror: null };
      const writes = [];
      tx.objectStore = () => ({
        put: (v, k) => writes.push([k, v]),
        clear: () => writes.push(['clear']),
        getAllKeys: () => { const r = {}; setTimeout(() => { r.result = [...data.keys()]; r.onsuccess?.(); }); return r; },
        getAll: () => { const r = {}; setTimeout(() => { r.result = [...data.values()]; r.onsuccess?.(); }); return r; },
      });
      setTimeout(() => {
        if (mode.abort) { tx.error = new Error('QuotaExceededError'); tx.onabort?.(); return; }
        for (const w of writes) if (w[0] === 'clear') data.clear(); else data.set(w[0], w[1]);
        tx.oncomplete?.();
      });
      return tx;
    },
  };
}
const tick = () => new Promise(r => setTimeout(r, 5));
const sector = v => new Uint8Array(4096).fill(v);

test('store: loads what is stored, writes sectors', async () => {
  const data = new Map([[0x97000, sector(1)]]);
  const notes = [];
  const st = new FlashStore(async () => fakeDb(data), n => notes.push(n));
  const got = await st.load();
  assert.deepEqual(got.map(([k]) => k), [0x97000]);
  st.put([{ off: 0x99000, bytes: sector(2) }]);
  await tick();
  assert.equal(data.get(0x99000)[0], 2);
  assert.equal(st.pending.size, 0);
  assert.deepEqual(notes, []);
});

test('store: a lost connection reopens once and the write lands', async () => {
  const data = new Map(), first = {};
  let opens = 0;
  const st = new FlashStore(async () => fakeDb(data, opens++ === 0 ? first : {}), () => {});
  await st.load();
  first.throwTx = true;                             // the connection goes bad (Safari after backgrounding)
  st.put([{ off: 0x97000, bytes: sector(3) }]);
  await tick(); await tick();
  assert.equal(opens, 2, 'reopened once');
  assert.equal(data.get(0x97000)?.[0], 3);
  assert.equal(st.pending.size, 0);
});

test('store: when writing keeps failing, a note says so and the sectors wait for the next write', async () => {
  const data = new Map();
  const notes = [];
  let bad = true;
  const st = new FlashStore(async () => fakeDb(data, bad ? { abort: true } : {}), n => notes.push(n));
  await st.load();
  st.put([{ off: 0x97000, bytes: sector(4) }]);
  await tick(); await tick();
  assert.equal(notes.length, 1, 'one note');
  assert.equal(st.pending.size, 1, 'kept');
  bad = false;
  st.db = null;                                     // storage works again (next open succeeds)
  st.put([{ off: 0x99000, bytes: sector(5) }]);
  await tick(); await tick();
  assert.equal(data.get(0x97000)?.[0], 4, 'the earlier sector is written too');
  assert.equal(data.get(0x99000)?.[0], 5);
  assert.equal(st.pending.size, 0);
});

test('store: storage blocked at load: a note, an empty start, puts do not throw', async () => {
  const notes = [];
  const st = new FlashStore(async () => { throw new Error('SecurityError'); }, n => notes.push(n));
  assert.deepEqual(await st.load(), []);
  assert.equal(notes.length, 1);
  st.put([{ off: 0x97000, bytes: sector(1) }]);
  await tick();
});

test('store: after clear (Reset to demos), late writes are dropped', async () => {
  const data = new Map([[0x97000, sector(1)]]);
  const st = new FlashStore(async () => fakeDb(data), () => {});
  await st.load();
  await st.clear();
  st.put([{ off: 0x99000, bytes: sector(2) }]);
  await tick();
  assert.equal(data.size, 0);
});

// ---- audio.js: keeping the AudioContext (the simulator's clock) running on phones
import { needsResume, playbackSession } from '../web/sim/audio.js';

test('needsResume: a suspended or interrupted context is resumed; a running or closed one is not', () => {
  assert.equal(needsResume('suspended'), true);
  assert.equal(needsResume('interrupted'), true);   // iOS: a call, another app took the audio
  assert.equal(needsResume('running'), false);
  assert.equal(needsResume('closed'), false);
});

test('playbackSession: asks for a playback session where the browser has one (iOS: sound with the silent switch on)', () => {
  const nav = { audioSession: { type: 'auto' } };
  assert.equal(playbackSession(nav), true);
  assert.equal(nav.audioSession.type, 'playback');
  assert.equal(playbackSession({}), false, 'no audioSession: nothing to do');
  assert.equal(playbackSession({ get audioSession() { throw new Error('denied'); } }), false, 'never throws');
});

// ---- the hero's reel: recorded screen, LEDs and knobs (tests/sim_record.c), replayed by reel.js
import { parseReel } from '../web/sim/reel.js';

test('reel: 30 s at 15 fps, replaying every frame rebuilds the recorder\'s last screen, small enough to ship', async () => {
  const gz = readFileSync(join(DIR, 'reel.bin.gz'));
  assert.ok(gz.length < 1.5e6, `reel.bin.gz is ${gz.length} B`);
  const buf = await new Response(new Blob([gz]).stream().pipeThrough(new DecompressionStream('gzip'))).arrayBuffer();
  const reel = parseReel(buf);
  assert.equal(reel.fps, 15);
  assert.equal(reel.frames, 450);
  const fb = new Uint16Array(240 * 240);
  let leds = 0, keys = 0, turns = 0;
  for (let i = 0; i < reel.frames; i++) {
    const f = reel.apply(i, fb);
    leds |= f.leds; keys |= f.keyLeds;
    turns += f.enc.reduce((a, s) => a + Math.abs(s), 0);
  }
  let h = 2166136261;
  for (const v of new Uint8Array(fb.buffer)) h = Math.imul(h ^ v, 16777619) >>> 0;
  assert.equal(h, Number(readFileSync(join(DIR, 'reel.hash'), 'utf8')), 'the replayed last frame is the recorded one');
  assert.ok(leds && keys, 'button and key LEDs light during the reel');
  assert.ok(turns > 0, 'knobs turn during the reel');
});
