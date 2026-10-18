// md.js: Machinedrum-style 240x240 drawing helpers for the Hortator redesign mockups (throwaway).
window.MD = (function () {
  const F = {
    'A':'###,#.#,###,#.#,#.#','B':'##.,#.#,##.,#.#,##.','C':'###,#..,#..,#..,###','D':'##.,#.#,#.#,#.#,##.',
    'E':'###,#..,###,#..,###','F':'###,#..,###,#..,#..','G':'###,#..,#.#,#.#,###','H':'#.#,#.#,###,#.#,#.#',
    'I':'###,.#.,.#.,.#.,###','J':'..#,..#,..#,#.#,###','K':'#.#,#.#,##.,#.#,#.#','L':'#..,#..,#..,#..,###',
    'M':'#.#,###,#.#,#.#,#.#','N':'##.,#.#,#.#,#.#,#.#','O':'###,#.#,#.#,#.#,###','P':'###,#.#,###,#..,#..',
    'Q':'###,#.#,#.#,###,..#','R':'###,#.#,##.,#.#,#.#','S':'###,#..,###,..#,###','T':'###,.#.,.#.,.#.,.#.',
    'U':'#.#,#.#,#.#,#.#,###','V':'#.#,#.#,#.#,#.#,.#.','W':'#.#,#.#,#.#,###,#.#','X':'#.#,#.#,.#.,#.#,#.#',
    'Y':'#.#,#.#,###,.#.,.#.','Z':'###,..#,.#.,#..,###',
    '0':'###,#.#,#.#,#.#,###','1':'##.,.#.,.#.,.#.,###','2':'###,..#,###,#..,###','3':'###,..#,###,..#,###',
    '4':'#.#,#.#,###,..#,..#','5':'###,#..,###,..#,###','6':'###,#..,###,#.#,###','7':'###,..#,..#,..#,..#',
    '8':'###,#.#,###,#.#,###','9':'###,#.#,###,..#,###',
    ':':'.,#,.,#,.','.':'.,.,.,.,#',',':'.,.,.,#,#','-':'...,...,###,...,...','+':'...,.#.,###,.#.,...',
    '/':'..#,..#,.#.,#..,#..','>':'#..,##.,###,##.,#..','<':'..#,.##,###,.##,..#','?':'###,..#,.##,...,.#.',
    '!':'#,#,#,.,#','%':'#.#,..#,.#.,#..,#.#','_':'...,...,...,...,###','#':'#.#,###,#.#,###,#.#',
    '=':'...,###,...,###,...','(':'.#,#.,#.,#.,.#',')':'#.,.#,.#,.#,#.',"'":'#,#,.,.,.','*':'#.#,.#.,#.#,...,...',
    ' ':'...,...,...,...,...'
  };
  const BIG = {
    '0':'.###.,#####,##.##,##.##,##.##,##.##,#####,.###.','1':'###,###,.##,.##,.##,.##,.##,.##',
    '2':'.###.,##.##,...##,..##.,.##..,##...,#####,#####','3':'####.,...##,...##,.###.,...##,...##,#####,####.',
    '4':'##.##,##.##,##.##,#####,#####,...##,...##,...##','5':'#####,##...,####.,...##,...##,...##,#####,####.',
    '6':'.###.,##...,####.,#####,##.##,##.##,#####,.###.','7':'#####,#####,...##,..##.,..##.,.##..,.##..,.##..',
    '8':'.###.,##.##,##.##,.###.,##.##,##.##,#####,.###.','9':'.###.,#####,##.##,#####,.####,...##,...##,.###.'
  };
  const ICON = {
    play:'#....,##...,###..,##...,#....', stop:'#####,#####,#####,#####,#####',
    recOn:'.###.,#####,#####,#####,.###.', recArm:'.###.,#...#,#...#,#...#,.###.',
    usb:'.#.#.,.#.#.,#####,.###.,..#..'
  };
  const THEMES = {
    red: ['#e0472b', '#38100a'], neg: ['#170605', '#f0532f'], blue: ['#2445c8', '#eaf1ff'], green: ['#a3bf3c', '#1c2a08']
  };

  function make(canvas, theme) {
    const [BGC, INKC] = THEMES[theme || 'red'];
    const g = canvas.getContext('2d');
    const pen = (c) => (x, y, w, h) => { g.fillStyle = c; g.fillRect(x, y, w || 1, h || 1); };
    const k = { g, P: pen(INKC), B: pen(BGC), W: canvas.width, H: canvas.height };
    g.fillStyle = BGC; g.fillRect(0, 0, canvas.width, canvas.height);
    const up = (s) => String(s).toUpperCase();
    k.glyph = (map, ch, x, y, s, p) => {
      const rows = (map[ch] || map[' ']).split(','), w = rows[0].length;
      rows.forEach((r, j) => { for (let i = 0; i < r.length; i++) if (r[i] === '#') p(x + i * s, y + j * s, s, s); });
      return (w + 1) * s;
    };
    k.text = (s, x, y, o = {}) => { const sc = o.sc || 2, p = o.p || k.P; for (const ch of up(s)) x += k.glyph(F, ch, x, y, sc, p); return x; };
    k.tw = (s, sc = 2) => [...up(s)].reduce((a, ch) => a + ((F[ch] || F[' ']).split(',')[0].length + 1) * sc, 0) - sc;
    k.ctext = (s, cx, y, o = {}) => k.text(s, Math.round(cx - k.tw(s, o.sc || 2) / 2), y, o);
    k.rtext = (s, rx, y, o = {}) => k.text(s, rx - k.tw(s, o.sc || 2), y, o);
    k.big = (s, x, y, sc = 2, p = k.P) => { for (const ch of s) x += k.glyph(BIG, ch, x, y, sc, p); return x; };
    k.icon = (name, x, y, p = k.P, sc = 2) => k.glyph(ICON, name, x, y, sc, p);
    k.hline = (x0, x1, y, p = k.P) => p(x0, y, x1 - x0 + 1, 1);
    k.vline = (x, y0, y1, p = k.P) => p(x, y0, 1, y1 - y0 + 1);
    k.hdot = (x0, x1, y, p = k.P) => { for (let x = x0; x <= x1; x += 2) p(x, y); };
    k.vdot = (x, y0, y1, p = k.P) => { for (let y = y0; y <= y1; y += 2) p(x, y); };
    k.frame = (x, y, w, h, p = k.P) => { k.hline(x, x + w - 1, y, p); k.hline(x, x + w - 1, y + h - 1, p); k.vline(x, y, y + h - 1, p); k.vline(x + w - 1, y, y + h - 1, p); };
    k.dither = (x, y, w, h, p = k.P) => { for (let yy = y; yy < y + h; yy++) for (let xx = x; xx < x + w; xx++) if ((xx + yy) % 2 === 0) p(xx, yy); };
    k.ring = (cx, cy, r, p = k.P) => {
      let x = r, y = 0, e = 1 - r;
      while (x >= y) { [[x, y], [y, x], [-y, x], [-x, y], [-x, -y], [-y, -x], [y, -x], [x, -y]].forEach(([a, b]) => p(cx + a, cy + b)); y++; if (e < 0) e += 2 * y + 1; else { x--; e += 2 * (y - x) + 1; } }
    };
    k.knob = (cx, cy, r, v, o = {}) => {
      const p = o.p || k.P;
      k.ring(cx, cy, r, p); p(cx, cy - r - 2, 1, 1);
      const es = (a) => p(Math.round(cx + Math.cos(a) * (r + 2)) - 1, Math.round(cy - Math.sin(a) * (r + 2)), 2, 1);
      es(Math.PI * 1.25); es(-Math.PI * 0.25);
      const a = Math.PI * 1.25 - v * Math.PI * 1.5, d = r - 3.5;
      p(Math.round(cx + Math.cos(a) * d) - 1, Math.round(cy - Math.sin(a) * d) - 1, 3, 3);
      if (o.lfo != null) {
        const b = Math.PI * 1.25 - o.lfo * Math.PI * 1.5, dd = r + 3;
        const x = Math.round(cx + Math.cos(b) * dd) - 1, y = Math.round(cy - Math.sin(b) * dd) - 1;
        p(x, y, 3, 1); p(x, y + 2, 3, 1); p(x, y + 1); p(x + 2, y + 1);
      }
    };
    k.tab = (s, x, y) => { const w = k.tw(s) + 6; k.P(x, y, w, 13); k.text(s, x + 3, y + 2, { p: k.B }); return w; };
    // MD window: 1px top/left, 2px right/bottom (shadow), notched corners, title tab centred on the top edge
    k.popup = (x, y, w, h, title) => {
      k.B(x, y, w, h);
      k.hline(x, x + w - 1, y); k.vline(x, y, y + h - 1);
      k.P(x + w - 2, y, 2, h + 1); k.P(x, y + h - 1, w, 2);
      k.B(x, y, 1, 1); k.B(x + w - 1, y, 1, 1); k.B(x, y + h, 1, 1);
      if (title) { const tw = k.tw(title) + 12, tx = Math.round(x + w / 2 - tw / 2); k.P(tx, y - 6, tw, 15); k.text(title, tx + 6, y - 3, { p: k.B }); }
    };
    // keycap: outlined box with a key name, the action after it
    k.keycap = (key, act, x, y) => { const w = k.tw(key) + 6; k.frame(x, y, w, 14); k.text(key, x + 3, y + 2); return act ? k.text(act, x + w + 4, y + 2) : x + w; };
    k.button = (s, x, y, w, on) => { if (on) { k.P(x, y, w, 15); k.ctext(s, x + w / 2, y + 3, { p: k.B }); } else { k.frame(x, y, w, 15); k.ctext(s, x + w / 2, y + 3); } };
    k.row = (s, right, y, sel, x0 = 2, x1 = 237) => {
      if (sel) { k.P(x0, y - 2, x1 - x0 + 1, 14); k.text(s, x0 + 4, y, { p: k.B }); if (right != null) k.rtext(right, x1 - 4, y, { p: k.B }); }
      else { k.text(s, x0 + 4, y); if (right != null) k.rtext(right, x1 - 4, y); }
    };
    k.hbar = (x, y, w, v, ticks = true) => {   // LEV-style horizontal meter: solid fill + tick dashes
      k.P(x, y, Math.round(w * v), 6);
      if (ticks) for (let t = x; t <= x + w; t += 6) { k.P(t, y - 3, 1, 2); k.P(t, y + 7, 1, 2); }
    };
    // 16 trig boxes: 10x10 on a 14 px pitch (4 px gaps) + 2 px extra between beats, so boxes never read as tied;
    // the playhead is an outline 2 px out, still clear of the neighbours
    k.trigs = (trig, step, sy, x0 = 6) => {
      trig.forEach((on, i) => {
        const sx = x0 + i * 14 + Math.floor(i / 4) * 2;
        if (on) k.P(sx, sy, 10, 10); else k.frame(sx, sy, 10, 10);
        if (i === step) k.frame(sx - 2, sy - 2, 14, 14);
        if (i % 4 === 0) k.P(sx + 4, sy + 14, 2, 2);
      });
    };
    return k;
  }

  // Layout A: inverted top bar, 4 compact knob cells, graph y61..180, trig strip, breadcrumb.
  function frameA(k, o) {
    const st = o.st || { playing: 1 };
    k.P(0, 0, 240, 16);
    k.icon(st.rec === 2 ? 'recOn' : st.rec === 1 ? 'recArm' : st.playing ? 'play' : 'stop', 4, 3, k.B);
    let x = k.text(o.bpm || '120', 18, 3, { p: k.B });
    if (st.fx) { k.B(x + 3, 1, 19, 14); k.text('FX', x + 6, 3); x += 24; }
    if (st.mode) k.text(st.mode, x + 6, 3, { p: k.B });
    k.text(o.track || 'T1', 150, 3, { p: k.B });
    k.icon('usb', 186, 3, k.B);
    k.frame(220, 4, 14, 8, k.B); k.B(234, 6, 2, 4); for (let i = 0; i < 3; i++) k.B(222 + i * 4, 6, 3, 4);
    k.frame(0, 0, 240, 240);
    if (o.cells) {
      [60, 120, 180].forEach(xd => k.vdot(xd, 17, 59));
      o.cells.forEach((c, i) => {
        if (!c) return;
        const x0 = i * 60, inv = c.lock, p = inv ? k.B : k.P;
        if (inv) k.P(x0 + 1, 17, 58, 43);
        k.ctext(c[0], x0 + 30, 19, { p });
        k.hdot(x0 + 2, x0 + 58, 31, p);
        if (typeof c[2] === 'number') {
          k.knob(x0 + 17, 45, 8, c[2], { p, lfo: c.lfo });
          if (c.turn) { const w = k.tw(c[1]) + 4; k.P(x0 + 55 - w + 2, 39, w, 13); k.rtext(c[1], x0 + 55, 41, { p: k.B }); }
          else k.rtext(c[1], x0 + 55, 41, { p });
        } else k.ctext(c[1], x0 + 30, 41, { p });   // discrete value: text instead of a knob (MD rule)
      });
      k.hline(1, 238, 60);
    }
    if (o.trig !== false) {
      k.hdot(1, 238, 181);
      const trig = o.trig || [1,0,0,0, 1,0,0,0, 1,0,1,0, 1,0,0,1];
      k.trigs(trig, o.step ?? 5, 188);
    }
    k.P(1, 212, 238, 27);
    k.text(o.crumb || 'T1>K909>SOUND', 6, 220, { p: k.B });
    if (o.page) k.rtext(o.page, 233, 220, { p: k.B });
    return { x0: 1, y0: o.cells ? 61 : 17, x1: 238, y1: o.trig === false ? 210 : 180 };
  }

  return { make, frameA, THEMES };
})();
