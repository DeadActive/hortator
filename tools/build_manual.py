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
<style>
@font-face { font-family: "Hortator Terminus"; src: url("../terminus.woff") format("woff"); font-display: swap; }
:root {
  --bg:#000; --rule:#282828; --dim:#505050; --label:#828282; --text2:#bababa; --text:#e2e2e2; --hi:#fff;
  --mono:"Hortator Terminus", ui-monospace, "SF Mono", Menlo, Consolas, monospace;
  --mark:#fff; color-scheme: dark;
}
:root[data-theme="light"] { --bg:#f4f4f2; --rule:#d4d4d0; --dim:#9a9a96; --label:#6a6a66; --text2:#3a3a38; --text:#1a1a18; --hi:#000; --mark:#000; color-scheme: light; }
@media (prefers-color-scheme: light) { :root:not([data-theme="dark"]) { --bg:#f4f4f2; --rule:#d4d4d0; --dim:#9a9a96; --label:#6a6a66; --text2:#3a3a38; --text:#1a1a18; --hi:#000; --mark:#000; color-scheme: light; } }
* { box-sizing:border-box; }
html { scroll-padding-top:72px; }
body { margin:0; background:var(--bg); color:var(--text); font:16px/1.6 var(--mono); -webkit-font-smoothing:none; }
a { color:var(--hi); text-underline-offset:3px; }
header.top { position:sticky; top:0; z-index:5; background:var(--bg); border-bottom:2px solid var(--rule);
  display:flex; gap:16px; align-items:center; padding:10px 16px; }
header.top .brand { color:var(--hi); text-decoration:none; letter-spacing:.2em; white-space:nowrap; }
header.top .brand span { color:var(--label); letter-spacing:0; margin-left:8px; }
#q { flex:1; min-width:0; background:transparent; color:var(--hi); border:2px solid var(--dim); padding:6px 10px;
  font:inherit; border-radius:0; }
#q:focus { outline:none; border-color:var(--hi); }
#q::placeholder { color:var(--label); }
.btn { background:transparent; color:var(--text); border:2px solid var(--dim); font:inherit; padding:6px 10px; cursor:pointer; }
.btn:hover, .btn:focus-visible { border-color:var(--hi); color:var(--hi); outline:none; }
#menu { display:none; }
.layout { display:grid; grid-template-columns:280px minmax(0,1fr); max-width:1240px; margin:0 auto; }
nav#side { position:sticky; top:58px; align-self:start; max-height:calc(100vh - 58px); overflow:auto; padding:16px 12px 40px 16px;
  border-right:2px solid var(--rule); font-size:14px; line-height:1.5; }
nav#side a { display:block; color:var(--text2); text-decoration:none; padding:3px 8px; }
nav#side a:hover { color:var(--hi); }
nav#side a.sec { color:var(--text); margin-top:10px; }
nav#side a.sub { display:none; padding-left:22px; color:var(--label); font-size:13px; }
nav#side a.sub.show { display:block; }
nav#side a.on { background:var(--hi); color:var(--bg); }
main { padding:24px 32px 80px; min-width:0; max-width:900px; }
main h1 { font-size:32px; line-height:1.2; margin:0 0 8px; color:var(--hi); font-weight:normal; letter-spacing:.04em; }
main h2 { font-size:20px; margin:40px 0 8px; padding-bottom:6px; border-bottom:2px solid var(--rule); color:var(--hi); font-weight:normal; }
main h3 { font-size:16px; margin:26px 0 4px; color:var(--hi); font-weight:normal; letter-spacing:.1em; }
main h3::before { content:"\25A0 "; color:var(--dim); }
main p, main li { color:var(--text); }
main li { margin:2px 0; }
main ul, main ol { padding-left:24px; }
code { color:var(--hi); background:var(--rule); padding:0 5px; }
kbd { display:inline-block; font:inherit; font-size:14px; border:2px solid var(--label); color:var(--hi); padding:0 6px; line-height:1.3; margin:0 1px; white-space:nowrap; }
pre { background:var(--rule); padding:12px 14px; overflow:auto; line-height:1.4; }
.note { border-left:4px solid var(--hi); padding:6px 14px; background:var(--rule); }
.tw { overflow-x:auto; margin:12px 0; }
table { border-collapse:collapse; min-width:100%; font-size:14px; line-height:1.45; }
th { text-align:left; color:var(--label); font-weight:normal; border-bottom:2px solid var(--dim); padding:4px 10px 4px 0; white-space:nowrap; }
td { vertical-align:top; padding:5px 12px 5px 0; border-bottom:1px solid var(--rule); }
td:first-child { color:var(--hi); } td:first-child b { white-space:nowrap; }
.lead { color:var(--text2); font-size:18px; }
.pager { display:flex; justify-content:space-between; gap:16px; margin-top:56px; padding-top:16px; border-top:2px solid var(--rule); }
.pager a { color:var(--text); text-decoration:none; border:2px solid var(--dim); padding:8px 14px; }
.pager a:hover { border-color:var(--hi); color:var(--hi); }
.pager small { display:block; color:var(--label); font-size:13px; }
.hits { display:grid; gap:8px; }
.hit { display:block; text-decoration:none; color:var(--text); border:2px solid var(--rule); padding:10px 14px; }
.hit:hover, .hit:focus-visible { border-color:var(--hi); outline:none; }
.hit small { color:var(--label); display:block; }
.hit mark, main mark { background:var(--mark); color:var(--bg); padding:0 1px; }
.empty { color:var(--label); }
.cover { margin:8px 0 28px; }
.cover .grid { display:grid; grid-template-columns:repeat(auto-fill,minmax(230px,1fr)); gap:10px; margin-top:16px; }
.cover .grid a { display:block; text-decoration:none; border:2px solid var(--rule); padding:12px 14px; color:var(--text); }
.cover .grid a:hover { border-color:var(--hi); }
.cover .grid b { display:block; color:var(--hi); font-weight:normal; }
.cover .grid small { color:var(--label); }
@media (max-width: 860px) {
  .layout { display:block; }
  #menu { display:inline-block; }
  nav#side { display:none; position:fixed; inset:58px 0 0 0; max-height:none; background:var(--bg); z-index:4; border:0; padding:16px; }
  body.open nav#side { display:block; }
  main { padding:20px 16px 64px; }
  header.top .brand span { display:none; }
}
@media print {
  header.top, nav#side, .pager { display:none; }
  .layout { display:block; }
  body { background:#fff; color:#000; }
}
</style>
</head>
<body>
<header class="top">
  <button class="btn" id="menu" aria-label="Sections">&#9776;</button>
  <a class="brand" href="#">HORTATOR<span>MANUAL</span></a>
  <input id="q" type="search" placeholder="Search the manual ( / )" autocomplete="off" aria-label="Search the manual">
  <button class="btn" id="theme" aria-label="Toggle light and dark">&#9680;</button>
</header>
<div class="layout">
  <nav id="side" aria-label="Sections"></nav>
  <main id="main" tabindex="-1"></main>
</div>
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
$('#theme').addEventListener('click', () => {
  const root = document.documentElement, dark = getComputedStyle(root).colorScheme === 'dark';
  root.dataset.theme = dark ? 'light' : 'dark';
  try { localStorage.setItem('hortator-manual-theme', root.dataset.theme); } catch (e) {}
});
try { const t = localStorage.getItem('hortator-manual-theme'); if (t) document.documentElement.dataset.theme = t; } catch (e) {}
window.addEventListener('hashchange', route);
buildNav(); route();
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
