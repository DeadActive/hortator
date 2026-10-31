// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The request queue after upstream Felucca's editor page (hugelton/Felucca 5d9694d, web/editor.html `Link`;
// GPL-3.0-only, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments).
//
// One request in flight (web/EDITOR_PROTOCOL.md §4 rules 1-2): every request takes the next seq (0..126), waits for
// the reply with its cmd and seq, is sent again with the same seq after its timeout (300 ms; SAVE / RESTORE COMMIT
// 2 s), 3 tries, then gives up. Pushes (seq 7F) may arrive at any time and go to onPush. Frames come only from
// proto.mjs encodeRequest; `allow` (a set of commands) gates what may be sent at all: before a port has identified
// itself as a Hortator, only INFO.
import { CMD, HDR, NAMES, SEQ_PUSH, TIMING, decodeFrame, encodeRequest, isEditorFrame, timeoutOf } from "./proto.mjs";

export class TimeoutError extends Error {
  constructor(cmd) { super(`The FM-1 did not answer (${NAMES[cmd] || cmd}).`); this.cmd = cmd; this.timeout = true; }
}
/** a reply with rc != 0: the device's message (its own text) */
export class DeviceError extends Error {
  constructor(r) { super(r.msg || `refused (rc ${r.rc}, detail ${r.detail})`); this.rc = r.rc; this.detail = r.detail; this.reply = r; }
}
const noop = () => {};

export class Link {
  /** send(frame: Uint8Array); opts: timeout, longTimeout, tries, allow (Set of cmds | null), timer ({ setTimeout,
   * clearTimeout }), onPush(push), onTimeout(cmd), onSend(frame) */
  constructor(send, opts = {}) {
    this.sendRaw = send;
    this.timer = opts.timer || globalThis;
    this.timeout = opts.timeout ?? TIMING.timeout;
    this.longTimeout = opts.longTimeout ?? TIMING.longTimeout;
    this.tries = opts.tries ?? TIMING.tries;
    this.allow = opts.allow ?? null;
    this.onPush = opts.onPush || noop;
    this.onTimeout = opts.onTimeout || noop;
    this.onSend = opts.onSend || noop;
    this.q = [];
    this.cur = null;
    this.seq = -1;
    this.closed = false;
    this.part = null; // a SysEx message arriving in pieces
    this.lastSent = 0;
  }
  get idle() { return !this.cur && !this.q.length; }
  nextSeq() { this.seq = (this.seq + 1) % SEQ_PUSH; return this.seq; }
  /** a request: resolves with the decoded reply (rc 0), rejects with DeviceError (rc != 0; opt.raw: resolves with
   * it), TimeoutError, or an Error (closed, not allowed, bad arguments). opt: key (a queued request with the same key
   * takes these args instead: the latest value wins), tries, timeout, quiet (no onTimeout), raw */
  request(cmd, args = {}, opt = {}) {
    if (this.closed) return Promise.reject(new Error("Not connected."));
    if (this.allow && !this.allow.has(cmd)) return Promise.reject(new Error(`${NAMES[cmd] || cmd} not allowed now.`));
    try { encodeRequest(cmd, 0, args); } catch (e) { return Promise.reject(e); } // checked now, sent with its seq later
    if (opt.key) {
      const old = this.q.find((x) => x.key === opt.key);
      if (old) { old.args = args; return old.promise; }
    }
    let res, rej;
    const promise = new Promise((a, b) => { res = a; rej = b; });
    this.q.push({ cmd, args, opt, key: opt.key, res, rej, promise, tries: opt.tries ?? this.tries,
      timeout: opt.timeout ?? (timeoutOf(cmd, args) > TIMING.timeout ? this.longTimeout : this.timeout) });
    this.pump();
    return promise;
  }
  pump() {
    if (this.cur || this.closed || !this.q.length) return;
    this.cur = this.q.shift();
    this.cur.seq = this.nextSeq();
    this.fire();
  }
  fire() {
    const c = this.cur;
    if (this.allow && !this.allow.has(c.cmd)) { this.finish(null, new Error(`${NAMES[c.cmd]} not allowed now.`)); return; }
    let f;
    try { f = encodeRequest(c.cmd, c.seq, c.args); } catch (e) { this.finish(null, e); return; }
    if (f[0] !== 0xf0 || f[1] !== HDR[0] || f[2] !== HDR[1] || f[3] !== HDR[2]) { this.finish(null, new Error("not an editor frame")); return; }
    c.tries--;
    this.lastSent = Date.now();
    this.onSend(f);
    try { this.sendRaw(f); } catch (e) { this.finish(null, e); return; }
    c.timer = this.timer.setTimeout(() => {
      if (this.cur !== c) return;
      if (c.tries > 0) { this.fire(); return; } // the same frame again: same seq (the device answers a repeat once)
      if (!c.opt.quiet) this.onTimeout(c.cmd);
      this.finish(null, new TimeoutError(c.cmd));
    }, c.timeout);
  }
  finish(reply, err) {
    const c = this.cur;
    if (!c) return;
    this.timer.clearTimeout(c.timer);
    this.cur = null;
    if (err) c.rej(err);
    else if (reply.rc && !c.opt.raw) c.rej(new DeviceError(reply));
    else c.res(reply);
    this.pump();
  }
  /** drop everything queued (not the request in flight) */
  clear(why = "Cancelled.") {
    for (const x of this.q.splice(0)) x.rej(new Error(why));
  }
  close() {
    this.closed = true;
    this.clear("Not connected.");
    if (this.cur) { const c = this.cur; this.timer.clearTimeout(c.timer); this.cur = null; c.rej(new Error("Not connected.")); }
  }
  /** raw MIDI bytes from the input (Web MIDI gives a whole SysEx message; pieces are joined) */
  receive(data) {
    const d = data instanceof Uint8Array ? data : Uint8Array.from(data);
    if (!d.length) return;
    let f;
    if (d[0] === 0xf0) {
      if (d[d.length - 1] !== 0xf7) { this.part = Array.from(d); return; }
      f = d;
      this.part = null;
    } else if (this.part && d[0] < 0x80) {
      this.part.push(...d);
      if (d[d.length - 1] !== 0xf7) return;
      f = Uint8Array.from(this.part);
      this.part = null;
    } else return; // clock, notes, another device's messages
    if (!isEditorFrame(f)) return;
    let r;
    try { r = decodeFrame(f); } catch { return; } // a damaged frame: the request times out and is sent again
    if (r.push) { if (!r.unknown) this.onPush(r); return; }
    const c = this.cur;
    if (c && r.seq === c.seq && r.cmd === c.cmd) this.finish(r);
  }
}

export const INFO_ONLY = new Set([CMD.INFO]);
