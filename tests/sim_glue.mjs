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
  assert.match(html, /<a [^>]*href="source\.tar\.gz(\?v=[0-9a-f]+)?"/);
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
import { parseReel, snapshots } from '../web/sim/reel.js';

test('reel: 34 s at 15 fps, replaying every frame rebuilds the recorder\'s last screen, small enough to ship', async () => {
  const gz = readFileSync(join(DIR, 'reel.bin.gz'));
  assert.ok(gz.length < 1.5e6, `reel.bin.gz is ${gz.length} B`);
  const buf = await new Response(new Blob([gz]).stream().pipeThrough(new DecompressionStream('gzip'))).arrayBuffer();
  const reel = parseReel(buf);
  assert.equal(reel.fps, 15);
  assert.equal(reel.frames, 510);
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

test('reel clips: the recorder names each page\'s stretch (the landing page plays them per feature)', () => {
  const clips = JSON.parse(readFileSync(join(DIR, 'reel.json'), 'utf8'));
  const names = clips.map(c => c.name);
  for (const n of ['home', 'seq', 'sound', 'fx', 'grids', 'comp', 'tracks', 'lfo'])
    assert.ok(names.includes(n), n);
  let last = 0;
  for (const c of clips) {
    assert.ok(c.from >= last && c.to > c.from && c.to <= 510, `${c.name} ${c.from}..${c.to}`);
    last = c.to;
  }
});

test('reel snapshots: a clip replayed from its start snapshot ends on the same screen as the whole reel played through', async () => {
  const gz = readFileSync(join(DIR, 'reel.bin.gz'));
  const buf = await new Response(new Blob([gz]).stream().pipeThrough(new DecompressionStream('gzip'))).arrayBuffer();
  const reel = parseReel(buf);
  const clips = JSON.parse(readFileSync(join(DIR, 'reel.json'), 'utf8'));
  const snaps = snapshots(reel, clips.map(c => c.from));
  const seq = new Uint16Array(240 * 240), ends = new Map();
  for (let i = 0; i < reel.frames; i++) {
    reel.apply(i, seq);
    for (const c of clips) if (i === c.to - 1) ends.set(c.name, seq.slice());
  }
  for (const c of clips) {
    const fb = snaps.get(c.from).fb.slice();
    for (let i = c.from + 1; i < c.to; i++) reel.apply(i, fb);
    assert.deepEqual(fb, ends.get(c.name), c.name);
  }
});

test('version.json: the firmware built here, its date and what is new, from VERSION.txt and CHANGELOG.md', () => {
  const v = JSON.parse(readFileSync(join(DIR, 'version.json'), 'utf8'));
  assert.equal(v.version, readFileSync('VERSION.txt', 'utf8').trim());
  assert.match(v.commit, /^[0-9a-f]{7,}$/);
  assert.match(v.date, /^\d{4}-\d{2}-\d{2}$/);
  assert.ok(Array.isArray(v.since) && Array.isArray(v.release.notes));
  assert.equal(v.release.version, v.version);
  assert.ok(v.since.length + v.release.notes.length > 0, 'something to show');
  for (const n of [...v.since, ...v.release.notes]) {
    assert.ok(n.title && n.title.length < 60, `title: ${n.title}`);
    assert.ok(n.text.length > 0);
  }
});

test('published page: every local file it loads carries the build stamp (?v=), so an update never mixes old and new files', () => {
  const html = readFileSync(join(DIR, 'index.html'), 'utf8');
  const stamp = html.match(/app\.js\?v=([0-9a-f]{8,})/)?.[1];
  assert.ok(stamp, 'app.js stamped');
  assert.match(html, new RegExp(`sim\\.css\\?v=${stamp}`));
  const local = ['controls.js', 'store.js', 'audio.js', 'reel.js', 'layout.js', 'engine.js', 'worklet.js',
    'fm1sim.wasm', 'reel.bin.gz', 'reel.json', 'version.json'];
  const js = ['app.js', 'layout.js', 'worklet.js'].map(f => readFileSync(join(DIR, f), 'utf8')).join('\n');
  for (const f of local) {
    const refs = [...js.matchAll(new RegExp(`['"](?:\\./)?${f.replace('.', '\\.')}(\\?v=[0-9a-f]+)?['"]`, 'g'))];
    for (const r of refs) assert.equal(r[1], `?v=${stamp}`, `${f} referenced without the stamp: ${r[0]}`);
  }
  assert.ok(js.includes(`worklet.js?v=${stamp}`) && js.includes(`fm1sim.wasm?v=${stamp}`));
});

// ---- lab.js: the feature pieces' engine (the analyser in the page; the player is the same calls in a worklet)
import { Lab } from '../web/sim/lab.js';

test('lab: ids from the firmware, a voice rendered, the Grids lanes, the probes parsed', async () => {
  const ids = JSON.parse(readFileSync(join(DIR, 'lab_ids.json'), 'utf8'));
  const lab = await Lab.create(WASM, ids);
  assert.equal(ids.models.length > 20, true);
  lab.reset();
  lab.model(0, ids.DM.K808);
  const hit = lab.voice(ids.fs / 2, false);
  assert.equal(hit.length, ids.fs / 2);
  assert.ok(Math.max(...hit.map(Math.abs)) > 0.01, 'the 808 kick is heard');
  lab.global(ids.G.GX, 0);
  const a = lab.grids();
  lab.global(ids.G.GX, 127);
  const b = lab.grids();
  assert.equal(a.length, 96);
  assert.notDeepEqual(Array.from(a), Array.from(b));
  lab.model(0, ids.DM.K909);
  for (let s = 0; s < 16; s += 4) lab.step(0, s, true);
  lab.play(true);
  lab.probeClear();
  lab.renderMono(ids.fs);
  const probes = lab.probes();
  assert.ok(probes.length > 100);
  assert.ok(probes.some(p => p.fired & 1), 'the kick fired');
  assert.equal(probes[0].lfo.length, 8);
});

// ---- the pieces' measurements (lab_dsp.js) on what the firmware rendered
import { fft, spectrum, tailLength } from '../web/sim/lab_dsp.js';

test('lab_dsp: the FFT finds a sine at its bin; a 909 kick is low, a closed hat high; decay lengthens the tail', async () => {
  const n = 1024, re = new Float64Array(n), im = new Float64Array(n);
  for (let i = 0; i < n; i++) re[i] = Math.sin(2 * Math.PI * 37 * i / n);
  fft(re, im);
  const mag = Array.from(re, (r, k) => Math.hypot(r, im[k])).slice(0, n / 2);
  assert.equal(mag.indexOf(Math.max(...mag)), 37);
  const ids = JSON.parse(readFileSync(join(DIR, 'lab_ids.json'), 'utf8'));
  const lab = await Lab.create(WASM, ids);
  const centroid = m => {                             // the spectrum's centre of mass, Hz
    lab.reset();
    lab.model(0, m);
    const db = spectrum(lab.voice(8192, false));
    let s = 0, w = 0;
    db.forEach((d, k) => { const p = 10 ** (d / 10); s += p * k; w += p; });
    return s / w * ids.fs / 8192;
  };
  const kick = centroid(ids.DM.K909), hat = centroid(ids.DM.HATC);
  assert.ok(kick < 400 && hat > 3000, `kick ${kick.toFixed(0)} Hz, hat ${hat.toFixed(0)} Hz`);
  lab.reset();
  lab.model(0, ids.DM.K808);
  lab.param(0, ids.P.E1, 10);
  const short = tailLength(lab.voice(ids.fs, false));
  lab.param(0, ids.P.E1, 120);
  assert.ok(tailLength(lab.voice(ids.fs, false)) > short * 1.5);
});

test('lab: a string tuned to A3 rings on the harmonics of 220 Hz; MIX 0 leaves the dry hit', async () => {
  const ids = JSON.parse(readFileSync(join(DIR, 'lab_ids.json'), 'utf8'));
  const lab = await Lab.create(WASM, ids);
  assert.ok(ids.tp.RSTRCT, 'STRCT from the firmware');
  const hit = (model, mix) => {
    lab.reset();
    lab.model(0, ids.DM.RIM);
    lab.param(0, ids.P.RMODEL, model);
    lab.param(0, ids.P.RTUNE, 57);
    lab.param(0, ids.P.RDECAY, 100);
    lab.param(0, ids.P.RMIX, mix);
    return lab.voice(ids.fs, true);
  };
  const db = spectrum(hit(ids.RS.STRNG, 127).subarray(4096), 16384);   // the ring, after the attack
  const hz = k => k * ids.fs / 16384;
  let best = 0;
  for (let k = 1; k < db.length; k++) if (hz(k) > 100 && hz(k) < 3000 && db[k] > db[best]) best = k;
  const ratio = hz(best) / 220;
  assert.ok(Math.abs(ratio - Math.round(ratio)) < 0.04, `the loudest partial at ${hz(best).toFixed(1)} Hz`);
  const dry = hit(ids.RS.OFF, 127), none = hit(ids.RS.STRNG, 0);
  assert.ok(tailLength(none) < tailLength(hit(ids.RS.STRNG, 127)) / 2, 'MIX 0: no ring');
  assert.ok(Math.abs(tailLength(none) - tailLength(dry)) < ids.fs / 20, 'MIX 0 is the dry hit');
});

test('lab: the pump: keyed by the kick it ducks; the kick muted, GHOST MUTE stops it, KEEP keeps it', async () => {
  const ids = JSON.parse(readFileSync(join(DIR, 'lab_ids.json'), 'utf8'));
  const lab = await Lab.create(WASM, ids);
  const { P, G, DM } = ids, bar = ids.fs * 2;
  const run = (src, muted, ghost) => {
    lab.reset();
    lab.model(0, DM.K909);
    lab.model(1, DM.HATO);
    for (let s = 0; s < 16; s++) { lab.step(0, s, s % 4 === 0); lab.step(1, s, true); }
    lab.param(1, P.DUCK, 1);
    lab.global(G.CSRC, src);
    lab.global(G.CTHR, 20);
    lab.global(G.CRAT, 90);
    lab.global(G.CGHOST, ghost);
    lab.param(0, P.MUTE, muted ? 1 : 0);
    lab.play(true);
    lab.renderMono(bar);
    lab.probeClear();
    lab.renderMono(bar);
    return Math.min(...lab.probes().map(p => p.gr)) * 241 / 327680;
  };
  const [MUTE, KEEP] = [ids.names.ghost.indexOf('MUTE'), ids.names.ghost.indexOf('KEEP')];
  assert.ok(run(1, false, KEEP) < -3, 'keyed: it ducks');
  assert.equal(run(0, false, KEEP), 0, 'SRC OFF: nothing');
  assert.ok(run(1, true, MUTE) > -0.5, 'the kick muted, GHOST MUTE: no pump');
  assert.ok(run(1, true, KEEP) < -3, 'the kick muted, GHOST KEEP: the pump goes on');
});

test('lab: an LFO on the hats\' DECAY: the tails differ hit to hit; at DEPTH 0 they do not', async () => {
  const ids = JSON.parse(readFileSync(join(DIR, 'lab_ids.json'), 'utf8'));
  const lab = await Lab.create(WASM, ids);
  assert.equal(ids.names.lsync[3], '1BAR');
  const tails = depth => {
    lab.reset();
    lab.model(0, ids.DM.HATC);
    for (let s = 0; s < 16; s++) lab.step(0, s, s % 4 === 0);   // quarters: room for each tail
    lab.param(0, ids.P.LFO1 + ids.LF.DEST, 2);                   // DECAY
    lab.param(0, ids.P.LFO1 + ids.LF.WAVE, ids.names.lwave.indexOf('SAW'));
    lab.param(0, ids.P.LFO1 + ids.LF.RATE, 23);                  // 1 bar
    lab.param(0, ids.P.LFO1 + ids.LF.DEPTH, depth);
    lab.play(true);
    const a = lab.renderMono(ids.fs * 2), q = ids.fs / 2;
    return [0, 1, 2, 3].map(b => tailLength(a.subarray(b * q + 64, (b + 1) * q)));
  };
  const flat = tails(0), moved = tails(60);
  assert.ok(Math.max(...flat) - Math.min(...flat) < ids.fs / 200, `DEPTH 0: ${flat}`);
  assert.ok(Math.max(...moved) - Math.min(...moved) > ids.fs / 50, `DEPTH 60: ${moved}`);
});

test('lab: the dice over 8 bars: 1/2 plays on bars 1, 3, 5, 7; 1-SHOT on bar 1; RATCH 4 hits; 100% always; the same after PLAY', async () => {
  const ids = JSON.parse(readFileSync(join(DIR, 'lab_ids.json'), 'utf8'));
  const lab = await Lab.create(WASM, ids);
  const c = name => ids.conds.find(x => x.name === name).store;
  const run = () => {
    lab.reset();
    lab.model(1, ids.DM.HATC);
    lab.step(1, 0, true, false, c('100%'), 3);
    lab.step(1, 4, true, false, c('1/2'));
    lab.step(1, 8, true, false, c('1-SHOT'));
    lab.step(1, 12, true, false, c('50%'));
    lab.play(true);
    return lab.dice(1, 8);
  };
  const d = run();
  assert.deepEqual(d.map(b => b[0]), [4, 4, 4, 4, 4, 4, 4, 4], 'RATCH 4 at 100%');
  assert.deepEqual(d.map(b => b[4]), [1, 0, 1, 0, 1, 0, 1, 0], '1/2');
  assert.deepEqual(d.map(b => b[8]), [1, 0, 0, 0, 0, 0, 0, 0], '1-SHOT');
  const half = d.map(b => b[12]);
  assert.ok(half.some(Boolean) && !half.every(Boolean), `50%: ${half}`);
  assert.deepEqual(run(), d, 'the same 8 bars after every PLAY');
});
