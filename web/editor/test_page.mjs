// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
//
// The editor page's logic against the mock device: node, no browser, no hardware. From the repo root:
//   node web/editor/test_page.mjs          (also run by node web/test_web.mjs)
// - connect / refuse: Hortator, Felucca, stock FM-1 and other gear, an older Hortator, another major, another magic
// - the outgoing whitelist: nothing to a port that is not a Hortator, only INFO until it identifies, every frame
//   from encodeRequest (F0 7D 48 54)
// - the request queue: one in flight, seq, timeout and retries with the same seq, coalescing, wrap-around
// - the Sound tab follows the device: pages from PAGES (SRR, INSERT, DLY MODE), the device's texts, pushes, selection,
//   model changes both ways, refusals; the header; the update silence and the resync after it

import { readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import * as P from "./proto.mjs";
import { createMockDevice, loadDeviceData } from "./mock_device.mjs";
import { DeviceError, INFO_ONLY, Link, TimeoutError } from "./link.mjs";
import { BUSY_MSG, Session, connect, listPorts } from "./session.mjs";
import { ID, SoundModel } from "./tab_sound.mjs";
import { headerModel } from "./header.mjs";

const { CMD, PUSH, SCOPE, NONE, SF } = P;
const HERE = new URL(".", import.meta.url).pathname;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function until(cond, ms = 3000) {
  for (const t0 = Date.now(); Date.now() - t0 < ms; await sleep(2)) if (cond()) return true;
  return cond();
}
const FAST = { timeout: 25, longTimeout: 60, tries: 3, silence: 60, retryInfo: 25, keepAlive: 100000 };

export async function runPageTests(log = console.log) {
  let passed = 0, failed = 0;
  const ok = (cond, what) => { if (cond) passed++; else { failed++; log(`FAIL page: ${what}`); } };
  const data = await loadDeviceData();
  const dev = (o) => createMockDevice(data, o);
  /** one access holding the ports of several mocks (and plain ports that record what they get) */
  const access = (...devs) => {
    const a = { inputs: new Map(), outputs: new Map(), sysexEnabled: true, onstatechange: null };
    devs.forEach((d, n) => {
      for (const p of d.access.inputs.values()) a.inputs.set(`${n}-in`, p);
      for (const p of d.access.outputs.values()) a.outputs.set(`${n}-out`, p);
    });
    return a;
  };
  const isInfo = (f) => { try { return P.decodeRequest(f).cmd === CMD.INFO; } catch { return false; } };
  const editorFrame = (f) => f[0] === 0xf0 && f[1] === 0x7d && f[2] === 0x48 && f[3] === 0x54 && f[f.length - 1] === 0xf7;

  /* ------------------------------------------------------------- connect / refuse --- */
  {
    const m = dev();
    const r = await connect(access(m), { timing: FAST });
    ok(r.ok && r.session.info.firmware === "0.28.0" && r.session.info.major === 1 && r.port === "Hortator"
      && r.session.state && isInfo(m.received[0]), "a Hortator connects; INFO first");
    r.session?.close();
  }
  {
    const fel = dev({ name: "Felucca" }), r = await connect(access(fel), { timing: FAST });
    ok(!r.ok && r.why === "felucca" && /Felucca's editor/.test(r.message) && fel.received.length === 0, "upstream Felucca: refused, nothing sent");
  }
  {
    const stock = dev({ name: "FM-1" }), iac = dev({ name: "IAC Driver Bus 1" }), r = await connect(access(stock, iac), { timing: FAST });
    ok(!r.ok && r.why === "none" && /No Hortator/.test(r.message) && r.links.some((l) => /installer/.test(l.href))
      && stock.received.length === 0 && iac.received.length === 0, "stock FM-1 and other gear: nothing sent, installer link");
  }
  {
    const old = dev({ editor: false }), r = await connect(access(old), { timing: FAST });
    const seqs = new Set(old.received.map((f) => f[5]));
    ok(!r.ok && r.why === "noeditor" && /update it/i.test(r.message) && old.received.length === FAST.tries
      && old.received.every(isInfo) && seqs.size === 1, "an older Hortator (no editor): INFO x3 (one seq), nothing else, update message");
  }
  {
    const nw = dev({ major: 2 }), r = await connect(access(nw), { timing: FAST });
    ok(!r.ok && r.why === "newer" && /newer Hortator/.test(r.message) && r.links.some((l) => /releases/.test(l.href))
      && nw.received.length === 1 && isInfo(nw.received[0]), "a newer major: refused after INFO alone, release link");
    const ol = dev({ major: 0 }), r2 = await connect(access(ol), { timing: FAST });
    ok(!r2.ok && r2.why === "older" && ol.received.every(isInfo), "an older major: refused, update message");
    const mg = dev({ magic: "FELUCCA" }), r3 = await connect(access(mg), { timing: FAST });
    ok(!r3.ok && r3.why === "magic" && mg.received.every(isInfo), "another magic: refused");
    const mn = dev({ minor: 9 }), r4 = await connect(access(mn), { timing: FAST });
    ok(r4.ok, "the same major with a newer minor: connected");
    r4.session?.close();
  }
  {
    const fel = dev({ name: "Felucca" }), stock = dev({ name: "M-VAVE FM-1" }), h = dev({ name: "Hortator 8ch MIDI 1" });
    const r = await connect(access(fel, stock, h), { timing: FAST });
    ok(r.ok && r.port === "Hortator 8ch MIDI 1" && fel.received.length === 0 && stock.received.length === 0,
      "Felucca + stock + Hortator: the Hortator connects, the others get nothing");
    r.session?.close();
    const off = dev();
    for (const p of [...off.access.inputs.values(), ...off.access.outputs.values()]) p.state = "disconnected";
    ok(!listPorts(access(off)).hortator.length, "a disconnected port is not asked");
  }
  {
    const m = dev({ caps: P.CAP.LIVE }), r = await connect(access(m), { timing: FAST });
    const n = m.received.length;
    const e = await r.session.request(CMD.STEPS_GET, { slot: NONE, var: 0, track: 0 }).catch((x) => x);
    ok(r.ok && !r.session.has(CMD.BACKUP_LIST) && r.session.has(CMD.SET) && /Not in this firmware/.test(e.message)
      && m.received.length === n, "a missing capability: hidden, nothing sent");
    r.session.close();
  }

  /* ------------------------------------------------------------- whitelist --- */
  {
    const sent = [], l = new Link((f) => sent.push(f), { allow: INFO_ONLY, timeout: 20 });
    const e1 = await l.request(CMD.SET, { scope: 0, track: NONE, id: 1, value: 3 }).catch((x) => x);
    l.allow = null;
    const e2 = await l.request(0, {}).catch((x) => x), e3 = await l.request(0x7f, {}).catch((x) => x);
    const e4 = await l.request(CMD.SET, { scope: 0, track: NONE, id: 1, value: 99999 }).catch((x) => x);
    ok(/not allowed/.test(e1.message) && e2 instanceof P.ProtoError && e3 instanceof P.ProtoError && e4 instanceof P.ProtoError
      && sent.length === 0, "before identifying only INFO; unknown commands and bad values never leave");
    l.close();
  }
  {
    const fel = dev({ name: "Felucca" }), other = dev({ name: "Launchpad X" }), m = dev();
    const r = await connect(access(fel, other, m), { timing: { ...FAST, timeout: 300 } });
    const s = r.session, snd = new SoundModel(s);
    await snd.load();
    await snd.set(SCOPE.TRACK, ID.MODEL, 3);
    await snd.set(SCOPE.GLOBAL, 0, 100);
    await s.select(4);
    await until(() => !snd.loading);
    m.turn(4, m.P.E0, 5);
    await until(() => !snd.loading && s.link.idle);
    const all = m.received;
    const firstReply = 1; // INFO's reply came before anything else went out
    ok(all.length > 50 && all.every((f) => editorFrame(f) && P.SCHEMA[P.decodeRequest(f).cmd] && f[5] < 0x7f)
      && isInfo(all[0]) && all.slice(firstReply).every((f) => P.decodeRequest(f).cmd !== CMD.INFO || f === all[0]),
      `a whole session: ${all.length} frames, every one F0 7D 48 54 from the table, INFO first`);
    ok(fel.received.length === 0 && other.received.length === 0, "the Felucca port and other gear got nothing");
    s.close();
    snd.destroy();
    const src = ["app.mjs", "session.mjs", "link.mjs", "header.mjs", "ui.mjs", ...readdirSync(HERE).filter((f) => /^tab_.*\.mjs$/.test(f))]
      .map((f) => [f, readFileSync(join(HERE, f), "utf8")]);
    const senders = src.filter(([, t]) => /\.send\(/.test(t)).map(([f]) => f);
    const encoders = src.filter(([, t]) => /encodeRequest\(/.test(t)).map(([f]) => f);
    ok(senders.join() === "session.mjs" && encoders.join() === "link.mjs" && !src.some(([, t]) => /0xf0\s*,\s*0x(00|22)/i.test(t)),
      "one sender (session.mjs -> Link), one encoder (link.mjs -> encodeRequest), no raw update / key frames");
  }

  /* ------------------------------------------------------------- the queue --- */
  {
    const sent = [], pushes = [], timeouts = [];
    const l = new Link((f) => sent.push(P.decodeRequest(f)), { timeout: 20, tries: 3, onPush: (p) => pushes.push(p),
      onTimeout: (c) => timeouts.push(c) });
    const reply = (q, body) => l.receive(P.encodeReply(q.cmd, q.seq, 0, body));
    const a = l.request(CMD.WATCH, { on: 1 }), b = l.request(CMD.SELECT, { track: 2 }), c = l.request(CMD.SELECT, { track: 3 });
    ok(sent.length === 1 && sent[0].cmd === CMD.WATCH && sent[0].seq === 0, "one request in flight");
    l.receive(P.encodeReply(CMD.WATCH, 5, 0, { on: 1 })); // another seq: not its reply
    l.receive(P.encodePush(PUSH.STEPS_CHANGED, { slot: 0, var: 0, track: 1 }));
    l.receive([0xf8]); // clock
    ok(sent.length === 1 && pushes.length === 1 && pushes[0].cmd === PUSH.STEPS_CHANGED, "a wrong seq is ignored; a push between is delivered");
    reply(sent[0], { on: 1 });
    ok((await a).on === 1 && sent.length === 2 && sent[1].seq === 1 && sent[1].cmd === CMD.SELECT, "the reply frees the queue: next seq");
    const t0 = Date.now();
    const eb = await b.catch((x) => x);
    ok(eb instanceof TimeoutError && sent.length === 5 && sent.slice(1, 4).every((x) => x.seq === 1 && x.args.track === 2)
      && timeouts.length === 1 && Date.now() - t0 >= 55, "no reply: 3 tries of the same frame (same seq), then a timeout");
    reply(sent[4], { track: 3 });
    ok((await c).track === 3 && sent[4].seq === 2, "the next request takes the next seq");
    l.seq = 125;
    const d = l.request(CMD.SELECT, { track: 1 });
    reply(sent[5], { track: 1 });
    await d;
    const e = l.request(CMD.SELECT, { track: 1 });
    ok(sent[5].seq === 126 && sent[6].seq === 0, "seq wraps 126 -> 0 (7F is the push mark)");
    const k1 = l.request(CMD.SET, { scope: 0, track: NONE, id: 2, value: 10 }, { key: "k" });
    const k2 = l.request(CMD.SET, { scope: 0, track: NONE, id: 2, value: 20 }, { key: "k" });
    reply(sent[6], { track: 1 });
    await e;
    ok(sent.length === 8 && sent[7].args.value === 20 && k1 === k2, "queued SETs of one knob coalesce: the latest value goes");
    l.receive(P.encodeReply(CMD.SET, sent[7].seq, 2, { detail: 18, msg: "ACID ON 2 TRACKS" }));
    const ek = await k1.catch((x) => x);
    ok(ek instanceof DeviceError && ek.rc === 2 && ek.detail === 18 && ek.message === "ACID ON 2 TRACKS", "rc != 0: the device's message");
    l.request(CMD.SAVE, { slot: NONE }).catch(() => {});
    await sleep(30);
    ok(sent.length === 9, "SAVE waits its long timeout");
    const piece = P.encodeReply(CMD.SAVE, sent[8].seq, 0, { slot: 0, state: { playing: 0, slot: 0, var: 0, qslot: 127, qvar: 127,
      sel: 0, flags: 0, used: 1, dirty: 0, vmask: [1, 1, 1, 1] } });
    l.receive(piece.slice(0, 10)); // a SysEx in two pieces
    l.receive(piece.slice(10));
    ok(!l.cur, "a reply in pieces is joined");
    l.close();
  }
  {
    const m = dev(), r = await connect(access(m), { timing: { ...FAST, timeout: 200 } }), s = r.session;
    m.faults.noReply = 2;
    const n = m.received.length, got = await s.request(CMD.SELECT, { track: 6 });
    const tries = m.received.slice(n);
    ok(got.track === 6 && tries.length === 3 && new Set(tries.map((f) => f[5])).size === 1 && m.sel === 6 && !s.busy,
      "two lost frames: the third try (same seq) is answered");
    s.close();
  }

  /* ------------------------------------------------------------- the Sound tab --- */
  {
    const m = dev(), r = await connect(access(m), { timing: { ...FAST, timeout: 300 } }), s = r.session;
    const snd = new SoundModel(s), errors = [];
    snd.on("error", (x) => errors.push(x));
    await snd.load();
    const titles = snd.groups.map((g) => g.title), byTitle = (t) => snd.groups.find((g) => g.title === t);
    const labelOf = (scope, id) => snd.descOf(scope, id)?.label;
    const valuesMatch = () => m.tracks[m.sel].p.every((v, id) => snd.value(SCOPE.TRACK, id)?.value === v)
      && m.g.every((v, id) => snd.value(SCOPE.GLOBAL, id)?.value === v);
    ok(["SOUND", "LAYER", "FILTER", "FX", "INSERT", "SLICER", "RESON", "LFO 1", "LFO 2", "DEST 1", "DLY", "GLOBAL"].every((t) => titles.includes(t))
      && !titles.includes("SONG") && byTitle("SOUND").ids.length > 8, `pages from PAGES, merged by title (${titles.length}: ${titles.join(" ")})`);
    ok(byTitle("INSERT").ids.includes(m.P.SRR) && labelOf(SCOPE.TRACK, m.P.SRR) === "SRR" && byTitle("INSERT").ids.includes(m.P.ITYPE)
      && byTitle("DLY").scope === SCOPE.GLOBAL && byTitle("DLY").ids.includes(m.G.DMODE)
      && snd.descOf(SCOPE.GLOBAL, m.G.DMODE).names.join() === "NORM,PING", "SRR, INSERT and DLY MODE appear by themselves");
    const lfo = (n) => byTitle(`LFO ${n}`).ids, lfoNames = (n) => lfo(n).map((id) => data.P[id]).join();
    ok(!titles.includes("LFO") && lfoNames(1) === "LFO1_WAVE,LFO1_RATE,LFO1_MORPH,LFO1_PHASE,LFO1_TRIG,LFO1_MODE"
      && lfoNames(2) === lfoNames(1).replaceAll("LFO1", "LFO2") && P.GR.LFO === data.gr.LFO
      && new Set(lfo(1).map((id) => labelOf(SCOPE.TRACK, id))).size === 6, "LFO: two groups, LFO 1 and LFO 2, each knob once");
    ok(ID.MODEL === m.P.MODEL && ID.RMODEL === m.P.RMODEL && ID.ITYPE === m.P.ITYPE && ID.GMODE === m.G.GMODE,
      "the ids the page names = core.h (device_data.json)");
    const md = snd.descOf(SCOPE.TRACK, ID.MODEL);
    ok(md.names.length === data.models.length && md.order && md.order.length === data.models.length
      && snd.value(SCOPE.TRACK, ID.MODEL).text === data.models[m.tracks[m.sel].p[0]].name, "MODEL: every name, the device's order");
    ok(snd.track === m.sel && valuesMatch() && snd.value(SCOPE.TRACK, m.P.E0).text !== "" && snd.value(SCOPE.GLOBAL, m.G.BPM).unit === "BPM",
      "values = the device's, with its texts");

    /* the device -> the page */
    m.turn(m.sel, m.P.E0 + 1, 99);
    await until(() => snd.value(SCOPE.TRACK, m.P.E0 + 1).value === 99);
    ok(snd.value(SCOPE.TRACK, m.P.E0 + 1).value === 99 && snd.value(SCOPE.TRACK, m.P.E0 + 1).text === "99", "a knob turned on the FM-1 moves here (CHANGED)");
    m.turnGlobal(m.G.BPM, 133);
    await until(() => snd.value(SCOPE.GLOBAL, m.G.BPM).value === 133);
    ok(snd.value(SCOPE.GLOBAL, m.G.BPM).text === "133", "a global turned on the FM-1 moves here");
    m.select(3);
    await until(() => snd.track === 3 && !snd.loading);
    ok(snd.track === 3 && s.state.sel === 3 && valuesMatch() && headerModel(s).sel === 3, "a track selected on the FM-1: the page follows (STATE)");
    const other = data.models.findIndex((x) => x.name === "K909");
    m.turn(3, m.P.MODEL, other);
    await until(() => !snd.loading && snd.value(SCOPE.TRACK, 0).value === other);
    await until(() => !snd.loading && labelOf(SCOPE.TRACK, m.P.E0 + 1) === data.models[other].edit[1].label);
    ok(snd.value(SCOPE.TRACK, 0).text === "K909" && [0, 1, 2, 3].every((k) => labelOf(SCOPE.TRACK, m.P.E0 + k) === data.models[other].edit[k].label)
      && valuesMatch(), "the MODEL changed on the FM-1: SOUND knobs described again");

    /* the page -> the device */
    const sv = await snd.set(SCOPE.TRACK, m.P.E0 + 2, 50);
    ok(m.tracks[3].p[m.P.E0 + 2] === 50 && sv.text === snd.value(SCOPE.TRACK, m.P.E0 + 2).text && snd.edited === 1, "SET: the device's value and text");
    await snd.set(SCOPE.TRACK, m.P.E0 + 2, 9999);
    ok(snd.value(SCOPE.TRACK, m.P.E0 + 2).value === m.tracks[3].p[m.P.E0 + 2] && m.tracks[3].p[m.P.E0 + 2] <= 127, "SET out of range: shows what the device kept");
    await s.select(5);
    await until(() => snd.track === 5 && !snd.loading);
    ok(m.sel === 5 && snd.track === 5 && valuesMatch() && headerModel(s).sel === 5, "a track chosen here is selected on the FM-1");
    const acid = data.consts.ACID;
    await snd.set(SCOPE.TRACK, ID.MODEL, acid);
    await until(() => !snd.loading);
    ok(m.tracks[5].p[0] === acid && snd.value(SCOPE.TRACK, 0).text === "ACID" && labelOf(SCOPE.TRACK, m.P.E0) === data.models[acid].edit[0].label
      && valuesMatch() && snd.preset === data.presets.findIndex((x) => x.model === acid) && snd.edited === 0,
    "the MODEL changed here: the model's first factory preset, the track and its SOUND page follow");
    m.g[m.G.CLOCK] = 1;
    const bpm = m.g[m.G.BPM], rb = await snd.set(SCOPE.GLOBAL, m.G.BPM, bpm + 7);
    m.g[m.G.CLOCK] = 0;
    ok(rb.rc === 0 && snd.value(SCOPE.GLOBAL, m.G.BPM).value === bpm && m.g[m.G.BPM] === bpm,
      "SET BPM while following a clock: rc 0, the page shows the value the device kept");
    m.tracks[6].p[0] = acid;
    await s.select(7);
    await until(() => snd.track === 7 && !snd.loading);
    const before = snd.value(SCOPE.TRACK, 0).value;
    const ra = await snd.set(SCOPE.TRACK, ID.MODEL, acid);
    ok(ra === null && errors.includes("ACID ON 2 TRACKS") && snd.value(SCOPE.TRACK, 0).value === before && m.tracks[7].p[0] === before,
      "a refusal: the device's message, the value as the device kept it");
    m.turnGlobal(m.G.GMODE, 1);
    await until(() => !snd.loading && byTitle("GRIDS")?.ids.includes(m.G.GLEN1));
    ok(byTitle("GRIDS").ids.includes(m.G.GLEN1), "G_GMODE changed: PAGES read again (GRIDS shows LEN)");

    /* the header */
    m.play();
    await until(() => headerModel(s).playing);
    const hm = headerModel(s);
    ok(hm.play === "PLAY" && hm.slot === "A*" && hm.variation === "VAR 1" && hm.slots[0].dirty && hm.slots[3].used === false
      && hm.firmware === "0.28.0" && hm.status === "", "header: PLAY, A* (unsaved), VAR 1, D empty");
    m.stop();
    const fake = (flags, busy) => headerModel({ busy, info: { firmware: "0.28.0" }, state: { ...s.state, flags } });
    ok(fake(SF.RESTORING, false).status === "RESTORING" && fake(0, true).status === BUSY_MSG && fake(SF.SONG, false).status === "SONG",
      "header: RESTORING, FM-1 busy (update?), SONG");

    /* the update silence */
    const busy = [], resync = [];
    s.on("busy", (x) => busy.push(x));
    s.on("resync", () => resync.push(1));
    await until(() => s.link.idle && !snd.loading);
    m.updateFrame(); // update traffic: the device is silent 10 s
    const n = m.received.length;
    const lost = await snd.set(SCOPE.TRACK, m.P.E0, 1);
    ok(lost === null && s.busy && busy[0] === true && headerModel(s).status === BUSY_MSG, "update traffic: no answer -> FM-1 busy (update?)");
    const rej = await s.request(CMD.GET, { scope: 0, track: NONE, id: 0, count: 1 }).catch((x) => x);
    await until(() => m.received.length >= n + FAST.tries + 2); // INFO, INFO ... (300 ms each here, 25 ms apart)
    ok(rej.message === BUSY_MSG && m.received.slice(n, n + FAST.tries).every((f) => f[4] === CMD.SET)
      && m.received.slice(n + FAST.tries).every(isInfo) && m.received.length >= n + FAST.tries + 2,
      "busy: nothing but INFO goes out, after the silence");
    m.advance(P.TIMING.silence);
    await until(() => !s.busy && resync.length && !snd.loading, 2000);
    m.turn(m.sel, m.P.E0 + 3, 7);
    await until(() => snd.value(SCOPE.TRACK, m.P.E0 + 3).value === 7);
    ok(!s.busy && busy.join() === "true,false" && resync.length === 1 && valuesMatch(), "the FM-1 answers again: resync, WATCH, values read again");
    s.close();
    snd.destroy();
  }
  return { passed, failed };
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const { passed, failed } = await runPageTests();
  console.log(failed ? `PAGE TESTS FAILED (${failed} of ${passed + failed})` : `page tests passed (${passed})`);
  process.exit(failed ? 1 : 0);
}
