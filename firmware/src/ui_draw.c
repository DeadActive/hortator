/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Drum UI drawing in the Machinedrum style (spec docs/superpowers/specs/2026-10-19-md-ui-design.md): the top bar,
 * the knob cells, the graphs, the popups, the trig strip and the breadcrumb; two colours (C_BG, C_INK) */
static void draw_menu(void);

static uint32_t str_hash(uint32_t h, const char *s)
{
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

/* src cut to n - 1 characters at most, and no wider than maxw in MD_F3 at 2x */
static void md_fit(char *d, const char *src, uint32_t n, int32_t maxw)
{
    str_cpy(d, src, n);
    while (d[0] && md_text_w(&MD_F3, 2, d) > maxw)
        d[str_len(d) - 1u] = 0;
}


/* ---- partial redraws (the LCD transfer is the cost: ~1.33 us a pixel at 12 MHz, the whole screen ~77 ms). A region
 * (a strip, a row, the graph) draws its canvas whole when its signature moves, else only windows of it (cv_begin_win):
 * the parts that changed, and the damage: what a popup left this frame (rg.dmg, less the popups still up, rg.ex) */
typedef struct {
    int16_t x, y, w, h;
} rc_t;
#define RG_WIN 40u
static struct {
    rc_t dmg;                    /* the screen rect a popup left / uncovered this frame (w 0: none) */
    rc_t ex[3];                  /* the popups up (drawn this frame, or there): no window covers them */
    uint8_t nex;
    uint8_t noex;                /* windows not less rg.ex (PRESETS' popup drawn whole: over the others, as before) */
    uint8_t thaw;                /* a popup came / went this frame: the graph draws as it is now (round the popups) */
    int16_t rx, ry, rw, rh;      /* the region planned (screen) */
    uint8_t n;                   /* its windows (its canvas's coordinates) */
    rc_t win[RG_WIN];
} rg;

static int rc_cut(rc_t *a, rc_t b)                   /* a = a & b; 0: empty */
{
    int32_t x0 = a->x > b.x ? a->x : b.x, y0 = a->y > b.y ? a->y : b.y;
    int32_t x1 = a->x + a->w < b.x + b.w ? a->x + a->w : b.x + b.w;
    int32_t y1 = a->y + a->h < b.y + b.h ? a->y + a->h : b.y + b.h;
    if (x1 <= x0 || y1 <= y0)
        return 0;
    a->x = (int16_t)x0, a->y = (int16_t)y0, a->w = (int16_t)(x1 - x0), a->h = (int16_t)(y1 - y0);
    return 1;
}

static void rg_damage(int32_t x, int32_t y, int32_t w, int32_t h)   /* dmg grows to hold this rect */
{
    if (rg.dmg.w) {
        int32_t x1 = rg.dmg.x + rg.dmg.w > x + w ? rg.dmg.x + rg.dmg.w : x + w;
        int32_t y1 = rg.dmg.y + rg.dmg.h > y + h ? rg.dmg.y + rg.dmg.h : y + h;
        x = rg.dmg.x < x ? rg.dmg.x : x;
        y = rg.dmg.y < y ? rg.dmg.y : y;
        w = x1 - x;
        h = y1 - y;
    }
    rg.dmg.x = (int16_t)x, rg.dmg.y = (int16_t)y, rg.dmg.w = (int16_t)w, rg.dmg.h = (int16_t)h;
}

static void rg_keep(int32_t x, int32_t y, int32_t w, int32_t h)    /* a popup still up: kept out of the damage */
{
    if (rg.nex < 3u) {
        rc_t *e = &rg.ex[rg.nex++];
        e->x = (int16_t)x, e->y = (int16_t)y, e->w = (int16_t)w, e->h = (int16_t)h;
    }
}

static int rg_meets(int32_t x, int32_t y, int32_t w, int32_t h)    /* the damage touches this screen rect */
{
    rc_t a = {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
    return rg.dmg.w && rc_cut(&a, rg.dmg);
}

static void rg_region(int32_t x, int32_t y, int32_t w, int32_t h)  /* plan a region's windows (none yet) */
{
    rg.rx = (int16_t)x, rg.ry = (int16_t)y, rg.rw = (int16_t)w, rg.rh = (int16_t)h;
    rg.n = 0;
}

/* a window, in the region's coordinates: less the popups up (rg.ex; unless rg.noex), each one splitting a piece in up
 * to four (above, below, left, right) */
static void rg_add(int32_t x, int32_t y, int32_t w, int32_t h)
{
    static rc_t p[64], q[64];                         /* (3 rects: 4 ^ 3 pieces at most) */
    rc_t a = {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h}, r = {0, 0, rg.rw, rg.rh};
    uint32_t n = 1, m, i, k;
    if (!rc_cut(&a, r))
        return;
    a.x = (int16_t)(a.x + rg.rx), a.y = (int16_t)(a.y + rg.ry);
    p[0] = a;
    for (k = 0; k < (rg.noex ? 0u : rg.nex); k++) {
        rc_t e = rg.ex[k];
        for (i = m = 0; i < n; i++) {
            rc_t c = p[i], o = e;
            if (!rc_cut(&o, c)) {                     /* clear of it: whole */
                q[m++] = c;
                continue;
            }
            if (o.y > c.y)
                q[m++] = (rc_t){c.x, c.y, c.w, (int16_t)(o.y - c.y)};
            if (o.y + o.h < c.y + c.h)
                q[m++] = (rc_t){c.x, (int16_t)(o.y + o.h), c.w, (int16_t)(c.y + c.h - o.y - o.h)};
            if (o.x > c.x)
                q[m++] = (rc_t){c.x, o.y, (int16_t)(o.x - c.x), o.h};
            if (o.x + o.w < c.x + c.w)
                q[m++] = (rc_t){(int16_t)(o.x + o.w), o.y, (int16_t)(c.x + c.w - o.x - o.w), o.h};
        }
        for (i = 0; i < m; i++)
            p[i] = q[i];
        n = m;
    }
    for (i = 0; i < n && rg.n < RG_WIN; i++) {
        p[i].x = (int16_t)(p[i].x - rg.rx), p[i].y = (int16_t)(p[i].y - rg.ry);
        rg.win[rg.n++] = p[i];
    }
}

static void rg_whole(void) { rg_add(0, 0, rg.rw, rg.rh); }

static void rg_clip_h(int32_t h)                     /* the windows planned, less the region's rows from h */
{
    uint32_t i, n = 0;
    rc_t c = {0, 0, rg.rw, (int16_t)h};
    for (i = 0; i < rg.n; i++) {
        rc_t a = rg.win[i];
        if (rc_cut(&a, c))
            rg.win[n++] = a;
    }
    rg.n = (uint8_t)n;
}

static void rg_damaged(void)                         /* the region's part in the damage */
{
    rc_t a = {rg.rx, rg.ry, rg.rw, rg.rh};
    if (rg.dmg.w && rc_cut(&a, rg.dmg))
        rg_add(a.x - rg.rx, a.y - rg.ry, a.w, a.h);
}

/* a region with one signature (screen x, y, w x h; *st the one shown): whole when ui.force or it moved, else its
 * damage. The windows' count */
static uint32_t rg_plan(uint32_t *st, uint32_t sig, int32_t x, int32_t y, int32_t w, int32_t h)
{
    rg_region(x, y, w, h);
    if (ui.force || sig != *st) {
        *st = sig;
        rg_whole();
        return rg.n;
    }
    rg_damaged();
    return rg.n;
}

static void rg_win(uint32_t i, uint32_t sy, uint16_t bg)          /* begin window i (sy: the canvas's screen row) */
{
    cv_begin_win((uint32_t)rg.win[i].x, (uint32_t)rg.win[i].y, (uint32_t)rg.win[i].w, (uint32_t)rg.win[i].h, sy, bg);
}

static void rg_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* a fill: whole on ui.force, else its
                                                                                * damage */
{
    uint32_t i;
    rg_region(x, y, w, h);
    if (ui.force)
        rg_whole();
    else
        rg_damaged();
    for (i = 0; i < rg.n; i++)
        lcd_fill((uint32_t)(x + rg.win[i].x), (uint32_t)(y + rg.win[i].y), (uint32_t)rg.win[i].w, (uint32_t)rg.win[i].h,
                 c);
}

static int32_t batt_level(void)
{
    return song.batt_raw >= 591 ? 3 : song.batt_raw >= 561 ? 2 : song.batt_raw >= 531 ? 1 : 0;
}
static int32_t batt_shown(void)
{
    if (usb.config && !usb.suspended)
        return 1 + (int32_t)((fm1_ms / 600u) % 3u);
    return batt_level();
}

/* the top bar's mode (spec §7): "STEP REC 05" (step record, the cursor's step), else KEYS and its lowest key's note
 * ("KEYS F3"); 0 (b "") when neither */
static int head_mode(char *b)
{
    uint32_t ls = lock_view_step();
    b[0] = 0;
    if (ls < NSTEP) {                                 /* a step held in STEPS: its locks on the knobs */
        str_cpy(b, "LOCK STEP ", 16);
        fmt_int(b + 10, (int32_t)ls + 1);
        return 1;
    }
    if (ui.srec) {
        str_cpy(b, "STEP REC ", 16);
        b[9] = (char)('0' + (ui.sr_cur + 1u) / 10u);
        b[10] = (char)('0' + (ui.sr_cur + 1u) % 10u);
        b[11] = 0;
        return 1;
    }
    if (!ui.keys)
        return 0;
    str_cpy(b, "KEYS ", 16);
    note_text(b + 5, 53 + 12 * song.octave);
    return 1;
}

/* the CPU icon (spec 2026-10-11 §10): on for 2 s after an overload shed (audio.c song.sheds) */
static uint32_t cpu_alert(void)
{
    if (song.sheds != ui.sheds) {
        ui.sheds = song.sheds;
        ui.cpu_ms = fm1_ms;
        ui.cpu_on = 1;
    }
    if (ui.cpu_on && fm1_ms - ui.cpu_ms >= 2000u)
        ui.cpu_on = 0;
    return ui.cpu_on;
}

#if FELUCCA_BLE
static uint32_t bt_head_icon(void);                    /* bt_page.c: 0 none, 1 struck (on, nothing linked), 2 lit */
static void draw_bt_icon(int32_t cx, uint32_t k)       /* the rune in BG lines, 7 x 10 px about cx; struck: a diagonal */
{
    const int32_t t = 3, b = 12;
    cv_line(cx, t, cx, b, C_BG);                       /* the spine, the two arrows on its right */
    cv_line(cx, t, cx + 3, t + 3, C_BG);
    cv_line(cx + 3, t + 3, cx - 3, b - 3, C_BG);
    cv_line(cx, b, cx + 3, b - 3, C_BG);
    cv_line(cx + 3, b - 3, cx - 3, t + 3, C_BG);
    if (k == 1u)
        cv_line(cx - 5, b + 1, cx + 5, t - 1, C_BG);
}
#endif
/* the top bar (spec 2026-10-19-md-ui §5): an ink bar; in BG the state glyph (stop, play, REC armed elsewhere: an
 * outlined circle, the selected track armed: solid), BPM, FX (a PERFORM knob effect on), the mode or the song's row,
 * T<n>, the CPU chip (steady for 2 s after an overload), USB, battery. Messages are a popup (draw_msg). */
static void draw_head(void)
{
    char b[16], m[16];
    uint32_t k;
    int32_t x, lvl;
    uint32_t rec = (song.rec >> song.sel) & 1u ? 2u : song.rec != 0u, cpu = cpu_alert();
    uint32_t pfx = (perf_k[0] | perf_k[1] | perf_k[2]) != 0;   /* PERFORM's knob effects on (spec §7; DEPTH acts only
                                                                 * with a held black key) */
    uint32_t blink = cpu;                             /* steady for its 2 s (cpu_alert) */
    uint32_t sig = (uint32_t)song.playing * 3u + rec * 5u + song.sel * 13131u + (uint32_t)song.g[G_BPM] * 101u +
                   (uint32_t)batt_shown() * 7777u + (usb.config && !usb.suspended) * 99991u +
                   (chain.running ? (chain.row + 1u) * 104729u : 0u) + ui.keys * 7919u +
                   (uint32_t)(song.octave + 2) * 1009u + ui.srec * 3571u + ui.sr_cur * 211u + pfx * 6151u +
                   (lock_view_step() + 1u) * 92821u + ui.env * 15485863u;
    sig += cpu * 50021u + blink * 27u;
#if FELUCCA_BLE
    sig += bt_head_icon() * 2654435761u;               /* (the Bluetooth icon: none, struck, lit) */
#endif
    if (!ui.force && sig == ui.head_sig)
        return;
    ui.head_sig = sig;
    cv_begin(240, H_HEAD, C_INK);
    md_text(4, 3, &MD_ICON, 2, rec == 2u ? "r" : rec ? "a" : song.playing ? "p" : "s", C_BG);
    fmt_int(b, song.g[G_BPM]);
    x = md_text(18, 3, &MD_F3, 2, b, C_BG);
    if (pfx) {                                        /* FX: an inverted tag after the BPM */
        cv_rect(x + 3, 1, 19, 14, C_BG);
        md_text(x + 6, 3, &MD_F3, 2, "FX", C_INK);
        x += 22;
    }
    if (head_mode(m)) {
        x = md_text(x + 6, 3, &MD_F3, 2, m, C_BG);    /* KEYS / STEP REC: before the song's row */
    } else if (chain.running) {                       /* a song: its row */
        str_cpy(b, "SONG ", sizeof b);
        fmt_int(b + 5, (int32_t)chain.row + 1);
        x = md_text(x + 6, 3, &MD_F3, 2, b, C_BG);
    }
    b[0] = 'T';
    b[1] = (char)('1' + song.sel);
    b[2] = 0;
    x = md_text(x + 8 > 150 ? x + 8 : 150, 3, &MD_F3, 2, b, C_BG);
    if (blink) {                                      /* the CPU chip: a 5x5 die with three pins a side */
        int32_t cx = x + 4;
        cv_frame(cx + 2, 5, 5, 5, C_BG);
        cv_pset(cx + 4, 7, C_BG);
        for (k = 0; k < 3u; k++) {
            cv_pset(cx + 2 + (int32_t)k * 2, 3, C_BG);
            cv_pset(cx + 2 + (int32_t)k * 2, 11, C_BG);
            cv_pset(cx, 5 + (int32_t)k * 2, C_BG);
            cv_pset(cx + 8, 5 + (int32_t)k * 2, C_BG);
        }
    }
#if FELUCCA_BLE
    if (bt_head_icon() && (blink ? x + 13 : x) <= 180)   /* Bluetooth (on: struck, linked: whole), left of USB
                                                       * (x 181..191); not when a long mode text took the room */
        draw_bt_icon(186, bt_head_icon());
#endif
    if (usb.config && !usb.suspended)
        md_text(196, 3, &MD_ICON, 2, "u", C_BG);
    if (ui.env) {                                     /* STEPS (parameter locks): a padlock, the icons' 5 x 5 at 2x,
                                                       * between USB and the battery */
        static const uint8_t PADLOCK[5] = {0x0E, 0x11, 0x1F, 0x1B, 0x1F};   /* .###. #...# ##### ##.## ##### */
        uint32_t r, c;
        for (r = 0; r < 5u; r++)
            for (c = 0; c < 5u; c++)
                if ((PADLOCK[r] >> (4u - c)) & 1u)
                    cv_rect(207 + (int32_t)c * 2, 3 + (int32_t)r * 2, 2, 2, C_BG);
    }
    lvl = batt_shown();                               /* the battery: outline, nub, up to 3 cells */
    cv_frame(220, 4, 14, 8, C_BG);
    cv_rect(234, 6, 2, 4, C_BG);
    for (k = 0; k < (uint32_t)lvl && k < 3u; k++)
        cv_rect(222 + (int32_t)k * 4, 6, 3, 4, C_BG);
    cv_blit(0, Y_HEAD);
}

/* the fixed parts (ui.force): row 16, the knob row's ground, its dotted dividers at x 60 / 120 / 180, the rule at y 60;
 * the 1 px frame's edges where no region draws them */
static void draw_frame(void)
{
    int32_t y;
    lcd_fill(1, H_HEAD, 238, 1, C_BG);                /* (only what no region covers: nothing sent twice) */
    lcd_fill(0, H_HEAD, 1, 1, C_INK);
    lcd_fill(239, H_HEAD, 1, 1, C_INK);
    if (ui.home && !ui.layer)                         /* the HOME strip: y 17..68 */
        return;
    lcd_fill(0, Y_KNOB, 1, H_KNOB, C_INK);           /* the knob cells: x 1..59, 61..119, 121..179, 181..238 */
    lcd_fill(239, Y_KNOB, 1, H_KNOB, C_INK);
    cv_begin(1, H_KNOB, C_BG);                        /* the dotted dividers */
    cv_oy = 0;
    for (y = 0; y < H_KNOB; y += 2)
        cv_pset(0, y, C_INK);
    cv_blit(60, Y_KNOB);
    cv_blit(120, Y_KNOB);
    cv_blit(180, Y_KNOB);
    lcd_fill(0, Y_GRAPH - 1, 240, 1, C_INK);
}

/* a knob cell (spec 2026-10-19-md-ui §5, §6), 59 x 43 at x c*60+1: the label centred over a dotted rule; a
 * continuous parameter (ratio 0..1000): the MD knob, its LFO's live value (mod) a hollow dot outside it, the value
 * right of it; a discrete one (ratio < 0): its text centred, no knob. st CV_HOT: touched (the value inverted, and
 * the breadcrumb shows it whole: ui.focus_*); CV_DIM: inactive (the value dithered). At most 4 glyphs of the value. */
static void draw_column_m(uint32_t c, const char *label, const char *val, const char *unit, uint32_t st,
                          int32_t ratio, int32_t mod)
{
    char l[8], v[8], key[40];
    int32_t w = c == 3u ? 58 : 59, vw, vx;
    uint32_t touched = st == CV_HOT, dim = st == CV_DIM, n;
    md_fit(l, label, sizeof l, w - 2);
    md_fit(v, val, 5, ratio >= 0 ? 30 : w - 4);
    n = str_len(v);
    if (n > 1u && v[n - 1u] == '.')                  /* "-24." -> "-24" */
        v[n - 1u] = 0;
    str_cpy(key, l, 8);
    str_cpy(key + str_len(key), "|", 2);
    str_cpy(key + str_len(key), v, 8);
    n = str_len(key);
    key[n] = (char)('A' + touched + dim * 2u);
    key[n + 1] = (char)(' ' + (ratio < 0 ? 0 : 1 + ratio / 10));
    key[n + 2] = (char)(' ' + (mod < 0 ? 0 : 1 + mod / 10));
    key[n + 3] = 0;
    if (c == ui.hot_col) {
        str_cpy(ui.focus_l, label, 8);
        str_cpy(ui.focus_v, val, 8);
        str_cpy(ui.focus_u, unit, 8);
    }
    if (!ui.force && str_eq(key, ui.col[c]))
        return;
    str_cpy(ui.col[c], key, sizeof ui.col[c]);
    cv_begin_at((uint32_t)w, H_KNOB, Y_KNOB, C_BG);
    if (l[0]) {
        md_text((w - md_text_w(&MD_F3, 2, l)) / 2, 2, &MD_F3, 2, l, C_INK);
        cv_hdot(1, w - 2, 14, C_INK);
    }
    vw = md_text_w(&MD_F3, 2, v);
    if (ratio >= 0) {                                 /* the knob, the value right of it (4 glyphs fit, 3 px clear of
                                                       * the LFO dot) */
        cv_knob(14, 28, 8, ratio, mod, C_INK);
        vx = 57 - vw;
    } else {
        vx = (w - vw) / 2;
    }
    if (touched && v[0]) {
        cv_rect(vx - 2, 22, vw + 4, 13, C_INK);
        md_text(vx, 24, &MD_F3, 2, v, C_BG);
    } else {
        md_text(vx, 24, &MD_F3, 2, v, C_INK);
        if (dim)                                      /* half its pixels: the MD's grey */
            cv_dither(vx, 24, vw, 10, C_BG);
    }
    cv_blit(c * 60u + 1u, Y_KNOB);
}

static void draw_column(uint32_t c, const char *label, const char *val, const char *unit, uint32_t st, int32_t ratio)
{
    draw_column_m(c, label, val, unit, st, ratio, -1);
}

static uint32_t steps_hash(const track_t *t)
{
    uint32_t h = 2166136261u, i, n = view_len(t);
    for (i = 0; i < n; i++) {
        step_t s = view_step(t, i);
        h = (h ^ (s.on + s.acc * 2u + s.cond * 4u + s.rat * 256u + s.slide * 1024u + s.tie * 2048u +
                  (uint32_t)(uint8_t)s.nt * 4096u)) * 16777619u;
    }
    return h ^ n * 7919u;
}

/* ACID's SOUND graph (spec §7): the filter's response at CUTOF / RESO (24 dB / octave, the RESO peak at the cutoff:
 * an approximation, as FILTER's), ENVMD's reach a dotted AMB line, 30 Hz .. 16 kHz across x 100..235 (15 px an
 * octave), +12 .. -32 dB (1 px a dB, 0 dB at y ACID_GY); under it the note's shape over 1 s (136 px): the amp gate
 * (DIM: GATE of a step at the tempo now) and the filter envelope falling over DECAY (HI) */
#define ACID_GY 14
#define ACID_EY 56                                      /* the shape's top; 36 px high */
static int32_t acid_cut_x(int32_t semis)                /* the cutoff MIDI 16 + semis -> px from 30 Hz (MIDI 23) */
{
    return clamp((16 + semis - 23) * 5 / 4, 0, 135);
}
/* KTRX: the pitch over TUNE (octaves, 12 px each: the drop and SWEEP over SDEC) and the level (the fade-in, DECAY)
 * over the first 300 ms, from the voice's own laws */
static int32_t ktrx_exp(uint32_t t, uint32_t tau)    /* e ^ (-t / tau), Q16 (t, tau in samples) */
{
    uint32_t e = pow2_q16(-(int32_t)clamp((int32_t)(t * 277u / tau), 0, 3072));
    return e > 65536u ? 65536 : (int32_t)e;          /* (pow2_q16(0) is 65549: 1 - e must not wrap) */
}
static void graph_ktrx(const track_t *t, uint16_t c)
{
    const int16_t *p = &t->p[P_E0];
    int32_t s = clamp(p[0], -24, 24), r = clamp(p[2], 0, 127), x, py = 0, pa = 0;
    uint32_t sc = pow2_q16(-s * 24);                     /* the drop re the pitch: 2 ^ (-s / 8) */
    uint32_t ta = ktrx_tau(2293u, (clamp(p[1], 0, 127) - 64) * 1518 / 100, 309u);
    uint32_t tr = ktrx_tau(4322u, (clamp(p[3], 0, 127) - 72) * 1427 / 100, 221u);
    uint32_t ramp = (uint32_t)(((uint64_t)(uint32_t)(r * r * r) * 23187u) >> 16);   /* 11.06 (r / 127) ^ 3, Q16 */
    uint32_t dq = (uint32_t)((34341ull * sc) >> 16), fq = (uint32_t)((173014ull * sc) >> 16);   /* 32.2, 162.1 / 61.4 */
    (void)c;
    md_text(100, 10, &MD_F3, 2, "PITCH / LEVEL", C_INK);
    cv_hdot(100, 235, 84, C_INK);
    for (x = 0; x < 136; x++) {
        uint32_t tt = (uint32_t)x * 97u;               /* 300 ms over 136 px */
        uint32_t q = 65536u + (uint32_t)(((uint64_t)dq * (uint32_t)ktrx_exp(tt, 213u)) >> 16) +
                     (uint32_t)(((uint64_t)fq * (uint32_t)ktrx_exp(tt, 158u)) >> 16) +
                     (uint32_t)(((uint64_t)ramp * (uint32_t)ktrx_exp(tt, tr)) >> 16);   /* the pitch re TUNE, Q16 */
        int32_t n = 0, oct, y, lv, ya;
        while (n < 15 && (q >> (n + 17)))
            n++;
        oct = n * 256 + (int32_t)(((q >> n) - 65536u) >> 8);   /* log2, Q8 (linear between octaves: a graph) */
        y = 84 - clamp(oct * 12 / 256, 0, 48);
        lv = (int32_t)(((uint32_t)ktrx_exp(tt, ta) * (uint32_t)(65536 - ktrx_exp(tt, 582u))) >> 16);
        ya = 84 - lv * 48 / 65536;
        if (x) {
            cv_line_dot(99 + x, pa, 100 + x, ya, C_INK);   /* the level: dotted */
            cv_line(99 + x, py, 100 + x, y, C_INK);        /* the pitch: solid */
        }
        py = y;
        pa = ya;
    }
}

static void graph_acid(const track_t *t, uint16_t c)
{
    const int16_t *p = &t->p[P_E0];
    int32_t cs = clamp(p[2], 0, 127) * 103 / 127, cx = acid_cut_x(cs), pk = -6 + clamp(p[3], 0, 127) * 18 / 127;
    int32_t x, y, py = 0, ex, gw, e = ENV1, kx = 65536;
    uint32_t k = acid_decay(p[1]), i;
    (void)c;
    cv_hdot(100, 235, ACID_GY, C_INK);                       /* 0 dB */
    for (x = 0; x < 136; x++) {
        int32_t dx = x - cx, db = dx <= -15 ? 0 : dx <= 0 ? pk * (15 + dx) / 15 : pk - dx * 24 / 15;
        y = ACID_GY - clamp(db, -32, 12);
        if (x)
            cv_line(99 + x, py, 100 + x, y, C_INK);
        py = y;
    }
    ex = acid_cut_x(cs + clamp(p[4], 0, 127) * 48 / 127);   /* ENVMD: the cutoff at the note's start */
    if (p[4] > 0)
        for (y = 2; y < 46; y += 4)
            cv_rect(100 + ex, y, 1, 2, C_INK);
    gw = clamp((int32_t)(acid_gate_len(t, div_period((uint32_t)t->p[P_SDIV], 0)) / 324u), 1, 135);   /* 324 samples a px */
    cv_hdot(100, 235, ACID_EY + 36, C_INK);
    cv_hdot(100, 100 + gw, ACID_EY, C_INK);                  /* the amp: open at once, GATE long, then closed */
    cv_vdot(100, ACID_EY, ACID_EY + 36, C_INK);
    cv_vdot(100 + gw, ACID_EY, ACID_EY + 36, C_INK);
    for (i = 0; i < 324u; i++)                               /* DECAY's factor per px */
        kx = mulq16(kx, k);
    for (x = 0; x < 136; x++) {
        y = ACID_EY + 36 - (int32_t)(((uint32_t)e >> 8) * 36u >> 16);
        if (x)
            cv_line(99 + x, py, 100 + x, y, C_INK);
        py = y;
        e = mulq16(e, (uint32_t)kx);
    }
}

/* the engine view's rendered hit (owner 2026-10-22: every model with no graph of its own, all but FM, ACID, BDTRX): the
 * voice's real sound, one hit at velocity 100 (as the keys play it) struck offline on a scratch track (drum_core.c
 * vg_hit: nothing live touched), its min / max over each block of CTL samples, then over each of the graph's 136
 * columns, normalised to its peak (a hat as tall as a kick: the shape, not the level; LEVEL is the mix's). The span
 * follows the sound: its length to -30 dB of its peak, + 1/4, rounded up to 100 150 200 300 400 500 750 1000 ms (a
 * rim's click and a cymbal's wash both fill the width; 1 s at most: the render stops there, or at the voice's end, or
 * after 100 ms under 1/64 of the peak). Rendered over the frames, VG_STEP samples each, while the engine view shows
 * (draw_graph), anew when the sound changes (vg_sig: MODEL, the 8 SOUND knobs, SAMPL's user samples); until it is done
 * the graph keeps the model's last one (no flicker while a knob turns); nothing on idle frames */
#define VG_NB (FS / CTL)                                /* blocks: 1 s at most */
#define VG_STEP 1024u                                   /* samples rendered a frame (~2.3 ms on the FM-1 at most) */
#define VG_VEL 100u
#define VG_QUIET (FS / 10u / CTL)                       /* 100 ms under 1/64 of the peak so far: the hit is over */
#define VG_CY 58                                        /* the centre line (graph y); the columns swing +- VG_A */
#define VG_A 28
static const uint16_t VG_SPAN[] = {100, 150, 200, 300, 400, 500, 750, 1000};
static struct {
    uint32_t sig, ok, shown;     /* the sound rendering; the sound the columns show (0: none); the one drawn */
    uint32_t n, work;            /* samples rendered: this hit; since power-on (the tests') */
    uint32_t loud;               /* the blocks up to the last at 1/64 of the peak so far */
    uint32_t span;               /* the columns' span, ms (0: the hit made no sound) */
    int32_t pk;                  /* the peak so far (>> 2, as the blocks) */
    uint8_t run, okm;            /* rendering; the columns' model */
    int16_t bmn[VG_NB], bmx[VG_NB];   /* each block's min / max (>> 2) */
    int8_t cmn[136], cmx[136];   /* the columns: px under / over the centre line */
} vg __attribute__((section(".pool")));

static int vg_model(uint32_t mi) { return mi != DM_FM && mi != DM_ACID && mi != DM_KTRX; }   /* (the others' own) */

static uint32_t vg_sig(const track_t *t)             /* what the hit is of: MODEL, the 8 SOUND knobs (+ SAMPL's samples) */
{
    uint32_t h = 2166136261u, i;
    for (i = P_MODEL; i <= P_E7; i++)
        h = (h ^ (uint32_t)(uint16_t)t->p[i]) * 16777619u;
    if ((uint32_t)t->p[P_MODEL] % NMODELS == DM_SMPL)
        h = (h ^ smp_user_gen) * 16777619u;
    return h | 1u;
}

static void vg_finish(uint32_t mi)                   /* the hit rendered: its span, its columns */
{
    uint32_t nb = vg.n / CTL, len = 0, b, c, k, nbs;
    int32_t pk = vg.pk;
    for (b = 0; b < nb; b++)
        if (pk && (vg.bmx[b] > -vg.bmn[b] ? vg.bmx[b] : -vg.bmn[b]) * 32 >= pk)
            len = b + 1u;
    len = len * CTL * 5u / 4u;
    for (k = 0; k + 1u < sizeof VG_SPAN / sizeof VG_SPAN[0] && VG_SPAN[k] * 441u / 10u < len; k++)
        ;
    nbs = VG_SPAN[k] * 441u / 10u / CTL;
    for (c = 0; c < 136u; c++) {
        uint32_t b0 = c * nbs / 136u, b1 = (c + 1u) * nbs / 136u;
        int32_t mn = 0, mx = 0;
        if (b1 <= b0)
            b1 = b0 + 1u;
        for (b = b0; b < b1 && b < nb; b++) {
            mn = vg.bmn[b] < mn ? vg.bmn[b] : mn;
            mx = vg.bmx[b] > mx ? vg.bmx[b] : mx;
        }
        vg.cmx[c] = (int8_t)(pk ? (mx * VG_A + pk / 2) / pk : 0);
        vg.cmn[c] = (int8_t)(pk ? -((-mn * VG_A + pk / 2) / pk) : 0);
    }
    vg.span = pk ? VG_SPAN[k] : 0u;
    vg.ok = vg.sig;
    vg.okm = (uint8_t)mi;
    vg.run = 0;
}

/* one frame's share of t's rendered hit (draw_graph, the engine view shown): a new sound starts it again; VG_STEP
 * samples at most; nothing once it is done */
static void vg_step(const track_t *t)
{
    uint32_t mi = (uint32_t)t->p[P_MODEL] % NMODELS, sig, k, i;
    int32_t b[CTL];
    if (!vg_model(mi)) {
        vg.run = 0;
        vg.sig = 0;
        return;
    }
    sig = vg_sig(t);
    if (sig != vg.sig) {
        vg.sig = sig;
        vg.n = 0;
        vg.pk = 0;
        vg.loud = 0;
        vg.run = 1;
        vg_hit(t, VG_VEL);
    }
    for (k = 0; vg.run && k < VG_STEP; k += CTL) {
        uint32_t on = vg_render(b, CTL), bi = vg.n / CTL;
        int32_t mn = 0, mx = 0, a;
        for (i = 0; i < CTL; i++) {
            mn = b[i] < mn ? b[i] : mn;
            mx = b[i] > mx ? b[i] : mx;
        }
        mn = clamp(mn >> 2, -32767, 0);
        mx = clamp(mx >> 2, 0, 32767);
        vg.bmn[bi] = (int16_t)mn;
        vg.bmx[bi] = (int16_t)mx;
        a = mx > -mn ? mx : -mn;
        vg.pk = a > vg.pk ? a : vg.pk;
        if (a && a * 64 >= vg.pk)
            vg.loud = bi + 1u;
        vg.n += CTL;
        vg.work += CTL;
        if (!on || !vg_trk.v[0].active || vg.n >= VG_NB * CTL || (vg.pk && bi + 1u >= vg.loud + VG_QUIET))
            vg_finish(mi);
    }
}

/* the rendered hit's graph (x 100..235): "HIT / 300 MS", a dotted centre line, each column's min .. max solid (a DAW's
 * waveform); a new model's first hit still rendering: the line only */
static void graph_wave(const track_t *t)
{
    uint32_t x, n;
    char b[20];
    cv_hdot(100, 235, VG_CY, C_INK);
    if (!vg.ok || vg.okm != (uint32_t)t->p[P_MODEL] % NMODELS)
        return;
    str_cpy(b, "HIT / ", sizeof b);
    if (vg.span) {
        fmt_int(b + 6, (int32_t)vg.span);
        n = str_len(b);
        str_cpy(b + n, " MS", sizeof b - n);
    } else {
        str_cpy(b + 6, "NO SOUND", sizeof b - 6);
    }
    md_text(100, 10, &MD_F3, 2, b, C_INK);
    vg.shown = vg.ok;
    for (x = 0; x < 136u; x++)
        if (vg.cmx[x] || vg.cmn[x])
            cv_rect(100 + (int32_t)x, VG_CY - vg.cmx[x], 1, vg.cmx[x] - vg.cmn[x] + 1, C_INK);
}

/* the track's preset (spec 2026-10-19 presets): "PRESET K909", "*" (amber) once a SOUND knob moved since; "PRESET -"
 * with none (or its user slot emptied, or holding another voice now: presets.c preset_of). At (x, y): an inverted
 * PRESET tab (label), the name in F3 at 2x, the mark after it */
static void preset_line(const track_t *t, int32_t x, int32_t y, int label)
{
    char nb[9];
    preset_name_of(preset_of(t), nb);
    if (label)
        x += cv_tab(x, y, "PRESET", C_INK, C_BG) + 4;
    x = md_text(x, y + 2, &MD_F3, 2, nb, C_INK);
    if (t->pre_ed)
        md_text(x + 2, y + 2, &MD_F3, 2, "*", C_INK);
}

/* the selected track's model (owner 2026-10-22: every model as FM, ACID, BDTRX): its preset in the band above
 * (preset_line); left, the name in F3 4x (5 glyphs, 76 px, end at x 80 at most), its place in the MODEL knob's list
 * under it ("#32/45", MODEL_LIST), voices, choke group, the layer under that; right (x 100..235) the graph: FM's
 * sidebands, ACID's filter, BDTRX's pitch and level, the others' rendered hit (graph_wave) */
static void graph_model(const track_t *t, uint16_t c)
{
    const dmodel_t *m = &DMODELS[(uint32_t)t->p[P_MODEL] % NMODELS];
    uint32_t mi = (uint32_t)t->p[P_MODEL] % NMODELS, n = model_list_idx(mi) + 1u;
    char b[16], nb[8];
    int32_t y = 50;
    preset_line(t, 4, 2 - G_OY, 1);
    nb[0] = '#';
    nb[1] = (char)('0' + n / 10u);
    nb[2] = (char)('0' + n % 10u);
    nb[3] = '/';
    nb[4] = (char)('0' + NMODELS / 10u);
    nb[5] = (char)('0' + NMODELS % 10u);
    nb[6] = 0;
    md_text(4, 10, &MD_F3, 4, m->name, C_INK);
    md_text(4, 34, &MD_F3, 2, nb, C_INK);
    md_text(4, y, &MD_F3, 2, m->voices > 1 ? "2 VOICES" : "1 VOICE", C_INK);
    if (t->p[P_CHOKE]) {
        str_cpy(b, "CHOKE 1", sizeof b);
        b[6] = (char)('0' + t->p[P_CHOKE]);
        y += 16;
        md_text(4, y, &MD_F3, 2, b, C_INK);
    }
    if (t->p[P_LLEVEL]) {
        y += 16;
        md_text(4, y, &MD_F3, 2, "+ LAYER", C_INK);
    }
    if (vg_model(mi))
        graph_wave(t);                               /* the rendered hit */
    if (mi == DM_FM) {                                 /* FM: the ratio, the carrier and its sidebands f +- k r f */
        uint32_t r8 = FM_RATIO_Q8[(uint32_t)clamp(t->p[P_E3], 0, 15)], k;
        int32_t h0 = clamp(t->p[P_E2], 0, 127) * 48 / 127;   /* INDEX 0: a pure sine, no sidebands */
        str_cpy(b, "RATIO ", sizeof b);
        str_cpy(b + 6, N_FMRATIO[(uint32_t)clamp(t->p[P_E3], 0, 15)], sizeof b - 6);
        md_text(100, 10, &MD_F3, 2, b, C_INK);
        cv_rect(100, 84, 136, 1, C_INK);                 /* the axis */
        cv_rect(100 + 17, 84 - 50, 2, 50, C_INK);        /* the carrier (x: 17 px per f, 0 .. 8 f) */
        for (k = 1; k <= 3u && h0 > 0; k++) {
            int32_t up = (int32_t)(256u + k * r8), dn = (int32_t)(256u) - (int32_t)(k * r8), hk = h0 / (int32_t)k;
            if (up * 17 / 256 < 136)
                cv_rect(100 + up * 17 / 256, 84 - hk, 2, hk, C_INK);
            dn = dn < 0 ? -dn : dn;
            if (dn * 17 / 256 < 136)                     /* the lower sidebands: dithered */
                cv_dither(100 + dn * 17 / 256, 84 - hk, 2, hk, C_INK);
        }
    }
    if (mi == DM_ACID)
        graph_acid(t, c);                            /* ACID: the filter and the note's shape */
    if (mi == DM_KTRX)
        graph_ktrx(t, c);                            /* KTRX: the pitch and the level */
}

/* a step's bar: hatched (every third row dark) when its PROB is not 100 %; RATCH > 1: that many ticks above */
static void step_bar(int32_t x, int32_t y, int32_t w, int32_t h, const step_t *st, int acc, int32_t tick)
{
    int32_t r;
    if (st->cond) {                                  /* PROB < 100 %: hatched */
        for (r = 0; r < h; r += 3)
            cv_rect(x, y + r, w, r + 2 <= h ? 2 : 1, C_INK);
    } else if (acc || w < 4) {                       /* an accent (or a narrow bar): solid */
        cv_rect(x, y, w, h, C_INK);
    } else {                                          /* a plain trig: outline + dither */
        cv_frame(x, y, w, h, C_INK);
        cv_dither(x + 1, y + 1, w - 2, h - 2, C_INK);
    }
    for (r = 0; st->rat && r <= st->rat; r++)
        cv_rect(x + r * tick, y - 4, tick > 2 ? tick - 1 : 1, 2, C_INK);
}

/* PATTERN page: 4 rows of 16 steps over LEN; accent tall, playhead white */
static void graph_steps(const track_t *t, uint16_t c)
{
    uint32_t i, len = view_len(t);
    for (i = 0; i < NSTEP && i < len; i++) {
        int32_t x = 6 + (int32_t)(i % 16u) * 14 + (int32_t)(i % 16u) / 4 * 4, y = 6 + (int32_t)(i / 16u) * 24;
        step_t sv = view_step(t, i);
        const step_t *st = &sv;
        if (st->on)
            step_bar(x, st->acc ? y : y + 4, 2, st->acc ? 14 : 10, st, st->acc, 3);
        else
            cv_rect(x, y + 13, 2, 1, C_INK);
        if (song.playing && i == view_idx(t))
            cv_rect(x - 1, y + 16, 4, 3, C_INK);
    }
}

static int srec_ok(void);                               /* ui_input.c: a REC tap starts step record */

/* the held step's line into h (32 B): "STEP n  PROB  RATCH n", + "  ACC" when accented; its index, NSTEP none held
 * (worst "STEP 64  1-SHOT  RATCH 4  ACC": 29 characters, 236 px) */
static uint32_t held_text(char *h)
{
    const step_t *hs;
    char cs[8];
    uint32_t si = held_step();                        /* (held STEP_HOLD_MS, or edited: a tap shows nothing) */
    h[0] = 0;
    if (si >= NSTEP)
        return NSTEP;
    hs = &TSEL->step[si];
    str_cpy(h, "STEP ", 32);
    fmt_int(h + str_len(h), (int32_t)si + 1);
    str_cpy(h + str_len(h), "  ", 32 - str_len(h));
    cond_format(hs->cond, cs);
    str_cpy(h + str_len(h), cs, 32 - str_len(h));
    str_cpy(h + str_len(h), "  RATCH ", 32 - str_len(h));
    fmt_int(h + str_len(h), (int32_t)hs->rat + 1);
    if (hs->acc)
        str_cpy(h + str_len(h), "  ACC", 32 - str_len(h));
    return si;
}

/* the STEP page's held step (a grid key held, KEYS off) at graph row y: held_text; 0: none held */
static int held_line(const track_t *t, int32_t y)
{
    char h[32];
    (void)t;
    if (held_text(h) >= NSTEP)
        return 0;
    md_text(4, y, &MD_F3, 2, h, C_INK);
    return 1;
}

/* STEP page: the bank's 16 steps as the keys show them; bank n/m on the left. Hint line: a held step's PROB /
 * RATCH, or a Grids track's channel (its grid is read-only) */
static void graph_grid(const track_t *t, uint16_t c)
{
    uint32_t i, len = view_len(t);
    char b[8];
    for (i = 0; i < 16u; i++) {
        uint32_t si = ui.bank * 16u + i;
        int32_t x = 4 + (int32_t)i * 14 + (int32_t)(i / 4u) * 2, y = 30;
        step_t sv = view_step(t, si);
        const step_t *st = &sv;
        if (si >= len) {                             /* past the length: a dotted line */
            cv_hdot(x, x + 10, y + 20, C_INK);
            continue;
        }
        if (st->on)
            step_bar(x, st->acc ? y : y + 10, 11, st->acc ? 40 : 30, st, st->acc, 3);
        else
            cv_rect(x, y + 38, 11, 2, C_INK);        /* empty: a 2 px baseline */
        if (song.playing && si == view_idx(t))
            cv_rect(x, y + 46, 11, 3, C_INK);
        if (st->on && st->nt)                         /* a transposed step (NOTE != C4): a small mark under it */
            cv_rect(x + 3, y + 42, 5, 2, C_INK);
        if (ui.srec && si == ui.sr_cur)               /* step record's cursor: a frame */
            cv_frame(x - 2, y - 3, 15, 54, C_INK);
    }
    fmt_int(b, (int32_t)ui.bank + 1);
    str_cpy(b + str_len(b), "/", 4);
    fmt_int(b + str_len(b), (int32_t)bank_count());
    md_text(4, 6, &MD_F3, 2, "BANK", C_INK);
    md_text(44, 6, &MD_F3, 2, b, C_INK);
    if (view_src(t)) {
        static const char *const GN[3] = {"GRIDS KICK", "GRIDS SNARE", "GRIDS HATS"};
        md_text(4, 84, &MD_F3, 2, GN[view_src(t) - 1u], C_INK);
    } else if (ui.srec) {                             /* step record: the ways on and out */
        md_text(4, 84, &MD_F3, 2, "EDIT: REST  REC: END", C_INK);
    } else if (keys_mode()) {                         /* KEYS: the keys are notes; REC: step record where a tap starts
                                                       * it (stopped; playing, a song: a tap arms the track) */
        md_text(4, 84, &MD_F3, 2, srec_ok() ? "KEYS  REC: STEP RECORD" : "KEYS", C_INK);
    } else if (held_line(t, 84)) {
    } else {
        cv_keycap(cv_keycap(4, 84, "TAP", "ON/OFF", C_INK) + 10, 84, "OCT+", "+TAP ACCENT", C_INK);   /* (item 1) */
    }
}

/* STEP page, a gated model's track (ACID; spec §7, after upstream Felucca's graph_roll): a piano roll of the bank's
 * 16 steps. PR_ROWS semitone rows of PR_RH px, the highest on top; the C rows named on the left (C4 = NOTE 60) and
 * closed by a DIM line, beside a keyboard strip (the key sounding on this track: its row WHITE); step lines LINE, the
 * beats DIM. A note is a 9 px bar on its row (ACC: WHITE, the row's full height; else HI, 2 px), a RATCH step's in its
 * 2..4 parts; a TIE carries the bar of the note before through its column (from the step line: one long bar); a rest
 * is empty; SLIDE an AMB line from the bar's end into the next note (not out of a ratcheted step). A note outside the
 * view: a 1 px mark on the edge row; the key sounding outside it: the strip's edge row WHITE. Step record's cursor: a
 * WHITE frame; the playhead: a 1 px WHITE line. The view (proll.lo, the bottom row's note) is roll_view's */
#define PR_ROWS 24                                      /* 2 octaves */
#define PR_RH 4                                         /* px per row */
#define PR_Y0 2                                         /* the top row (graph y) */
#define PR_X0 38                                        /* the first step column */
#define PR_CW 12                                        /* px per step */
#define PR_KX 26                                        /* the keyboard strip, 9 px (labels clear of x 0) */
static struct { uint8_t lo, trk; } proll;               /* the bottom row's note; the track it is for + 1 */
static const uint8_t KEY_BLACK[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};

static int32_t pr_row_y(int32_t n) { return PR_Y0 + ((int32_t)proll.lo + PR_ROWS - 1 - n) * PR_RH; }

/* the step whose note sounds at si: si when ON, the ON step before a run of TIEs (NSTEP: none) */
static uint32_t pr_src(const track_t *t, uint32_t si, uint32_t len)
{
    const step_t *st = seq_steps(t);
    uint32_t k;
    for (k = 0; k < len && st[si].tie; k++)
        si = (si + len - 1u) % len;
    return st[si].on && !st[si].tie ? si : NSTEP;
}

/* the bank's sounding notes into nn (a TIE: its source's, pr_src); their count */
static uint32_t pr_notes(const track_t *t, int32_t *nn)
{
    const step_t *st = seq_steps(t);
    uint32_t i, s, n = 0, len = view_len(t), base = ui.bank * 16u;
    for (i = 0; i < 16u && base + i < len; i++)
        if ((s = pr_src(t, base + i, len)) != NSTEP)
            nn[n++] = 60 + st[s].nt;
    return n;
}
static uint32_t pr_count(const int32_t *nn, uint32_t n, int32_t lo)   /* the notes in the window from lo */
{
    uint32_t k, c = 0;
    for (k = 0; k < n; k++)
        c += nn[k] >= lo && nn[k] < lo + PR_ROWS;
    return c;
}

/* the view (proll.lo; draw_graph, when the graph changes, never on an idle frame): of the windows with a note on their
 * bottom row (16 x 16 compares at most), the one holding the most of the bank's sounding notes, the lowest of equals;
 * the view now kept while it holds as many. Step record: kept while it holds the cursor step's sounding note (a rest:
 * kept), else that window shifted only as far as the note needs. Neither the playhead nor a KEYS key moves it (a key
 * outside: graph_roll lights the strip's edge row). No notes: the track's last key note in the middle */
static void roll_view(const track_t *t)
{
    int32_t nn[16], lo = 0, w, cn;
    uint32_t n = pr_notes(t, nn), k, c, best = 0, s, len = view_len(t);
    int cur = proll.trk == song.sel + 1u;
    for (k = 0; k < n; k++) {
        w = clamp(nn[k], 0, 128 - PR_ROWS);
        c = pr_count(nn, n, w);
        if (c > best || (c == best && w < lo)) {
            best = c;
            lo = w;
        }
    }
    if (!n)
        lo = 60 + t->key_nt - PR_ROWS / 2;
    if (cur && pr_count(nn, n, proll.lo) >= best)
        lo = proll.lo;                                /* as good as the best: no jump */
    if (ui.srec) {
        s = ui.sr_cur < len ? pr_src(t, ui.sr_cur, len) : NSTEP;
        cn = s == NSTEP ? 0 : 60 + seq_steps(t)[s].nt;
        if (cur && (s == NSTEP || (cn >= proll.lo && cn < proll.lo + PR_ROWS)))
            lo = proll.lo;                            /* the cursor's note in the view, or a rest: no move */
        else if (s != NSTEP && cn < lo)
            lo = cn;
        else if (s != NSTEP && cn >= lo + PR_ROWS)
            lo = cn - PR_ROWS + 1;
    }
    proll.lo = (uint8_t)clamp(lo, 0, 128 - PR_ROWS);
    proll.trk = (uint8_t)(song.sel + 1u);
}

/* a part of a bar: whole, or hatched (PROB: every other column, as step_bar's rows) */
static void pr_part(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, int prob)
{
    int32_t i;
    (void)c;
    if (!prob) {
        cv_rect(x, y, w, h, C_INK);
        return;
    }
    for (i = 0; i < w; i += 2)
        cv_rect(x + i, y, 1, h, C_INK);
}

/* a note's bar at row y in `hits` parts 1 px apart (RATCH; 1: whole), hatched when its PROB is not 100 % (prob); out
 * of the view: a 1 px mark on its edge */
static void pr_bar(int32_t x, int32_t y, int32_t w, int acc, uint16_t c, uint32_t hits, int prob)
{
    int32_t k, pw, ybot = PR_Y0 + PR_ROWS * PR_RH, yy = acc ? y : y + 1, h = acc ? PR_RH : PR_RH - 2;
    if (y < PR_Y0) {
        yy = PR_Y0;
        h = 1;
    } else if (y >= ybot) {
        yy = ybot - 1;
        h = 1;
    }
    if (hits < 2u) {
        pr_part(x, yy, w, h, c, prob);
        return;
    }
    pw = (w - (int32_t)hits + 1) / (int32_t)hits;
    for (k = 0; k < (int32_t)hits; k++)
        pr_part(x + k * (pw + 1), yy, pw, h, c, prob);
}

static void graph_roll(const track_t *t, uint16_t c)
{
    const step_t *st = seq_steps(t);
    uint32_t i, len = view_len(t), base = ui.bank * 16u, ncol = base < len ? (len - base < 16u ? len - base : 16u) : 0u;
    int32_t r, ybot = PR_Y0 + PR_ROWS * PR_RH, gw = (int32_t)ncol * PR_CW + 1;
    int32_t gx1 = PR_X0 + (int32_t)ncol * PR_CW, top = (int32_t)proll.lo + PR_ROWS - 1;
    int32_t held = kz.key && kz.trk == song.sel ? 60 + t->key_nt : -1000;
    char nb[8];
    for (r = 0; r < PR_ROWS; r++) {                   /* rows: the keyboard strip (black keys, the key sounding), the
                                                       * C rows named and closed by a dotted line */
        int32_t n = top - r, y = PR_Y0 + r * PR_RH, lx;
        uint32_t pc = (uint32_t)n % 12u;
        if (n == held || (held > -1000 && ((r == 0 && held > top) || (r == PR_ROWS - 1 && held < proll.lo))))
            cv_rect(PR_KX, y, 9, PR_RH - 1, C_INK);
        else if (KEY_BLACK[pc])
            cv_rect(PR_KX, y, 5, PR_RH - 1, C_INK);
        if (pc == 0u) {
            cv_hdot(PR_X0 + 1, PR_X0 + gw - 1, y + PR_RH - 1, C_INK);
            note_text(nb, n);
            lx = PR_KX - 2 - md_text_w(&MD_F3, 2, nb);
            md_text(lx < 2 ? 2 : lx, y < 4 ? 0 : y - 4, &MD_F3, 2, nb, C_INK);
        }
    }
    for (i = 0; i <= ncol; i += 4u)                   /* the beats: dotted lines */
        cv_vdot(PR_X0 + (int32_t)i * PR_CW, PR_Y0, ybot - 1, C_INK);
    for (i = 0; i < ncol; i++) {                      /* the notes */
        uint32_t si = base + i, s = pr_src(t, si, len);
        const step_t *own = &st[si], *ns = &st[(si + 1u) % len], *ss;
        int32_t x = PR_X0 + (int32_t)i * PR_CW, y;
        if (s == NSTEP)
            continue;                                 /* a rest, empty, or a TIE after one */
        ss = &st[s];
        y = pr_row_y(60 + ss->nt);
        if (s == si)
            pr_bar(x + 2, y, 9, ss->acc, c, ss->rat + 1u, ss->cond != 0u);
        else                                          /* a TIE: on through the step line (its note's PROB: hatched) */
            pr_bar(x - 1, y, 12, ss->acc, c, 1u, ss->cond != 0u);
        if (own->slide && (own->tie || !own->rat) && ns->on && !ns->tie && y >= PR_Y0 && y < ybot) {
            int32_t ny = clamp(pr_row_y(60 + ns->nt), PR_Y0, ybot - PR_RH);
            cv_line_dot(x + 11, y + 1, x + 14 < gx1 ? x + 14 : gx1, ny + 1, C_INK);   /* the slide into the next note
                                                         * (out of the last column: to the grid's edge) */
        }
    }
    for (i = 0; i < ncol; i++) {                      /* the playhead, step record's cursor */
        uint32_t si = base + i;
        int32_t x = PR_X0 + (int32_t)i * PR_CW;
        if (song.playing && si == view_idx(t))
            cv_rect(x + 6, PR_Y0, 1, ybot - PR_Y0, C_INK);
        if (ui.srec && si == ui.sr_cur)
            cv_frame(x, PR_Y0 - 2, PR_CW + 1, ybot - PR_Y0 + 4, C_INK);
    }
}

/* GRIDS pages. MAP: the 5 x 5 node map (faint) with the X / Y point, and the three channels' 32 steps without
 * chaos (accent tall, playhead underlined). EUCLID: three rings of LEN positions (accent large, playhead marked).
 * Page 2 adds the routing: which tracks play each channel. */
/* OFF (a type / model switched off): F3 at 6x centred, a 3-row dither band above and below it (graph rows) */
static void off_word(void)
{
    int32_t w = md_text_w(&MD_F3, 6, "OFF"), x = (240 - w) / 2;
    md_text(x, 35, &MD_F3, 6, "OFF", C_INK);
    cv_dither(x - 6, 30, w + 12, 3, C_INK);
    cv_dither(x - 6, 67, w + 12, 3, C_INK);
}

/* a horizontal meter (the MD's LEV, lying down): v (0..1000) of w px solid, 9 px tall, 2 px tick dashes every 6 px
 * above (y - 3) and below (y + 10); dith: the fill dithered (a second meter beside a solid one) */
static void hmeter(int32_t x, int32_t y, int32_t w, int32_t v, int dith)
{
    int32_t t;
    v = clamp(v, 0, 1000);
    if (dith)
        cv_dither(x, y, w * v / 1000, 9, C_INK);
    else
        cv_rect(x, y, w * v / 1000, 9, C_INK);
    for (t = x; t <= x + w; t += 6) {
        cv_rect(t, y - 3, 1, 2, C_INK);
        cv_rect(t, y + 10, 1, 2, C_INK);
    }
}

static void graph_grids(uint16_t c, int routing)
{
    static const char *const CH[3] = {"K", "S", "H"};
    uint32_t ch, i;
    (void)c;
    if (!song.g[G_GMODE]) {
        int32_t px = 6 + song.g[G_GX] * 72 / 127, py = 6 + song.g[G_GY] * 72 / 127;
        for (i = 0; i < 25u; i++)                    /* the node map: 2x2 dots, the X / Y point a 7x7 block */
            cv_rect(5 + (int32_t)(i % 5u) * 18, 5 + (int32_t)(i / 5u) * 18, 2, 2, C_INK);
        cv_rect(px - 4, py - 4, 9, 9, C_BG);
        cv_rect(px - 3, py - 3, 7, 7, C_INK);
        for (ch = 0; ch < 3u; ch++) {
            int32_t y = 4 + (int32_t)ch * 24;
            md_text(92, y + 4, &MD_F3, 2, CH[ch], C_INK);
            for (i = 0; i < 32u; i++) {
                uint32_t b = grids_preview(ch, i);
                int32_t x = 106 + (int32_t)i * 4 + (int32_t)(i / 8u) * 2;
                int ph = song.playing && i == grids.last;
                if (b & 1u)
                    cv_rect(x, (b & 2u) ? y : y + 8, 3, (b & 2u) ? 18 : 10, C_INK);
                else
                    cv_rect(x + 1, y + 17, 1, 1, C_INK);
                if (ph)
                    cv_rect(x, y + 20, 3, 2, C_INK);
            }
        }
    } else {
        for (ch = 0; ch < 3u; ch++) {
            uint32_t len = grids_len(ch);
            int32_t cx = 40 + (int32_t)ch * 80, cy = 36;
            char b[8];
            cv_ring(cx, cy, 28, C_INK);
            for (i = 0; i < len; i++) {
                uint32_t ph = i * (0xFFFFFFFFu / len), bb = grids_preview(ch, i);
                int32_t x = cx + sine_i(ph) * 28 / 32768, y = cy - sine_i(ph + 0x40000000u) * 28 / 32768;
                if (bb & 2u)                          /* an accent: 7x7 solid; a hit: 5x5 solid; none: a 3x3 hole */
                    cv_rect(x - 3, y - 3, 7, 7, C_INK);
                else if (bb & 1u)
                    cv_rect(x - 2, y - 2, 5, 5, C_INK);
                else
                    cv_frame(x - 1, y - 1, 3, 3, C_INK);
                if (song.playing && i == grids.elast[ch])
                    cv_rect(x - 4, y + 5, 9, 2, C_INK);
            }
            str_cpy(b, CH[ch], sizeof b);
            str_cpy(b + 1, " ", sizeof b - 1);
            fmt_int(b + 2, (int32_t)len);
            md_text(cx - md_text_w(&MD_F3, 2, b) / 2, 70, &MD_F3, 2, b, C_INK);
        }
    }
    if (routing) {                                   /* "K: T1 T6  S: T2  H: -" */
        char r[48];
        uint32_t n = 0, k;
        r[0] = 0;
        for (ch = 0; ch < 3u; ch++) {
            uint32_t any = 0;
            str_cpy(r + n, ch ? "  " : "", sizeof r - n);
            n = str_len(r);
            str_cpy(r + n, CH[ch], sizeof r - n);
            str_cpy(r + str_len(r), ":", sizeof r - str_len(r));
            n = str_len(r);
            for (k = 0; k < NTRK && n + 4u < sizeof r; k++)
                if (trk[k].p[P_SRC] == (int16_t)(ch + 1u)) {
                    r[n++] = ' ';
                    r[n++] = 'T';
                    r[n++] = (char)('1' + k);
                    r[n] = 0;
                    any = 1;
                }
            if (!any && n + 3u < sizeof r) {
                str_cpy(r + n, " -", sizeof r - n);
                n = str_len(r);
            }
        }
        md_text(4, 86, &MD_F3, 2, r, C_INK);
    }
}

static void graph_fx(const track_t *t, uint16_t c)
{
    uint32_t i;
    int32_t y;
    (void)c;
    for (i = 0; i < 4u; i++) {                       /* the sends as MD LEV bars: solid from the bottom, tick dashes */
        int32_t h = t->p[P_DIST + i] * 80 / 127, x = (int32_t)i * 60 + 28;
        cv_rect(x - 3, 90 - h, 7, h, C_INK);
        for (y = 10; y <= 90; y += 6) {
            cv_rect(x - 7, y, 2, 1, C_INK);
            cv_rect(x + 6, y, 2, 1, C_INK);
        }
    }
}

static void graph_slicer(const track_t *t, uint16_t c)
{
    uint32_t i, pat = sl_pattern(t), mode = (uint32_t)t->p[P_SLCR], cur = sl[t - trk].idx;
    int32_t open = 70 - t->p[P_SLDEPTH] * 70 / 127;      /* px a closed GATE step keeps */
    (void)c;
    for (i = 0; i < 16u; i++) {
        int32_t x = 4 + (int32_t)i * 14 + (int32_t)(i / 4u) * 2, y;
        if ((pat >> i) & 1u) {
            if (mode == SL_OFF)                          /* off: the pattern dithered */
                cv_dither(x, 10, 11, 70, C_INK);
            else
                cv_rect(x, 10, 11, 70, C_INK);
        } else if (mode == SL_STUT) {
            for (y = 10; y < 80; y += 4)
                cv_rect(x, y, 11, 1, C_INK);
        } else {
            cv_hdot(x, x + 10, 79, C_INK);
            if (open)
                cv_dither(x, 80 - open, 11, open, C_INK);
        }
        if (mode != SL_OFF && i == cur)
            cv_rect(x, 85, 11, 3, C_INK);
    }
}

/* oscilloscope of the output, triggered on a rising zero crossing: a 1 px wave round cy (+- amp), a dotted zero
 * line */
static void graph_scope(int32_t cy, int32_t amp, int fresh)   /* fresh: a new snapshot (else the one shown again) */
{
    static int16_t snap[SCOPE_N];
    uint32_t w = scope_w, i, trig = 0;
    int32_t py = cy, x, peak = 1500;
    if (fresh)
        for (i = 0; i < SCOPE_N; i++)
            snap[i] = scope_buf[(w + i) & (SCOPE_N - 1u)];
    for (i = 0; i < SCOPE_N; i++) {
        if (snap[i] > peak)
            peak = snap[i];
        else if (-snap[i] > peak)
            peak = -snap[i];
    }
    for (i = 1; i < SCOPE_N - 240u; i++)
        if (snap[i - 1] < 0 && snap[i] >= 0) {
            trig = i;
            break;
        }
    cv_hdot(2, 237, cy, C_INK);
    for (x = 2; x < 238; x++) {
        int32_t y = cy - snap[trig + (uint32_t)x] * amp / peak;   /* auto-scaled */
        if (x > 2)
            cv_line(x - 1, py, x, y, C_INK);
        py = y;
    }
}


/* a list row (spec 2026-10-19-md-ui §8): l from x0 + 4, r right-aligned at x1 - 4; sel: an ink bar 14 px from y - 2,
 * the text in BG */
static void list_row(int32_t x0, int32_t x1, int32_t y, const char *l, const char *r, int sel)
{
    uint16_t pen = sel ? C_BG : C_INK;
    if (sel)
        cv_rect(x0, y - 2, x1 - x0 + 1, 14, C_INK);
    md_text(x0 + 4, y, &MD_F3, 2, l, pen);
    if (r && r[0])
        md_text(x1 - 4 - md_text_w(&MD_F3, 2, r), y, &MD_F3, 2, r, pen);
}

static void graph_slots(void)
{
    uint32_t i;
    char nb[NAME_LEN + 1u], f[NAME_LEN + 1u], row[NAME_LEN + 5u];
    cv_tab(2, 2, "PROJECTS", C_INK, C_BG);
    for (i = 0; i < 4u; i++) {
        int sel = (int32_t)i + 1 == song.g[G_SLOT], used = project_name(i, nb);
        md_fit(f, used ? (nb[0] ? nb : "USED") : "EMPTY", sizeof f, 180);
        row[0] = (char)('A' + i);
        row[1] = ' ';
        row[2] = ' ';
        str_cpy(row + 3, f, sizeof row - 3);
        list_row(2, 237, 22 + (int32_t)i * 16, row, 0, sel);
    }
    if (chain.name[0]) {
        md_text(6, 92, &MD_F3, 2, "NOW", C_INK);
        md_fit(f, chain.name, sizeof f, 180);
        md_text(40, 92, &MD_F3, 2, f, C_INK);
    }
}

/* TRACKS mixer: one 15 px row per track: number (+ REC-armed dot), model, the 16 steps of the bank
 * its playhead is in (stopped: the first bank; the selected track: the bank STEP shows), and the
 * level fader over the output meter (MUTE when muted). A row redraws whole when its body (all but the level, the
 * meter and the playhead) changes; else only the level cell, the meter's mark (old to new) and the playhead's cells. */
#define MX_RH 14u
#define MX_ROW_Y(r) (Y_GRAPH + 4u + (r) * MX_RH)
#define MX_CELL_X(k) (62u + (k) * 8u + (k) / 4u * 3u)   /* 16 cells of 6 px, a gap per beat */
#define MX_LX 203                                       /* level: fader + meter */
#define MX_LW 34
static struct {
    uint32_t sig[NTRK];          /* the body shown */
    uint8_t meter[NTRK];
    uint8_t lvl[NTRK];           /* the level cell shown: the level + 1, 0 MUTE */
    uint8_t mk[NTRK];            /* the meter's mark shown: its x in the fader + 1, 0 none */
    uint8_t ph[NTRK];            /* the playhead's cell shown + 1, 0 none */
} mx;

static int32_t meter_px(int32_t a)                   /* |sample| (Q15) -> px, 6 dB = MX_LW / 10 */
{
    int32_t lg = 0, v;
    if (a < 64)
        return 0;
    while ((a >> lg) > 1)
        lg++;
    v = lg * 8 + (((a << 3) >> lg) & 7);
    return clamp((v - 48) * MX_LW / 80, 0, MX_LW);
}

/* row c in the canvas (or a window of it): bank's steps, the playhead in cell ph - 1 (0: none), the meter mx.meter */
static void mix_row(uint32_t c, uint32_t bank, uint32_t ph)
{
    const track_t *t = &trk[c];
    uint32_t sel = c == song.sel, lvl = (uint32_t)t->p[P_LEVEL] & 127u, mute = !lvl || trk_silent(t);
    uint32_t arm = (song.rec >> c) & 1u, len = view_len(t), k;
    const char *mn = N_MODEL[(uint32_t)t->p[P_MODEL] % NMODELS];
    uint16_t pen = sel ? C_BG : C_INK;
    char b[2] = {(char)('1' + c), 0};
    int32_t lw;
    cv_rect(0, 0, 1, MX_RH, C_INK);
    cv_rect(239, 0, 1, MX_RH, C_INK);
    if (sel)                                      /* the cursor: number + model inverted */
        cv_rect(2, 0, 20 + md_text_w(&MD_F3, 2, mn), 13, C_INK);
    md_text(4, 2, &MD_F3, 2, b, pen);
    if (arm) {                                    /* REC armed: playing a solid dot, stopped an outline */
        if (song.playing)
            cv_rect(12, 5, 4, 4, pen);
        else
            cv_frame(12, 5, 4, 4, pen);
    }
    md_text(18, 2, &MD_F3, 2, mn, pen);
    for (k = 0; k < 16u; k++) {                   /* the bank's steps: an accent tall, a step short, dithered
                                                   * when muted; none a baseline; the playhead underlined */
        uint32_t si = bank * 16u + k;
        int32_t x = (int32_t)MX_CELL_X(k);
        step_t sv = view_step(t, si);
        const step_t *st = &sv;
        if (si >= len)
            continue;
        if (st->on) {
            if (mute)
                cv_dither(x, st->acc ? 1 : 4, 6, st->acc ? 11 : 7, C_INK);
            else
                cv_rect(x, st->acc ? 1 : 4, 6, st->acc ? 10 : 7, C_INK);
        } else {
            cv_rect(x, 10, 6, 1, C_INK);
        }
        if (k + 1u == ph)
            cv_rect(x, 12, 6, 2, C_INK);
    }
    if (mute) {
        md_text(MX_LX + 2, 2, &MD_F3, 2, "MUTE", C_INK);
    } else {                                      /* the level: a solid bar over a dithered box; the live peak a
                                                   * 1 px mark (BG on the bar, ink past it) */
        lw = (int32_t)lvl * (MX_LW - 2) / 127;
        cv_frame(MX_LX, 1, MX_LW, 11, C_INK);
        cv_dither(MX_LX + 1, 2, MX_LW - 2, 9, C_INK);
        cv_rect(MX_LX + 1, 2, lw, 9, C_INK);
        if (mx.meter[c]) {
            int32_t px = mx.meter[c] * (MX_LW - 2) / MX_LW;
            cv_rect(MX_LX + 1 + px, 2, 1, 9, px < lw ? C_BG : C_INK);
        }
    }
}

static void draw_mix(void)
{
    uint32_t c, i;
    if (ui.force)
        for (c = 0; c < NTRK; c++)
            mx.meter[c] = 0;
    rg_fill(1, Y_GRAPH, 238, MX_ROW_Y(0) - Y_GRAPH, C_BG);   /* the bands over and under the rows (on ui.force; else
                                                               * where a popup was) */
    rg_fill(0, Y_GRAPH, 1, MX_ROW_Y(0) - Y_GRAPH, C_INK);
    rg_fill(239, Y_GRAPH, 1, MX_ROW_Y(0) - Y_GRAPH, C_INK);
    rg_fill(1, MX_ROW_Y(NTRK), 238, Y_GRAPH + H_GRAPH - MX_ROW_Y(NTRK), C_BG);
    rg_fill(0, MX_ROW_Y(NTRK), 1, Y_GRAPH + H_GRAPH - MX_ROW_Y(NTRK), C_INK);
    rg_fill(239, MX_ROW_Y(NTRK), 1, Y_GRAPH + H_GRAPH - MX_ROW_Y(NTRK), C_INK);
    for (c = 0; c < NTRK; c++) {
        track_t *t = &trk[c];
        uint32_t sel = c == song.sel, lvl = (uint32_t)t->p[P_LEVEL] & 127u, mute = !lvl || trk_silent(t);
        uint32_t arm = (song.rec >> c) & 1u, len = view_len(t), bank, sig, vi, ph, lv, mk;
        int32_t m = mute ? 0 : meter_px(t->peak);
        t->peak = 0;
        if (m < mx.meter[c] - 1)
            m = mx.meter[c] - 1;
        mx.meter[c] = (uint8_t)(m < 0 ? 0 : m);
        bank = song.playing ? (view_idx(t) % len) / 16u : sel ? ui.bank : 0u;
        vi = view_idx(t);
        ph = song.playing && vi >= bank * 16u && vi < bank * 16u + 16u && vi < len ? vi - bank * 16u + 1u : 0u;
        lv = mute ? 0u : lvl + 1u;
        mk = mute || !mx.meter[c] ? 0u : (uint32_t)(mx.meter[c] * (MX_LW - 2) / MX_LW) + 1u;
        sig = 1u + sel + arm * 2u + mute * 4u + (uint32_t)song.playing * 8u + bank * 65536u +
              (uint32_t)t->p[P_MODEL] * 7919u + steps_hash(t) * 131u;
        rg_region(0, (int32_t)MX_ROW_Y(c), 240, MX_RH);
        if (ui.force || sig != mx.sig[c]) {
            rg_whole();
        } else {
            if (lv != mx.lvl[c])                       /* the level cell (the fader, MUTE) */
                rg_add(MX_LX, 1, 239 - MX_LX, 11);
            else if (mk != mx.mk[c]) {                /* the mark alone: from its old place to the new */
                int32_t a = (int32_t)(mk ? mk : mx.mk[c]), z = (int32_t)(mx.mk[c] ? mx.mk[c] : mk);
                rg_add(MX_LX + (a < z ? a : z), 2, (a < z ? z - a : a - z) + 1, 9);
            }
            if (ph != mx.ph[c]) {                     /* the playhead: its old cell and its new */
                if (mx.ph[c])
                    rg_add((int32_t)MX_CELL_X(mx.ph[c] - 1u), 12, 6, 2);
                if (ph)
                    rg_add((int32_t)MX_CELL_X(ph - 1u), 12, 6, 2);
            }
            rg_damaged();
        }
        mx.sig[c] = sig;
        mx.lvl[c] = (uint8_t)lv;
        mx.mk[c] = (uint8_t)mk;
        mx.ph[c] = (uint8_t)ph;
        for (i = 0; i < rg.n; i++) {
            rg_win(i, MX_ROW_Y(c), C_BG);
            mix_row(c, bank, ph);
            cv_blit(0, MX_ROW_Y(c));
        }
    }
}

/* the COMP source in the mix: HEARD, GHOST (keys the compressor, not heard: muted with GHOST KEEP, or HIDE),
 * MUTED (GHOST MUTE: keys nothing); "" with SRC OFF */
static const char *comp_src_state(void)
{
    uint32_t src = comp_src();
    if (src >= NTRK)
        return "";
    if (trk_silent(&trk[src]))
        return song.g[G_CGHOST] == CG_MUTE ? "MUTED" : "GHOST";
    return song.g[G_CGHOST] == CG_HIDE ? "GHOST" : "HEARD";
}

/* COMP pages: the routing, the gain reduction now and the source's level; page 2 adds the curve (input -> output
 * level, -48..0 dB in, -48..+12 dB out) for the knobs' THRSH / RATIO / MKUP / KNEE */
static void graph_comp(uint16_t c, int curve)
{
    char r[48], v[12];
    uint32_t k, src = comp_src(), n, any = 0;
    int32_t dbx10 = comp_on() ? (-comp.gr) * 241 / 32768 : 0;
    str_cpy(r, "SRC: ", sizeof r);
    n = str_len(r);
    if (src < NTRK) {
        r[n++] = 'T';
        r[n++] = (char)('1' + src);
        r[n] = 0;
    } else {
        str_cpy(r + n, "OFF", sizeof r - n);
    }
    str_cpy(r + str_len(r), "  DUCK:", sizeof r - str_len(r));
    n = str_len(r);
    for (k = 0; k < NTRK && n + 4u < sizeof r; k++)
        if (trk[k].p[P_DUCK] && k != src) {
            r[n++] = ' ';
            r[n++] = 'T';
            r[n++] = (char)('1' + k);
            r[n] = 0;
            any = 1;
        }
    if (!any)
        str_cpy(r + n, " -", sizeof r - n);
    md_text(4, 2, &MD_F3, 2, r, C_INK);
    md_text(4, 27, &MD_F3, 2, "GR", C_INK);
    hmeter(36, 27, curve ? 70 : 120, clamp(dbx10, 0, 240) * 1000 / 240, 0);
    comp_fmt_db10(v, -dbx10);
    str_cpy(v + str_len(v), "DB", sizeof v - str_len(v));
    md_text(curve ? 36 : 162, curve ? 42 : 27, &MD_F3, 2, v, C_INK);
    md_text(4, curve ? 65 : 52, &MD_F3, 2, "IN", C_INK);
    hmeter(36, curve ? 65 : 52, curve ? 70 : 120, (comp_on() ? meter_px(comp.peak) : 0) * 1000 / MX_LW, 1);
    {                                                   /* the source in the mix, next to its level (IN) */
        const char *st = comp_src_state();
        md_text(curve ? 36 : 162, curve ? 84 : 52, &MD_F3, 2, st, C_INK);
    }
    if (curve) {                                        /* the curve in a box, a dotted 4 x 4 grid */
        comp_set_t s = comp_knobs();
        comp_cfg_t cf;
        comp_configure(&s, &cf);
        cv_frame(118, 22, 119, 77, C_INK);
        for (k = 1; k < 4u; k++) {
            cv_vdot(118 + (int32_t)k * 119 / 4, 24, 96, C_INK);
            cv_hdot(120, 235, 22 + (int32_t)k * 77 / 4, C_INK);
        }
        for (k = 0; k < 116u; k++) {
            int32_t in10 = -480 + (int32_t)k * 480 / 116, lvl = in10 * 65536 / 60;
            int32_t out10 = (lvl + comp_atten(&cf, lvl) + cf.makeup) * 60 / 65536;
            int32_t y = 98 - (out10 + 480) * 76 / 600;
            if (y >= 23 && y <= 96)
                cv_rect(120 + (int32_t)k, y, 1, 2, C_INK);
        }
    }
    (void)c;
}

static int lfo_random(uint32_t wave) { return wave == LW_SH || wave == LW_WANDER || wave == LW_RWALK; }

/* every frame on an LFO page: each LFO's trail gets a point each 1/LTR_N of its cycle (the width = one cycle; at most
 * one a frame); a trail starts over on another track or another wave of its LFO, or after frames away from the LFO
 * pages */
static void lfo_trail(void)
{
    const track_t *t = TSEL;
    uint32_t l, cont = ui.frame == ui.tr_frame + 1u;
    ui.tr_frame = ui.frame;
    for (l = 0; l < 2u; l++) {
        uint32_t ph = t->lfo[l].ph;
        uint32_t key = 1u << 16 | (uint32_t)song.sel << 8 | (uint32_t)clamp(t->p[(l ? P_LFO2 : P_LFO1) + LF_WAVE], 0, 15);
        if (key != ui.tr_key[l] || !cont) {
            ui.tr_key[l] = key;
            ui.tr_n[l] = ui.tr_h[l] = 0;
        }
        if (ui.tr_n[l] && ph - ui.tr_ph[l] < 0xFFFFFFFFu / LTR_N)
            continue;
        ui.tr_ph[l] = ph;
        ui.tr[l][ui.tr_h[l]] = (int16_t)(lfo_out(t, l) >> 1);
        ui.tr_h[l] = (uint8_t)((ui.tr_h[l] + 1u) % LTR_N);
        if (ui.tr_n[l] < LTR_N)
            ui.tr_n[l]++;
    }
}

/* the routing of LFO l under its picture: "> TONE" (one slot), "> 2 DEST" (more), "> OFF" (none) */
static void lfo_route_text(const track_t *t, uint32_t l, char *b, uint32_t n)
{
    uint32_t s, cnt = 0, first = 0;
    for (s = 0; s < LS_N; s++)
        if (t->p[LS_ID[s][LS_DEST]] && t->p[LS_ID[s][LS_DEPTH]] && (t->p[LS_ID[s][LS_SRC]] ? 1u : 0u) == l && !cnt++)
            first = s;
    str_cpy(b, "> ", n);
    if (cnt == 1u)
        str_cpy(b + 2, lfo_dest_label(t, t->p[LS_ID[first][LS_DEST]]), n - 2u);
    else if (cnt) {
        fmt_int(b + 2, (int32_t)cnt);
        str_cpy(b + str_len(b), " DEST", n - str_len(b));
    } else
        str_cpy(b + 2, "OFF", n - 2u);
}

/* LFO pages: both LFOs side by side, LFO 1 over KNOB 1-2, LFO 2 over KNOB 3-4: the name and the rate mode; one cycle
 * of the wave (as morphed, phase-shifted) with the live position, or for the random waves a scope (the live value at
 * the half's right edge, its trail to the left); TRIG and the routing (the DEST slots it feeds) */
static void graph_lfo_half(const track_t *t, uint32_t l)
{
    const int16_t *q = &t->p[l ? P_LFO2 : P_LFO1];
    uint32_t wave = (uint32_t)clamp(q[LF_WAVE], 0, LW_COUNT - 1), k, off = (uint32_t)clamp(q[LF_PHASE], 0, 127) << 25;
    int32_t m = clamp(q[LF_MORPH], 0, 127), x0 = l ? 120 : 0, gx = x0 + (int32_t)LG_X0, px, dx, dy;
    const char *mode = N_LMODE[clamp(q[LF_MODE], 0, 2)], *trig = N_LTRIG[clamp(q[LF_TRIG], 0, 2)];
    char b[16];
    md_text(x0 + 6, 2, &MD_F3, 2, l ? "LFO 2" : "LFO 1", C_INK);
    md_text(x0 + 114 - md_text_w(&MD_F3, 2, mode), 2, &MD_F3, 2, mode, C_INK);
    cv_hdot(gx, gx + (int32_t)LTR_N - 1, LG_CY, C_INK);
    if (lfo_random(wave)) {
        for (k = 0, px = 0; k < ui.tr_n[l]; k++) {   /* newest at the right edge, older to the left */
            int32_t py = LG_CY - ui.tr[l][(ui.tr_h[l] + LTR_N - 1u - k) % LTR_N] * (2 * LG_A) / 32767;
            if (!k)
                px = py;
            cv_rect(gx + (int32_t)LTR_N - 1 - (int32_t)k, py < px ? py : px, 1, (py < px ? px - py : py - px) + 2, C_INK);
            px = py;
        }
        dx = gx + (int32_t)LTR_N - 1;
    } else {
        for (k = 0, px = 0; k < LTR_N; k++) {        /* px: the previous point's y (joined: no gaps at edges); the
                                                       * area to the zero line dithered */
            int32_t py = LG_CY - lfo_shape(wave, m, k * (0xFFFFFFFFu / LTR_N) + off) * LG_A / 32767;
            if (!k)
                px = py;
            if (py < LG_CY - 1)
                cv_dither(gx + (int32_t)k, py + 2, 1, LG_CY - py - 2, C_INK);
            else if (py > LG_CY + 1)
                cv_dither(gx + (int32_t)k, LG_CY + 1, 1, py - LG_CY - 1, C_INK);
            cv_rect(gx + (int32_t)k, py < px ? py : px, 1, (py < px ? px - py : py - px) + 2, C_INK);
            px = py;
        }
        k = t->lfo[l].ph / (0xFFFFFFFFu / LTR_N);
        dx = gx + (int32_t)(k < LTR_N ? k : LTR_N - 1u);
    }
    dy = LG_CY - lfo_out(t, l) * LG_A / 32767;
    cv_rect(dx - 3, dy - 3, 7, 7, C_BG);             /* the live value: 5x5 solid, a clear ring round it */
    cv_rect(dx - 2, dy - 2, 5, 5, C_INK);
    md_text(x0 + 6, 86, &MD_F3, 2, trig, C_INK);
    lfo_route_text(t, l, b, sizeof b);
    md_text(x0 + 114 - md_text_w(&MD_F3, 2, b), 86, &MD_F3, 2, b, C_INK);
}
static void graph_lfo(uint16_t c)
{
    (void)c;
    graph_lfo_half(TSEL, 0);
    graph_lfo_half(TSEL, 1);
    cv_vdot(120, 2, 98, C_INK);                       /* between the halves (KNOB 2 | KNOB 3) */
}

/* a DEST slot's modulation now: its source's last value x DEPTH, Q15 (0: off) */
static int32_t lfo_slot_now(const track_t *t, uint32_t s)
{
    if (!t->p[LS_ID[s][LS_DEST]] || !t->p[LS_ID[s][LS_DEPTH]])
        return 0;
    return lfo_out(t, t->p[LS_ID[s][LS_SRC]] ? 1u : 0u) * clamp(t->p[LS_ID[s][LS_DEPTH]], -64, 64) / 64;
}

/* LFO DEST: the 4 slots as rows ("3  LFO2  > DECAY  -40%" and a meter of the slot's modulation now, from its middle);
 * the selected slot (KNOB 1) inverted */
static void graph_ldest(void)
{
    const track_t *t = TSEL;
    uint32_t s;
    char b[16], v[12];
    const char *u;
    for (s = 0; s < LS_N; s++) {
        int32_t y = 6 + (int32_t)s * 24, w = lfo_slot_now(t, s) * 20 / 32767;
        uint16_t pen = s == (song.lslot & 3u) ? C_BG : C_INK;
        if (s == (song.lslot & 3u))
            cv_rect(1, y - 3, 238, 18, C_INK);
        b[0] = (char)('1' + s);
        b[1] = 0;
        md_text(8, y, &MD_F3, 2, b, pen);
        md_text(28, y, &MD_F3, 2, N_LSRC[t->p[LS_ID[s][LS_SRC]] ? 1 : 0], pen);
        str_cpy(b, "> ", sizeof b);
        str_cpy(b + 2, lfo_dest_label(t, t->p[LS_ID[s][LS_DEST]]), sizeof b - 2u);
        md_text(76, y, &MD_F3, 2, b, pen);
        param_format(&TP[P_LS3DEPTH], t->p[LS_ID[s][LS_DEPTH]], v, &u);
        str_cpy(v + str_len(v), u, sizeof v - str_len(v));
        md_text(186 - md_text_w(&MD_F3, 2, v), y, &MD_F3, 2, v, pen);
        cv_rect(214, y, 1, 11, pen);                  /* the meter's middle; the modulation from it */
        if (w)
            cv_rect(w > 0 ? 215 : 214 + w, y + 2, w > 0 ? w : -w, 7, pen);
    }
    cv_vdot(22, 3, 97, C_INK);                        /* the column rules: slot | source | dest, depth | meter */
    cv_vdot(70, 3, 97, C_INK);
    cv_vdot(192, 3, 97, C_INK);
}

/* RESON pages: the model in large type, the pitch (CHORD: its four notes), the ring's partials on a 4-octave
 * axis (STRNG all, PIPE odd, CHORD its notes), a meter of the ring */
static void graph_reson(uint16_t c)
{
    static const uint8_t HX[16] = {0, 34, 54, 68, 79, 88, 95, 102, 108, 113, 118, 122, 126, 129, 133, 136};
    const track_t *t = TSEL;
    uint32_t m = (uint32_t)clamp(t->p[P_RMODEL], 0, RS_NMODEL - 1), h, n;
    int32_t note = clamp(t->p[P_RTUNE], 24, 96);
    char b[24];
    if (!m) {
        off_word();
        return;
    }
    md_text(4, 4, &MD_F3, 4, N_RMODEL[m], C_INK);
    b[0] = 0;
    if (m == RS_CHORD) {
        note = note < 48 ? 48 : note;
        for (h = 0; h < 4u; h++) {                    /* "C3 E3 G3 C4" */
            int32_t nn = note + RS_CHORD_IV[rs_chord(t)][h];
            str_cpy(b + str_len(b), N_NOTE[(uint32_t)nn % 12u], sizeof b - str_len(b));
            fmt_int(b + str_len(b), nn / 12 - 1);
            str_cpy(b + str_len(b), " ", sizeof b - str_len(b));
        }
    } else {
        str_cpy(b, N_NOTE[(uint32_t)note % 12u], sizeof b);
        fmt_int(b + str_len(b), note / 12 - 1);
    }
    md_text(4, 44, &MD_F3, 2, b, C_INK);
    cv_hdot(100, 235, 74, C_INK);
    if (m == RS_MODAL) {
        for (h = 0; h < PX_NMODE; h++) {
            uint32_t r = px_modal_ratio(clamp(t->p[P_RSTRCT], 0, 127) * 516, h), lg = 0, x;
            while (r >= (2u << 16)) {                 /* log2: whole octaves, then the linear rest */
                r >>= 1;
                lg += 256u;
            }
            lg += (r - 65536u) >> 8;
            x = lg * 34u / 256u;
            if (x < 136u)
                cv_rect(100 + (int32_t)x, 74 - (int32_t)(56u - h * 4u), 2, (int32_t)(56u - h * 4u), C_INK);
        }
    } else
    for (h = 0; h < (m == RS_CHORD ? 4u : 16u); h++) {
        uint32_t x = m == RS_CHORD ? (uint32_t)RS_CHORD_IV[rs_chord(t)][h] * 136u / 48u : HX[h];
        if (m == RS_PIPE && (h & 1u))
            continue;                                 /* PIPE: odd partials only (h = 0 is the 1st) */
        n = m == RS_CHORD ? 50u : 56u - h * 3u;
        cv_rect(100 + (int32_t)x, 74 - (int32_t)n, 2, (int32_t)n, C_INK);
    }
    (void)c;
    hmeter(4, 80, 86, (int32_t)((uint32_t)t->rs.peak * 1000u / 32767u), 0);   /* the ring's level */
}

/* FILTER: TYPE's response at CUT / RESO (an approximation: flat, the RESO peak, 12 dB / octave; BP 6 dB; the
 * notch's width by RESO), 30 Hz .. 16 kHz across x 100..235 (15 px an octave), +18 .. -36 dB; ENV's reach a
 * dotted line at CUT + ENV */
static int32_t flt_resp_db(uint32_t ty, int32_t dx, int32_t reso)   /* dx: px from the cutoff (15 an octave) */
{
    int32_t pk = -6 + reso * 24 / 127, ad = dx < 0 ? -dx : dx, w = 4 + (127 - reso) / 8;
    if (ty == FT_HP)
        dx = -dx;
    if (ty == FT_LP || ty == FT_HP)
        return dx <= -15 ? 0 : dx <= 0 ? pk * (15 + dx) / 15 : pk - dx * 12 / 15;
    if (ty == FT_BP)
        return pk - ad * 6 / 15;
    return ad >= w ? 0 : -36 + ad * 36 / w;                    /* NOTCH */
}

static void graph_filter(uint16_t c)
{
    const track_t *t = TSEL;
    uint32_t ty = (uint32_t)clamp(t->p[P_FTYPE], 0, FT_N - 1);
    int32_t cx = clamp(t->p[P_FCUT], 0, 127) * 135 / 127, x, py = 0, ex;
    char b[16];
    const char *u;
    (void)c;
    if (!ty) {
        off_word();
        return;
    }
    md_text(4, 4, &MD_F3, 4, N_FTYPE[ty], C_INK);
    param_format(&TP[P_FCUT], t->p[P_FCUT], b, &u);
    str_cpy(b + str_len(b), u, sizeof b - str_len(b));    /* "1.0kHz" */
    md_text(4, 44, &MD_F3, 2, b, C_INK);
    cv_hdot(100, 235, 28, C_INK);                              /* 0 dB */
    for (x = 0; x < 136; x++) {
        int32_t db = clamp(flt_resp_db(ty, x - cx, clamp(t->p[P_FRESO], 0, 127)), -36, 18);
        int32_t y = 28 - db * 3 / 2;
        if (x)
            cv_line(99 + x, py, 100 + x, y, C_INK);
        py = y;
    }
    ex = clamp(t->p[P_FCUT] + t->p[P_FENV], 0, 127) * 135 / 127;   /* ENV: the cutoff at the hit */
    if (t->p[P_FENV])
        for (x = 10; x < 84; x += 4)
            cv_rect(100 + ex, x, 1, 2, C_INK);
}

static uint32_t graph_signature(void)
{
    const page_t *pg = cur_page();
    const track_t *t = TSEL;
    uint32_t h = 2166136261u, i;
    if (ui.hot_t && settings.zoom)
        h = str_hash(str_hash(str_hash(h ^ 0x5555u, ui.focus_v), ui.focus_l), ui.focus_u);
    if (ui.home)
        return h ^ (ui.frame / 2u);                  /* scope: redraw every other frame */
    h ^= (uint32_t)pg->graph * 131u + song.sel * 7777u + ui.bank * 104729u;
    for (i = 0; i < P_COUNT; i++)
        h = (h ^ (uint32_t)t->p[i]) * 16777619u;
    h ^= (uint32_t)song.g[G_SLOT] * 13u;
    if (pg->graph == GR_SLCR && t->p[P_SLCR])
        h ^= (sl[song.sel].idx + 1u) * 2654435761u;
    if (pg->graph == GR_SLOTS) {
        char nb[NAME_LEN + 1u];
        for (i = 0; i < 4u; i++) {
            h ^= (uint32_t)project_name(i, nb) << (20u + i);
            h = str_hash(h, nb);
        }
        h = str_hash(h, chain.name);
    }
    if (pg->graph == GR_STEPS || pg->graph == GR_GRID)
        h ^= steps_hash(t) + (song.playing ? view_idx(t) + 1u : 0u) * 31u;
        h ^= (held_step() + 1u) * 977u;
    if (pg->graph == GR_GRID)                         /* step record's cursor, the key sounding, KEYS (the roll's view
                                                       * follows from these: draw_graph) */
        h ^= (ui.srec ? ui.sr_cur + 1u : 0u) * 1543u +
             (kz.key && kz.trk == song.sel ? (uint32_t)(t->key_nt + 61) : 0u) * 389u + (uint32_t)keys_mode() * 99991u;
    if (pg->graph == GR_MODEL) {
        char nb[9];
        h ^= (uint32_t)song.g[G_BPM] * 40503u + vg.ok * 2654435761u;   /* ACID's gate: GATE of a step at the tempo; the
                                                                          * rendered hit done */
        preset_name_of(preset_of(t), nb);             /* the preset's line (its user slot may change) */
        h = str_hash(h ^ t->pre_ed * 7919u, nb);
    }
    if (pg->graph == GR_GRIDS) {
        for (i = G_GMODE; i <= G_GLEN3; i++)
            h = (h ^ (uint32_t)song.g[i]) * 16777619u;
        for (i = 0; i < NTRK; i++)
            h = (h ^ (uint32_t)trk[i].p[P_SRC]) * 16777619u;
        h ^= ui.page * 389u;
        if (song.playing)
            h ^= (grids.last + 1u + (grids.elast[0] | grids.elast[1] << 8 | (uint32_t)grids.elast[2] << 16) * 64u) *
                 2654435761u;
    }
    if (pg->graph == GR_COMP) {
        for (i = G_CSRC; i <= G_CGHOST; i++)
            h = (h ^ (uint32_t)song.g[i]) * 16777619u;
        for (i = 0; i < NTRK; i++)
            h = (h ^ (uint32_t)(trk[i].p[P_DUCK] | trk_silent(&trk[i]) << 1)) * 16777619u;
        h ^= ui.page * 389u;
        if (comp_on())
            h ^= ((uint32_t)(-comp.gr) >> 7) * 2654435761u + (uint32_t)meter_px(comp.peak) * 40503u;
    }
    if (pg->graph == GR_LFO || pg->graph == GR_LDEST) {
        uint32_t l;
        h ^= ui.page * 389u + song.lslot * 7919u;
        for (l = 0; l < 2u; l++) {
            h ^= ((t->lfo[l].ph >> 26) * 2654435761u + (uint32_t)(lfo_out(t, l) >> 10) * 40503u) * (l + 1u);
            if (lfo_random((uint32_t)t->p[(l ? P_LFO2 : P_LFO1) + LF_WAVE]))
                h ^= ((ui.tr_h[l] + 1u) * 97u + ui.tr_n[l] * 65537u) * (l + 3u);
        }
    }
    if (pg->graph == GR_RESON)
        h ^= ((uint32_t)t->rs.peak >> 9) * 2654435761u + (uint32_t)rs_chord(t) * 7919u;
    return h;
}

/* PERFORM (perform.c), while FX is held: the black keys' effects in two rows of five (as on the keys: 1/8 1/16 1/32
 * REV TAPE, LPF HPF FRZ OCT+ OCT-) and the white keys' tracks; held: lit, running: inverted, unavailable at the
 * tempo: dim, a track muted (by its key or its MUTE): amber */
static const char *const PF_NAME[PF_M1] = {"R1/8", "R1/16", "R1/32", "REV", "LPF", "HPF", "TAPE", "FRZ", "OCT+", "OCT-"};
static void perf_cell(int32_t x, int32_t y, int32_t w, const char *s, uint32_t st)   /* st: 0 off 1 held 2 run 3 dim 4 mute */
{
    int32_t tx = x + (w - md_text_w(&MD_F3, 2, s)) / 2;
    if (st == 1u || st == 2u || st == 4u) {          /* held / running / muted: solid, the name in BG */
        cv_rect(x, y, w, 34, C_INK);
        md_text(tx, y + 12, &MD_F3, 2, s, C_BG);
        if (st == 4u)                                /* muted: struck through */
            cv_rect(x + 3, y + 16, w - 6, 1, C_BG);
        return;
    }
    if (st == 3u) {                                  /* not available: a dotted outline */
        cv_hdot(x, x + w - 1, y, C_INK);
        cv_hdot(x, x + w - 1, y + 33, C_INK);
        cv_vdot(x, y, y + 33, C_INK);
        cv_vdot(x + w - 1, y, y + 33, C_INK);
    } else {
        cv_frame(x, y, w, 34, C_INK);
    }
    md_text(tx, y + 12, &MD_F3, 2, s, C_INK);
}
static void graph_perf(void)
{
    uint32_t held = perf_kill ? 0u : perf_held, act = perf_act, ok = perf_avail(), i, nw, wi;
    uint32_t sig = held * 31u + act * 7u + ok;
    for (i = 0; i < NTRK; i++)
        sig = sig * 3u + (uint32_t)(trk[i].p[P_MUTE] != 0);
    for (nw = rg_plan(&ui.graph_sig, sig, 0, Y_GRAPH, 240, H_GRAPH), wi = 0; wi < nw; wi++) {
        rg_win(wi, Y_GRAPH, C_BG);
        cv_oy = 0;
        cv_rect(0, 0, 1, H_GRAPH, C_INK);
        cv_rect(239, 0, 1, H_GRAPH, C_INK);
        for (i = 0; i < 10u; i++) {
            uint32_t e = PF_BLACK[i];
            perf_cell(4 + (int32_t)(i % 5u) * 47, i < 5u ? 4 : 44, 44, PF_NAME[e],
                      !((ok >> e) & 1u) ? 3u : (act >> e) & 1u ? 2u : (held >> e) & 1u ? 1u : 0u);
        }
        for (i = 0; i < NTRK; i++) {
            char n[2] = {(char)('1' + i), 0};
            perf_cell(4 + (int32_t)i * 29, 84, 27, n,
                      (held >> (PF_M1 + i)) & 1u || trk[i].p[P_MUTE] ? 4u : 0u);
        }
        cv_blit(0, Y_GRAPH);
    }
}
/* the MUTE screen (spec §5, GLO held): the 8 tracks as boxes, two rows of 4 (screen y MU_Y.., a 240 x 124 canvas) */
#define MU_Y 22u
#define MU_BW 55u
#define MU_BH 40u
#define MU_BX(k) (4u + (k) % 4u * 59u)
#define MU_BY(k) (MU_Y + 4u + (k) / 4u * 60u)
/* filled = audible, outline = muted, a mute waiting for the bar blinks (250 ms, amber); in each the track and its
 * model, under it the key that toggles it; below, the mode and its keys, and G4's MIDI LEARN. The top bar as
 * everywhere */
static void draw_mute(void)
{
    uint32_t k, blink = (fm1_ticks() / (250u * 1000u * FM1_TICKS_PER_US)) & 1u;
    uint32_t sig = settings.mutebar * 7u + song.mute_q * 977u + (song.mute_q ? blink + 1u : 0u) * 131u + ui.ml.on * 5003u;
    int32_t x, w;
    char b[24];
    for (k = 0; k < NTRK; k++)
        sig = (sig ^ ((uint32_t)(trk[k].p[P_MUTE] != 0) + (uint32_t)trk[k].p[P_MODEL] * 2u)) * 16777619u;
    if (ui.force) {                                   /* the ground between the two canvases (y 22..145, 164..225) */
        static const uint8_t BAND[3][2] = {{H_HEAD, MU_Y}, {MU_Y + 124u, 164u}, {226u, 239u}};
        for (k = 0; k < 3u; k++) {
            lcd_fill(1, BAND[k][0], 238, (uint32_t)(BAND[k][1] - BAND[k][0]), C_BG);
            lcd_fill(0, BAND[k][0], 1, (uint32_t)(BAND[k][1] - BAND[k][0]), C_INK);
            lcd_fill(239, BAND[k][0], 1, (uint32_t)(BAND[k][1] - BAND[k][0]), C_INK);
        }
        lcd_fill(0, 239, 240, 1, C_INK);
    }
    draw_head();
    if (ui.msg_t)
        ui.msg_t--;
    if (ui.bpm_t)
        ui.bpm_t--;
    if (!ui.force && sig == ui.graph_sig)
        return;
    ui.graph_sig = sig;
    ui.force = 0;
    cv_begin_at(240, 124, MU_Y, C_BG);
    cv_oy = 0;
    cv_rect(0, 0, 1, 124, C_INK);
    cv_rect(239, 0, 1, 124, C_INK);
    for (k = 0; k < NTRK; k++) {                     /* audible: solid; muted: an outline; waiting for the bar: the
                                                       * box blinks solid / outline */
        int32_t bx = (int32_t)MU_BX(k), by = (int32_t)(MU_BY(k) - MU_Y);
        uint32_t wait = (song.mute_q >> k) & 1u;
        int on = !trk[k].p[P_MUTE] ^ (wait && blink);
        const char *mn = N_MODEL[(uint32_t)trk[k].p[P_MODEL] % NMODELS];
        uint16_t pen = on ? C_BG : C_INK;
        if (on)
            cv_rect(bx, by, MU_BW, MU_BH, C_INK);
        else
            cv_frame(bx, by, MU_BW, MU_BH, C_INK);
        b[0] = (char)('1' + k);
        b[1] = 0;
        md_text(bx + ((int32_t)MU_BW - md_text_w(&MD_F3, 2, b)) / 2, by + 6, &MD_F3, 2, b, pen);
        md_text(bx + ((int32_t)MU_BW - md_text_w(&MD_F3, 2, mn)) / 2, by + 22, &MD_F3, 2, mn, pen);
        note_text(b, 53 + KEY_TRK_KEY[k]);
        md_text(bx + ((int32_t)MU_BW - md_text_w(&MD_F3, 2, b)) / 2, by + (int32_t)MU_BH + 4, &MD_F3, 2, b, C_INK);
    }
    cv_blit(0, MU_Y);
    cv_begin_at(240, 62, 164, C_BG);                  /* "MUTE: NOW", "C# NOW  D# NEXT BAR" (the mode's dithered
                                                       * when not chosen), "G4 LEARN: OFF" / "ON" (MIDI LEARN) */
    cv_rect(0, 0, 1, 62, C_INK);
    cv_rect(239, 0, 1, 62, C_INK);
    str_cpy(b, settings.mutebar ? "MUTE: NEXT BAR" : "MUTE: NOW", sizeof b);
    md_text((240 - md_text_w(&MD_F3, 2, b)) / 2, 4, &MD_F3, 2, b, C_INK);
    w = md_text_w(&MD_F3, 2, "C# NOW  D# NEXT BAR");
    x = (240 - w) / 2;
    k = (uint32_t)md_text(x, 24, &MD_F3, 2, "C# NOW", C_INK);
    if (settings.mutebar)
        cv_dither(x, 24, (int32_t)k - x, 10, C_BG);
    x = (int32_t)k + 12;
    k = (uint32_t)md_text(x, 24, &MD_F3, 2, "D# NEXT BAR", C_INK);
    if (!settings.mutebar)
        cv_dither(x, 24, (int32_t)k - x, 10, C_BG);
    str_cpy(b, ui.ml.on ? "G4 LEARN: ON" : "G4 LEARN: OFF", sizeof b);
    x = (240 - md_text_w(&MD_F3, 2, b)) / 2;
    md_text(x, 44, &MD_F3, 2, b, C_INK);
    if (!ui.ml.on)
        cv_dither(x, 44, md_text_w(&MD_F3, 2, b), 10, C_BG);
    cv_blit(0, 164);
}
/* PERFORM: KNOB 1..4 FILTER CRUSH THROW DEPTH (SHIMMER while OCT UP / DN plays) */
static void perf_columns(void)
{
    char val[12];
    int32_t m = perf_k[0];
    fmt_int(val, m < 0 ? -m : m);
    draw_column(0, "FILT", m ? val : "OFF", m < 0 ? "LPF" : m > 0 ? "HPF" : "", m ? HOT(0u) : CV_DIM, (m + 100) * 5);
    fmt_int(val, perf_k[1]);
    draw_column(1, "CRUSH", perf_k[1] ? val : "OFF", perf_k[1] ? "%" : "", perf_k[1] ? HOT(1u) : CV_DIM,
                perf_k[1] * 10);
    fmt_int(val, perf_k[2]);
    draw_column(2, "THROW", perf_k[2] ? val : "OFF", perf_k[2] ? "%" : "", perf_k[2] ? HOT(2u) : CV_DIM,
                perf_k[2] * 10);
    if (perf_harm_on()) {
        fmt_int(val, perf_k[3]);
        draw_column(3, "SHIMR", perf_k[3] ? val : "OFF", perf_k[3] ? "%" : "", perf_k[3] ? HOT(3u) : CV_DIM,
                    perf_k[3] * 10);
    } else {
        fmt_int(val, 100 - perf_k[3]);
        draw_column(3, "DEPTH", val, "%", HOT(3u), (100 - perf_k[3]) * 10);
    }
}

/* MOTION: the selected track's LEN steps, 16 a row; a step holding events lit (grey with PLAY OFF), the
 * playhead outlined */
static void graph_motion(void)
{
    const track_t *t = TSEL;
    uint32_t k = song.sel, len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), i, sig, n = 0, nw, wi;
    uint32_t has[2] = {0, 0}, ph = song.playing ? t->seq_idx % len : 0xFFu, z = lk_seek(&mo.b, k + 1u, 0);
    for (i = lk_seek(&mo.b, k, 0); i < z; i++)          /* the track's motion events (not its locks) */
        if (!lk_is_lock(&mo.b, i)) {
            uint32_t st = ((uint32_t)mo.b.key[i] >> 7) & 63u;
            has[st >> 5] |= 1u << (st & 31u);
            n++;
        }
    sig = has[0] * 31u + has[1] * 131u + len * 7u + ph * 1009u + k * 65537u + n * 3u + (uint32_t)motion_on(k);
    for (nw = rg_plan(&ui.graph_sig, sig, 0, Y_GRAPH, 240, H_GRAPH), wi = 0; wi < nw; wi++) {
        rg_win(wi, Y_GRAPH, C_BG);
        cv_oy = 0;
        for (i = 0; i < len; i++) {                       /* an event: solid (MOTION PLAY on) or dithered (off); the
                                                           * playhead a second outline */
            int32_t x = 8 + (int32_t)(i & 15u) * 14, y = 8 + (int32_t)(i >> 4) * 28;
            int ev = (int)((has[i >> 5] >> (i & 31u)) & 1u);
            cv_frame(x, y, 12, 22, C_INK);
            if (ev && motion_on(k))
                cv_rect(x + 2, y + 2, 8, 18, C_INK);
            else if (ev)
                cv_dither(x + 2, y + 2, 8, 18, C_INK);
            if (i == ph)
                cv_frame(x - 1, y - 1, 14, 24, C_INK);
        }
        cv_rect(0, 0, 1, H_GRAPH, C_INK);
        cv_rect(239, 0, 1, H_GRAPH, C_INK);
        cv_blit(0, Y_GRAPH);
    }
}
/* MOTION: KNOB 1 PLAY, 2 MOTION and 3 LOCKS (the selected track's recorded motion events and parameter locks), 4
 * CLEAR (the motion) */
static void motion_columns(void)
{
    char val[12];
    uint32_t k = song.sel;
    draw_column(0, "PLAY", motion_on(k) ? "ON" : "OFF", "", HOT(0u), -1);
    fmt_int(val, (int32_t)motion_count(k));
    draw_column(1, "MOTION", val, "", CV_NORM, -1);
    fmt_int(val, (int32_t)lock_count(k));
    draw_column(2, "LOCKS", val, "", CV_NORM, -1);
    draw_column(3, "CLEAR", "", "", CV_NORM, -1);
}

/* SONG: 4 rows of the list ("2  B  x4  BREAK"), the + row after the last; the playing row marked, its repeats left */
static void graph_song(void)
{
    const chain_config_t *c = &chain.cfg;
    uint32_t sel = ui.song_row < c->count ? ui.song_row : c->count, first = sel > 2u ? sel - 2u : 0u, i,
             sig = 2166136261u, last, nw, wi;
    char b[16], nm[NAME_LEN + 1u];
    for (i = 0; i < c->count; i++)
        sig = (sig ^ (c->row[i].slot | (uint32_t)c->row[i].repeat << 2)) * 16777619u;
    for (i = 0; i < 4u; i++) {
        project_name(i, nm);
        sig = str_hash(sig, nm) + (uint32_t)project_used(i) * (i + 3u);
    }
    sig += c->count * 7u + sel * 1009u + c->loop * 13u +
           (chain.running ? (chain.row + 1u) * 104729u + chain.left * 65537u : 0u);
    for (nw = rg_plan(&ui.graph_sig, sig, 0, Y_GRAPH, 240, H_GRAPH), wi = 0; wi < nw; wi++) {
        rg_win(wi, Y_GRAPH, C_BG);
        cv_oy = 0;
        last = c->count < CHAIN_ROWS ? c->count : CHAIN_ROWS - 1u;
        for (i = first; i <= last && i < first + 4u; i++) {   /* the cursor's row inverted; the playing row: a play
                                                           * mark */
            int32_t y = 6 + (int32_t)(i - first) * 28;
            int s = i == sel, play = chain.running && i == chain.row;
            uint16_t pen = s ? C_BG : C_INK;
            if (s)
                cv_rect(1, y - 3, 238, 16, C_INK);
            if (play)
                md_text(4, y, &MD_ICON, 2, "p", pen);
            if (i >= c->count) {
                md_text(16, y, &MD_F3, 2, "+", pen);
                if (!c->count)
                    md_text(44, y, &MD_F3, 2, "KNOB 2: ADD A ROW", pen);
                continue;
            }
            fmt_int(b, (int32_t)i + 1);
            md_text(16, y, &MD_F3, 2, b, pen);
            b[0] = (char)('A' + c->row[i].slot);
            b[1] = 0;
            md_text(48, y, &MD_F3, 2, b, pen);
            b[0] = 'x';
            fmt_int(b + 1, c->row[i].repeat);
            md_text(70, y, &MD_F3, 2, b, pen);
            if (play) {
                fmt_int(b, chain.left);
                str_cpy(b + str_len(b), " LEFT", 8);
                md_text(234 - md_text_w(&MD_F3, 2, b), y, &MD_F3, 2, b, pen);
            } else if (project_name(c->row[i].slot, nm) && nm[0]) {
                char f[NAME_LEN + 1u];
                md_fit(f, nm, sizeof f, 124);
                md_text(108, y, &MD_F3, 2, f, pen);
            }
        }
        cv_vdot(40, 3, 112, C_INK);                       /* the column rules: row | slot | repeats | name (from the */
        cv_vdot(64, 3, 112, C_INK);                       /* first row's bar down) */
        cv_vdot(100, 3, 112, C_INK);
        cv_rect(0, 0, 1, H_GRAPH, C_INK);
        cv_rect(239, 0, 1, H_GRAPH, C_INK);
        cv_blit(0, Y_GRAPH);
    }
}
/* SONG: KNOB 1 ROW, 2 SLOT, 3 REPS, 4 LOOP */
static void song_columns(void)
{
    const chain_config_t *c = &chain.cfg;
    uint32_t r = ui.song_row < c->count ? ui.song_row : c->count;
    char v[12], u[8];
    if (r >= c->count) {
        draw_column(0, "ROW", "+", "", HOT(0u), -1);
        draw_column(1, "SLOT", "--", "", CV_DIM, -1);
        draw_column(2, "REPS", "--", "", CV_DIM, -1);
    } else {
        fmt_int(v, (int32_t)r + 1);
        u[0] = '/';
        fmt_int(u + 1, c->count);
        draw_column(0, "ROW", v, u, HOT(0u), -1);
        v[0] = (char)('A' + c->row[r].slot);
        v[1] = 0;
        draw_column(1, "SLOT", v, "", HOT(1u), c->row[r].slot * 1000 / 3);
        v[0] = 'x';
        fmt_int(v + 1, c->row[r].repeat);
        draw_column(2, "REPS", v, "", HOT(2u), (c->row[r].repeat - 1) * 1000 / 15);
    }
    draw_column(3, "LOOP", c->loop ? "ON" : "OFF", "", HOT(3u), -1);
}

/* HOME's scope (y 69..180) in the canvas or a window of it: the tab, the wave (fresh: a new snapshot), ZOOM */
static void scope_paint(int fresh)
{
    cv_oy = 0;
    cv_tab(1, 0, "SCOPE", C_INK, C_BG);
    graph_scope(62, 46, fresh);
    if (ui.hot_t && settings.zoom) {                   /* ZOOM: the touched value large, under the tab */
        int32_t x;
        cv_popup(4, 16, 150, 50, "", C_INK, C_BG);
        md_text(10, 20, &MD_F3, 2, ui.focus_l, C_INK);
        x = md_text(10, 34, &MD_F3, 4, ui.focus_v, C_INK);
        md_text(x + 4, 44, &MD_F3, 2, ui.focus_u, C_INK);
    }
    cv_rect(0, 0, 1, 112, C_INK);
    cv_rect(239, 0, 1, 112, C_INK);
}

/* the page's graph in the canvas or a window of it, with ZOOM and the frame's edges; 1: something in the band above
 * the graph proper (the canvas from row 0). whole: the graph changed (the roll's view is chosen again) */
static uint32_t graph_paint(int whole)
{
    const page_t *pg = cur_page();
    const track_t *t = TSEL;
    uint16_t c = C_INK;
    uint32_t top = 0;
    cv_oy = G_OY;
    switch (pg->graph) {
    case GR_MODEL:
        graph_model(t, c);
        top = 1;                                      /* (the preset's line in the band above) */
        break;
    case GR_STEPS:
        graph_steps(t, c);
        break;
    case GR_GRID:
        if (trk_model(t)->gate && !view_src(t)) {
            if (whole)
                roll_view(t);                         /* (the graph changed: the view chosen again) */
            graph_roll(t, c);
            if (!ui.srec && held_line(t, -18))      /* a held step's line in the band above the roll (screen y
                                                       * 74..97, blitted this frame; the next one clears it) */
                top = 1;
        } else
            graph_grid(t, c);
        break;
    case GR_FX:
        graph_fx(t, c);
        break;
    case GR_SLCR:
        graph_slicer(t, c);
        break;
    case GR_GRIDS:
        graph_grids(c, pg->id[0] == G_GFILL1);
        break;
    case GR_COMP:
        graph_comp(c, pg->id[0] == G_CATK);
        break;
    case GR_LFO:
        graph_lfo(c);
        break;
    case GR_LDEST:
        graph_ldest();
        break;
    case GR_RESON:
        graph_reson(c);
        break;
    case GR_FILTER:
        graph_filter(c);
        break;
    case GR_SLOTS:
        cv_oy = 0;
        top = 1;
        graph_slots();
        break;
    default:
        break;
    }
    cv_oy = 0;
    if (ui.hot_t && settings.zoom) {                   /* ZOOM: the touched value large, in a popup */
        int32_t x;
        top = 1;
        cv_popup(4, 4, 150, 50, "", C_INK, C_BG);
        md_text(10, 8, &MD_F3, 2, ui.focus_l, C_INK);
        x = md_text(10, 22, &MD_F3, 4, ui.focus_v, C_INK);
        md_text(x + 4, 32, &MD_F3, 2, ui.focus_u, C_INK);
    }
    cv_rect(0, 0, 1, H_GRAPH, C_INK);                 /* the frame's edges */
    cv_rect(239, 0, 1, H_GRAPH, C_INK);
    return top;
}

static void draw_graph(void)
{
    const page_t *pg = cur_page();
    uint32_t sig, i, n, top;
    int whole;
    if (!ui.home && !ui.layer && pg->graph == GR_MODEL) {
        vg_step(TSEL);                                /* the engine view's rendered hit: a frame's share (under a popup
                                                       * too: MODEL's message) */
        if (vg.ok != vg.shown && vg.okm == (uint32_t)TSEL->p[P_MODEL] % NMODELS && vg_model(vg.okm))
            rg.thaw = 1;                              /* .. done, not shown yet: drawn now, round the popups */
    }
    if ((ui.msg_t || ui.bpm_t) && !ui.force && !rg.thaw)   /* a popup over the graph: the graph holds still under it
                                                       * (no redraw-then-popup flicker); a popup coming / going: the
                                                       * graph as it is now, round the popups up (rg.ex) */
        return;
    if (ui.layer) {                                     /* PERFORM: the map */
        graph_perf();
        ui.graph_top = 1;
        return;
    }
    if (!ui.home && pg->graph == GR_MOTION) {          /* MOTION: the steps holding events */
        graph_motion();
        ui.graph_top = 1;
        return;
    }
    if (!ui.home && pg->graph == GR_SONG) {            /* SONG: the rows */
        graph_song();
        ui.graph_top = 1;
        return;
    }
    if (!ui.home && pg->graph == GR_MIX) {
        draw_mix();
        ui.graph_top = 1;
        return;
    }
    if (!ui.home && pg->graph == GR_LFO)
        lfo_trail();
    sig = graph_signature();
    if (ui.home && ui.hot_t && !settings.zoom)        /* a knob turning: the scope waits (the LCD's time to the knob) */
        sig = ui.graph_sig;
    whole = ui.force || sig != ui.graph_sig;
    if (ui.home) {                                    /* HOME: the scope under the strip, y 69..180 */
        for (n = rg_plan(&ui.graph_sig, sig, 0, 69, 240, 112), i = 0; i < n; i++) {
            rg_win(i, 69, C_BG);
            scope_paint(whole && !i);                 /* (one snapshot for all its windows) */
            cv_blit(0, 69);
        }
        return;
    }
    n = rg_plan(&ui.graph_sig, sig, 0, Y_GRAPH, 240, H_GRAPH);
    if (whole) {                                      /* (the band above the graph proper only when it holds
                                                       * something, or held something) */
        uint32_t r0;
        cv_begin_at(240, H_GRAPH, Y_GRAPH, C_BG);
        top = graph_paint(1);
        r0 = top || ui.graph_top || ui.force || rg.dmg.w ? 0u : G_OY;
        ui.graph_top = (uint8_t)top;
        rg_region(0, Y_GRAPH, 240, H_GRAPH);
        rg_add(0, (int32_t)r0, 240, H_GRAPH - (int32_t)r0);
        if (rg.n == 1u && rg.win[0].w == 240 && rg.win[0].h == H_GRAPH - (int32_t)r0) {
            cv_blit_from(0, Y_GRAPH, r0);
            return;
        }
        n = rg.n;                                     /* round a popup up: in windows */
    }
    for (i = 0; i < n; i++) {                         /* a popup's place */
        rg_win(i, Y_GRAPH, C_BG);
        top = graph_paint(0);
        cv_blit(0, Y_GRAPH);
        ui.graph_top |= (uint8_t)top;
    }
}

/* the footer's note (spec §7): step record's cursor step, else the step held on the grid, in the steps the roll and
 * the grid show (a song's row while a song runs): "C#3", + " ACC" " SLD"; a TIE the note it holds (pr_src) "C#3 TIE",
 * after a rest "TIE"; 0 (b "") when neither, or the step is empty */
static int foot_note(char *b)
{
    const track_t *t = TSEL;
    const step_t *st = seq_steps(t), *s;
    uint32_t si, src, len = view_len(t);
    b[0] = 0;
    if (ui.srec)
        si = ui.sr_cur % NSTEP;
    else if (step_keys() && held_step() < NSTEP)
        si = held_step();
    else
        return 0;
    s = &st[si];
    if (!s->on && !s->tie)
        return 0;
    if (s->tie) {
        src = si < len ? pr_src(t, si, len) : NSTEP;
        if (src == NSTEP) {
            str_cpy(b, "TIE", 4);
            return 1;
        }
        note_text(b, 60 + st[src].nt);
        str_cpy(b + str_len(b), " TIE", 6);
        return 1;
    }
    note_text(b, 60 + s->nt);
    if (s->acc)
        str_cpy(b + str_len(b), " ACC", 6);
    if (s->slide)
        str_cpy(b + str_len(b), " SLD", 6);
    return 1;
}

/* the trig strip (spec 2026-10-19-md-ui §5): a dotted rule, the bank's 16 steps (accent solid, a trig outline +
 * dither, none an outline; the playhead an outline 2 px out) | the breadcrumb: T<n>>model>page left, the page number
 * right; a held / cursor step's note at x 120 (the number then hidden); MIDI LEARN's line across it */
static struct {
    uint32_t trig_sig;           /* the trig strip's steps shown: on | acc << 16 (exact, no hash) */
    uint32_t lock_sig;           /* .. its lock marks (STEPS: the bank's locked steps) */
    uint8_t trig_n;              /* .. and their count */
    uint8_t ph;                  /* its playhead shown (16+: none) */
    int16_t x0, x1;              /* the breadcrumb's text span shown */
} ft;
static void draw_foot(void)
{
    char left[24], right[16], fn[16], ml[32], b[24], pn[10];   /* pn: a preset (8), "*" */
    const track_t *t = TSEL;
    const page_t *pg = cur_page();
    const char *mn = N_MODEL[(uint32_t)t->p[P_MODEL] % NMODELS];
    uint32_t sig, i, on = 0, acc = 0, n = 0, len = view_len(t), ph = 99u, lmask;
    int32_t x0, x1;
    int note = foot_note(fn);                        /* the held / cursor step's note */
    right[0] = 0;
    b[0] = 'T';
    b[1] = (char)('1' + song.sel);
    b[2] = '>';
    b[3] = 0;
    str_cpy(b + 3, mn, sizeof b - 3);
    str_cpy(b + str_len(b), ">", 2);
    if (ui.layer) {
        str_cpy(b + str_len(b), "PERFORM", sizeof b - str_len(b));
        str_cpy(right, "FX HOLD", sizeof right);
    } else if (ui.home) {
        str_cpy(b + str_len(b), "SOUND", sizeof b - str_len(b));
        str_cpy(right, "HOME 1/3", sizeof right);
    } else {
        uint32_t k, np = fam_pages(pg->fam, &k);
        if (pg->fam == FAM_HOME)                     /* COMP: HOME 2/3, 3/3 */
            k++, np++;
        str_cpy(b + str_len(b), pg->title, sizeof b - str_len(b));
        if (np > 1) {
            fmt_int(right, (int32_t)k);
            str_cpy(right + str_len(right), "/", 4);
            fmt_int(right + str_len(right), (int32_t)np);
        }
    }
    pn[0] = 0;
    if (!ui.home && !ui.layer && pg->graph == GR_MIX && !note) {   /* TRACKS: the selected track's preset ("K909*") */
        preset_name_of(preset_of(t), pn);
        if (t->pre_ed)
            str_cpy(pn + str_len(pn), "*", 2);
        if (!right[0])
            str_cpy(right, pn, sizeof right);
    }
    if (note)
        right[0] = 0;
    md_fit(left, b, sizeof left, note ? 112 : 226 - (right[0] ? md_text_w(&MD_F3, 2, right) + 8 : 0));
    if (ui.hot_t && ui.hot_col < 4u && ui.focus_l[0]) {   /* a knob touched: its whole value ("CUT 16.0 KHZ") */
        str_cpy(b, ui.focus_l, sizeof b);
        str_cpy(b + str_len(b), " ", 2);
        str_cpy(b + str_len(b), ui.focus_v, sizeof b - str_len(b));
        if (ui.focus_u[0]) {
            str_cpy(b + str_len(b), " ", 2);
            str_cpy(b + str_len(b), ui.focus_u, sizeof b - str_len(b));
        }
        md_fit(left, b, sizeof left, 226);
        right[0] = 0;
        note = 0;
    }
    ml[0] = 0;
    if (ui.ml.on)                                    /* MIDI LEARN (ui_learn.c): its line across the breadcrumb */
        ml_line(ml);
    for (i = 0; i < 16u; i++) {
        uint32_t si = ui.bank * 16u + i;
        step_t sv;
        if (si >= len)
            break;
        sv = view_step(t, si);
        on |= (uint32_t)sv.on << i;
        acc |= (uint32_t)(sv.on && sv.acc) << i;
        n++;
    }
    if (song.playing && view_idx(t) / 16u == ui.bank)
        ph = view_idx(t) % 16u;
    {                                                 /* STEPS: the bank's locked steps marked (a song's row: its
                                                       * slot's bank) */
        const lk_bank_t *lb = chain.running && chain.src[chain.slot].m ? chain.src[chain.slot].m : &mo.b;
        lmask = env_steps() && !view_src(t) ? lk_step_mask(lb, song.sel, ui.bank * 16u) : 0u;
    }
    sig = on | acc << 16;                             /* the trig strip (all it shows: on, acc, n; the playhead):
                                                       * whole when the steps change, else the playhead's old box and
                                                       * its new */
    rg_region(0, Y_TRIG, 240, H_TRIG);
    if (ui.force || sig != ft.trig_sig || n != ft.trig_n || lmask != ft.lock_sig)
        rg_whole();
    else if (ph != ft.ph) {
        if (ft.ph < 16u)
            rg_add(4 + (int32_t)ft.ph * 14 + (int32_t)ft.ph / 4 * 2, 3, 14, 14);
        if (ph < 16u)
            rg_add(4 + (int32_t)ph * 14 + (int32_t)ph / 4 * 2, 3, 14, 14);
    }
    ft.trig_sig = sig;
    ft.lock_sig = lmask;
    ft.trig_n = (uint8_t)n;
    ft.ph = (uint8_t)ph;
    for (i = 0; i < rg.n; i++) {
        rg_win(i, Y_TRIG, C_BG);
        cv_rect(0, 0, 1, H_TRIG, C_INK);
        cv_rect(239, 0, 1, H_TRIG, C_INK);
        cv_hdot(1, 238, 0, C_INK);
        cv_trigs(6, 5, n, on, acc, ph, C_INK);
        cv_lock_marks(6, 5, n, lmask, C_INK);
        cv_blit(0, Y_TRIG);
    }
    sig = str_hash(str_hash(str_hash(str_hash(0x9E3779B9u, left), right), fn), ml) + (uint32_t)note * 31u;
    if (!ui.force && sig == ui.foot_sig)
        return;
    ui.foot_sig = sig;
    x0 = 240, x1 = 0;                                 /* the breadcrumb: the text's span (all else is ink) */
    if (ml[0]) {
        int32_t hx = 234 - md_text_w(&MD_F3, 2, "OCT+ DONE"), e = 6 + md_text_w(&MD_F3, 2, ml);
        x0 = 6, x1 = e;
        if (e + 2 + 8 <= hx)                          /* (as md_text's pen + 8 below) */
            x1 = 234;
    } else {
        if (left[0])
            x0 = 6, x1 = 6 + md_text_w(&MD_F3, 2, left);
        if (note && fn[0])
            x0 = x0 < 120 ? x0 : 120, x1 = x1 > 120 + md_text_w(&MD_F3, 2, fn) ? x1 : 120 + md_text_w(&MD_F3, 2, fn);
        else if (!note && right[0])
            x0 = x0 < 234 - md_text_w(&MD_F3, 2, right) ? x0 : 234 - md_text_w(&MD_F3, 2, right), x1 = 234;
    }
    rg_region(0, Y_CRUMB, 240, H_CRUMB);
    if (ui.force)
        rg_whole();
    else                                              /* the glyph rows of the old text's span and the new's */
        rg_add(x0 < ft.x0 ? x0 : ft.x0, 8, (x1 > ft.x1 ? x1 : ft.x1) - (x0 < ft.x0 ? x0 : ft.x0), 10);
    ft.x0 = (int16_t)x0;
    ft.x1 = (int16_t)x1;
    for (i = 0; i < rg.n; i++) {
        rg_win(i, 0, C_INK);
        if (ml[0]) {                                 /* "T3 DECAY: SEND A CC", OCT+ DONE at the right where it fits */
            int32_t hx = 234 - md_text_w(&MD_F3, 2, "OCT+ DONE");
            if (md_text(6, 8, &MD_F3, 2, ml, C_BG) + 8 <= hx)
                md_text(hx, 8, &MD_F3, 2, "OCT+ DONE", C_BG);
        } else {
            md_text(6, 8, &MD_F3, 2, left, C_BG);
            if (note)
                md_text(120, 8, &MD_F3, 2, fn, C_BG);     /* "C#3 ACC SLD" */
            else if (right[0])
                md_text(234 - md_text_w(&MD_F3, 2, right), 8, &MD_F3, 2, right, C_BG);
        }
        cv_blit(0, Y_CRUMB);
    }
}

/* a message (ui_say): a small MD popup over the graph area while ui.msg_t runs (the graph holds still under it);
 * drawn when it changes, or a popup's end uncovered its place (rg.dmg), or TEMPO was drawn over it this frame (again:
 * the message over TEMPO, as a full repaint). Gone (or narrower), what it covered draws again (ui_draw: the damage) */
#define MSG_Y 106
#define MSG_H 31                                        /* (30 + the shadow's row) */
static struct {
    int16_t x, w;                /* the message's popup on the screen */
} msgp;
static int32_t msg_w(void)
{
    int32_t w = md_text_w(&MD_F3, 2, ui.msg) + 24;
    return w > 236 ? 236 : w;
}
static void draw_msg(int again)
{
    uint32_t sig = ui.msg_t ? str_hash(7u, ui.msg) : 0u;
    int32_t w = msg_w(), h = 30;
    if (!sig || (!again && !ui.force && sig == ui.msg_sig && !rg_meets(120 - w / 2, MSG_Y, w, MSG_H)))
        return;
    ui.msg_sig = sig;
    cv_begin((uint32_t)w, (uint32_t)h + 1u, C_BG);
    cv_popup(0, 0, w, h, "", C_INK, C_BG);
    md_text(12, 10, &MD_F3, 2, ui.msg, C_INK);
    cv_blit((uint32_t)(120 - w / 2), MSG_Y);
    msgp.x = (int16_t)(120 - w / 2);
    msgp.w = (int16_t)w;
}

/* a popup's title tab where cv_popup puts it (6 px over the box at x, y, w wide), as its own canvas: the box's canvas
 * starts at the box (cv_popup drew the tab's lower part in it), so nothing beside the tab is wiped */
static void popup_tab(int32_t x, int32_t y, int32_t w, const char *title)
{
    int32_t tw = md_text_w(&MD_F3, 2, title) + 12;
    cv_begin((uint32_t)tw, 6u, C_INK);
    md_text(6, 3, &MD_F3, 2, title, C_BG);
    cv_blit((uint32_t)(x + w / 2 - tw / 2), (uint32_t)(y - 6));
}

/* TEMPO (spec 2026-10-19-md-ui §7): while the BPM is being changed (ui.bpm_t), a popup with the BPM in the MD's tempo
 * digits at 3x (as messages: drawn when it changes, or uncovered; gone, what it covered draws again). x 30..209, y
 * 70..166 (the tab over the box) */
#define TEMPO_RC 30, 70, 180, 97
static int draw_tempo(void)                          /* 1: drawn */
{
    char b[8];
    int32_t w;
    if (!ui.bpm_t || (!ui.force && ui.tempo_sig == (uint32_t)song.g[G_BPM] + 1u && !rg_meets(TEMPO_RC)))
        return 0;
    ui.tempo_sig = (uint32_t)song.g[G_BPM] + 1u;
    cv_begin(180, 91, C_BG);
    cv_popup(0, 0, 180, 90, "TEMPO", C_INK, C_BG);
    fmt_int(b, song.g[G_BPM]);
    w = md_text_w(&MD_BIG, 3, b);
    md_text(90 - w / 2, 28, &MD_BIG, 3, b, C_INK);
    cv_blit(30, 76);
    popup_tab(30, 76, 180, "TEMPO");
    return 1;
}

/* a confirm (spec §7): a popup over the page, the question's first word its title, OCT- NO / OCT+ YES as keycaps */
static void draw_confirm(const char *q, const char *q2)
{
    char verb[12];
    uint32_t n = 0;
    while (q[n] && q[n] != ' ' && q[n] != ':' && n < sizeof verb - 1u) {
        verb[n] = q[n];
        n++;
    }
    verb[n] = 0;
    cv_begin(184, 75, C_BG);
    cv_popup(0, 0, 184, 74, verb, C_INK, C_BG);
    md_text(92 - md_text_w(&MD_F3, 2, q) / 2, 10, &MD_F3, 2, q, C_INK);
    if (q2)
        md_text(92 - md_text_w(&MD_F3, 2, q2) / 2, 24, &MD_F3, 2, q2, C_INK);
    cv_keycap(11, 40, "OCT-", "NO", C_INK);
    cv_keycap(99, 40, "OCT+", "YES", C_INK);
    cv_blit(28, 82);
    popup_tab(28, 82, 184, verb);
}

/* a parameter's label in at most 4 letters (the HOME strip's 38 px cells; the MD's own abbreviations) */
static const char *abbr4(const char *l, char *b)
{
    static const char *const AB[][2] = {{"DECAY", "DEC"}, {"SWEEP", "SWP"}, {"CLICK", "CLIK"}, {"PITCH", "PTCH"},
                                        {"DEPTH", "DPTH"}, {"NOISE", "NOIS"}, {"LEVEL", "LEV"}, {"SHIMR", "SHMR"},
                                        {"CHAOS", "CHAS"}, {"COLOR", "COLR"}, {"SPEED", "SPD"}, {"RANGE", "RNG"},
                                        {"PUNCH", "PNCH"}, {"SNAPY", "SNAP"}, {"SPRED", "SPRD"}, {"INDEX", "INDX"},
                                        {"RATIO", "RATI"}, {"ACCNT", "ACC"}, {"CUTOF", "CUT"}, {"ENVMD", "ENV"}};
    uint32_t i;
    for (i = 0; i < sizeof AB / sizeof AB[0]; i++)
        if (str_eq(l, AB[i][0]))
            return AB[i][1];
    str_cpy(b, l, 5);
    return b;
}

/* HOME's LEV column (x 63..86, y 17..67; the user, 2026-10-19: a level meter): the selected track's live level as the
 * solid bar (its peak since the last frame, falling 1 px a frame), the level setting a 10 px marker wider than it;
 * redrawn when either changes (again: the strip was just redrawn under it). PRESETS' popup up (from y 61): only the
 * rows above it */
static void draw_home_lev(int again)
{
    track_t *t = TSEL;
    int32_t lvl = t->p[P_LEVEL] & 127, m = !lvl || trk_silent(t) ? 0 : meter_px(t->peak) * 35 / MX_LW, hs, y;
    uint32_t sig;
    t->peak = 0;
    if (m < (int32_t)ui.lev_m - 1)
        m = (int32_t)ui.lev_m - 1;
    if (m < 0)
        m = 0;
    ui.lev_m = (uint32_t)m;
    sig = (uint32_t)m * 131u + (uint32_t)lvl + 1u;
    if (!again && !ui.force && sig == ui.lev_sig)
        return;
    ui.lev_sig = sig;
    cv_begin_at(24, !ui.force && pp_on() ? (uint32_t)(Y_GRAPH - Y_KNOB) : 51u, Y_KNOB, C_BG);
    md_text(12 - md_text_w(&MD_F3, 2, "LEV") / 2, 2, &MD_F3, 2, "LEV", C_INK);
    if (m)
        cv_rect(9, 49 - m, 6, m, C_INK);
    for (y = 14; y <= 48; y += 4) {
        cv_rect(5, y, 2, 1, C_INK);
        cv_rect(17, y, 2, 1, C_INK);
    }
    hs = lvl * 35 / 127;
    cv_rect(7, 47 - hs, 10, 2, C_INK);               /* the setting */
    cv_blit(63, Y_KNOB);
}

/* HOME (spec 2026-10-19-md-ui §9), y 17..68 under the top bar: the MD main screen's strip. The 4 bar boxes (fix 6:
 * the bar of a 4-bar cycle, home_bar), the project box (inverted: slot, name), the mode line | LEV (the track's level) |
 * the model's first four sound knobs (rings r 7, the values under them; enums as text) | a rule at y 68. Drawn whole
 * on ui.force; else in parts: the left block, a knob's cell (its label, value, touched), a knob's ring alone (its LFO's
 * live dot); LEV its own (draw_home_lev) */
#define HS_CX(c) (107 + (int32_t)(c) * 38)
static struct {
    uint32_t left, cell[4];      /* the parts' signatures shown */
    int16_t lfo[4];              /* the LFO dots shown (-1 none) */
    int16_t lx1;                 /* the left block's mode line's end shown */
} hs;
/* the bar HOME's boxes light (fix 6, owner 2026-10-21): the bar of a 4-bar cycle, 0..3, from the 1/16s since PLAY (the
 * shared clock seq.c's gclk counts: any track's LEN or bank aside), bar 1 at every PLAY; stopped: bar 1 */
static uint32_t home_bar(void)
{
    return song.playing && gclk.cnt != 0xFFFFFFFFu ? (gclk.cnt >> 4) & 3u : 0u;
}

static void draw_home_strip(void)
{
    char l[4][8], v[4][8], ab[8], m[16], nm[NAME_LEN + 1u], pj[8];
    int32_t r[4], md[4], lx1;
    uint32_t c, i, sig, cur = home_bar();
    uint32_t hot = ui.hot_t ? ui.hot_col : 9u, lev = 0, inv = 0;   /* inv: bit c, cell c's value inverted */
    for (c = 0; c < 4u; c++) {
        int16_t *vp, sv;
        const param_desc_t *d = home_param(c, &vp);
        char val[12];
        const char *unit = "";
        int32_t mv;
        int lk;
        l[c][0] = v[c][0] = 0;
        r[c] = md[c] = -1;
        if (!d->label || d->label[0] == '-') {
            if (c == ui.hot_col)                      /* touched, no parameter: no whole value in the breadcrumb */
                ui.focus_l[0] = 0;
            continue;
        }
        sv = lock_shown(lock_id(c), *vp, &lk);        /* (a step held in STEPS: the macro's lock there) */
        inv |= (uint32_t)(lk || c == hot) << c;
        param_format(d, sv, val, &unit);
        md_fit(l[c], abbr4(d->label, ab), sizeof l[c], 36);
        md_fit(v[c], val, 5, 36);
        if (d->fmt != F_ENUM)
            r[c] = RATIO(d, sv);
        if (lfo_live(TSEL, P_E0 + c, &mv))
            md[c] = RATIO(d, mv);
        if (c == ui.hot_col) {                        /* the breadcrumb's whole value while touched */
            str_cpy(ui.focus_l, d->label, 8);
            str_cpy(ui.focus_v, val, 8);
            str_cpy(ui.focus_u, unit, 8);
        }
    }
    m[0] = 0;
    if (!head_mode(m) && chain.running) {
        str_cpy(m, "SONG ", sizeof m);
        fmt_int(m + 5, (int32_t)chain.row + 1);
    }
    if (lock_view_step() < NSTEP && 4 + md_text_w(&MD_F3, 2, m) > 61) {   /* the left block's 58 px (LEV's column
                                                       * paints over more): "LOCK 5" (the top bar has LOCK STEP 5) */
        str_cpy(m, "LOCK ", sizeof m);
        fmt_int(m + 5, (int32_t)lock_view_step() + 1);
    }
    pj[0] = 'P', pj[1] = 'R', pj[2] = 'O', pj[3] = 'J', pj[4] = ' ';
    pj[5] = (char)('A' + (uint32_t)(song.g[G_SLOT] > 0 ? song.g[G_SLOT] - 1 : 0) % 4u);
    pj[6] = 0;
    md_fit(nm, chain.name[0] ? chain.name : "NEW", sizeof nm, 54);
    lx1 = m[0] ? 4 + md_text_w(&MD_F3, 2, m) : 0;
    rg_region(0, Y_KNOB, 240, 52);
    if (ui.force) {                                   /* whole, but LEV's column (its own canvas, next) */
        rg_add(0, 0, 63, 52);
        rg_add(63, 51, 24, 1);
        rg_add(87, 0, 153, 52);
        lev = 1;
    }
    sig = str_hash(str_hash(str_hash(0x811C9DC5u, m), nm), pj) + cur * 101u;
    if (!ui.force && sig != hs.left) {                /* the left block (the mode line may run on) */
        int32_t x1 = lx1 > hs.lx1 ? lx1 : hs.lx1;
        rg_add(0, 0, x1 > 62 ? x1 : 62, 51);
        lev = x1 > 63;                                /* (over LEV's column: LEV again) */
    }
    hs.left = sig;
    hs.lx1 = (int16_t)lx1;
    for (c = 0; c < 4u; c++) {                        /* a cell: its label, ring, value (touched: inverted) */
        int32_t cx = HS_CX(c), x0 = cx - 21 < 87 ? 87 : cx - 21, x1 = cx + 22 > 240 ? 240 : cx + 22;
        sig = str_hash(str_hash(0x811C9DC5u, l[c]), v[c]) + (uint32_t)(r[c] + 1) * 7u + ((inv >> c) & 1u) * 7919u;
        if (ui.force) {
        } else if (sig != hs.cell[c])
            rg_add(x0, 0, x1 - x0, 51);
        else if (md[c] != hs.lfo[c] && r[c] >= 0)     /* the LFO's dot alone: the ring's box */
            rg_add(cx - 11, 13, 23, 23);
        hs.cell[c] = sig;
        hs.lfo[c] = (int16_t)md[c];
    }
    if (!ui.force && pp_on())                         /* PRESETS' popup over the strip's last rows (y 61..68): the
                                                       * rows above it (a full repaint draws the popup over them) */
        rg_clip_h(Y_GRAPH - Y_KNOB);
    for (i = 0; i < rg.n; i++) {
        rg_win(i, Y_KNOB, C_BG);
        cv_rect(0, 0, 1, 52, C_INK);
        cv_rect(239, 0, 1, 52, C_INK);
        for (c = 0; c < 4u; c++)                      /* the bar boxes (home_bar) */
            if (c == cur)
                cv_rect(6 + (int32_t)c * 13, 3, 10, 6, C_INK);
            else
                cv_frame(6 + (int32_t)c * 13, 3, 10, 6, C_INK);
        cv_rect(2, 13, 59, 24, C_INK);                /* the project box */
        md_text(31 - md_text_w(&MD_F3, 2, pj) / 2, 15, &MD_F3, 2, pj, C_BG);
        md_text(31 - md_text_w(&MD_F3, 2, nm) / 2, 26, &MD_F3, 2, nm, C_BG);
        md_text(4, 40, &MD_F3, 2, m, C_INK);
        cv_rect(62, 0, 1, 51, C_INK);
        cv_rect(87, 0, 1, 51, C_INK);
        cv_vdot(125, 0, 50, C_INK);
        cv_vdot(163, 0, 50, C_INK);
        cv_vdot(201, 0, 50, C_INK);
        cv_hdot(88, 238, 12, C_INK);
        for (c = 0; c < 4u; c++) {
            int32_t cx = HS_CX(c), vw = md_text_w(&MD_F3, 2, v[c]), vy = r[c] >= 0 ? 37 : 24;
            if (!l[c][0])
                continue;
            md_text(cx - md_text_w(&MD_F3, 2, l[c]) / 2, 2, &MD_F3, 2, l[c], C_INK);
            if (r[c] >= 0)
                cv_knob(cx, 24, 7, r[c], md[c], C_INK);
            if ((inv >> c) & 1u) {                    /* touched, or a step's lock: the value inverted */
                cv_rect(cx - vw / 2 - 2, vy - 2, vw + 4, 13, C_INK);
                md_text(cx - vw / 2, vy, &MD_F3, 2, v[c], C_BG);
            } else {
                md_text(cx - vw / 2, vy, &MD_F3, 2, v[c], C_INK);
            }
        }
        cv_rect(1, 51, 238, 1, C_INK);
        cv_blit(0, Y_KNOB);
    }
    draw_home_lev((int)lev);
}

static void draw_columns(void)
{
    uint32_t c;
    char val[12];
    const char *unit;
    const page_t *pg = cur_page();
    if (ui.layer) {                                     /* PERFORM: the macros */
        perf_columns();
        return;
    }
    if (!ui.home && pg->graph == GR_MOTION) {          /* MOTION: PLAY EVENT - CLEAR */
        motion_columns();
        return;
    }
    if (!ui.home && pg->graph == GR_SONG) {            /* SONG: ROW SLOT REPS LOOP */
        song_columns();
        return;
    }
    if (ui.home) {                                    /* HOME: the MD strip */
        draw_home_strip();
        return;
    }
    if (pg->scope == SC_MIX) {                          /* TRACK LEVEL LEN PAN of the selected track */
        const track_t *t = TSEL;
        uint32_t lvl = (uint32_t)t->p[P_LEVEL];
        fmt_int(val, (int32_t)song.sel + 1);
        draw_column(0, "TRACK", val, "/8", HOT(0u), (int32_t)song.sel * 1000 / (NTRK - 1));
        int lk;
        lvl = (uint32_t)lock_shown(P_LEVEL, (int16_t)lvl, &lk);   /* (a step held in STEPS: LEVEL's lock) */
        if (!lk && (!lvl || t->p[P_MUTE])) {
            str_cpy(val, "MUTE", 12);
            unit = "";
        } else {
            param_format(&TP[P_LEVEL], (int32_t)lvl, val, &unit);
        }
        draw_column(1, "LEVEL", val, unit, lk ? CV_HOT : lvl && !trk_silent(t) ? HOT(1u) : CV_DIM, (int32_t)lvl * 1000 / 127);
        param_format(&TP[P_SLEN], t->p[P_SLEN], val, &unit);
        draw_column(2, "LEN", val, unit, HOT(2u), RATIO(&TP[P_SLEN], t->p[P_SLEN]));
        {                                               /* (a step held in STEPS: PAN's lock) */
            int lp;
            int16_t pv = lock_shown(P_PAN, t->p[P_PAN], &lp);
            param_format(&TP[P_PAN], pv, val, &unit);
            draw_column(3, "PAN", val, unit, lp ? CV_HOT : HOT(3u), RATIO(&TP[P_PAN], pv));
        }
        return;
    }
    if (pg->scope == SC_GRID && ui.srec) {             /* step record: STEP NOTE TIME FLAG of the cursor step */
        static const char *const TM[3] = {"NOTE", "TIE", "REST"}, *const FL[4] = {"-", "ACC", "SLD", "A+S"};
        const step_t *st = &TSEL->step[ui.sr_cur % NSTEP];
        char u[8];
        fmt_int(val, (int32_t)ui.sr_cur + 1);
        str_cpy(u, "/", 8);
        fmt_int(u + 1, (int32_t)view_len(TSEL));
        draw_column(0, "STEP", val, u, HOT(0u), -1);
        note_text(val, 60 + st->nt);
        draw_column(1, "NOTE", val, "", HOT(1u), (st->nt - NT_MIN) * 1000 / (NT_MAX - NT_MIN));
        draw_column(2, "TIME", TM[srec_time(st)], "", HOT(2u), -1);
        draw_column(3, "FLAG", FL[st->acc | st->slide << 1], "", HOT(3u), -1);
        return;
    }
    if (pg->scope == SC_GRID && held_step() < NSTEP) {  /* a step held: KNOB 1 PROB, 2 RATCH, 3 ACC (step_edit) */
        const step_t *st = &TSEL->step[held_step()];
        cond_format(st->cond, val);
        draw_column(0, "PROB", val, "", HOT(0u), -1);
        fmt_int(val, (int32_t)st->rat + 1);
        draw_column(1, "RATCH", val, "", HOT(1u), -1);
        draw_column(2, "ACC", st->acc ? "ON" : "OFF", "", HOT(2u), -1);
        draw_column(3, "", "", "", CV_NORM, -1);
        return;
    }
    if (pg->scope == SC_GRID) {                         /* BANK; the rest: the grid on the keys */
        char u[8];
        fmt_int(val, (int32_t)ui.bank + 1);
        str_cpy(u, "/", 8);
        fmt_int(u + 1, (int32_t)bank_count());
        draw_column(0, "BANK", val, u, HOT(0u), -1);
        for (c = 1; c < 4u; c++)
            draw_column(c, "", "", "", CV_NORM, -1);
        return;
    }
    for (c = 0; c < 4u; c++) {
        int16_t *vp;
        const param_desc_t *d = page_desc(pg, c, &vp);
        if (pg->graph == GR_LDEST && c == 0u) {        /* LFO DEST: KNOB 1 the slot */
            fmt_int(val, (int32_t)(song.lslot & 3u) + 1);
            draw_column(0, "SLOT", val, "/4", HOT(0u), -1);
            continue;
        }
        if (!d || !d->label || d->label[0] == '-') {
            draw_column(c, "", "", "", CV_NORM, -1);
            continue;
        }
        if (pg->id[c] == G_MIDI && pg->scope == SC_GLOBAL) {
            str_cpy(val, !usb.up ? "OFF" : usb.config ? "MIDI" : usb.setups ? "ENUM" : usb.sof_seen ? "BUS" : "WAIT", 12);
            draw_column(c, "USB", val, "USB", CV_NORM, -1);
            continue;
        }
        if (pg->id[c] == G_NAME && pg->scope == SC_GLOBAL) {
            draw_column(c, "NAME", "EDIT", "", CV_NORM, -1);
            continue;
        }
        int lk = 0;
        int16_t sv = pg->scope == SC_TRACK ? lock_shown(lock_id(c), *vp, &lk) : *vp;   /* (a step held in STEPS: the
                                                                                          * knob's lock there) */
        if (pg->id[c] == G_INFO && pg->scope == SC_GLOBAL) {
            fmt_int(val, (int32_t)(song.cpu_q8 * 100u / 256u));
            unit = "%";
        } else {
            param_format(d, sv, val, &unit);
        }
        {
            int32_t mv, mod = -1;
            if (pg->scope == SC_TRACK && lfo_live(TSEL, page_id(pg, c), &mv))
                mod = RATIO(d, mv);
            draw_column_m(c, d->label, val, unit, lk ? CV_HOT : HOT(c),
                          d->fmt == F_ENUM || d->fmt == F_LDEST ? -1 : RATIO(d, sv), mod);
        }
    }
}

/* inlined into fm1_main (main.c), as before M2: fm1_main then stays a function of its own, the shape H2 compares
 * with upstream (the boot sequence up to the fm1_main call) */
__attribute__((always_inline)) static inline void ui_draw(void)
{
    if (safe_start) {                                 /* safe start: one static screen, drawn again after */
        static uint8_t drawn;                         /* an update session cleared it (main.c sets ui.force) */
        if (!drawn || ui.force) {
            lcd_fill(0, 0, 240, 240, C_BG);
            draw_text_box(0, 92, 240, &FONT_L, "SAFE START", C_INK, 1);
            draw_text_box(0, 128, 240, &FONT_S, "NO AUDIO - USB UPDATE READY", C_INK, 1);
            draw_text_box(0, 148, 240, &FONT_S, "POWER OFF TO LEAVE", C_INK, 1);
            drawn = 1;
            ui.force = 0;
        }
        return;
    }
    ui.frame++;
    if (ui.menu) {
        draw_menu();
        ui.force = 0;
        return;
    }
    if (ui.confirm) {
        if (ui.force) {
            char b[20] = "CLEAR TRACK 1?";
            if (ui.confirm == 2u) {
                str_cpy(b, "CLEAR MOTION T1?", sizeof b);
                b[14] = (char)('1' + ui.confirm_trk);
            } else if (ui.confirm == 3u) {
                str_cpy(b, "DELETE ROW ", sizeof b);
                fmt_int(b + 11, (int32_t)ui.confirm_trk + 1);
                str_cpy(b + str_len(b), "?", 4);
            } else if (ui.confirm == 4u) {
                str_cpy(b, "CLEAR SONG?", sizeof b);
            } else if (ui.confirm == 6u) {              /* MIDI LEARN (ui_learn.c ML_CONFIRM) */
                str_cpy(b, "CLEAR ALL LEARNED?", sizeof b);
            } else if (ui.confirm == 5u) {              /* the power-on load failed (project.c project_boot) */
                str_cpy(b, "LOAD FAILED: SLOT A", sizeof b);
                b[18] = (char)('A' + ui.confirm_trk % 4u);
            } else {
                b[12] = (char)('1' + ui.confirm_trk);
            }
            bank_fix();                               /* the page under it, then the popup */
            draw_frame();
            draw_head();
            draw_columns();
            draw_graph();
            draw_foot();
            draw_confirm(b, ui.confirm == 5u ? "START NEW PROJECT?" : 0);
            ui.force = 0;
        }
        return;
    }
    if (name_on()) {                                  /* NAME: its own screen */
        draw_name();
        return;
    }
    if (pl_on()) {                                    /* the PRESETS list (ui_preset.c): its own screen */
        draw_plist();
        return;
    }
    if (ui.mute) {                                    /* GLO held: the MUTE screen */
        draw_mute();
        return;
    }
    bank_fix();
    rg.dmg.w = 0;                                     /* a popup gone: its place is damage (what lies under it draws
                                                       * there again); another message: the graph as it is now round it
                                                       * (thaw), the old one's place damage (it may be narrower) */
    rg.nex = 0;
    rg.thaw = ui.msg_t && ui.msg_sig && str_hash(7u, ui.msg) != ui.msg_sig;   /* (another message) */
    if (!ui.msg_t && ui.msg_sig) {
        rg_damage(msgp.x, MSG_Y, msgp.w, MSG_H);
        ui.msg_sig = 0;
    }
    if (!ui.bpm_t && ui.tempo_sig) {
        rg_damage(TEMPO_RC);
        ui.tempo_sig = 0;
    }
    if (rg.thaw)
        rg_damage(msgp.x, MSG_Y, msgp.w, MSG_H);
    rg.thaw = rg.dmg.w != 0;                          /* (a popup came and went, another one still up: the graph as it
                                                       * is now round it, as the full repaint once did) */
    if (rg.dmg.w && pp_on())                          /* PRESETS' popup up (over the HOME strip's last rows): all, as
                                                       * before */
        ui.force = 1;
    if (ui.force)
        rg.dmg.w = 0, rg.thaw = 0;
    if (ui.msg_t)                                     /* the popups up: no window covers them */
        rg_keep(120 - msg_w() / 2, MSG_Y, msg_w(), MSG_H);
    if (ui.bpm_t) {                                   /* (TEMPO: its box, its tab) */
        rg_keep(30, 76, 180, 91);
        rg_keep(120 - (md_text_w(&MD_F3, 2, "TEMPO") + 12) / 2, 70, md_text_w(&MD_F3, 2, "TEMPO") + 12, 6);
    }
    if (ui.force)
        draw_frame();
    felucca_dbg.stage = 3;
    draw_head();
    felucca_dbg.stage = 4;
    draw_columns();
    felucca_dbg.stage = 5;
    if (pp_on())
        draw_popup();                                 /* PRESETS turned: the popup in the graph's place */
    else
        draw_graph();
    draw_msg(draw_tempo());                           /* (the message over TEMPO) */
    if (ui.msg_t)
        ui.msg_t--;
    if (ui.bpm_t)
        ui.bpm_t--;
    if (ui.arm_t && !--ui.arm_t)
        ui.arm = 0;
    if (ui.hot_t)
        ui.hot_t--;
    felucca_dbg.stage = 6;
    draw_foot();
    ui.force = 0;
    rg.dmg.w = 0;
    rg.nex = 0;
    rg.thaw = 0;
}
