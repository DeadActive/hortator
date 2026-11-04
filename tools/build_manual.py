#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""Build the Hortator manual: docs/manual/*.md -> docs/manual/index.html (one self-contained page).

  tools/build_manual.py [OUT.html]

Each docs/manual/NN-slug.md is a section: its `# Title` line, then `##` subsections. The page shows one section
at a time (#slug, #slug/sub), has a sidebar, previous / next links and a search over every paragraph, table row and
heading (all in the page: no server). Markdown subset: # ## ### headings, paragraphs, - lists, 1. lists, > notes,
| tables |, ``` code ```, **bold**, *italic*, `code`, [text](url), and [[KEY]] for a key / knob chip.
"""
import html
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "docs" / "manual"


def slugify(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-") or "x"


def inline(s):
    s = html.escape(s, quote=False)
    s = re.sub(r"`([^`]+)`", lambda m: "<code>" + m.group(1) + "</code>", s)
    s = re.sub(r"&lt;(https?://[^\s&]+)&gt;", r'<a href="\1" target="_blank" rel="noopener">\1</a>', s)
    s = re.sub(r"\[\[([^\]]+)\]\]", r'<kbd>\1</kbd>', s)
    s = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", s)
    s = re.sub(r"(?<![*\w])\*([^*\s][^*]*)\*(?!\w)", r"<i>\1</i>", s)
    s = re.sub(r"\[([^\]]+)\]\(([^)]+)\)",
               lambda m: f'<a href="{m.group(2)}"' + (' target="_blank" rel="noopener"' if m.group(2).startswith("http") else "")
               + f">{m.group(1)}</a>", s)
    return s


def plain(s):
    s = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", s)
    return re.sub(r"[`*]|\[\[|\]\]", "", s)


def render(md):
    """markdown subset -> (html, [(subsection id, title)], [search records])"""
    lines = md.split("\n")
    out, subs, recs = [], [], []
    i, cur = 0, {"id": "", "title": ""}

    def rec(text):
        text = plain(text).strip()
        if text:
            recs.append({"s": cur["id"], "t": text})

    while i < len(lines):
        ln = lines[i]
        if not ln.strip():
            i += 1
        elif ln.startswith("```"):
            j = i + 1
            while j < len(lines) and not lines[j].startswith("```"):
                j += 1
            code = "\n".join(lines[i + 1:j])
            out.append(f"<pre>{html.escape(code)}</pre>")
            rec(code.replace("\n", " "))
            i = j + 1
        elif re.match(r"#{2,3} ", ln):
            lvl = len(ln.split()[0])
            title = ln[lvl + 1:].strip()
            sid = slugify(title)
            if lvl == 2:
                cur = {"id": sid, "title": title}
                subs.append((sid, title))
            out.append(f'<h{lvl} id="{{sec}}/{sid}">{inline(title)}</h{lvl}>' if lvl == 2
                       else f'<h3 id="{{sec}}/{cur["id"]}-{sid}">{inline(title)}</h3>')
            rec(title)
            i += 1
        elif ln.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")])
                i += 1
            head, body = rows[0], [r for r in rows[2:]]
            t = "<div class=tw><table><thead><tr>" + "".join(f"<th>{inline(c)}</th>" for c in head) + "</tr></thead><tbody>"
            for r in body:
                t += "<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>"
                rec(" · ".join(r))
            out.append(t + "</tbody></table></div>")
        elif re.match(r"(- |\d+\. )", ln):
            ordered = ln[0].isdigit()
            items = []
            while i < len(lines) and re.match(r"(- |\d+\. |  +\S)", lines[i]):
                if re.match(r"(- |\d+\. )", lines[i]):
                    items.append(re.sub(r"^(- |\d+\. )", "", lines[i]))
                else:
                    items[-1] += " " + lines[i].strip()
                i += 1
            tag = "ol" if ordered else "ul"
            out.append(f"<{tag}>" + "".join(f"<li>{inline(x)}</li>" for x in items) + f"</{tag}>")
            for x in items:
                rec(x)
        elif ln.startswith(">"):
            buf = []
            while i < len(lines) and lines[i].startswith(">"):
                buf.append(lines[i][1:].strip())
                i += 1
            p = " ".join(buf)
            out.append(f'<p class="note">{inline(p)}</p>')
            rec(p)
        else:
            buf = []
            while i < len(lines) and lines[i].strip() and not re.match(r"(#|\||```|- |\d+\. |>)", lines[i]):
                buf.append(lines[i].strip())
                i += 1
            p = " ".join(buf)
            out.append(f"<p>{inline(p)}</p>")
            rec(p)
    return "\n".join(out), subs, recs


TEMPLATE = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Hortator Manual</title>
<meta name="description" content="The complete manual for Hortator, drum-machine firmware for the M-VAVE FM-1.">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Silkscreen&display=swap" rel="stylesheet" media="print" onload="this.media='all'">
<link rel="preload" href="../terminus.woff" as="font" type="font/woff" crossorigin>
<style>
@font-face { font-family: "Hortator Terminus"; src: url("../terminus.woff") format("woff"); font-display: swap; }
:root {
  --k:#000; --c1:#282828; --c2:#505050; --c3:#828282; --c4:#bababa; --c5:#e2e2e2; --w:#fff;
  --mono:"Hortator Terminus", ui-monospace, monospace;
  --d25: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='4' height='4' shape-rendering='crispEdges'%3E%3Crect width='2' height='2' fill='%23fff'/%3E%3C/svg%3E");
  --d50: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='4' height='4' shape-rendering='crispEdges'%3E%3Crect width='2' height='2' fill='%23fff'/%3E%3Crect x='2' y='2' width='2' height='2' fill='%23fff'/%3E%3C/svg%3E");
  color-scheme: dark;
}
* { box-sizing:border-box; }
[hidden] { display:none !important; }
html { scroll-padding-top:80px; }
body { margin:0; background:var(--k); color:var(--c5); font:16px/24px var(--mono); -webkit-font-smoothing:none; min-height:100vh; display:flex; flex-direction:column; }
::selection { background:var(--w); color:var(--k); }
a { color:var(--w); }
.silk { font:16px/1 var(--mono); letter-spacing:.25em; }
header.top { position:sticky; top:0; z-index:5; background:var(--k); border-bottom:2px solid var(--c1); display:flex; gap:16px; align-items:center; padding:12px 16px; }
header.top .brand { color:var(--w); text-decoration:none; white-space:nowrap; }
header.top .brand span { color:var(--c3); letter-spacing:.1em; margin-left:12px; }
.nav-links { display:flex; gap:16px; }
.nav-links a, .btn { color:var(--w); text-decoration:none; padding:0 8px; border:2px solid var(--w); background:transparent; font:inherit; cursor:pointer; }
.nav-links a:hover, .btn:hover, .btn:focus-visible { background:var(--w); color:var(--k); outline:none; }
#q { flex:1; min-width:0; background:var(--k); color:var(--w); border:2px solid var(--c2); padding:0 10px; font:inherit; border-radius:0; }
#q:focus { outline:none; border-color:var(--w); }
#q::placeholder { color:var(--c3); }
#menu { display:none; }
.layout { display:grid; grid-template-columns:288px minmax(0,1fr); width:100%; max-width:1240px; margin:0 auto; flex:1; }
nav#side { position:sticky; top:50px; align-self:start; max-height:calc(100vh - 50px); overflow:auto; padding:24px 16px 48px; border-right:2px solid var(--c1); }
nav#side a { display:block; color:var(--c4); text-decoration:none; padding:0 8px; }
nav#side a:hover { color:var(--k); background:var(--w); }
nav#side a.sec { color:var(--c5); margin-top:8px; text-transform:uppercase; }
nav#side a.sec::before { content:"> "; color:var(--c2); }
nav#side a.sub { display:none; padding-left:32px; color:var(--c3); }
nav#side a.sub.show { display:block; }
nav#side a.on { background:var(--w); color:var(--k); }
nav#side a.on::before { color:var(--k); }
main { padding:48px 48px 96px; min-width:0; max-width:960px; counter-reset:sub; }
main h1 { font:48px/48px var(--mono); margin:0 0 24px; max-width:24ch; color:var(--w); text-transform:uppercase; position:relative; }
main h2 { counter-increment:sub; display:flex; align-items:center; gap:16px; font:16px/32px var(--mono); margin:64px 0 16px; padding-top:16px; border-top:2px solid var(--c1); color:var(--w); text-transform:uppercase; }
main h2::before { content:counter(sub, decimal-leading-zero); padding:0 6px; color:var(--k); background:var(--w); }
main h3 { font:16px/24px var(--mono); margin:32px 0 8px; color:var(--c3); text-transform:uppercase; }
main p, main li { color:var(--c4); }
main p { margin:0 0 16px; max-width:72ch; }
main li { margin:4px 0; }
main ul, main ol { padding-left:24px; max-width:72ch; margin:0 0 16px; }
main b { color:var(--c5); font-weight:normal; }
main i { color:var(--c5); font-style:normal; text-decoration:underline; text-decoration-color:var(--c2); text-underline-offset:4px; }
code { color:var(--w); background:var(--c1); padding:0 6px; }
kbd { font:12px/1 "Silkscreen", ui-monospace, monospace; letter-spacing:.04em; padding:3px 6px 2px; border:2px solid var(--c3); color:var(--c5); vertical-align:1px; white-space:nowrap; margin:0 1px; }
pre { background:var(--c1); color:var(--c5); padding:16px; overflow:auto; line-height:24px; }
.note { border:2px dashed var(--c3); padding:8px 16px !important; color:var(--c4); }
.tw { overflow-x:auto; margin:16px 0 24px; }
table { border-collapse:collapse; min-width:100%; }
th { text-align:left; color:var(--c3); font-weight:normal; text-transform:uppercase; border-bottom:2px solid var(--c2); padding:0 16px 4px 0; white-space:nowrap; }
td { vertical-align:top; padding:8px 16px 8px 0; border-bottom:2px solid var(--c1); color:var(--c4); }
td:first-child { color:var(--w); }
td:first-child b { white-space:nowrap; color:var(--w); }
.lead { color:var(--c4); }
.pager { display:flex; justify-content:space-between; gap:16px; margin-top:96px; padding-top:16px; border-top:2px solid var(--c1); }
.pager a { color:var(--c5); text-decoration:none; border:2px solid var(--c2); padding:8px 16px; }
.pager a:hover { border-color:var(--w); background:var(--w); color:var(--k); }
.pager small { display:block; color:var(--c3); font:inherit; text-transform:uppercase; }
.pager a:hover small { color:var(--k); }
.hits { display:grid; gap:16px; }
.hit { display:block; text-decoration:none; color:var(--c4); border:2px solid var(--c1); padding:12px 16px; }
.hit:hover, .hit:focus-visible { border-color:var(--w); outline:none; }
.hit small { color:var(--c3); display:block; text-transform:uppercase; }
mark { background:var(--w); color:var(--k); padding:0 2px; }
.empty { color:var(--c3); }
.cover .grid { display:grid; grid-template-columns:repeat(auto-fill,minmax(256px,1fr)); gap:16px; margin-top:32px; }
.cover .grid a { display:block; text-decoration:none; border:2px solid var(--c1); padding:12px 16px; color:var(--c4); }
.cover .grid a:hover { background:var(--w); color:var(--k); border-color:var(--w); }
.cover .grid a:hover b, .cover .grid a:hover small { color:var(--k); }
.cover .grid b { display:block; color:var(--w); text-transform:uppercase; }
.cover .grid small { color:var(--c3); }
.glitch { position:relative; }
.glitch.glitching { animation:glitch-base .5s steps(1,end) 1; }
.glitch.glitching::before, .glitch.glitching::after { content:attr(data-text); position:absolute; inset:0; background:var(--k); pointer-events:none; }
.glitch.glitching::before { color:var(--w); animation:glitch-a .5s steps(1,end) 1 forwards; }
.glitch.glitching::after { color:var(--c3); animation:glitch-b .5s steps(1,end) 1 forwards; }
@keyframes glitch-base { 0% { transform:translateX(0); } 20% { transform:translateX(-4px); } 40% { transform:translateX(4px); } 60% { transform:translateX(0); opacity:.6; } 80% { opacity:1; } }
@keyframes glitch-a { 0% { clip-path:inset(8% 0 72% 0); transform:translateX(-12px); } 20% { clip-path:inset(52% 0 30% 0); transform:translateX(16px); } 40% { clip-path:inset(22% 0 60% 0); transform:translateX(-6px); } 60% { clip-path:inset(70% 0 8% 0); transform:translateX(10px); } 80% { clip-path:inset(40% 0 44% 0); transform:translateX(-20px); } 100% { clip-path:inset(100% 0 0 0); transform:none; } }
@keyframes glitch-b { 0% { clip-path:inset(60% 0 20% 0); transform:translateX(8px); } 25% { clip-path:inset(12% 0 76% 0); transform:translateX(-14px); } 50% { clip-path:inset(80% 0 4% 0); transform:translateX(6px); } 75% { clip-path:inset(30% 0 50% 0); transform:translateX(12px); } 100% { clip-path:inset(100% 0 0 0); transform:none; } }
.foot-band { width:100%; height:48px; margin-top:48px; position:relative; }
.foot-band::before { content:""; position:absolute; left:0; right:0; top:0; height:16px; background:var(--d25); opacity:.35; }
.foot-band::after { content:""; position:absolute; left:0; right:0; top:16px; height:16px; background:var(--d50); opacity:.5; }
.foot { width:min(100% - 32px,1240px); margin:0 auto; display:grid; grid-template-columns:1.1fr .9fr 1.6fr; gap:40px; padding:24px 0 32px; color:var(--c3); }
.foot p { margin:0 0 8px; max-width:60ch; color:var(--c3); }
.foot-mark { font-size:32px !important; line-height:32px; letter-spacing:.1em; color:var(--w) !important; margin-bottom:16px !important; }
.foot-nav { display:flex; flex-direction:column; gap:8px; }
.foot-nav a { color:var(--c5); text-decoration:none; }
.foot-nav a::before { content:"> "; color:var(--c2); }
.foot-nav a:hover { color:var(--k); background:var(--w); }
@media (max-width:860px) {
  .layout { display:block; }
  #menu { display:inline-block; }
  .nav-links { display:none; }
  nav#side { display:none; position:fixed; inset:50px 0 0 0; max-height:none; background:var(--k); z-index:4; border:0; }
  body.open nav#side { display:block; }
  main { padding:32px 16px 64px; }
  main h1 { font-size:32px; line-height:32px; }
  header.top .brand span { display:none; }
  .foot { grid-template-columns:1fr; gap:24px; }
}
@media (prefers-reduced-motion:reduce) { .glitch.glitching, .glitch.glitching::before, .glitch.glitching::after { animation:none; } }
@media print { header.top, nav#side, .pager, .foot-band { display:none; } .layout { display:block; } body { background:#fff; color:#000; } main *, .foot * { color:#000 !important; background:none !important; } }
</style>
</head>
<body>
<header class="top">
  <button class="btn" id="menu" aria-label="Sections">MENU</button>
  <a class="brand silk" href="#">HORTATOR<span>MANUAL</span></a>
  <input id="q" type="search" placeholder="Search the manual ( / )" autocomplete="off" aria-label="Search the manual">
  <span class="nav-links"><a href="../">Play</a><a href="../webapp/editor/">Editor</a><a href="../webapp/installer/">Install</a></span>
</header>
<div class="layout">
  <nav id="side" aria-label="Sections"></nav>
  <main id="main" tabindex="-1"></main>
</div>
<div class="foot-band" aria-hidden="true"></div>
<footer class="foot">
  <div>
    <p class="silk foot-mark">HORTATOR</p>
    <p>The hortator kept a galley's rowers in time. This one keeps eight tracks.</p>
  </div>
  <nav class="foot-nav" aria-label="Around the site">
    <a href="../">Play it in the browser</a>
    <a href="../webapp/installer/">Install on your FM-1</a>
    <a href="../webapp/editor/">Edit it from the browser</a>
    <a href="https://github.com/DeadActive/hortator" target="_blank" rel="noopener">Source code (GPL-3.0)</a>
  </nav>
  <div>
    <p>By DEADACTIVE, built on Felucca by Leo Kuroshita (@kurogedelic), <a href="https://hugelton.com" target="_blank" rel="noopener">Hügelton Instruments</a>. GPL-3.0, no warranty.</p>
    <p>Drum algorithms (Plaits, stmlib), GRIDS and the compressor (Streams): Emilie Gillet, <a href="https://github.com/pichenettes/eurorack" target="_blank" rel="noopener">Mutable Instruments</a>. SKIN and RESON: <a href="https://github.com/electro-smith/DaisySP" target="_blank" rel="noopener">DaisySP</a>. Samples: Versilian Studios VSCO-2 CE and VCSL (CC0 1.0). Font: Terminus (Dimitar Toshkov Zhekov, SIL OFL 1.1). Machinedrum is a trademark of Elektron; M-VAVE and FM-1 are trademarks of their owners; Hortator is not affiliated with either.</p>
  </div>
</footer>
<script>
const SECTIONS = /*SECTIONS*/;
const INDEX = /*INDEX*/;
const VERSION = /*VERSION*/;
const $ = s => document.querySelector(s);
const main = $('#main'), side = $('#side'), q = $('#q');
const esc = s => s.replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const secById = Object.fromEntries(SECTIONS.map(s => [s.id, s]));

function buildNav() {
  side.innerHTML = SECTIONS.map(s =>
    `<a class="sec" href="#${s.id}" data-id="${s.id}">${esc(s.title)}</a>` +
    s.subs.map(([id, t]) => `<a class="sub" href="#${s.id}/${id}" data-id="${s.id}">${esc(t)}</a>`).join('')).join('');
}
function mark(text, terms) {
  let h = esc(text);
  for (const t of terms) h = h.replace(new RegExp('(' + t.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + ')(?![^<]*>)', 'ig'), '<mark>$1</mark>');
  return h;
}
function search(query) {
  const terms = query.toLowerCase().split(/\s+/).filter(t => t.length > 0);
  if (!terms.length) return [];
  const hits = [];
  for (const r of INDEX) {
    const low = r.t.toLowerCase();
    if (!terms.every(t => low.includes(t))) continue;
    const head = r.h ? 3 : 0, first = Math.min(...terms.map(t => low.indexOf(t)));
    hits.push({ r, score: head + (first < 40 ? 1 : 0) - low.length / 4000 });
  }
  hits.sort((a, b) => b.score - a.score);
  return { terms, hits: hits.slice(0, 60), total: hits.length };
}
function showSearch(query) {
  const res = search(query);
  if (!res.hits || !res.hits.length) { main.innerHTML = `<h1>Search</h1><p class="empty">Nothing found for “${esc(query)}”.</p>`; return; }
  main.innerHTML = `<h1>Search</h1><p class="empty">${res.total} match${res.total === 1 ? '' : 'es'} for “${esc(query)}”` +
    (res.total > 60 ? ' (the first 60)' : '') + `</p><div class="hits">` + res.hits.map(({ r }) => {
      const s = secById[r.sec], i = Math.max(0, r.t.toLowerCase().indexOf(res.terms[0]) - 60);
      const snip = (i ? '…' : '') + r.t.slice(i, i + 260) + (r.t.length > i + 260 ? '…' : '');
      return `<a class="hit" href="#${r.sec}${r.s ? '/' + r.s : ''}" data-q="${esc(query)}"><small>${esc(s.title)}${r.s ? ' › ' + esc(r.subt) : ''}</small>${mark(snip, res.terms)}</a>`;
    }).join('') + `</div>`;
  side.querySelectorAll('a.on').forEach(a => a.classList.remove('on'));
}
function cover() {
  main.innerHTML = `<div class="cover"><h1>Hortator manual</h1>
    <p class="lead">Everything the drum-machine firmware for the M-VAVE FM-1 does, section by section. Version ${esc(VERSION)}.</p>
    <p>New here? Start with <a href="#quick-start">Quick start</a>. Looking for one thing? Type in the search box, or press <kbd>/</kbd>.</p>
    <div class="grid">${SECTIONS.map(s => `<a href="#${s.id}"><b>${esc(s.title)}</b><small>${esc(s.blurb)}</small></a>`).join('')}</div></div>`;
  side.querySelectorAll('a.on').forEach(a => a.classList.remove('on'));
  document.title = 'Hortator Manual';
}
function showSection(id, sub, hl) {
  const s = secById[id];
  if (!s) return cover();
  const i = SECTIONS.indexOf(s), p = SECTIONS[i - 1], n = SECTIONS[i + 1];
  main.innerHTML = s.html.replaceAll('{sec}', s.id) +
    `<div class="pager">${p ? `<a href="#${p.id}"><small>Previous</small>← ${esc(p.title)}</a>` : '<span></span>'}` +
    `${n ? `<a href="#${n.id}"><small>Next</small>${esc(n.title)} →</a>` : '<span></span>'}</div>`;
  side.querySelectorAll('a.sec').forEach(a => a.classList.toggle('on', a.dataset.id === id));
  side.querySelectorAll('a.sub').forEach(a => a.classList.toggle('show', a.dataset.id === id));
  document.title = s.title + ' · Hortator Manual';
  const target = sub && document.getElementById(id + '/' + sub);
  if (target) target.scrollIntoView(); else window.scrollTo(0, 0);
  if (hl) { // highlight the searched words in the section
    const terms = hl.toLowerCase().split(/\s+/).filter(Boolean);
    const w = document.createTreeWalker(main, NodeFilter.SHOW_TEXT);
    const nodes = []; while (w.nextNode()) nodes.push(w.currentNode);
    for (const node of nodes) {
      if (!terms.some(t => node.nodeValue.toLowerCase().includes(t))) continue;
      const span = document.createElement('span'); span.innerHTML = mark(node.nodeValue, terms); node.replaceWith(span);
    }
  }
}
function glitch() {
  if (matchMedia('(prefers-reduced-motion: reduce)').matches) return;
  const el = main.querySelector('h1'); if (!el) return;
  el.dataset.text = el.textContent; el.classList.add('glitch');
  el.addEventListener('animationend', e => { if (e.animationName === 'glitch-base') el.classList.remove('glitching'); });
  void el.offsetWidth; el.classList.add('glitching');
}
function route() {
  document.body.classList.remove('open');
  const h = decodeURIComponent(location.hash.slice(1));
  if (!h) return cover();
  const [id, sub] = h.split('/');
  showSection(id, sub, q.value.trim() && h === lastHit ? q.value.trim() : '');
}
let lastHit = '';
main.addEventListener('click', e => {
  const a = e.target.closest('a.hit'); if (a) lastHit = decodeURIComponent(a.getAttribute('href').slice(1));
});
let timer;
q.addEventListener('input', () => {
  clearTimeout(timer);
  timer = setTimeout(() => { const v = q.value.trim(); if (v) showSearch(v); else route(); }, 120);
});
q.addEventListener('keydown', e => { if (e.key === 'Escape') { q.value = ''; q.blur(); route(); } });
document.addEventListener('keydown', e => {
  if (e.key === '/' && document.activeElement !== q && !e.metaKey && !e.ctrlKey) { e.preventDefault(); q.focus(); q.select(); }
});
$('#menu').addEventListener('click', () => document.body.classList.toggle('open'));
window.addEventListener('hashchange', () => { route(); glitch(); });
buildNav(); route(); glitch();
</script>
</body>
</html>
"""


def build(out):
    files = sorted(SRC.glob("[0-9][0-9]-*.md"))
    if not files:
        raise SystemExit(f"no sections in {SRC}")
    sections, index = [], []
    for f in files:
        text = f.read_text(encoding="utf-8")
        m = re.match(r"# (.+)\n+(?:<!--blurb: (.+?)-->\n+)?", text)
        if not m:
            raise SystemExit(f"{f.name}: must start with '# Title' (and optionally <!--blurb: ...-->)")
        title, blurb = m.group(1).strip(), (m.group(2) or "").strip()
        sid = f.stem[3:]
        body, subs, recs = render(text[m.end():])
        titles = dict(subs)
        sections.append({"id": sid, "title": title, "blurb": blurb, "subs": subs,
                         "html": f'<h1 id="{sid}">{inline(title)}</h1>\n' + body})
        index.append({"sec": sid, "s": "", "subt": "", "h": 1, "t": title})
        for r in recs:
            index.append({"sec": sid, "s": r["s"], "subt": titles.get(r["s"], ""), "h": 0, "t": r["t"]})
    version = (ROOT / "VERSION.txt").read_text().strip()
    safe = lambda o: json.dumps(o, ensure_ascii=False).replace("</", "<\\/")      # nothing can close the <script>
    page = (TEMPLATE.replace("/*SECTIONS*/", safe(sections))
            .replace("/*INDEX*/", safe(index))
            .replace("/*VERSION*/", json.dumps(version)))
    Path(out).write_text(page, encoding="utf-8")
    words = sum(len(r["t"].split()) for r in index)
    print(f"manual: {out}: {len(sections)} sections, {len(index)} entries, ~{words} words, {len(page)} B")


if __name__ == "__main__":
    build(sys.argv[1] if len(sys.argv) > 1 else SRC / "index.html")
