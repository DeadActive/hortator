// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// The landing page's typeface: the firmware's own Terminus 8x16 (assets/fonts/ter-u16n.bdf, the bitmap
// tools/gen_font.py renders for the FM-1's screen; SIL OFL 1.1, no reserved name) turned into a web font whose
// outlines are the bitmap's pixels: 64 units a pixel, 1024 an em, so at 16 px (and its multiples) every pixel lands on
// the device grid. A plain TrueType built by hand (no dependencies), wrapped as WOFF 1.0.
//   node tests/sim_font.mjs IN.bdf OUT.woff
import { readFileSync, writeFileSync } from 'node:fs';
import { deflateSync } from 'node:zlib';

const PX = 64, EM = 1024, ADV = 8 * PX;
const FAMILY = 'Hortator Terminus';                   // a derived font: Terminus by Dimitar Toshkov Zhekov, OFL 1.1
export const WANT = cp => (cp >= 0x20 && cp <= 0x7e) || (cp >= 0xa0 && cp <= 0xff) ||
  [0x2013, 0x2014, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2026, 0x2190, 0x2191, 0x2192, 0x2193, 0x2212, 0x25b6,
    0x25a0, 0x2588, 0x2591, 0x2592, 0x2593].includes(cp);

// ---- the BDF: ascent, descent, per glyph its code point, box and rows
export function parseBdf(text) {
  const lines = text.split('\n');
  const prop = k => Number(lines.find(l => l.startsWith(k + ' ')).split(' ')[1]);
  const glyphs = [];
  for (let i = 0; i < lines.length; i++) {
    if (!lines[i].startsWith('STARTCHAR')) continue;
    const g = { rows: [] };
    for (i++; !lines[i].startsWith('ENDCHAR'); i++) {
      const [k, ...v] = lines[i].trim().split(/\s+/);
      if (k === 'ENCODING') g.cp = Number(v[0]);
      else if (k === 'BBX') [g.w, g.h, g.x, g.y] = v.map(Number);
      else if (k === 'BITMAP') {
        for (i++; !lines[i].startsWith('ENDCHAR'); i++) g.rows.push(parseInt(lines[i].trim(), 16));
        i--;
      }
    }
    glyphs.push(g);
  }
  return { ascent: prop('FONT_ASCENT'), descent: prop('FONT_DESCENT'), glyphs };
}

// a glyph's pixels as rectangles (runs in a row, stacked while the next row has the same run), y up from the baseline
function rects(g) {
  const bytes = Math.ceil(g.w / 8), out = [], open = new Map();
  g.rows.forEach((row, r) => {
    const runs = [];
    for (let c = 0; c < g.w; c++) {
      if (!((row >> (bytes * 8 - 1 - c)) & 1)) continue;
      if (runs.length && runs[runs.length - 1][1] === c) runs[runs.length - 1][1]++;
      else runs.push([c, c + 1]);
    }
    const next = new Map();
    for (const [a, b] of runs) {
      const k = `${a},${b}`, top = g.y + g.h - r;  // this row spans top - 1 .. top (pixels, y up)
      if (open.has(k)) { const o = open.get(k); o.y0 = top - 1; next.set(k, o); open.delete(k); }
      else next.set(k, { x0: g.x + a, x1: g.x + b, y0: top - 1, y1: top });
    }
    for (const o of open.values()) out.push(o);
    open.clear();
    for (const [k, o] of next) open.set(k, o);
  });
  for (const o of open.values()) out.push(o);
  return out.map(o => ({ x0: o.x0 * PX, x1: o.x1 * PX, y0: o.y0 * PX, y1: o.y1 * PX }));
}

// ---- TrueType
const u16 = v => [(v >> 8) & 255, v & 255];
const i16 = v => u16(v & 0xffff);
const u32 = v => [(v >>> 24) & 255, (v >>> 16) & 255, (v >>> 8) & 255, v & 255];
const pad4 = a => { while (a.length % 4) a.push(0); return a; };
function sum32(bytes) {
  let s = 0;
  for (let i = 0; i < bytes.length; i += 4)
    s = (s + ((bytes[i] << 24) | ((bytes[i + 1] ?? 0) << 16) | ((bytes[i + 2] ?? 0) << 8) | (bytes[i + 3] ?? 0))) >>> 0;
  return s;
}

function glyf(rs) {                                   // a simple glyph of rectangles, clockwise, all points on the curve
  if (!rs.length) return [];
  const xs = rs.flatMap(r => [r.x0, r.x1]), ys = rs.flatMap(r => [r.y0, r.y1]);
  const pts = rs.flatMap(r => [[r.x0, r.y0], [r.x0, r.y1], [r.x1, r.y1], [r.x1, r.y0]]);
  const out = [...i16(rs.length), ...i16(Math.min(...xs)), ...i16(Math.min(...ys)), ...i16(Math.max(...xs)), ...i16(Math.max(...ys))];
  rs.forEach((_, k) => out.push(...u16(k * 4 + 3)));
  out.push(...u16(0));                                // no instructions
  for (let k = 0; k < pts.length; k++) out.push(0x01);
  let px = 0, py = 0;
  for (const [x] of pts) { out.push(...i16(x - px)); px = x; }
  for (const [, y] of pts) { out.push(...i16(y - py)); py = y; }
  return pad4(out);
}

function nameTable(names) {
  const recs = [], strs = [];
  let off = 0;
  for (const [id, s] of Object.entries(names)) {
    const b = [];
    for (const ch of s) b.push(...u16(ch.charCodeAt(0)));
    recs.push([...u16(3), ...u16(1), ...u16(0x409), ...u16(Number(id)), ...u16(b.length), ...u16(off)]);
    strs.push(...b);
    off += b.length;
  }
  const n = recs.length;
  return [...u16(0), ...u16(n), ...u16(6 + 12 * n), ...recs.flat(), ...strs];
}

function cmap(cps) {                                  // format 4: runs of consecutive code points -> consecutive glyphs
  const segs = [];
  cps.forEach((cp, k) => {
    const gid = k + 1, last = segs[segs.length - 1];
    if (last && last.end === cp - 1 && last.gid + (cp - last.start) === gid) last.end = cp;
    else segs.push({ start: cp, end: cp, gid });
  });
  segs.push({ start: 0xffff, end: 0xffff, gid: 0, delta: 1 });
  const n = segs.length, sr = 2 ** Math.floor(Math.log2(n)) * 2;
  const sub = [...u16(4), ...u16(16 + 8 * n), ...u16(0), ...u16(2 * n), ...u16(sr), ...u16(Math.log2(sr / 2)), ...u16(2 * n - sr)];
  segs.forEach(s => sub.push(...u16(s.end)));
  sub.push(...u16(0));
  segs.forEach(s => sub.push(...u16(s.start)));
  segs.forEach(s => sub.push(...i16(s.delta ?? (s.gid - s.start))));
  segs.forEach(() => sub.push(...u16(0)));
  return [...u16(0), ...u16(1), ...u16(3), ...u16(1), ...u32(12), ...sub];
}

export function buildFont(bdf) {
  const glyphs = bdf.glyphs.filter(g => WANT(g.cp)).sort((a, b) => a.cp - b.cp);
  const asc = bdf.ascent * PX, desc = bdf.descent * PX;
  const notdef = [{ x0: PX, x1: 7 * PX, y0: 0, y1: PX }, { x0: PX, x1: 7 * PX, y0: 9 * PX, y1: 10 * PX },
    { x0: PX, x1: 2 * PX, y0: PX, y1: 9 * PX }, { x0: 6 * PX, x1: 7 * PX, y0: PX, y1: 9 * PX }];
  const outlines = [notdef, ...glyphs.map(rects)];
  const glyfBytes = [], loca = [];
  let maxPts = 0, maxCont = 0;
  for (const rs of outlines) {
    loca.push(glyfBytes.length);
    glyfBytes.push(...glyf(rs));
    maxPts = Math.max(maxPts, rs.length * 4);
    maxCont = Math.max(maxCont, rs.length);
  }
  loca.push(glyfBytes.length);
  const all = outlines.flat();
  const bbox = [Math.min(0, ...all.map(r => r.x0)), Math.min(-desc, ...all.map(r => r.y0)), Math.max(...all.map(r => r.x1)),
    Math.max(asc, ...all.map(r => r.y1))];
  const n = outlines.length, cps = glyphs.map(g => g.cp);
  const tables = {
    'OS/2': [...u16(4), ...i16(ADV), ...u16(400), ...u16(5), ...u16(0), ...i16(PX * 10), ...i16(PX * 10), ...i16(0),
      ...i16(PX * 2), ...i16(PX * 10), ...i16(PX * 10), ...i16(0), ...i16(PX * 6), ...i16(PX), ...i16(PX * 5), ...i16(0),
      2, 0, 5, 9, 0, 0, 0, 0, 0, 0, ...u32(1), ...u32(0), ...u32(0), ...u32(0), ...'NONE'.split('').map(c => c.charCodeAt(0)),
      ...u16(0x40), ...u16(Math.min(...cps)), ...u16(Math.min(0xffff, Math.max(...cps))), ...i16(asc), ...i16(-desc), ...i16(0),
      ...u16(asc), ...u16(desc), ...u32(1), ...u32(0), ...i16(PX * 7), ...i16(PX * 10), ...u16(0), ...u16(32), ...u16(1)],
    cmap: cmap(cps),
    glyf: glyfBytes,
    head: [...u32(0x00010000), ...u32(0x00010000), ...u32(0), ...u32(0x5f0f3cf5), ...u16(0x000b), ...u16(EM),
      ...u32(0), ...u32(0), ...u32(0), ...u32(0), ...i16(bbox[0]), ...i16(bbox[1]), ...i16(bbox[2]), ...i16(bbox[3]),
      ...u16(0), ...u16(8), ...i16(2), ...i16(1), ...i16(0)],
    hhea: [...u32(0x00010000), ...i16(asc), ...i16(-desc), ...i16(0), ...u16(ADV), ...i16(0), ...i16(0), ...i16(bbox[2]),
      ...i16(1), ...i16(0), ...i16(0), 0, 0, 0, 0, 0, 0, 0, 0, ...i16(0), ...u16(n)],
    hmtx: outlines.flatMap(rs => [...u16(ADV), ...i16(rs.length ? Math.min(...rs.map(r => r.x0)) : 0)]),
    loca: loca.flatMap(u32),
    maxp: [...u32(0x00010000), ...u16(n), ...u16(maxPts), ...u16(maxCont), ...u16(0), ...u16(0), ...u16(2), ...u16(0),
      ...u16(0), ...u16(0), ...u16(0), ...u16(0), ...u16(0), ...u16(0), ...u16(0)],
    name: nameTable({
      0: 'Copyright (C) 2020 Dimitar Toshkov Zhekov (Terminus). Pixel outlines: 2026 DEADACTIVE.',
      1: FAMILY, 2: 'Regular', 3: `${FAMILY} Regular 16`, 4: `${FAMILY} Regular`, 5: 'Version 1.0',
      6: 'HortatorTerminus-Regular', 13: 'SIL Open Font License, Version 1.1', 14: 'https://openfontlicense.org',
    }),
    post: [...u32(0x00030000), ...u32(0), ...i16(-PX * 2), ...i16(PX), ...u32(1), ...u32(0), ...u32(0), ...u32(0), ...u32(0)],
  };
  return tables;
}

// the tables as a TrueType file (checksums, head's adjustment) -> { sfnt, tags, sums }
function sfnt(tables) {
  const tags = Object.keys(tables).sort(), nt = tags.length, sr = 2 ** Math.floor(Math.log2(nt)) * 16;
  let off = 12 + 16 * nt;
  const dir = [], body = [], sums = {};
  for (const t of tags) {
    const data = pad4([...tables[t]]);
    sums[t] = sum32(data);
    dir.push(...t.split('').map(c => c.charCodeAt(0)), ...u32(sums[t]), ...u32(off), ...u32(tables[t].length));
    body.push(...data);
    off += data.length;
  }
  const head = [...u32(0x00010000), ...u16(nt), ...u16(sr), ...u16(Math.log2(sr / 16)), ...u16(nt * 16 - sr)];
  const file = [...head, ...dir, ...body];
  const adj = (0xb1b0afba - sum32(file)) >>> 0, at = 12 + 16 * nt + tags.slice(0, tags.indexOf('head')).reduce(
    (a, t) => a + pad4([...tables[t]]).length, 0) + 8;
  file.splice(at, 4, ...u32(adj));
  tables.head.splice(8, 4, ...u32(adj));            // (its directory checksum stays the one taken with 0 there)
  return { sfnt: file, tags, sums };
}

export function woff(tables) {
  const { sfnt: whole, tags, sums } = sfnt(tables);
  const entries = tags.map(t => {
    const orig = Buffer.from(tables[t]), z = deflateSync(orig, { level: 9 });
    return { t, orig, data: z.length < orig.length ? z : orig };
  });
  let off = 44 + 20 * entries.length;
  const dir = [], body = [];
  for (const e of entries) {
    dir.push(...e.t.split('').map(c => c.charCodeAt(0)), ...u32(off), ...u32(e.data.length), ...u32(e.orig.length), ...u32(sums[e.t]));
    const d = pad4([...e.data]);
    body.push(...d);
    off += d.length;
  }
  const head = [...'wOFF'.split('').map(c => c.charCodeAt(0)), ...u32(0x00010000), ...u32(off), ...u16(entries.length), ...u16(0),
    ...u32(whole.length), ...u16(1), ...u16(0), ...u32(0), ...u32(0), ...u32(0), ...u32(0), ...u32(0)];
  return Buffer.from([...head, ...dir, ...body]);
}

if (process.argv[1] && import.meta.url.endsWith(process.argv[1].split('/').pop())) {
  const [inp, out] = process.argv.slice(2);
  const bdf = parseBdf(readFileSync(inp, 'utf8')), font = woff(buildFont(bdf));
  writeFileSync(out, font);
  console.log(`sim_font: ${bdf.glyphs.filter(g => WANT(g.cp)).length} glyphs, ${font.length} B -> ${out}`);
}
