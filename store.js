// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The simulator's flash in IndexedDB (object store "flash", key = sector offset, value = 4096 bytes). Sectors the
// firmware wrote wait in `pending` until a write transaction completes: a lost connection is reopened once, a
// failure leaves them for the next write and says so, so no save is dropped silently. `open` returns an IDBDatabase.
export class FlashStore {
  constructor(open, note) {
    this.open = open;
    this.note = note;
    this.db = null;
    this.pending = new Map();
    this.closed = false;                           // after clear(): Reset to demos reloads; late writes are dropped
    this.busy = false;
    this.again = false;
    this.said = '';
  }

  say(msg) { if (msg !== this.said) { this.said = msg; this.note(msg); } }

  async connect() {
    const db = await this.open();
    db.onclose = () => { if (this.db === db) this.db = null; };
    db.onversionchange = () => { db.close?.(); if (this.db === db) this.db = null; };
    return db;
  }

  async load() {                                   // -> [[offset, Uint8Array], ...]
    try {
      this.db = await this.connect();
      return await new Promise((resolve, reject) => {
        const store = this.db.transaction('flash').objectStore('flash');
        const keys = store.getAllKeys(), vals = store.getAll();
        let n = 0;
        const done = () => { if (++n === 2) resolve(keys.result.map((k, i) => [Number(k), new Uint8Array(vals.result[i])])); };
        keys.onsuccess = vals.onsuccess = done;
        keys.onerror = vals.onerror = () => reject(keys.error || vals.error);
      });
    } catch {
      this.db = null;
      this.say('This browser blocks storage here, so saves last until you close the page.');
      return [];
    }
  }

  put(sectors) {                                   // sectors: [{ off, bytes }]
    if (this.closed) return;
    for (const { off, bytes } of sectors) this.pending.set(off, bytes);
    this.flush();
  }

  async flush() {
    if (this.busy) { this.again = true; return; }
    if (this.closed || !this.pending.size) return;
    this.busy = true;
    for (let attempt = 0; attempt < 2 && this.pending.size && !this.closed; attempt++) {
      try {
        if (!this.db) this.db = await this.connect();
        await this.write(new Map(this.pending));
        this.said = '';
      } catch {
        this.db = null;                            // a lost connection: reopen once
        if (attempt === 1) this.say('Saving to this browser failed. Your latest changes will be saved again with the next one.');
      }
    }
    this.busy = false;
    if (this.again) {                              // sectors that arrived meanwhile
      this.again = false;
      this.flush();
    }
  }

  write(batch) {
    return new Promise((resolve, reject) => {
      const tx = this.db.transaction('flash', 'readwrite');
      const store = tx.objectStore('flash');
      for (const [off, bytes] of batch) store.put(bytes, off);
      tx.oncomplete = () => {
        for (const [off, bytes] of batch) if (this.pending.get(off) === bytes) this.pending.delete(off);
        resolve();
      };
      tx.onabort = tx.onerror = () => reject(tx.error);
    });
  }

  async clear() {                                  // Reset to demos: wipe, and drop anything the worklet still sends
    this.closed = true;
    this.pending.clear();
    try {
      const db = this.db ?? await this.connect();
      await new Promise((resolve, reject) => {
        const tx = db.transaction('flash', 'readwrite');
        tx.objectStore('flash').clear();
        tx.oncomplete = resolve;
        tx.onabort = tx.onerror = () => reject(tx.error);
      });
    } catch { /* nothing stored, or storage blocked: the reload starts fresh anyway */ }
  }
}

export function openFlashDb() {
  return new Promise((resolve, reject) => {
    const r = indexedDB.open('fm1-sim', 1);
    r.onupgradeneeded = () => r.result.createObjectStore('flash');
    r.onsuccess = () => resolve(r.result);
    r.onerror = () => reject(r.error);
    r.onblocked = () => reject(new Error('blocked'));
  });
}
