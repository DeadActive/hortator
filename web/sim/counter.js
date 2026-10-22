// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The install count (GoatCounter, https://www.goatcounter.com): the installer sends one anonymous event after an
// install says Done; the landing page shows the public count beside its install button. No script from GoatCounter,
// no cookies (credentials omitted), no referrer, nothing about the device. Off when GC_CODE is empty, and on any page
// that is not the published site over https (localhost, a preview, a file, the tests). Used by the landing page
// (imported) and the installer (inlined by web/make_site.py and tools/build_sim.sh).

// the GoatCounter site code (CODE.goatcounter.com); '' = the counter is off: no ping, no count shown. The site's
// settings must allow "adding visitor counts on your website" for the count to be readable.
export const GC_CODE = '';
const GC_EVENT = 'install';                          // the event's path (its name in the dashboard)
const LOCAL = /^(localhost|127\.\d+\.\d+\.\d+|\[::1\]|0\.0\.0\.0|.*\.local)$/;

// on: a code, and a page served over https from a real host
export function counterOn(loc = globalThis.location, code = GC_CODE) {
  return !!(code && loc && loc.protocol === 'https:' && loc.hostname && !LOCAL.test(loc.hostname));
}

// one event "install" titled with the firmware version: fire and forget. Returns at once (true: a request went out);
// never throws, and a failed request is ignored, so an install can never wait on it or fail because of it.
export function countInstall(version, { code = GC_CODE, loc = globalThis.location, fetch = globalThis.fetch } = {}) {
  try {
    if (!counterOn(loc, code) || typeof fetch !== 'function') return false;
    const url = `https://${code}.goatcounter.com/count?p=${GC_EVENT}&e=true&t=${encodeURIComponent(version)}` +
      `&rnd=${Math.random().toString(36).slice(2)}`;
    const sent = fetch(url, { mode: 'no-cors', keepalive: true, credentials: 'omit', cache: 'no-store',
      referrerPolicy: 'no-referrer' });
    if (sent && typeof sent.catch === 'function') sent.catch(() => {});
    return true;
  } catch {
    return false;
  }
}

// the public count of installs (GoatCounter's visitor counter, cached by it for up to four hours) -> a number, or
// null when off, slow (timeout ms), failed, or missing
export async function installCount({ code = GC_CODE, loc = globalThis.location, fetch = globalThis.fetch,
  timeout = 3000 } = {}) {
  if (!counterOn(loc, code) || typeof fetch !== 'function') return null;
  const ctl = new AbortController(), timer = setTimeout(() => ctl.abort(), timeout);
  try {
    const r = await fetch(`https://${code}.goatcounter.com/counter/${GC_EVENT}.json`,
      { credentials: 'omit', referrerPolicy: 'no-referrer', signal: ctl.signal });
    if (!r.ok) return null;
    const digits = String((await r.json())?.count ?? '').replace(/[^\d]/g, '');   // "1,234" -> 1234
    return digits && Number(digits) > 0 ? Number(digits) : null;
  } catch {
    return null;
  } finally {
    clearTimeout(timer);
  }
}
