// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// Web MIDI connect and port handling after upstream Felucca's editor page (hugelton/Felucca 5d9694d,
// web/editor.html; GPL-3.0-only, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments).
//
// Connecting (web/EDITOR_PROTOCOL.md §7): only ports named "Hortator" are asked anything, and only INFO until the
// answer says Hortator editor protocol 1; a "Felucca" port, a stock FM-1 or other gear get nothing. A Session is one
// identified FM-1: its state block (STATE pushes, replies that carry it), the pushes as events, the WATCH keep-alive,
// and the update silence (§4 rule 4: a request unanswered -> "FM-1 busy (update?)", nothing for 10 s, then INFO every
// 2 s until it answers, then "resync": every view reads again). No DOM here: node tests drive it against the mock.
import { CMD, PUSH, TIMING, checkInfo, classifyPort, has } from "./proto.mjs";
import { INFO_ONLY, Link } from "./link.mjs";

export const LINKS = {
  installer: { text: "Open the installer", href: "../installer/" },
  releases: { text: "Hortator releases", href: "https://github.com/DeadActive/hortator/releases" },
  felucca: { text: "Felucca", href: "https://github.com/hugelton/Felucca" },
};
export const BUSY_MSG = "FM-1 busy (update?)";
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
    this.connected = false;
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
  /** a request went unanswered after its tries: the update silence (or the FM-1 went away) */
  silent() {
    if (!this.connected || this.busy) return;
    this.busy = true;
    this.link.allow = INFO_ONLY;
    this.link.clear(BUSY_MSG);
    this.emit("busy", true);
    this.probeAt(this.timing.silence);
  }
  probeAt(ms) {
    this.timer.clearTimeout(this.probeT);
    this.probeT = this.timer.setTimeout(() => this.probe(), ms);
  }
  async probe() {
    if (!this.connected) return;
    let info = null;
    try { info = await this.link.request(CMD.INFO, {}, { raw: true, quiet: true, tries: 1 }); } catch { /* still silent */ }
    if (!this.connected) return;
    if (!info) { this.probeAt(this.timing.retryInfo); return; }
    const c = checkInfo(info);
    if (!c.ok) { this.emit("refused", refusal(c.why, c.message)); this.close("refused"); return; } // updated to another major
    this.info = info;
    this.busy = false;
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
