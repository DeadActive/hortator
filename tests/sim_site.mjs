// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The site around the simulator: the landing page (web/sim/index.html) and the restyled English installer
// (web/index_pkg.html). Run from the repo root: node tests/sim_site.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';

// ---- the installer: restyled and rebranded, its install logic untouched
const ORIG_REV = '12a1ba0';                         // the installer's last change on main (upstream 1.0 installer)
const NOW = readFileSync('web/index_pkg.html', 'utf8');
let ORIG = null;
try { ORIG = execFileSync('git', ['show', `${ORIG_REV}:web/index_pkg.html`], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }); }
catch { /* a source archive without git history: the guard is skipped */ }

const script = html => html.match(/<script type="module">([\s\S]*?)<\/script>/)[1];
const TEXT_RE = /const TEXT = (\{[\s\S]*?\n\});\n/;
const masked = s => s.replace(TEXT_RE, 'const TEXT = /*TEXT*/;\n');
const table = html => Function(`return ${script(html).match(TEXT_RE)[1]}`)();
// the only script changes allowed (spec): English only, and the status label's name
const CHANGES = [
  ['let lang = (navigator.language || "en").toLowerCase().startsWith("ja") ? "ja" : "en";', 'const lang = "en";'],
  ['  $("lang").textContent = t("other");\n', ''],
  ['$("lang").addEventListener("click", () => { lang = lang === "ja" ? "en" : "ja"; applyLang(); });\n', ''],
  ['say(`Felucca ${meta.version}`);', 'say(`FM-1 Drums ${meta.version}`);'],
];

test('installer: the script is main\'s, changed only in its text table, the language lines and the label', { skip: !ORIG && 'no git history' }, () => {
  let want = masked(script(ORIG));
  for (const [from, to] of CHANGES) {
    assert.ok(want.includes(from), `main's installer no longer has: ${from.trim()} (update CHANGES)`);
    want = want.replace(from, to);
  }
  assert.equal(masked(script(NOW)), want);
});

test('installer: English only, every message the logic can show is still there', { skip: !ORIG && 'no git history' }, () => {
  const old = table(ORIG), now = table(NOW);
  assert.deepEqual(Object.keys(now), ['en'], 'one language');
  for (const k of Object.keys(old.en)) if (k !== 'other' && k !== 'editor') assert.equal(typeof now.en[k], 'string', k);
  for (const [, k] of NOW.matchAll(/data-t="(\w+)"/g)) assert.equal(typeof now.en[k], 'string', `data-t ${k}`);
});

test('installer: named FM-1 Drums, no language switch, /*LIB*/ and /*META*/ kept for make_site.py', () => {
  assert.match(NOW, /<html lang="en">/);
  assert.match(NOW.match(/<title>(.*)<\/title>/)[1], /FM-1 Drums/);
  assert.match(NOW.match(/<h1[^>]*>([\s\S]*?)<\/h1>/)[1], /FM-1 Drums/);
  assert.doesNotMatch(NOW, /id="lang"/);
  assert.equal(NOW.split('/*LIB*/').length, 2);
  assert.equal(NOW.split('/*META*/').length, 2);
  for (const id of ['go', 'bar', 'status', 'log']) assert.match(NOW, new RegExp(`id="${id}"`), id);
});
