// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { readFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import { extname, resolve, sep } from 'node:path';

const { chromium } = await import('../../build/grind-browser/node_modules/playwright-core/index.mjs');
const preview = resolve('build/sim/preview');
const web = resolve('web');
const types = {
  '.css': 'text/css', '.html': 'text/html', '.js': 'text/javascript',
  '.json': 'application/json', '.mjs': 'text/javascript',
  '.wasm': 'application/wasm', '.woff': 'font/woff',
};
const server = createServer(async (request, response) => {
  const path = new URL(request.url, 'http://localhost').pathname;
  const fromWeb = path.startsWith('/editor-source/');
  const root = fromWeb ? web : preview;
  const file = resolve(root, fromWeb ? path.slice('/editor-source/'.length) : path.slice(1) || 'index.html');
  if (!file.startsWith(root + sep)) {
    response.writeHead(404).end();
    return;
  }
  let body;
  try {
    body = await readFile(file);
  } catch {
    response.writeHead(404).end();
    return;
  }
  response.writeHead(200, { 'Content-Type': types[extname(file)] ?? 'application/octet-stream' }).end(body);
});

server.listen(0, '127.0.0.1');
await once(server, 'listening');
const base = `http://127.0.0.1:${server.address().port}`;
let browser;
try {
  browser = await chromium.launch({ executablePath: process.env.GRIND_CHROMIUM });
  const page = await browser.newPage({ reducedMotion: 'reduce' });
  page.setDefaultTimeout(15_000);
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));

  const [wasm] = await Promise.all([
    page.waitForResponse(response => new URL(response.url()).pathname === '/fm1sim.wasm'),
    page.goto(base),
  ]);
  assert.equal(wasm.status(), 200);
  const power = page.getByRole('switch', { name: 'Power' });
  await power.click();
  await page.locator('#power-note').waitFor({ state: 'hidden' });
  assert.equal(await power.getAttribute('aria-checked'), 'true');
  assert.doesNotMatch(await page.locator('#note').textContent(), /firmware failed/i);

  await page.goto(`${base}/editor-source/editor.html?mock=auto`);
  const projects = page.getByRole('tab', { name: 'Projects' });
  await projects.click();
  assert.equal(await projects.getAttribute('aria-selected'), 'true');
  assert.deepEqual(errors, []);
  console.log('PASS: Chromium simulator power-on and mock editor navigation');
} finally {
  await browser?.close();
  server.close();
}
