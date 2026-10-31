// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// One editor per browser. Two tabs talking to one FM-1 would each think they own it: the restore lock is per page and
// the device's duplicate rule is keyed by (cmd, seq) only, so one tab's request could be taken for the other's retry.
// The page takes a Web Lock (navigator.locks) named "hortator-editor" when it opens and keeps it; a second tab finds it
// held and shows "The editor is open in another tab" with "Use here", which asks the holder (BroadcastChannel) to let
// go: it disconnects (its session closed, its views torn down) and releases the lock, then the new tab has it. A
// holder that does not answer in time (frozen, throttled) has its lock stolen (Web Locks `steal`): it learns it from
// its lock request failing and disconnects too. Without Web Locks, the BroadcastChannel alone: "who?" answered by the
// holder; without either, every tab may connect (as before). No DOM here: web/editor/test_page.mjs drives it.
export const LOCK_NAME = "hortator-editor";
const ASK_MS = 150; // BroadcastChannel only: how long "who?" waits for the holder's answer

export class SingleTab {
  /** opts: locks (a LockManager; default navigator.locks), Channel (default BroadcastChannel), name, onLost() (another
   * tab took the editor: disconnect now; release() follows by itself), id (this tab's, for its messages) */
  constructor(opts = {}) {
    this.locks = opts.locks !== undefined ? opts.locks : globalThis.navigator?.locks || null;
    const Ch = opts.Channel !== undefined ? opts.Channel : globalThis.BroadcastChannel || null;
    this.name = opts.name || LOCK_NAME;
    this.onLost = opts.onLost || (() => {});
    this.id = opts.id || Math.random().toString(36).slice(2);
    this.held = false;
    this.letGo = null; // resolves the held lock's callback: the lock is released
    this.waiters = new Set();
    this.ch = Ch ? new Ch(`${this.name}-tabs`) : null;
    if (this.ch) this.ch.onmessage = (e) => this.message(e.data || {});
  }
  get mode() { return this.locks ? "locks" : this.ch ? "channel" : "none"; }

  /** take the editor for this tab if no other tab holds it: true (held now) or false (another tab has it) */
  async claim() {
    if (this.held) return true;
    if (this.locks) return this.lock({ ifAvailable: true });
    if (this.ch) {
      const here = await this.ask({ t: "who" }, (m) => m.t === "here", ASK_MS);
      if (here) return false;
    }
    this.held = true;
    return true;
  }
  /** "Use here": the other tab disconnects and lets go (or, after ms without its answer, the lock is taken from it) */
  async takeOver(ms = 3000) {
    if (this.held) return true;
    if (!this.ch && !this.locks) { this.held = true; return true; }
    if (this.locks) {
      this.post({ t: "release" });
      const ctl = typeof AbortController === "function" ? new AbortController() : null;
      const t = ctl && setTimeout(() => ctl.abort(), ms);
      const got = await this.lock(ctl ? { signal: ctl.signal } : {});
      clearTimeout(t);
      return got || this.lock({ steal: true });
    }
    await this.ask({ t: "release" }, (m) => m.t === "released", ms);
    this.held = true;
    return true;
  }
  /** let another tab have it (this tab disconnected) */
  release() {
    if (!this.held) return;
    this.held = false;
    const f = this.letGo;
    this.letGo = null;
    f?.();
    if (!this.locks) this.post({ t: "released" });
  }
  close() { this.release(); this.ch?.close(); this.ch = null; }

  /* ------------------------------------------------------------------------------------------------------------- */
  lock(opt) { // the Web Lock, held until release(): resolves true when held, false when not (ifAvailable, aborted)
    return new Promise((res) => {
      let granted = false;
      this.locks.request(this.name, { mode: "exclusive", ...opt }, (lock) => {
        if (!lock) { res(false); return undefined; }
        granted = true;
        this.held = true;
        res(true);
        return new Promise((done) => { this.letGo = done; });
      }).catch(() => { // aborted while waiting (takeOver's timeout), or stolen from this tab by another's takeOver
        if (!granted) { res(false); return; }
        if (this.held) this.lost();
      });
    });
  }
  lost() {
    if (!this.held) return;
    try { this.onLost(); } finally { this.release(); }
  }
  message(m) {
    if (m.from === this.id) return;
    for (const w of this.waiters) w(m);
    if (!this.held) return;
    if (m.t === "who") this.post({ t: "here" });
    else if (m.t === "release") this.lost();
  }
  post(m) { try { this.ch?.postMessage({ ...m, from: this.id }); } catch { /* closed */ } }
  ask(m, match, ms) { // post m, resolve with the first answer that matches, or null after ms
    return new Promise((res) => {
      const w = (x) => { if (match(x)) { done(); res(x); } };
      const t = setTimeout(() => { done(); res(null); }, ms);
      const done = () => { clearTimeout(t); this.waiters.delete(w); };
      this.waiters.add(w);
      this.post(m);
    });
  }
}
