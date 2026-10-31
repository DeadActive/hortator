// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// Web MIDI connect and port handling after upstream Felucca's editor page (hugelton/Felucca 5d9694d,
// web/editor.html; GPL-3.0-only, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments).
//
// Connecting (web/EDITOR_PROTOCOL.md §7): only ports named "Hortator" are asked anything, and only INFO until the
// answer says Hortator editor protocol 1; a "Felucca" port, a stock FM-1 or other gear get nothing. A Session is one
// identified FM-1: its state block (STATE pushes, replies that carry it), the pushes as events, the WATCH keep-alive,
// and the update silence (§4 rule 4: a request unanswered -> "FM-1 busy (update?)", nothing for the device's 10 s,
// then one INFO at a time backing off 10 s -> 60 s, none after 2 min busy ("asleep": the user's Reconnect asks again;
// an update in progress holds one SysEx frame on the FM-1, so the page stays out of its way); answered -> "resync":
// every view reads again). No DOM here: node tests drive it against the mock.
import { CMD, PUSH, SCOPE, TIMING, checkInfo, classifyPort, has } from "./proto.mjs";
import { INFO_ONLY, Link } from "./link.mjs";

export const LINKS = {
  installer: { text: "Open the installer", href: "../installer/" },
  releases: { text: "Hortator releases", href: "https://github.com/DeadActive/hortator/releases" },
  felucca: { text: "Felucca", href: "https://github.com/hugelton/Felucca" },
};
export const BUSY_MSG = "FM-1 busy (update?)";
export const ASLEEP_MSG = "FM-1 busy: Reconnect when the update is done";
const restoreBusy = (l) => `Writing ${l.what} to the FM-1 (${l.owner} tab).`;
const REFUSAL = {
  nomidi: { message: "This browser has no Web MIDI. Use Chrome, Edge or Opera (desktop or Android), or Firefox.", links: [] },
  denied: { message: "MIDI access was refused. Allow MIDI (with SysEx) for this page, then connect again.", links: [] },
  none: { message: "No Hortator FM-1 found. Connect it by USB (Bluetooth is not an editor link), then connect again.",
    links: [LINKS.installer] },
  felucca: { message: "This FM-1 runs Felucca: use Felucca's editor. Nothing was sent to it.", links: [LINKS.felucca] },
  noeditor: { links: [LINKS.installer] },
  older: { links: [LINKS.installer] },
  newer: { links: [LINKS.releases] },
  magic: { links: [] },
};
/** a refusal to show: { why, message, links } */
export const refusal = (why, message) => ({ ok: false, why, message: message || REFUSAL[why].message, links: REFUSAL[why]?.links || [] });

export const G_SYNC = 12; // core.h G_SYNC: MIDI clock out (OFF, USB, BLE, BOTH); device_data.json (the tests compare)
export const JITTER_MSG = "Clock out may jitter during the transfer — stop first?";
export const JITTER_CHOICES = ["Stop", "Continue", "Cancel"];
/** Before a big transfer (EDITOR_PROTOCOL.md §4 rule 8: a 306-byte frame holds MIDI clock out ~7 ms): while the FM-1
 * plays with SYNC out on, ask choose(JITTER_MSG, JITTER_CHOICES). Stop: protocol 1 has no transport command, so the
 * player is told to press STOP on the FM-1 (notify) and the transfer starts once a STATE push says it stopped (none
 * within waitMs: cancelled). Resolves true to go on, false to drop the transfer. */
export async function clockGuard(session, choose, notify = () => {}, waitMs = 60000) {
  if (!(await session.clockAtRisk())) return true;
  const c = await choose(JITTER_MSG, JITTER_CHOICES);
  if (c === "Continue") return true;
  if (c !== "Stop") return false;
  notify("Press STOP on the FM-1: the transfer starts when it stops.");
  const stopped = await session.untilStopped(waitMs);
  if (!stopped) notify("The FM-1 kept playing: nothing was transferred.");
  return stopped;
}
const live = (p) => p.state !== "disconnected";
/** the access' ports by kind: hortator [{ name, input, output }], felucca [names], other [names] */
export function listPorts(access) {
  const ins = [...access.inputs.values()].filter(live), outs = [...access.outputs.values()].filter(live);
  const out = { hortator: [], felucca: [], other: [] }, used = new Set();
  for (const o of outs) {
    const kind = classifyPort(o.name);
    if (kind !== "hortator") { (kind === "felucca" ? out.felucca : out.other).push(o.name); continue; }
    const i = ins.find((x) => !used.has(x) && x.name === o.name)
      ?? ins.find((x) => !used.has(x) && classifyPort(x.name) === "hortator");
    if (!i) continue;
    used.add(i);
    out.hortator.push({ name: o.name, input: i, output: o });
  }
  for (const i of ins) if (!used.has(i) && classifyPort(i.name) === "felucca" && !out.felucca.includes(i.name)) out.felucca.push(i.name);
  return out;
}

/** connect to the first Hortator port that identifies itself: { ok: true, session } or a refusal */
export async function connect(access, opts = {}) {
  const ports = listPorts(access);
  if (!ports.hortator.length) return refusal(ports.felucca.length ? "felucca" : "none");
  let last;
  for (const p of ports.hortator) {
    const s = new Session(access, p, opts);
    const r = await s.identify();
    if (r.ok) { await s.start(); return { ok: true, session: s, port: p.name }; }
    s.close();
    last = { ...refusal(r.why, r.message), port: p.name };
  }
  return last;
}

export class Session {
  constructor(access, port, opts = {}) {
    this.access = access;
    this.port = port;
    this.timer = opts.timer || globalThis;
    this.timing = { ...TIMING, ...opts.timing };
    this.handlers = new Map();
    this.info = null;
    this.state = null;
    this.busy = false;
    this.asleep = false; // busy, and the page stopped asking (TIMING.giveUp): only reconnect() asks again
    this.connected = false;
    this.restoring = null; // the restore lock: { owner, what } while a tab runs RESTORE BEGIN .. COMMIT / ABORT
    this.link = new Link((f) => port.output.send(f), { timer: this.timer, timeout: this.timing.timeout,
      longTimeout: this.timing.longTimeout, tries: this.timing.tries, allow: INFO_ONLY, onPush: (p) => this.push(p),
      onTimeout: () => this.silent(), onSend: opts.onSend });
    port.input.onmidimessage = (e) => this.link.receive(e.data);
  }
  on(ev, fn) {
    if (!this.handlers.has(ev)) this.handlers.set(ev, new Set());
    this.handlers.get(ev).add(fn);
    return () => this.handlers.get(ev).delete(fn);
  }
  emit(ev, x) { for (const fn of this.handlers.get(ev) || []) { try { fn(x); } catch (e) { console.error(e); } } }

  /** INFO (its tries), the answer checked: { ok } or { ok: false, why, message }. Only INFO can go out before. */
  async identify() {
    let info = null;
    try { info = await this.link.request(CMD.INFO, {}, { raw: true, quiet: true }); } catch (e) { if (!e.timeout) throw e; }
    const c = checkInfo(info);
    if (!c.ok) return c;
    this.info = info;
    this.state = info.state;
    this.link.allow = null;
    this.connected = true;
    return c;
  }
  async start() {
    const prev = this.access.onstatechange;
    this.access.onstatechange = (e) => {
      if (prev) prev(e);
      if (e.port && (e.port === this.port.output || e.port === this.port.input) && e.port.state === "disconnected") this.close("unplugged");
    };
    this.keepAlive();
    await this.request(CMD.WATCH, { on: 1 }).catch(() => {});
  }
  keepAlive() {
    this.timer.clearInterval?.(this.ka);
    this.ka = this.timer.setInterval(() => {
      if (!this.connected || this.busy || !this.link.idle || Date.now() - this.link.lastSent < this.timing.keepAlive * 0.9) return;
      this.request(CMD.WATCH, { on: 1 }).catch(() => {});
    }, Math.max(10, this.timing.keepAlive / 2));
  }
  /** what this firmware has (INFO caps) */
  has(cmd) { return !!this.info && has(this.info, cmd); }
  /** a request through the one queue; replies carrying the state block update it */
  request(cmd, args = {}, opt = {}) {
    if (!this.connected) return Promise.reject(new Error("Not connected."));
    if (this.busy) return Promise.reject(new Error(BUSY_MSG));
    if (!this.has(cmd)) return Promise.reject(new Error("Not in this firmware."));
    return this.link.request(cmd, args, opt).then((r) => {
      if (r.state) this.setState(r.state);
      if (cmd === CMD.SELECT && !r.rc) this.setState({ ...this.state, sel: r.track });
      return r;
    });
  }
  /** The restore lock: the FM-1 runs one restore at a time and a second RESTORE BEGIN cancels the first, so every tab
   * that restores takes this first. owner: the tab's id; what: words for the others ("the user presets"). Returns the
   * release function; throws when another owner holds it. Emits "restore" ({ owner, what } or null). */
  lockRestore(owner, what) {
    if (this.restoring) throw new Error(`${restoreBusy(this.restoring)} Wait for it to finish.`);
    const lock = { owner, what };
    this.restoring = lock;
    this.emit("restore", lock);
    return () => { if (this.restoring === lock) { this.restoring = null; this.emit("restore", null); } };
  }
  /** why owner cannot restore now ("" = it can): another tab's restore runs */
  restoreBlocked(owner) { return this.restoring && this.restoring.owner !== owner ? restoreBusy(this.restoring) : ""; }
  /** a tab changed what the FM-1 holds in a way no push reports to the other views: { track } (a preset loaded onto
   * it), { slot } (the slot restored and loaded again) or { presets: true } (the user presets restored). Views showing
   * it read it again (event "reload"). */
  reloaded(what) { if (this.connected && !this.busy) this.emit("reload", what); }
  /** playing with SYNC (MIDI clock out) on: a big transfer may make the clock jitter (clockGuard) */
  async clockAtRisk() {
    if (!this.state?.playing || !this.connected || this.busy) return false;
    try {
      const r = await this.request(CMD.GET, { scope: SCOPE.GLOBAL, track: 0, id: G_SYNC, count: 1 });
      return !!r.values?.[0]?.value;
    } catch { return false; }
  }
  /** true once the FM-1 is stopped (now or by a STATE push), false after ms or when the session ends */
  untilStopped(ms = 60000) {
    if (!this.state?.playing) return Promise.resolve(true);
    return new Promise((res) => {
      const done = (v) => { offS(); offC(); this.timer.clearTimeout(t); res(v); };
      const offS = this.on("state", ({ state }) => { if (!state.playing) done(true); });
      const offC = this.on("closed", () => done(false));
      const t = this.timer.setTimeout(() => done(false), ms);
    });
  }
  /** one selection, the device's: choosing a track here selects it on the FM-1 */
  select(track) { return this.request(CMD.SELECT, { track }); }
  setState(st) {
    const prev = this.state;
    if (prev && JSON.stringify(prev) === JSON.stringify(st)) return;
    this.state = st;
    this.emit("state", { state: st, prev });
  }
  push(p) {
    if (!this.connected || this.busy) return;
    if (p.cmd === PUSH.STATE) this.setState(p.state);
    else if (p.cmd === PUSH.CHANGED) this.emit("changed", p);
    else if (p.cmd === PUSH.STEPS_CHANGED) this.emit("steps", p);
    else if (p.cmd === PUSH.LOCKS_CHANGED) this.emit("locks", p);
  }
  /** a request went unanswered after its tries: the update silence (or the FM-1 went away). Nothing goes out for the
   * device's silence, then one INFO (one try) at a time, the waits doubling from TIMING.backoff to backoffMax, the
   * last at TIMING.giveUp after the silence began; then nothing ("asleep") until reconnect() */
  silent() {
    if (!this.connected || this.busy) return;
    this.busy = true;
    this.link.allow = INFO_ONLY;
    this.link.clear(BUSY_MSG);
    this.emit("busy", true);
    this.backoffFrom(this.timing.silence);
  }
  backoffFrom(first) {
    this.asleep = false;
    this.busyFor = 0; // ms of waits scheduled since the silence (or a Reconnect) began
    this.backoffMs = this.timing.backoff;
    this.probeAt(first);
  }
  probeAt(ms) {
    this.timer.clearTimeout(this.probeT);
    this.busyFor += ms;
    this.probeT = this.timer.setTimeout(() => this.probe(), ms);
  }
  /** "Reconnect": the page stopped asking (asleep) or the user does not want to wait: INFO now, then backing off again */
  reconnect() {
    if (!this.connected || !this.busy || this.probing) return;
    if (this.asleep) this.emit("asleep", false);
    this.backoffFrom(0);
  }
  async probe() {
    if (!this.connected || this.probing) return;
    let info = null;
    this.probing = true;
    try { info = await this.link.request(CMD.INFO, {}, { raw: true, quiet: true, tries: 1 }); } catch { /* still silent */ }
    this.probing = false;
    if (!this.connected) return;
    if (!info) {
      const next = Math.min(this.backoffMs, this.timing.backoffMax, this.timing.giveUp - this.busyFor);
      if (next <= 0) { this.asleep = true; this.emit("asleep", true); return; } // no more: Reconnect asks again
      this.backoffMs = Math.min(this.backoffMs * 2, this.timing.backoffMax);
      this.probeAt(next);
      return;
    }
    const c = checkInfo(info);
    if (!c.ok) { this.emit("refused", refusal(c.why, c.message)); this.close("refused"); return; } // updated to another major
    this.info = info;
    this.busy = false;
    this.asleep = false;
    this.link.allow = null;
    this.emit("busy", false);
    this.setState(info.state);
    await this.request(CMD.WATCH, { on: 1 }).catch(() => {});
    this.emit("resync");
  }
  close(why = "closed") {
    const was = this.connected;
    this.connected = false;
    this.timer.clearInterval?.(this.ka);
    this.timer.clearTimeout(this.probeT);
    this.link.close();
    if (this.port.input.onmidimessage) this.port.input.onmidimessage = null;
    if (was) this.emit("closed", why);
  }
}
