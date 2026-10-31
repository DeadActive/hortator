// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// Drum machine fork: 2026 DEADACTIVE
//
// Node checks of the web pages' JS (no browser, no hardware). Run from the repo root:
//   node web/test_web.mjs
// - editor.html (Hortator's editor, protocol 1): its tab modules, no CDN, and editor/test_page.mjs (the page's
//   logic against editor/mock_device.mjs). The protocol codec and the mock itself: node web/editor/test_editor.mjs
// - fm1pkg.js: productOf and logicalImage on build/felucca.fwsc (skipped without a build)
// - fm1ota.js: a full install and an unplug during the write against a simulated FM-1
// - sim/counter.js: the GoatCounter install count (off without a site code; never blocks or breaks an install)

import { execFileSync } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { logicalImage, productOf } from "./fm1pkg.js";
import { Updater, pack7, unpack7 } from "./fm1ota.js";
import { GC_CODE, counterOn, countInstall, installCount, loadPageCounter } from "./sim/counter.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(64)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const py = (code, ...args) => execFileSync("python3", ["-c", code, ...args], { maxBuffer: 1 << 26 });
const HERE = new URL(".", import.meta.url).pathname;

/* ------------------------------------------------------------ the editor page --- */
// editor.html is Hortator's editor (protocol 1, web/EDITOR_PROTOCOL.md); its logic lives in editor/*.mjs and is
// driven against the mock device by editor/test_page.mjs (connect / refuse, the outgoing whitelist, the queue, the
// Sound tab following the device)
async function editorPage() {
  const html = readFileSync(join(HERE, "editor.html"), "utf8"), app = readFileSync(join(HERE, "editor/app.mjs"), "utf8");
  const tabs = [...app.matchAll(/import \{ tab as (\w+) \} from "\.\/(tab_\w+\.mjs)"/g)];
  const mods = await Promise.all(tabs.map(([, , f]) => import(`./editor/${f}`)));
  ok(tabs.map((x) => x[1]).join() === "sound,presets,projects,patterns" && /TABS = \[sound, presets, projects, patterns\]/.test(app)
    && mods.every((m, i) => m.tab.id === tabs[i][1] && typeof m.tab.mount === "function" && m.tab.cap > 0),
    "editor: tabs Sound / Presets / Projects / Patterns, one module each");
  ok(/<script type="module" src="editor\/app\.mjs"><\/script>/.test(html) && html.split("</body>").length === 2
    && !/<script[^>]+src="(https?:)?\/\/|<link[^>]+href="(https?:)?\/\/|@import|googleapis/.test(html),
    "editor: one local module, no CDN (works offline once loaded)");
  ok(!/Felucca Editor|PROTO-BEGIN|makeMockDevice/.test(html) && /DEADACTIVE/.test(html) && /Leo Kuroshita/.test(html),
    "editor: upstream's page replaced, both credits kept");
  const { runPageTests } = await import("./editor/test_page.mjs");
  const r = await runPageTests((s) => console.log(s));
  ok(!r.failed, `editor: page logic against the mock device (${r.passed} checks)`);
}

/* ------------------------------------------------------- packages: JS == Python --- */
async function packages() {
  const pkg = join(HERE, "../build/felucca.fwsc");
  if (!existsSync(pkg)) {
    console.log("packages: skipped (run ./build.sh first)");
    return;
  }
  const raw = readFileSync(pkg);
  const logical = py(`import sys; raw = open(sys.argv[1], "rb").read()
sys.stdout.buffer.write(b"".join(raw[i * 48:i * 48 + 47] for i in range(20)) + raw[960:])`, pkg);
  ok(eq(logicalImage(raw), logical), "fm1pkg.js logicalImage");
  ok(/^FM-1_9\d\d$/.test(productOf(raw)), "fm1pkg.js productOf");
}

/* ------------------------------------------------- update protocol (fm1ota.js) --- */
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];

/* an FM-1 on WebMIDI: identity, then "device asks, host answers" reads of the image */
class FakeFM1 {
  constructor(image, { unplugAfter = Infinity, after = "FM-1_900", identity = "FM-1_015", name = "FM-1", afterName = "Felucca" } = {}) {
    this.image = image; this.unplugAfter = unplugAfter; this.after = after; this.afterName = afterName; this.served = 0; this.bad = 0;
    this.access = { inputs: new Map(), outputs: new Map() };
    this.boot(identity, name);
  }
  boot(identity, name) {
    this.identity = identity; this.waiting = null; this.queue = [];
    for (const m of [this.access.inputs, this.access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
    const id = Math.random().toString(36).slice(2);
    this.input = { id: "i" + id, name, state: "connected", onmidimessage: null, open: async () => {} };
    this.output = { id: "o" + id, name, state: "connected", open: async () => {}, send: (d) => {
      if (this.output.state !== "connected") throw new Error("InvalidStateError");
      setTimeout(() => this.rx(Array.from(d)), 1);
    } };
    this.access.inputs.set(this.input.id, this.input);
    this.access.outputs.set(this.output.id, this.output);
  }
  tx(bytes) { const i = this.input; setTimeout(() => { if (i.state === "connected" && i.onmidimessage) i.onmidimessage({ data: Uint8Array.from(bytes) }); }, 1); }
  rx(d) {
    if (eq(d, HS)) {
      const t = [...new TextEncoder().encode(this.identity)];
      const body = [0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)];
      this.tx([0xF0, ...pack7(body), 0xF7]);
    } else if (eq(d, UPGRADE)) {
      this.queue = this.identity.startsWith("ota-")
        ? [...Array.from({ length: 6 }, (_, k) => [k * 512, 512]), [0xF0000000, 8]]
        : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
      this.next();
    } else if (this.waiting) {
      const u = unpack7(d.slice(1, -1));
      const [addr, len] = this.waiting;
      const got = u.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
      const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0] : Array.from(this.image.subarray(addr, addr + len));
      if (!eq(got, want)) this.bad++;
      this.waiting = null;
      this.served++;
      if (this.served >= this.unplugAfter) { this.input.state = this.output.state = "disconnected"; return; }
      if (addr === 0xE0000000) setTimeout(() => this.boot("ota-FM-1_900", "Felucca Update"), 300);
      else if (addr === 0xF0000000) setTimeout(() => this.boot(this.after, this.afterName), 300);
      else this.next();
    }
  }
  next() {
    const r = this.queue.shift();
    if (!r) return;
    this.waiting = r;
    const [addr, len] = r;
    const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    u.push(~s & 0xFF);
    this.tx([0xF0, ...pack7(u), 0xF7]);
  }
}

async function updater() {
  const image = Uint8Array.from({ length: 0x2000 }, (_, i) => (i * 7) & 0xFF);
  const dev = new FakeFM1(image);
  const steps = [];
  const got = await new Updater(dev.access).install(image, "FM-1_900", (k) => steps.push(k));
  ok(got === "FM-1_900" && dev.bad === 0 && steps.includes("write") && steps.at(-1) === "done",
    `fm1ota.js: install: running firmware -> loader -> Felucca (${dev.served} reads)`);

  // the app's USB name is "Hortator" (older firmware: "Felucca"): an FM-1 running it is found, and found again after the update
  const devH = new FakeFM1(image, { identity: "FM-1_900", name: "Hortator", after: "FM-1_901", afterName: "Hortator" });
  const gotH = await new Updater(devH.access).install(image, "FM-1_901").then((r) => r, (x) => x);
  ok(gotH === "FM-1_901" && devH.bad === 0, "fm1ota.js: install: running Hortator -> loader -> Hortator");
  // MENU > USB AUDIO 10 CH: the app's USB name is "Hortator 8ch" (and comes back as "Hortator" from a 2 CH build)
  for (const [nm, back] of [["Hortator 8ch", "Hortator 8ch"], ["Hortator 8ch MIDI 1", "Hortator"], ["Hortator 8ch Port 1", "Hortator"]]) {
    const dev8 = new FakeFM1(image, { identity: "FM-1_900", name: nm, after: "FM-1_901", afterName: back });
    const got8 = await new Updater(dev8.access).install(image, "FM-1_901").then((r) => r, (x) => x);
    ok(got8 === "FM-1_901" && dev8.bad === 0, `fm1ota.js: install: running ${nm} (USB AUDIO 10 CH) -> loader -> ${back}`);
  }

  const dev2 = new FakeFM1(image, { unplugAfter: 3 });
  dev2.boot("ota-FM-1_900", "Felucca Update");
  const t0 = Date.now();
  const done = await new Updater(dev2.access).resume(image);
  ok(done === false && Date.now() - t0 < 6000, "fm1ota.js: unplugged during the write -> stops at once");

  const dev3 = new FakeFM1(image, { unplugAfter: 2 });
  const e = await new Updater(dev3.access).install(image, "FM-1_900").then(() => null, (x) => x);
  ok(e && e.code === "lost", "fm1ota.js: unplugged in step 1 -> error code 'lost'");
  const e2 = await new Updater({ inputs: new Map(), outputs: new Map() }).install(image, "FM-1_900").then(() => null, (x) => x);
  ok(e2 && e2.code === "notfound", "fm1ota.js: no device -> error code 'notfound'");

  // upstream 1.0: resume waits for the device after the write and checks it reports the package's identity
  const dev4 = new FakeFM1(image);
  dev4.boot("ota-FM-1_900", "Felucca Update");
  const st4 = [];
  const r4 = await new Updater(dev4.access).resume(image, (k) => st4.push(k), "FM-1_900");
  ok(r4 === true && st4.includes("reboot") && st4.at(-1) === "done", "fm1ota.js: resume waits for the reboot and checks the identity");
  const dev5 = new FakeFM1(image, { after: "FM-1_901" });
  dev5.boot("ota-FM-1_900", "Felucca Update");
  const e5 = await new Updater(dev5.access).resume(image, null, "FM-1_900").then(() => null, (x) => x);
  ok(e5 && e5.code === "mismatch", "fm1ota.js: resume -> another identity after the reboot is error 'mismatch'");
  // upstream 1.0: an FM-1 input is never paired with "the only output" of another name (that may be other gear)
  const dev6 = new FakeFM1(image);
  dev6.output.name = "Other gear";
  const e6 = await new Updater(dev6.access).install(image, "FM-1_900").then(() => null, (x) => x);
  ok(e6 && e6.code === "notfound" && dev6.served === 0, "fm1ota.js: no same-named output -> not paired with the only output");
}

/* ------------------------------------------------------------ the install counter (GoatCounter) --- */
async function counter() {
  const site = { protocol: "https:", hostname: "deadactive.github.io" };
  const calls = [];
  const spy = (url, opt) => { calls.push([url, opt]); return Promise.resolve({ ok: true }); };
  ok(GC_CODE === "deadactive" && counterOn(site), "counter: the site's code, deadactive: on at the published site");
  ok(!counterOn(site, "") && countInstall("0.23.0", { code: "", loc: site, fetch: spy }) === false && calls.length === 0,
    "counter: off without a code: no ping");
  ok(await installCount({ code: "", loc: site, fetch: spy }) === null && calls.length === 0, "counter: off: no read");
  for (const loc of [{ protocol: "http:", hostname: "localhost" }, { protocol: "http:", hostname: "127.0.0.1" },
    { protocol: "https:", hostname: "localhost" }, { protocol: "file:", hostname: "" }, { protocol: "http:", hostname: "example.org" }, undefined])
    ok(!counterOn(loc, "hortator") && countInstall("0.23.0", { code: "hortator", loc, fetch: spy }) === false && calls.length === 0,
      `counter: never from ${loc ? loc.protocol + "//" + loc.hostname : "no page"}`);
  const r = countInstall("0.23.0", { code: "hortator", loc: site, fetch: spy });
  const [url, opt] = calls[0] || [];
  ok(r === true && calls.length === 1 && /^https:\/\/hortator\.goatcounter\.com\/count\?p=install&e=true&t=0\.23\.0&rnd=\w+$/.test(url),
    "counter: one event ping, GET /count?p=install&e=true&t=VERSION");
  ok(opt && opt.keepalive === true && opt.mode === "no-cors" && opt.credentials === "omit" && opt.referrerPolicy === "no-referrer",
    "counter: keepalive, no cookies, no referrer");
  ok(typeof r === "boolean", "counter: returns at once (nothing to await)");
  let unhandled = 0;
  const onUnhandled = () => { unhandled++; };
  process.on("unhandledRejection", onUnhandled);
  ok(countInstall("0.23.0", { code: "hortator", loc: site, fetch: () => { throw new Error("blocked"); } }) === false,
    "counter: a throwing fetch is swallowed");
  ok(countInstall("0.23.0", { code: "hortator", loc: site, fetch: () => Promise.reject(new Error("offline")) }) === true,
    "counter: a failing request is ignored");
  ok(countInstall("0.23.0", { code: "hortator", loc: site, fetch: null }) === false, "counter: no fetch: nothing");
  await new Promise((res) => setTimeout(res, 20));
  process.off("unhandledRejection", onUnhandled);
  ok(unhandled === 0, "counter: no unhandled rejection");
  const reply = (status, body) => () => Promise.resolve({ ok: status === 200, status, json: () => Promise.resolve(body) });
  const read = [];
  ok(await installCount({ code: "hortator", loc: site, fetch: (u, o) => { read.push([u, o]); return reply(200, { count: "1,234" })(); } }) === 1234
    && read[0][0] === "https://hortator.goatcounter.com/counter/install.json" && read[0][1].credentials === "omit",
    "counter: the public count, /counter/install.json, \"1,234\" -> 1234");
  ok(await installCount({ code: "hortator", loc: site, fetch: reply(404, {}) }) === null, "counter: 404 (no installs yet / counts off) -> hidden");
  ok(await installCount({ code: "hortator", loc: site, fetch: reply(200, {}) }) === null, "counter: no count -> hidden");
  ok(await installCount({ code: "hortator", loc: site, fetch: () => Promise.reject(new Error("x")) }) === null, "counter: a failed read -> hidden");
  const t0 = Date.now();
  const slow = (u, o) => new Promise((res, rej) => o.signal.addEventListener("abort", () => rej(new Error("aborted"))));
  ok(await installCount({ code: "hortator", loc: site, fetch: slow, timeout: 50 }) === null && Date.now() - t0 < 1000,
    "counter: a slow read gives up after its timeout");
  // page visits: GoatCounter's count.js, added only where the counter is on, once
  const fakeDoc = () => {
    const added = [];
    return { added, querySelector: (q) => added.find((e) => q.includes(e.src)) || null,
      createElement: (tag) => ({ tag, dataset: {}, setAttribute(k, v) { this[k] = v; } }), head: { append: (e) => added.push(e) } };
  };
  const d1 = fakeDoc();
  ok(loadPageCounter({ doc: d1, loc: site }) === true && d1.added.length === 1 && d1.added[0].tag === "script"
    && d1.added[0].src === "https://gc.zgo.at/count.js" && d1.added[0].async === true
    && d1.added[0]["data-goatcounter"] === "https://deadactive.goatcounter.com/count", "counter: count.js on the site, async, https");
  ok(loadPageCounter({ doc: d1, loc: site }) === false && d1.added.length === 1, "counter: count.js added once");
  for (const loc of [{ protocol: "http:", hostname: "127.0.0.1" }, { protocol: "http:", hostname: "localhost" }, undefined]) {
    const d = fakeDoc();
    ok(loadPageCounter({ doc: d, loc }) === false && d.added.length === 0, `counter: no count.js on ${loc ? loc.hostname : "no page"}`);
  }
  const d2 = fakeDoc();
  ok(loadPageCounter({ doc: d2, loc: site, code: "" }) === false && d2.added.length === 0, "counter: no count.js without a code");
  ok(loadPageCounter({ doc: { createElement() { throw new Error("x"); }, querySelector: () => null }, loc: site }) === false,
    "counter: a failing page never throws");
  const editorSite = py(`import sys; sys.path.insert(0, ${JSON.stringify(HERE)}); import make_site; print(make_site.editor_html())`).toString();
  ok(/loadPageCounter\(\);\s*<\/script>\s*<\/body>/.test(editorSite) && editorSite.includes("function loadPageCounter"),
    "counter: the published editor loads count.js the same way");
  for (const f of ["index_pkg.html", "editor.html", "sim/index.html", "sim/counter.js", "sim/app.js"])
    ok(!/["']\/\/gc\.zgo\.at/.test(readFileSync(join(HERE, f), "utf8")), `counter: no protocol-relative count.js in ${f}`);
  // the installer: the ping is the "done" step's last act, never awaited; the note shows only when it is on
  const inst = readFileSync(join(HERE, "index_pkg.html"), "utf8");
  const uses = inst.match(/.*countInstall\(.*$/gm) || [];
  ok(uses.length === 1 && /if \(k === "done"\) \{[^}]*sayK\("done"\); countInstall\(meta\.version\); \}/.test(uses[0]) && !/await\s+countInstall/.test(inst),
    "installer: one ping, after Done, not awaited");
  ok(/<p id="counted" data-t="counted" hidden><\/p>/.test(inst) && /if \(counterOn\(\)\) \$\("counted"\)\.hidden = false;/.test(inst)
    && inst.includes('counted: "Counts visits and installs anonymously with GoatCounter: no cookies, no personal data."'),
    "installer: the privacy note, shown only when counting");
  ok(/^loadPageCounter\(\);$/m.test(inst), "installer: its visits counted (count.js, guarded)");
}

/* ------------------------------------------------- the simulator: parameter locks (2026-10-24) --- */
async function simLocks() {
  const { BTN } = await import("./sim/controls.js");
  const env = BTN.find((b) => b.c === "B_ENV");
  ok(env && /STEPS \/ TRACKS/.test(env.tip || "") && /lock/.test(env.tip || ""), "sim: ENV has a tip (STEPS / TRACKS, locks)");
  const page = readFileSync(join(HERE, "sim/index.html"), "utf8");
  const seq = page.slice(page.indexOf('id="f-seq"'), page.indexOf('data-piece="seq"'));
  ok(/parameter lock/i.test(seq) && /<b>ENV<\/b>/.test(seq), "sim: the features' steps chapter names parameter locks and ENV");
}

/* ------------------------------------------------- the simulator: live cue, variations, FILL, COPY (2026-10-26) --- */
async function simCue() {
  const { BTN } = await import("./sim/controls.js");
  const save = BTN.find((b) => b.c === "B_SAVE"), rec = BTN.find((b) => b.c === "B_REC");
  ok(save && /cue/i.test(save.tip || "") && /project/i.test(save.tip || ""), "sim: SAVE's tip names the cue map and the projects page");
  ok(rec && /FILL/.test(rec.tip || ""), "sim: REC's tip names FILL");
  const page = readFileSync(join(HERE, "sim/index.html"), "utf8");
  const seq = page.slice(page.indexOf('id="f-seq"'), page.indexOf('data-piece="seq"'));
  ok(/variation/i.test(seq) && /FILL/.test(seq) && /COPY/.test(seq) && /PASTE/.test(seq),
    "sim: the features' steps chapter names variations, FILL and COPY / PASTE");
  ok(/black keys? variation/i.test(save.tip || "") && !/5-12/.test(save.tip || "") && /black keys queue a variation/i.test(seq) &&
    !/5–12/.test(seq), "sim: the variations are on the black keys (SAVE's tip, the steps chapter), no longer keys 5-12");
}

await simLocks();
await simCue();
await editorPage();
await packages();
await updater();
await counter();
console.log(failed ? `WEB TESTS FAILED (${failed})` : "web tests passed");
process.exit(failed ? 1 : 0);
