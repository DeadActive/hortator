/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Drum UI drawing: status bar (top), columns + gauges, graphs, focus readout, footer
 * (the bank's steps + model / track / page). */
static void draw_menu(void);

static uint32_t str_hash(uint32_t h, const char *s)
{
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

/* at most 5 characters, and no wider than maxw */
static void fit(char *d, const char *src, const felucca_font_t *f, int32_t maxw)
{
    str_cpy(d, src, 6);
    while (d[0] && text_w(f, d) > maxw)
        d[str_len(d) - 1u] = 0;
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
    b[0] = 0;
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

#define C_ALERT RGB(255, 40, 32)                     /* the CPU icon (a fixed red, not the palette's) */
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

/* top bar: transport, (FX) BPM | track, (the CPU icon), USB, battery; messages replace it */
static void draw_head(void)
{
    char b[16], m[16];
    uint32_t i;
    int32_t x;
    uint32_t rec = (song.rec >> song.sel) & 1u ? 2u : song.rec != 0u, cpu = cpu_alert();
    uint32_t pfx = (perf_k[0] | perf_k[1] | perf_k[2]) != 0;   /* PERFORM's knob effects on (spec §7; DEPTH acts only
                                                                 * with a held black key) */
    uint32_t sig = (uint32_t)song.playing * 3u + rec * 5u + song.sel * 13131u +
                   (ui.msg_t ? str_hash(7u, ui.msg) : 0u) + (uint32_t)song.g[G_BPM] * 101u + (ui.bpm_t != 0) * 31u +
                   (uint32_t)batt_shown() * 7777u + (usb.config && !usb.suspended) * 99991u +
                   (chain.running ? (chain.row + 1u) * 104729u : 0u) + ui.keys * 7919u +
                   (uint32_t)(song.octave + 2) * 1009u + ui.srec * 3571u + ui.sr_cur * 211u + pfx * 6151u;
    sig += cpu * 50021u;
    if (!ui.force && sig == ui.head_sig)
        return;
    ui.head_sig = sig;
    cv_begin(240, H_HEAD, C_BLACK);
    if (ui.msg_t) {
        cv_text(4, 1, &FONT_S, ui.msg, C_HI);
        cv_blit(0, Y_HEAD);
        return;
    }
    if (song.playing) {
        for (i = 0; i < 5u; i++)
            cv_rect(4 + (int32_t)i * 2, 4 + (int32_t)i, 2, 10 - 2 * (int32_t)i, C_WHITE);
    } else {
        cv_rect(4, 5, 8, 8, C_HI);
    }
    if (rec)
        cv_rect(18, 6, 6, 6, rec == 2u ? C_WHITE : C_GRAY);
    fmt_int(b, song.g[G_BPM]);
    x = 32;
    if (pfx) {                                        /* FX (a knob effect on) in the tempo icon's place */
        cv_rect(27, 2, 18, 15, C_AMB);                /* (x 27..44: clear of the REC dot and the BPM) */
        cv_text(28, 1, &FONT_S, "FX", C_BLACK);
        x = 48;
    } else if (FELUCCA_ICONS) {
        cv_icon(x, 2, ICON_TEMPO, C_GRAY);
        x += 14;
    }
    cv_text(x, 1, &FONT_S, b, ui.bpm_t ? C_WHITE : C_HI);
    if (head_mode(m)) {
        cv_text(76, 1, &FONT_S, m, ui.srec ? C_WHITE : C_HI);   /* KEYS / STEP REC: before the song's row */
    } else if (chain.running) {                       /* a song: its row */
        str_cpy(b, "SONG ", sizeof b);
        fmt_int(b + 5, (int32_t)chain.row + 1);
        cv_text(84, 1, &FONT_S, b, C_HI);
    }
    b[0] = 'T';
    b[1] = (char)('1' + song.sel);
    b[2] = 0;
    cv_text(ui.srec ? 168 : 140, 1, &FONT_S, b, C_HI);   /* (STEP REC nn reaches x 164) */
    if (cpu) {                                        /* after T<n>; in step record (T at 168) in USB's place */
        x = ui.srec ? 191 : 162;
        if (FELUCCA_ICONS)
            cv_icon(x + 3, 2, ICON_CHIP, C_ALERT);
        else
            cv_text(x, 1, &FONT_S, "CPU", C_ALERT);
    }
    {
        int32_t lvl = batt_shown(), k, bx = 236 - 19;
        cv_rect(bx, 4, 17, 1, C_GRAY);
        cv_rect(bx, 12, 17, 1, C_GRAY);
        cv_rect(bx, 4, 1, 9, C_GRAY);
        cv_rect(bx + 16, 4, 1, 9, C_GRAY);
        cv_rect(bx + 17, 6, 2, 5, C_GRAY);
        for (k = 0; k < lvl; k++)
            cv_rect(bx + 2 + k * 5, 6, 3, 5, lvl == 1 && batt_level() <= 1 ? C_WHITE : C_HI);
        if (usb.config && !usb.suspended && !(cpu && ui.srec))
            cv_text(bx - 28, 1, &FONT_S, "USB", C_DIM);
    }
    cv_blit(0, Y_HEAD);
}

static void draw_frame(void)
{
    uint32_t i;
    lcd_fill(0, H_HEAD, 240, Y_LABEL - H_HEAD, C_BLACK);
    lcd_fill(0, Y_SEP_END, 240, Y_GRAPH - Y_SEP_END, C_BLACK);
    lcd_fill(0, Y_GRAPH + H_GRAPH, 240, Y_FOOT - Y_GRAPH - H_GRAPH, C_BLACK);
    for (i = 0; i < 4u; i++)
        lcd_fill(i * 60u, Y_LABEL, 4, Y_SEP_END - Y_LABEL, C_BLACK);
    lcd_fill(0, H_HEAD, 240, 1, C_LINE);
    lcd_fill(0, Y_FOOT - 2, 240, 1, C_LINE);
    for (i = 1; i < 4u; i++)
        lcd_fill(i * 60u - 1u, H_HEAD + 4, 1, Y_SEP_END - H_HEAD - 4, C_LINE);
}

#define LABEL_X (FELUCCA_ICONS ? ICON_CELL + ICON_GAP : 0)
static void draw_column_m(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc,
                          int32_t ratio, uint32_t icon, int32_t mod)
{
    char l[8], v[8], u[8], key[32];
    int32_t x, gw = 52, fx;
    if (icon == ICON_AUTO)
        icon = icon_for_label(label);
    fit(l, label, &FONT_S, 54 - LABEL_X);
    fit(v, val, &FONT_S, 48);
    fit(u, unit, &FONT_S, 54 - text_w(&FONT_S, v) - 3);
    str_cpy(key, l, 8);
    str_cpy(key + str_len(key), "|", 2);
    str_cpy(key + str_len(key), v, 8);
    str_cpy(key + str_len(key), "|", 2);
    str_cpy(key + str_len(key), u, 8);
    {
        uint32_t n = str_len(key);
        key[n] = (char)('A' + (vc == C_WHITE) + (vc == C_DIM) * 2);
        key[n + 1] = (char)(' ' + (ratio < 0 ? 0 : 1 + ratio / 20));
        key[n + 2] = (char)(icon == ICON_NONE ? '~' : '!' + icon % 90u);
        key[n + 3] = (char)(' ' + (mod < 0 ? 0 : 1 + mod / 20));
        key[n + 4] = 0;
    }
    if (c == ui.hot_col) {
        str_cpy(ui.focus_l, l, 8);
        str_cpy(ui.focus_v, v, 8);
        str_cpy(ui.focus_u, u, 8);
    }
    if (!ui.force && str_eq(key, ui.col[c]))
        return;
    str_cpy(ui.col[c], key, sizeof ui.col[c]);
    cv_begin(55, Y_SEP_END - Y_LABEL, C_BLACK);
    if (FELUCCA_ICONS && icon != ICON_NONE && l[0])
        cv_icon(0, 1, icon, C_GRAY);
    cv_text(l[0] ? LABEL_X : 0, 0, &FONT_S, l, C_GRAY);
    x = cv_text(0, Y_VALUE - Y_LABEL, &FONT_S, v, vc);
    cv_text(x + 3, Y_VALUE - Y_LABEL, &FONT_S, u, C_DIM);
    if (ratio >= 0) {
        int32_t gy = Y_GAUGE - Y_LABEL;
        fx = ratio * gw / 1000;
        cv_rect(0, gy + 1, gw, 1, C_LINE);
        cv_rect(0, gy, fx, 3, C_DIM);
        cv_rect(fx, gy - 1, 1, 5, vc == C_WHITE ? C_WHITE : C_HI);
        if (mod >= 0)                                 /* an LFO on this knob: its live value */
            cv_rect(mod * gw / 1000, gy + 4, 2, 2, C_AMB);
    }
    cv_blit(c * 60u + 4u, Y_LABEL);
}

static void draw_column(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc,
                        int32_t ratio, uint32_t icon)
{
    draw_column_m(c, label, val, unit, vc, ratio, icon, -1);
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
static void graph_acid(const track_t *t, uint16_t c)
{
    const int16_t *p = &t->p[P_E0];
    int32_t cs = clamp(p[2], 0, 127) * 103 / 127, cx = acid_cut_x(cs), pk = -6 + clamp(p[3], 0, 127) * 18 / 127;
    int32_t x, y, py = 0, ex, gw, e = ENV1, kx = 65536;
    uint32_t k = acid_decay(p[1]), i;
    cv_rect(100, ACID_GY, 136, 1, C_LINE);                   /* 0 dB */
    for (x = 0; x < 136; x++) {
        int32_t dx = x - cx, db = dx <= -15 ? 0 : dx <= 0 ? pk * (15 + dx) / 15 : pk - dx * 24 / 15;
        y = ACID_GY - clamp(db, -32, 12);
        if (x)
            cv_line(99 + x, py, 100 + x, y, c);
        py = y;
    }
    ex = acid_cut_x(cs + clamp(p[4], 0, 127) * 48 / 127);   /* ENVMD: the cutoff at the note's start */
    if (p[4] > 0)
        for (y = 2; y < 46; y += 4)
            cv_rect(100 + ex, y, 1, 2, C_AMB);
    gw = clamp((int32_t)(acid_gate_len(t, div_period((uint32_t)t->p[P_SDIV], 0)) / 324u), 1, 135);   /* 324 samples a px */
    cv_rect(100, ACID_EY + 36, 136, 1, C_LINE);
    cv_rect(100, ACID_EY, gw, 1, C_DIM);                     /* the amp: open at once, GATE long, then closed */
    cv_rect(100, ACID_EY, 1, 36, C_DIM);
    cv_rect(100 + gw, ACID_EY, 1, 36, C_DIM);
    for (i = 0; i < 324u; i++)                               /* DECAY's factor per px */
        kx = mulq16(kx, k);
    for (x = 0; x < 136; x++) {
        y = ACID_EY + 36 - (int32_t)(((uint32_t)e >> 8) * 36u >> 16);
        if (x)
            cv_line(99 + x, py, 100 + x, y, c);
        py = y;
        e = mulq16(e, (uint32_t)kx);
    }
}

/* the selected track's model: name large, voices and choke group */
static void graph_model(const track_t *t, uint16_t c)
{
    const dmodel_t *m = &DMODELS[(uint32_t)t->p[P_MODEL] % NMODELS];
    char b[16];
    cv_text(6, 10, &FONT_L, m->name, c);
    str_cpy(b, m->voices > 1 ? "2 VOICES" : "1 VOICE", sizeof b);
    cv_text(6, 52, &FONT_S, b, C_GRAY);
    {
        int fm = (uint32_t)t->p[P_MODEL] % NMODELS == DM_FM ||
                 (uint32_t)t->p[P_MODEL] % NMODELS == DM_ACID;   /* FM, ACID: their graph right: the labels go left */
        if (t->p[P_CHOKE]) {
            str_cpy(b, "CHOKE 1", sizeof b);
            b[6] = (char)('0' + t->p[P_CHOKE]);
            cv_text(fm ? 6 : 100, fm ? 70 : 52, &FONT_S, b, C_AMB);
        }
        if (t->p[P_LLEVEL])
            cv_text(6, fm ? 86 : 70, &FONT_S, fm ? "+ LAYER" : "+ SAMPLE LAYER", C_AMB);
    }
    if ((uint32_t)t->p[P_MODEL] % NMODELS == DM_FM) {   /* FM: the ratio, the carrier and its sidebands f +- k r f */
        uint32_t r8 = FM_RATIO_Q8[(uint32_t)clamp(t->p[P_E3], 0, 15)], k;
        int32_t h0 = clamp(t->p[P_E2], 0, 127) * 48 / 127;   /* INDEX 0: a pure sine, no sidebands */
        str_cpy(b, "RATIO ", sizeof b);
        str_cpy(b + 6, N_FMRATIO[(uint32_t)clamp(t->p[P_E3], 0, 15)], sizeof b - 6);
        cv_text(100, 10, &FONT_S, b, C_HI);
        cv_rect(100, 84, 136, 1, C_LINE);
        cv_rect(100 + 17, 84 - 50, 2, 50, c);            /* the carrier (x: 17 px per f, 0 .. 8 f) */
        for (k = 1; k <= 3u && h0 > 0; k++) {
            int32_t up = (int32_t)(256u + k * r8), dn = (int32_t)(256u) - (int32_t)(k * r8), hk = h0 / (int32_t)k;
            if (up * 17 / 256 < 136)
                cv_rect(100 + up * 17 / 256, 84 - hk, 2, hk, c);
            dn = dn < 0 ? -dn : dn;
            if (dn * 17 / 256 < 136)
                cv_rect(100 + dn * 17 / 256, 84 - hk, 2, hk, C_AMB);
        }
    }
    if ((uint32_t)t->p[P_MODEL] % NMODELS == DM_ACID)
        graph_acid(t, c);                            /* ACID: the filter and the note's shape */
}

/* a step's bar: hatched (every third row dark) when its PROB is not 100 %; RATCH > 1: that many ticks above */
static void step_bar(int32_t x, int32_t y, int32_t w, int32_t h, const step_t *st, uint16_t col, int32_t tick)
{
    int32_t r;
    if (st->cond) {
        for (r = 0; r < h; r += 3)
            cv_rect(x, y + r, w, r + 2 <= h ? 2 : 1, col);
    } else {
        cv_rect(x, y, w, h, col);
    }
    for (r = 0; st->rat && r <= st->rat; r++)
        cv_rect(x + r * tick, y - 4, tick > 2 ? tick - 1 : 1, 2, col);
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
            step_bar(x, st->acc ? y : y + 4, 2, st->acc ? 14 : 10, st, st->acc ? C_WHITE : c, 3);
        else
            cv_rect(x, y + 13, 2, 1, C_DIM);
        if (song.playing && i == view_idx(t))
            cv_rect(x - 1, y + 16, 4, 3, C_WHITE);
    }
}

static int srec_ok(void);                               /* ui_input.c: a REC tap starts step record */

/* the STEP page's held step (a grid key held, KEYS off) at graph row y: "STEP n  PROB  RATCH n"; 0: none held */
static int held_line(const track_t *t, int32_t y)
{
    const step_t *hs;
    char h[32], cs[8];
    if (!(ui.held < 16u && ui.step_t0[ui.held] && ui.step_si[ui.held] != 0xFFFFu))
        return 0;
    hs = &t->step[ui.step_si[ui.held]];
    str_cpy(h, "STEP ", sizeof h);
    fmt_int(h + str_len(h), (int32_t)ui.step_si[ui.held] + 1);
    str_cpy(h + str_len(h), "  ", sizeof h - str_len(h));
    cond_format(hs->cond, cs);
    str_cpy(h + str_len(h), cs, sizeof h - str_len(h));
    str_cpy(h + str_len(h), "  RATCH ", sizeof h - str_len(h));
    fmt_int(h + str_len(h), (int32_t)hs->rat + 1);
    cv_text(4, y, &FONT_S, h, C_WHITE);
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
        if (si >= len) {
            cv_rect(x, y + 20, 11, 1, C_LINE);
            continue;
        }
        if (st->on)
            step_bar(x, st->acc ? y : y + 10, 11, st->acc ? 40 : 30, st, st->acc ? C_WHITE : c, 3);
        else
            cv_rect(x, y + 36, 11, 4, C_DIM);
        if (song.playing && si == view_idx(t))
            cv_rect(x, y + 46, 11, 3, C_WHITE);
        if (st->on && st->nt)                         /* a transposed step (NOTE != C4): a small mark under it */
            cv_rect(x + 3, y + 42, 5, 2, C_AMB);
        if (ui.srec && si == ui.sr_cur) {             /* step record's cursor: a frame */
            cv_rect(x - 2, y - 3, 15, 1, C_WHITE);
            cv_rect(x - 2, y + 50, 15, 1, C_WHITE);
            cv_rect(x - 2, y - 3, 1, 54, C_WHITE);
            cv_rect(x + 12, y - 3, 1, 54, C_WHITE);
        }
    }
    fmt_int(b, (int32_t)ui.bank + 1);
    str_cpy(b + str_len(b), "/", 4);
    fmt_int(b + str_len(b), (int32_t)bank_count());
    cv_text(4, 4, &FONT_S, "BANK", C_GRAY);
    cv_text(48, 4, &FONT_S, b, C_HI);
    if (view_src(t)) {
        static const char *const GN[3] = {"GRIDS KICK", "GRIDS SNARE", "GRIDS HATS"};
        cv_text(4, 84, &FONT_S, GN[view_src(t) - 1u], C_AMB);
    } else if (ui.srec) {                             /* step record: the ways on and out */
        cv_text(4, 84, &FONT_S, "EDIT: REST  REC: END", C_DIM);
    } else if (keys_mode()) {                         /* KEYS: the keys are notes; REC: step record where a tap starts
                                                       * it (stopped; playing, a song: a tap arms the track) */
        cv_text(4, 84, &FONT_S, srec_ok() ? "KEYS  REC: STEP RECORD" : "KEYS", C_DIM);
    } else if (held_line(t, 84)) {
    } else {
        cv_text(4, 84, &FONT_S, "TAP: ON/OFF  HOLD: ACCENT", C_DIM);
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
#define PR_X0 34                                        /* the first step column */
#define PR_CW 12                                        /* px per step */
#define PR_KX 22                                        /* the keyboard strip, 9 px */
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
    if (!prob) {
        cv_rect(x, y, w, h, c);
        return;
    }
    for (i = 0; i < w; i += 2)
        cv_rect(x + i, y, 1, h, c);
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
    for (r = 0; r < PR_ROWS; r++) {                   /* rows: the keyboard strip, the C rows named and closed */
        int32_t n = top - r, y = PR_Y0 + r * PR_RH, lx;
        uint32_t pc = (uint32_t)n % 12u;
        if (n == held || (held > -1000 && ((r == 0 && held > top) || (r == PR_ROWS - 1 && held < proll.lo))))
            cv_rect(PR_KX, y, 9, PR_RH - 1, C_WHITE);
        else
            cv_rect(PR_KX, y, KEY_BLACK[pc] ? 5 : 9, PR_RH - 1, KEY_BLACK[pc] ? C_LINE : C_DIM);
        if (pc == 0u) {
            cv_rect(PR_X0, y + PR_RH - 1, gw, 1, C_DIM);
            note_text(nb, n);
            lx = PR_KX - 2 - text_w(&FONT_S, nb);
            cv_text(lx < 0 ? 0 : lx, y < 6 ? 0 : y - 6, &FONT_S, nb, C_GRAY);   /* (C-1: from x 0) */
        }
    }
    for (i = 0; i <= ncol; i++)                       /* step lines, the beats brighter */
        cv_rect(PR_X0 + (int32_t)i * PR_CW, PR_Y0, 1, ybot - PR_Y0, i % 4u ? C_LINE : C_DIM);
    for (i = 0; i < ncol; i++) {                      /* the notes */
        uint32_t si = base + i, s = pr_src(t, si, len);
        const step_t *own = &st[si], *ns = &st[(si + 1u) % len], *ss;
        int32_t x = PR_X0 + (int32_t)i * PR_CW, y;
        if (s == NSTEP)
            continue;                                 /* a rest, empty, or a TIE after one */
        ss = &st[s];
        y = pr_row_y(60 + ss->nt);
        if (s == si)
            pr_bar(x + 2, y, 9, ss->acc, ss->acc ? C_WHITE : c, ss->rat + 1u, ss->cond != 0u);
        else                                          /* a TIE: on through the step line (its note's PROB: hatched) */
            pr_bar(x - 1, y, 12, ss->acc, ss->acc ? C_WHITE : c, 1u, ss->cond != 0u);
        if (own->slide && (own->tie || !own->rat) && ns->on && !ns->tie && y >= PR_Y0 && y < ybot) {
            int32_t ny = clamp(pr_row_y(60 + ns->nt), PR_Y0, ybot - PR_RH);
            cv_line(x + 11, y + 1, x + 14 < gx1 ? x + 14 : gx1, ny + 1, C_AMB);   /* the slide into the next note
                                                         * (out of the last column: to the grid's edge) */
        }
    }
    for (i = 0; i < ncol; i++) {                      /* the playhead, step record's cursor */
        uint32_t si = base + i;
        int32_t x = PR_X0 + (int32_t)i * PR_CW;
        if (song.playing && si == view_idx(t))
            cv_rect(x + 6, PR_Y0, 1, ybot - PR_Y0, C_WHITE);
        if (ui.srec && si == ui.sr_cur) {
            cv_rect(x, PR_Y0 - 2, PR_CW + 1, 1, C_WHITE);
            cv_rect(x, ybot + 1, PR_CW + 1, 1, C_WHITE);
            cv_rect(x, PR_Y0 - 2, 1, ybot - PR_Y0 + 4, C_WHITE);
            cv_rect(x + PR_CW, PR_Y0 - 2, 1, ybot - PR_Y0 + 4, C_WHITE);
        }
    }
}

/* GRIDS pages. MAP: the 5 x 5 node map (faint) with the X / Y point, and the three channels' 32 steps without
 * chaos (accent tall, playhead underlined). EUCLID: three rings of LEN positions (accent large, playhead marked).
 * Page 2 adds the routing: which tracks play each channel. */
static void graph_grids(uint16_t c, int routing)
{
    static const char *const CH[3] = {"K", "S", "H"};
    uint32_t ch, i;
    if (!song.g[G_GMODE]) {
        int32_t px = 6 + song.g[G_GX] * 72 / 127, py = 6 + song.g[G_GY] * 72 / 127;
        for (i = 0; i < 25u; i++)
            cv_rect(5 + (int32_t)(i % 5u) * 18, 5 + (int32_t)(i / 5u) * 18, 2, 2, C_DIM);
        cv_rect(px - 3, py - 3, 7, 7, C_WHITE);
        for (ch = 0; ch < 3u; ch++) {
            int32_t y = 4 + (int32_t)ch * 24;
            cv_text(92, y + 2, &FONT_S, CH[ch], C_GRAY);
            for (i = 0; i < 32u; i++) {
                uint32_t b = grids_preview(ch, i);
                int32_t x = 106 + (int32_t)i * 4 + (int32_t)(i / 8u) * 2;
                int ph = song.playing && i == grids.last;
                if (b & 1u)
                    cv_rect(x, (b & 2u) ? y : y + 8, 3, (b & 2u) ? 18 : 10, ph ? C_WHITE : (b & 2u) ? C_WHITE : c);
                else
                    cv_rect(x, y + 17, 3, 1, C_DIM);
                if (ph)
                    cv_rect(x, y + 20, 3, 2, C_WHITE);
            }
        }
    } else {
        for (ch = 0; ch < 3u; ch++) {
            uint32_t len = grids_len(ch);
            int32_t cx = 40 + (int32_t)ch * 80, cy = 36;
            char b[8];
            for (i = 0; i < len; i++) {
                uint32_t ph = i * (0xFFFFFFFFu / len), bb = grids_preview(ch, i);
                int32_t x = cx + sine_i(ph) * 28 / 32768, y = cy - sine_i(ph + 0x40000000u) * 28 / 32768;
                if (bb & 2u)
                    cv_rect(x - 3, y - 3, 7, 7, C_WHITE);
                else if (bb & 1u)
                    cv_rect(x - 2, y - 2, 5, 5, c);
                else
                    cv_rect(x - 1, y - 1, 2, 2, C_DIM);
                if (song.playing && i == grids.elast[ch])
                    cv_rect(x - 4, y + 5, 9, 2, C_WHITE);
            }
            str_cpy(b, CH[ch], sizeof b);
            str_cpy(b + 1, " ", sizeof b - 1);
            fmt_int(b + 2, (int32_t)len);
            cv_text(cx - text_w(&FONT_S, b) / 2, 68, &FONT_S, b, C_GRAY);
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
        cv_text(4, 84, &FONT_S, r, C_AMB);
    }
}

static void graph_fx(const track_t *t, uint16_t c)
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        int32_t h = t->p[P_DIST + i] * 80 / 127, x = (int32_t)i * 60 + 28;
        cv_rect(x, 10, 1, 80, C_LINE);
        cv_rect(x, 90 - h, 1, h, c);
        cv_rect(x - 3, 90 - h, 7, 1, c);
    }
}

static void graph_slicer(const track_t *t, uint16_t c)
{
    uint32_t i, pat = sl_pattern(t), mode = (uint32_t)t->p[P_SLCR], cur = sl[t - trk].idx;
    int32_t open = 70 - t->p[P_SLDEPTH] * 70 / 127;      /* px a closed GATE step keeps */
    uint16_t col = mode == SL_OFF ? C_DIM : c;
    for (i = 0; i < 16u; i++) {
        int32_t x = 4 + (int32_t)i * 14 + (int32_t)(i / 4u) * 2, y;
        if ((pat >> i) & 1u) {
            cv_rect(x, 10, 11, 70, col);
        } else if (mode == SL_STUT) {
            for (y = 10; y < 80; y += 4)
                cv_rect(x, y, 11, 1, col);
        } else {
            cv_rect(x, 79, 11, 1, C_LINE);
            if (open)
                cv_rect(x, 80 - open, 11, open, C_DIM);
        }
        if (mode != SL_OFF && i == cur)
            cv_rect(x, 85, 11, 3, C_WHITE);
    }
}

/* oscilloscope of the output, triggered on a rising zero crossing */
static void graph_scope(uint16_t c)
{
    static int16_t snap[SCOPE_N];
    uint32_t w = scope_w, i, trig = 0;
    int32_t py = 50, x, peak = 1500;
    for (i = 0; i < SCOPE_N; i++) {
        snap[i] = scope_buf[(w + i) & (SCOPE_N - 1u)];
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
    cv_line(0, 50, 239, 50, C_LINE);
    for (x = 0; x < 240; x++) {
        int32_t y = 50 - snap[trig + (uint32_t)x] * 44 / peak;   /* auto-scaled */
        if (x)
            cv_line(x - 1, py, x, y, c);
        py = y;
    }
}


static void graph_slots(void)
{
    uint32_t i;
    char nb[NAME_LEN + 1u], f[NAME_LEN + 1u];
    for (i = 0; i < 4u; i++) {
        int32_t y = 4 + (int32_t)i * 24;
        char b[2] = {(char)('A' + i), 0};
        int sel = (int32_t)i + 1 == song.g[G_SLOT], used = project_name(i, nb);
        if (sel)
            cv_rect(4, y + 6, 3, 3, C_WHITE);
        cv_text(14, y, &FONT_S, b, sel ? C_WHITE : C_GRAY);
        fit(f, used ? (nb[0] ? nb : "USED") : "EMPTY", &FONT_S, 190);
        cv_text(40, y, &FONT_S, f, used ? (sel ? C_WHITE : C_HI) : C_DIM);
    }
    if (chain.name[0]) {
        cv_text(14, 102, &FONT_S, "NOW", C_DIM);
        cv_text(52, 102, &FONT_S, chain.name, C_GRAY);
    }
}

/* TRACKS mixer: one 15 px row per track: number (+ REC-armed dot), model, the 16 steps of the bank
 * its playhead is in (stopped: the first bank; the selected track: the bank STEP shows), and the
 * level fader over the output meter (MUTE when muted). A row redraws only when its signature changes. */
#define MX_RH 15u
#define MX_ROW_Y(r) (Y_GRAPH + 2u + (r) * MX_RH)
#define MX_CELL_X(k) (62u + (k) * 8u + (k) / 4u * 3u)   /* 16 cells of 6 px, a gap per beat */
#define MX_LX 203                                       /* level: fader + meter */
#define MX_LW 34
static struct {
    uint32_t sig[NTRK];
    uint8_t meter[NTRK];
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

static void draw_mix(void)
{
    uint32_t c;
    if (ui.force) {
        lcd_fill(0, Y_GRAPH, 240, H_GRAPH, C_BLACK);
        for (c = 0; c < NTRK; c++)
            mx.meter[c] = 0;
    }
    for (c = 0; c < NTRK; c++) {
        track_t *t = &trk[c];
        uint32_t sel = c == song.sel, lvl = (uint32_t)t->p[P_LEVEL] & 127u, mute = !lvl || trk_silent(t);
        uint32_t arm = (song.rec >> c) & 1u, len = view_len(t), bank, sig, k;
        int32_t m = mute ? 0 : meter_px(t->peak);
        uint16_t ink = sel ? C_HI : C_GRAY;
        char b[2] = {(char)('1' + c), 0};
        t->peak = 0;
        if (m < mx.meter[c] - 1)
            m = mx.meter[c] - 1;
        mx.meter[c] = (uint8_t)(m < 0 ? 0 : m);
        bank = song.playing ? (view_idx(t) % len) / 16u : sel ? ui.bank : 0u;
        sig = 1u + sel + arm * 2u + mute * 4u + lvl * 8u + mx.meter[c] * 1024u + bank * 65536u +
              (uint32_t)t->p[P_MODEL] * 7919u + steps_hash(t) * 131u +
              (song.playing ? view_idx(t) + 1u : 0u) * 2654435761u;
        if (!ui.force && sig == mx.sig[c])
            continue;
        mx.sig[c] = sig;
        cv_begin(240, MX_RH, C_BLACK);
        cv_text(2, -1, &FONT_S, b, sel ? C_WHITE : C_GRAY);
        if (arm)
            cv_rect(12, 5, 4, 4, song.playing ? C_WHITE : C_AMB);
        cv_text(18, -1, &FONT_S, N_MODEL[(uint32_t)t->p[P_MODEL] % NMODELS], ink);
        for (k = 0; k < 16u; k++) {
            uint32_t si = bank * 16u + k;
            int32_t x = (int32_t)MX_CELL_X(k);
            step_t sv = view_step(t, si);
            const step_t *st = &sv;
            int ph = song.playing && si == view_idx(t);     /* the playhead: its step drawn white */
            if (si >= len)
                continue;
            if (st->on)
                cv_rect(x, st->acc ? 2 : 4, 6, st->acc ? 11 : 7,
                        ph ? C_WHITE : mute ? C_DIM : st->acc ? (sel ? C_WHITE : C_HI) : ink);
            else
                cv_rect(x, ph ? 9 : 10, 6, ph ? 2 : 1, ph ? C_WHITE : C_DIM);
        }
        if (mute) {
            cv_text(MX_LX, -1, &FONT_S, "MUTE", C_DIM);
        } else {
            cv_rect(MX_LX, 7, MX_LW, 1, C_LINE);
            if (mx.meter[c])
                cv_rect(MX_LX, 5, mx.meter[c], 5, C_AMB);
            cv_rect(MX_LX + (int32_t)lvl * (MX_LW - 2) / 127, 2, 2, 11, sel ? C_WHITE : C_GRAY);
        }
        cv_blit(0, MX_ROW_Y(c));
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
    cv_text(4, 2, &FONT_S, r, C_AMB);
    cv_text(4, 24, &FONT_S, "GR", C_GRAY);
    cv_rect(36, 31, curve ? 70 : 120, 1, C_LINE);
    cv_rect(36, 27, clamp(dbx10, 0, 240) * (curve ? 70 : 120) / 240, 9, c);
    comp_fmt_db10(v, -dbx10);
    str_cpy(v + str_len(v), "dB", sizeof v - str_len(v));
    cv_text(curve ? 36 : 162, curve ? 40 : 24, &FONT_S, v, C_WHITE);
    cv_text(4, curve ? 62 : 46, &FONT_S, "IN", C_GRAY);
    cv_rect(36, curve ? 69 : 53, curve ? 70 : 120, 1, C_LINE);
    cv_rect(36, curve ? 65 : 49, (comp_on() ? meter_px(comp.peak) : 0) * (curve ? 70 : 120) / MX_LW, 9, C_AMB);
    {                                                   /* the source in the mix, next to its level (IN) */
        const char *st = comp_src_state();
        cv_text(curve ? 36 : 162, curve ? 84 : 46, &FONT_S, st, str_eq(st, "HEARD") ? C_GRAY : C_AMB);
    }
    if (curve) {
        comp_set_t s = comp_knobs();
        comp_cfg_t cf;
        comp_configure(&s, &cf);
        cv_rect(118, 22, 1, 77, C_LINE);
        cv_rect(118, 98, 118, 1, C_LINE);
        for (k = 0; k < 116u; k++) {
            int32_t in10 = -480 + (int32_t)k * 480 / 116, lvl = in10 * 65536 / 60;
            int32_t out10 = (lvl + comp_atten(&cf, lvl) + cf.makeup) * 60 / 65536;
            int32_t y = 98 - (out10 + 480) * 76 / 600;
            if (y >= 22 && y <= 97)
                cv_rect(120 + (int32_t)k, y, 1, 2, c);
        }
    }
}

static int lfo_random(uint32_t wave) { return wave == LW_SH || wave == LW_WANDER || wave == LW_RWALK; }

/* every frame on an LFO page: the shown LFO's trail gets a point each 1/136 of its cycle (the width = one cycle; at
 * most one a frame); it starts over on another track, LFO or wave, or after frames away from the LFO pages */
static void lfo_trail(void)
{
    const track_t *t = TSEL;
    uint32_t l = song.lsel & 1u, ph = t->lfo[l].ph;
    uint32_t key = (uint32_t)song.sel << 8 | l << 4 | (uint32_t)clamp(t->p[(l ? P_LFO2 : P_LFO1) + LF_WAVE], 0, 15);
    if (key != ui.tr_key || ui.frame != ui.tr_frame + 1u) {
        ui.tr_key = key;
        ui.tr_n = ui.tr_h = 0;
    }
    ui.tr_frame = ui.frame;
    if (ui.tr_n && ph - ui.tr_ph < 0xFFFFFFFFu / 136u)
        return;
    ui.tr_ph = ph;
    ui.tr[ui.tr_h] = (int16_t)(lfo_out(t, l) >> 1);
    ui.tr_h = (uint8_t)((ui.tr_h + 1u) % 136u);
    if (ui.tr_n < 136u)
        ui.tr_n++;
}

/* LFO pages: the LFO's name; one cycle of the wave (as morphed, phase-shifted) with the live position, or for the
 * random waves a scope (the live value at the right edge, its trail to the left); the rate mode; the routing and
 * TRIG */
static void graph_lfo(uint16_t c, uint32_t l)
{
    const track_t *t = TSEL;
    const int16_t *q = &t->p[l ? P_LFO2 : P_LFO1];
    uint32_t wave = (uint32_t)clamp(q[LF_WAVE], 0, LW_COUNT - 1), k, off = (uint32_t)clamp(q[LF_PHASE], 0, 127) << 25;
    int32_t m = clamp(q[LF_MORPH], 0, 127), px;
    char b[24];
    str_cpy(b, l ? "LFO 2" : "LFO 1", sizeof b);
    cv_text(4, 2, &FONT_L, b, C_WHITE);
    str_cpy(b, "RATE: ", sizeof b);
    str_cpy(b + str_len(b), N_LMODE[clamp(q[LF_MODE], 0, 2)], sizeof b - str_len(b));
    cv_text(4, 44, &FONT_S, b, C_GRAY);
    cv_rect(100, 40, 136, 1, C_LINE);
    if (lfo_random(wave)) {
        for (k = 0, px = 0; k < ui.tr_n; k++) {      /* newest at the right edge, older to the left */
            int32_t py = 40 - ui.tr[(ui.tr_h + 135u - k) % 136u] * 68 / 32767;
            if (!k)
                px = py;
            cv_rect(235 - (int32_t)k, py < px ? py : px, 1, (py < px ? px - py : py - px) + 2, c);
            px = py;
        }
        cv_rect(233, 40 - lfo_out(t, l) * 34 / 32767 - 2, 5, 5, C_WHITE);
    } else {
        for (k = 0, px = 0; k < 136u; k++) {         /* px: the previous point's y (joined: no gaps at edges) */
            int32_t py = 40 - lfo_shape(wave, m, k * (0xFFFFFFFFu / 136u) + off) * 34 / 32767;
            if (!k)
                px = py;
            cv_rect(100 + (int32_t)k, py < px ? py : px, 1, (py < px ? px - py : py - px) + 2, c);
            px = py;
        }
        px = (int32_t)(t->lfo[l].ph / (0xFFFFFFFFu / 136u));
        cv_rect(100 + px - 2, 40 - lfo_out(t, l) * 34 / 32767 - 2, 5, 5, C_WHITE);
    }
    {
        str_cpy(b, "-> ", sizeof b);
        str_cpy(b + 3, lfo_dest_label(t, q[LF_DEST]), sizeof b - 3);
        str_cpy(b + str_len(b), " ", sizeof b - str_len(b));
        fmt_int(b + str_len(b), clamp(q[LF_DEPTH], -64, 64) * 100 / 64);
        str_cpy(b + str_len(b), "%", sizeof b - str_len(b));
        cv_text(4, 84, &FONT_S, b, C_AMB);
        cv_text(170, 84, &FONT_S, N_LTRIG[clamp(q[LF_TRIG], 0, 2)], C_GRAY);
    }
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
    cv_text(4, 2, &FONT_L, N_RMODEL[m], m ? C_WHITE : C_GRAY);   /* the knob's names: 5 letters fit left of x 100 */
    if (!m)
        return;
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
    cv_text(4, 44, &FONT_S, b, C_GRAY);
    cv_rect(100, 74, 136, 1, C_LINE);
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
                cv_rect(100 + (int32_t)x, 74 - (int32_t)(56u - h * 4u), 2, (int32_t)(56u - h * 4u), c);
        }
    } else
    for (h = 0; h < (m == RS_CHORD ? 4u : 16u); h++) {
        uint32_t x = m == RS_CHORD ? (uint32_t)RS_CHORD_IV[rs_chord(t)][h] * 136u / 48u : HX[h];
        if (m == RS_PIPE && (h & 1u))
            continue;                                 /* PIPE: odd partials only (h = 0 is the 1st) */
        n = m == RS_CHORD ? 50u : 56u - h * 3u;
        cv_rect(100 + (int32_t)x, 74 - (int32_t)n, 2, (int32_t)n, c);
    }
    cv_rect(4, 84, 92, 1, C_LINE);
    cv_rect(4, 82, (int32_t)((uint32_t)t->rs.peak * 92u / 32767u), 5, C_AMB);
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
    cv_text(4, 2, &FONT_L, N_FTYPE[ty], ty ? C_WHITE : C_GRAY);
    if (!ty)
        return;
    param_format(&TP[P_FCUT], t->p[P_FCUT], b, &u);
    str_cpy(b + str_len(b), u, sizeof b - str_len(b));    /* "1.0kHz" */
    cv_text(4, 44, &FONT_S, b, C_GRAY);
    cv_rect(100, 28, 136, 1, C_LINE);                          /* 0 dB */
    for (x = 0; x < 136; x++) {
        int32_t db = clamp(flt_resp_db(ty, x - cx, clamp(t->p[P_FRESO], 0, 127)), -36, 18);
        int32_t y = 28 - db * 3 / 2;
        if (x)
            cv_line(99 + x, py, 100 + x, y, c);
        py = y;
    }
    ex = clamp(t->p[P_FCUT] + t->p[P_FENV], 0, 127) * 135 / 127;   /* ENV: the cutoff at the hit */
    if (t->p[P_FENV])
        for (x = 10; x < 84; x += 4)
            cv_rect(100 + ex, x, 1, 2, C_AMB);
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
        h ^= (ui.held < 16u && ui.step_t0[ui.held] ? ui.held + 1u : 0u) * 977u;
    if (pg->graph == GR_GRID)                         /* step record's cursor, the key sounding, KEYS (the roll's view
                                                       * follows from these: draw_graph) */
        h ^= (ui.srec ? ui.sr_cur + 1u : 0u) * 1543u +
             (kz.key && kz.trk == song.sel ? (uint32_t)(t->key_nt + 61) : 0u) * 389u + (uint32_t)keys_mode() * 99991u;
    if (pg->graph == GR_MODEL)
        h ^= (uint32_t)song.g[G_BPM] * 40503u;        /* ACID's gate: GATE of a step at the tempo */
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
    if (pg->graph == GR_LFO) {
        uint32_t l = song.lsel & 1u;
        h ^= ui.page * 389u + l * 7919u + (t->lfo[l].ph >> 26) * 2654435761u + (uint32_t)(lfo_out(t, l) >> 10) * 40503u;
        if (lfo_random((uint32_t)t->p[(l ? P_LFO2 : P_LFO1) + LF_WAVE]))
            h ^= (ui.tr_h + 1u) * 97u + ui.tr_n * 65537u;
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
    uint16_t fill = st == 2u ? C_HI : st == 1u ? C_LINE : st == 4u ? C_AMB : C_BLACK;
    uint16_t ink = st == 2u || st == 4u ? C_BLACK : st == 3u ? C_DIM : st == 1u ? C_WHITE : C_GRAY;
    cv_rect(x, y, w, 34, st == 3u ? C_BLACK : C_LINE);
    cv_rect(x + 1, y + 1, w - 2, 32, fill);
    cv_text(x + (w - text_w(&FONT_S, s)) / 2, y + 9, &FONT_S, s, ink);
}
static void graph_perf(void)
{
    uint32_t held = perf_kill ? 0u : perf_held, act = perf_act, ok = perf_avail(), i, sig = held * 31u + act * 7u + ok;
    for (i = 0; i < NTRK; i++)
        sig = sig * 3u + (uint32_t)(trk[i].p[P_MUTE] != 0);
    if (!ui.force && sig == ui.graph_sig)
        return;
    ui.graph_sig = sig;
    cv_begin(240, H_GRAPH, C_BLACK);
    cv_oy = 0;
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
    cv_blit_from(0, Y_GRAPH, 0);
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
    char b[24];
    for (k = 0; k < NTRK; k++)
        sig = (sig ^ ((uint32_t)(trk[k].p[P_MUTE] != 0) + (uint32_t)trk[k].p[P_MODEL] * 2u)) * 16777619u;
    if (ui.force) {
        lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, C_BLACK);
        lcd_fill(0, H_HEAD, 240, 1, C_LINE);
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
    cv_begin(240, 124, C_BLACK);
    cv_oy = 0;
    for (k = 0; k < NTRK; k++) {
        int32_t x = (int32_t)MU_BX(k), y = (int32_t)(MU_BY(k) - MU_Y);
        uint32_t wait = (song.mute_q >> k) & 1u;
        int on = !trk[k].p[P_MUTE] ^ (wait && blink);
        const char *mn = N_MODEL[(uint32_t)trk[k].p[P_MODEL] % NMODELS];
        uint16_t ink = on ? C_BLACK : wait ? C_AMB : C_GRAY;
        cv_rect(x, y, MU_BW, MU_BH, wait ? C_AMB : on ? C_HI : C_GRAY);   /* (waiting: amber, both phases) */
        if (!on)
            cv_rect(x + 1, y + 1, MU_BW - 2, MU_BH - 2, C_BLACK);
        b[0] = (char)('1' + k);
        b[1] = 0;
        cv_text(x + ((int32_t)MU_BW - text_w(&FONT_S, b)) / 2, y + 3, &FONT_S, b, on ? C_BLACK : C_WHITE);
        cv_text(x + ((int32_t)MU_BW - text_w(&FONT_S, mn)) / 2, y + 21, &FONT_S, mn, ink);
        note_text(b, 53 + KEY_TRK_KEY[k]);
        cv_text(x + ((int32_t)MU_BW - text_w(&FONT_S, b)) / 2, y + (int32_t)MU_BH + 2, &FONT_S, b, C_DIM);
    }
    cv_blit(0, MU_Y);
    cv_begin(240, 62, C_BLACK);                       /* "MUTE: NOW", "C# NOW  D# NEXT BAR" (the mode's key lit),
                                                       * "G4 LEARN: OFF" / "G4 LEARN: ON" (MIDI LEARN, ui_learn.c) */
    str_cpy(b, settings.mutebar ? "MUTE: NEXT BAR" : "MUTE: NOW", sizeof b);
    cv_text((240 - text_w(&FONT_S, b)) / 2, 0, &FONT_S, b, C_WHITE);
    k = (uint32_t)cv_text((240 - text_w(&FONT_S, "C# NOW  D# NEXT BAR")) / 2, 22, &FONT_S, "C# NOW",
                          settings.mutebar ? C_DIM : C_HI);
    cv_text((int32_t)k + 16, 22, &FONT_S, "D# NEXT BAR", settings.mutebar ? C_HI : C_DIM);
    str_cpy(b, ui.ml.on ? "G4 LEARN: ON" : "G4 LEARN: OFF", sizeof b);
    cv_text((240 - text_w(&FONT_S, b)) / 2, 44, &FONT_S, b, ui.ml.on ? C_WHITE : C_DIM);
    cv_blit(0, 164);
}
/* PERFORM: KNOB 1..4 FILTER CRUSH THROW DEPTH (SHIMMER while OCT UP / DN plays) */
static void perf_columns(void)
{
    char val[12];
    int32_t m = perf_k[0];
    fmt_int(val, m < 0 ? -m : m);
    draw_column(0, "FILT", m ? val : "OFF", m < 0 ? "LPF" : m > 0 ? "HPF" : "", m ? VAL(0u) : C_DIM, (m + 100) * 5,
                ICON_AUTO);
    fmt_int(val, perf_k[1]);
    draw_column(1, "CRUSH", perf_k[1] ? val : "OFF", perf_k[1] ? "%" : "", perf_k[1] ? VAL(1u) : C_DIM,
                perf_k[1] * 10, ICON_AUTO);
    fmt_int(val, perf_k[2]);
    draw_column(2, "THROW", perf_k[2] ? val : "OFF", perf_k[2] ? "%" : "", perf_k[2] ? VAL(2u) : C_DIM,
                perf_k[2] * 10, ICON_AUTO);
    if (perf_harm_on()) {
        fmt_int(val, perf_k[3]);
        draw_column(3, "SHIMR", perf_k[3] ? val : "OFF", perf_k[3] ? "%" : "", perf_k[3] ? VAL(3u) : C_DIM,
                    perf_k[3] * 10, ICON_AUTO);
    } else {
        fmt_int(val, 100 - perf_k[3]);
        draw_column(3, "DEPTH", val, "%", VAL(3u), (100 - perf_k[3]) * 10, ICON_AUTO);
    }
}

/* MOTION: the selected track's LEN steps, 16 a row; a step holding events lit (grey with PLAY OFF), the
 * playhead outlined */
static void graph_motion(void)
{
    const track_t *t = TSEL;
    uint32_t k = song.sel, len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), i, sig, n = 0;
    uint32_t has[2] = {0, 0}, ph = song.playing ? t->seq_idx % len : 0xFFu;
    for (i = 0; i < mo.s.count; i++)
        if (mo.s.ev[i].trk == k) {
            has[mo.s.ev[i].step >> 5] |= 1u << (mo.s.ev[i].step & 31u);
            n++;
        }
    sig = has[0] * 31u + has[1] * 131u + len * 7u + ph * 1009u + k * 65537u + n * 3u + (uint32_t)motion_on(k);
    if (!ui.force && sig == ui.graph_sig)
        return;
    ui.graph_sig = sig;
    cv_begin(240, H_GRAPH, C_BLACK);
    cv_oy = 0;
    for (i = 0; i < len; i++) {
        int32_t x = 8 + (int32_t)(i & 15u) * 14, y = 8 + (int32_t)(i >> 4) * 28;
        int ev = (int)((has[i >> 5] >> (i & 31u)) & 1u);
        cv_rect(x, y, 12, 22, i == ph ? C_WHITE : C_LINE);
        cv_rect(x + 1, y + 1, 10, 20, ev ? (motion_on(k) ? C_HI : C_GRAY) : C_BLACK);
    }
    cv_blit_from(0, Y_GRAPH, 0);
}
/* MOTION: KNOB 1 PLAY, 2 EVENT (the count), 4 CLEAR */
static void motion_columns(void)
{
    char val[12];
    uint32_t k = song.sel;
    draw_column(0, "PLAY", motion_on(k) ? "ON" : "OFF", "", VAL(0u), -1, ICON_AUTO);
    fmt_int(val, (int32_t)motion_count(k));
    draw_column(1, "EVENT", val, "", C_HI, -1, ICON_AUTO);
    draw_column(2, "", "", "", C_HI, -1, ICON_AUTO);
    draw_column(3, "CLEAR", "", "", C_HI, -1, ICON_AUTO);
}

/* SONG: 4 rows of the list ("2  B  x4  BREAK"), the + row after the last; the playing row marked, its repeats left */
static void graph_song(void)
{
    const chain_config_t *c = &chain.cfg;
    uint32_t sel = ui.song_row < c->count ? ui.song_row : c->count, first = sel > 2u ? sel - 2u : 0u, i,
             sig = 2166136261u, last;
    char b[16], nm[NAME_LEN + 1u];
    for (i = 0; i < c->count; i++)
        sig = (sig ^ (c->row[i].slot | (uint32_t)c->row[i].repeat << 2)) * 16777619u;
    for (i = 0; i < 4u; i++) {
        project_name(i, nm);
        sig = str_hash(sig, nm) + (uint32_t)project_used(i) * (i + 3u);
    }
    sig += c->count * 7u + sel * 1009u + c->loop * 13u +
           (chain.running ? (chain.row + 1u) * 104729u + chain.left * 65537u : 0u);
    if (!ui.force && sig == ui.graph_sig)
        return;
    ui.graph_sig = sig;
    cv_begin(240, H_GRAPH, C_BLACK);
    cv_oy = 0;
    last = c->count < CHAIN_ROWS ? c->count : CHAIN_ROWS - 1u;
    for (i = first; i <= last && i < first + 4u; i++) {
        int32_t y = 6 + (int32_t)(i - first) * 28;
        int s = i == sel, play = chain.running && i == chain.row;
        uint16_t col = s ? C_WHITE : C_GRAY;
        if (s)
            cv_rect(0, y - 3, 240, 24, C_LINE);
        if (play) {
            uint32_t j;
            for (j = 0; j < 5u; j++)                  /* the play mark */
                cv_rect(4 + (int32_t)j, y + 2 + (int32_t)j, 1, 14 - 2 * (int32_t)j, C_WHITE);
        }
        if (i >= c->count) {
            cv_text(16, y, &FONT_S, "+", col);
            if (!c->count)
                cv_text(44, y, &FONT_S, "KNOB 2: ADD A ROW", C_DIM);
            continue;
        }
        fmt_int(b, (int32_t)i + 1);
        cv_text(16, y, &FONT_S, b, col);
        b[0] = (char)('A' + c->row[i].slot);
        b[1] = 0;
        cv_text(48, y, &FONT_S, b, s ? C_WHITE : C_HI);
        b[0] = 'x';
        fmt_int(b + 1, c->row[i].repeat);
        cv_text(70, y, &FONT_S, b, col);
        if (play) {
            fmt_int(b, chain.left);
            str_cpy(b + str_len(b), " LEFT", 8);
            cv_text(236 - text_w(&FONT_S, b), y, &FONT_S, b, C_AMB);
        } else if (project_name(c->row[i].slot, nm) && nm[0]) {
            char f[NAME_LEN + 1u];
            fit(f, nm, &FONT_S, 124);
            cv_text(108, y, &FONT_S, f, C_DIM);
        }
    }
    cv_blit_from(0, Y_GRAPH, 0);
}
/* SONG: KNOB 1 ROW, 2 SLOT, 3 REPS, 4 LOOP */
static void song_columns(void)
{
    const chain_config_t *c = &chain.cfg;
    uint32_t r = ui.song_row < c->count ? ui.song_row : c->count;
    char v[12], u[8];
    if (r >= c->count) {
        draw_column(0, "ROW", "+", "", VAL(0u), -1, ICON_AUTO);
        draw_column(1, "SLOT", "--", "", C_DIM, -1, ICON_AUTO);
        draw_column(2, "REPS", "--", "", C_DIM, -1, ICON_AUTO);
    } else {
        fmt_int(v, (int32_t)r + 1);
        u[0] = '/';
        fmt_int(u + 1, c->count);
        draw_column(0, "ROW", v, u, VAL(0u), -1, ICON_AUTO);
        v[0] = (char)('A' + c->row[r].slot);
        v[1] = 0;
        draw_column(1, "SLOT", v, "", VAL(1u), c->row[r].slot * 1000 / 3, ICON_AUTO);
        v[0] = 'x';
        fmt_int(v + 1, c->row[r].repeat);
        draw_column(2, "REPS", v, "", VAL(2u), (c->row[r].repeat - 1) * 1000 / 15, ICON_AUTO);
    }
    draw_column(3, "LOOP", c->loop ? "ON" : "OFF", "", VAL(3u), -1, ICON_AUTO);
}

static void draw_graph(void)
{
    const page_t *pg = cur_page();
    const track_t *t = TSEL;
    uint16_t c = ACC;
    uint32_t sig, top = 0;
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
    if (!ui.force && sig == ui.graph_sig)
        return;
    ui.graph_sig = sig;
    cv_begin(240, H_GRAPH, C_BLACK);
    cv_oy = G_OY;
    if (ui.home) {
        graph_scope(c);
    } else {
        switch (pg->graph) {
        case GR_MODEL:
            graph_model(t, c);
            break;
        case GR_STEPS:
            graph_steps(t, c);
            break;
        case GR_GRID:
            if (trk_model(t)->gate && !view_src(t)) {
                roll_view(t);                         /* (the graph changed: the view chosen again) */
                graph_roll(t, c);
                if (!ui.srec && held_line(t, -18))  /* a held step's line in the band above the roll (screen y
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
            graph_lfo(c, song.lsel & 1u);
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
    }
    cv_oy = 0;
    if (ui.hot_t && settings.zoom) {
        int32_t x;
        top = 1;
        cv_rect(0, 0, 150, 50, C_BLACK);
        cv_text(4, 0, &FONT_S, ui.focus_l, C_GRAY);
        x = cv_text(4, 16, &FONT_L, ui.focus_v, C_WHITE);
        cv_text(x + 4, 30, &FONT_S, ui.focus_u, C_DIM);
    }
    cv_blit_from(0, Y_GRAPH, top || ui.graph_top || ui.force ? 0u : G_OY);
    ui.graph_top = (uint8_t)top;
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
    else if (step_keys() && ui.held < 16u && ui.step_t0[ui.held] && ui.step_si[ui.held] < NSTEP)
        si = ui.step_si[ui.held];
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

/* footer: the bank's 16 steps; the model, track and page */
static void draw_foot(void)
{
    char ti[20], tn[4], fn[16], ml[32];
    const track_t *t = TSEL;
    const page_t *pg = cur_page();
    const char *mn = N_MODEL[(uint32_t)t->p[P_MODEL] % NMODELS];
    uint32_t sig, i;
    int note = foot_note(fn);                        /* the held / cursor step's note */
    if (ui.layer) {
        str_cpy(ti, "[FX] HOLD", sizeof ti);
    } else if (ui.home) {
        str_cpy(ti, "HOME 1/3", sizeof ti);
    } else {
        uint32_t k, n = fam_pages(pg->fam, &k);
        if (pg->fam == FAM_HOME)                     /* COMP: HOME 2/3, 3/3 */
            k++, n++;
        str_cpy(ti, pg->title, 10);
        if (n > 1 && !note) {                        /* (a note shown: the title alone) */
            str_cpy(ti + str_len(ti), " ", 4);
            fmt_int(ti + str_len(ti), (int32_t)k);
            str_cpy(ti + str_len(ti), "/", 4);
            fmt_int(ti + str_len(ti), (int32_t)n);
        }
    }
    tn[0] = 'T';
    tn[1] = (char)('1' + song.sel);
    tn[2] = 0;
    sig = str_hash(str_hash(0x9E3779B9u, ti), mn) + song.sel * 7u + steps_hash(t) + ui.bank * 3001u +
          (song.playing && view_idx(t) / 16u == ui.bank ? view_idx(t) + 1u : 0u) * 97u + str_hash(0x51u, fn) * 3u;
    ml[0] = 0;
    if (ui.ml.on) {                                  /* MIDI LEARN (ui_learn.c): its line in the steps' row */
        ml_line(ml);
        sig += str_hash(0x1EA2u, ml);
    }
    if (!ui.force && sig == ui.foot_sig)
        return;
    ui.foot_sig = sig;
    cv_begin(240, H_FOOT, C_BLACK);
    i = 0;
    if (ml[0]) {                                     /* row 1: "T3 DECAY: SEND A CC", OCT+ DONE at the right where
                                                       * it fits */
        int32_t hx = 236 - text_w(&FONT_S, "OCT+ DONE");
        if (cv_text(4, 0, &FONT_S, ml, C_WHITE) + 8 <= hx)
            cv_text(hx, 0, &FONT_S, "OCT+ DONE", C_GRAY);
        i = 16u;
    }
    for (; i < 16u; i++) {
        uint32_t si = ui.bank * 16u + i;
        int32_t sx = 6 + (int32_t)i * 14 + (int32_t)(i / 4u) * 4;
        step_t sv = view_step(t, si);
        const step_t *st = &sv;
        if (si >= view_len(t))
            continue;
        if (st->on)
            cv_rect(sx, st->acc ? 1 : 3, 2, st->acc ? 10 : 8, st->acc ? C_WHITE : C_HI);
        else
            cv_rect(sx, step_keys() ? 8 : 10, 2, step_keys() ? 3 : 1, step_keys() ? C_GRAY : C_DIM);   /* the keys
                                                         * are the steps here: the empty ones marked a little more */
        if (song.playing && si == view_idx(t))
            cv_rect(sx - 1, 13, 4, 3, C_WHITE);
    }
    cv_text(4, 20, &FONT_S, mn, C_HI);
    cv_text(60, 20, &FONT_S, tn, C_AMB);
    if (note)
        cv_text(80, 20, &FONT_S, fn, C_WHITE);        /* "C#3 ACC SLD": x 80..176, the title right of it */
    cv_text(236 - text_w(&FONT_S, ti), 20, &FONT_S, ti, C_GRAY);
    cv_blit(0, Y_FOOT);
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
    if (ui.home) {
        for (c = 0; c < 4u; c++) {
            int16_t *vp;
            const param_desc_t *d = home_param(c, &vp);
            if (!d->label || d->label[0] == '-') {
                draw_column(c, "", "", "", C_HI, -1, ICON_AUTO);
                continue;
            }
            param_format(d, *vp, val, &unit);
            {
                int32_t mv, mod = -1;                /* an LFO on the knob: its live value, as on EDIT */
                if (lfo_live(TSEL, P_E0 + c, &mv))
                    mod = RATIO(d, mv);
                draw_column_m(c, d->label, val, unit, VAL(c), RATIO(d, *vp), param_icon(d, *vp), mod);
            }
        }
        return;
    }
    if (pg->scope == SC_MIX) {                          /* TRACK LEVEL LEN PAN of the selected track */
        const track_t *t = TSEL;
        uint32_t lvl = (uint32_t)t->p[P_LEVEL];
        fmt_int(val, (int32_t)song.sel + 1);
        draw_column(0, "TRACK", val, "/8", VAL(0u), (int32_t)song.sel * 1000 / (NTRK - 1), ICON_AUTO);
        if (!lvl || t->p[P_MUTE]) {
            str_cpy(val, "MUTE", 12);
            unit = "";
        } else {
            param_format(&TP[P_LEVEL], (int32_t)lvl, val, &unit);
        }
        draw_column(1, "LEVEL", val, unit, lvl && !trk_silent(t) ? VAL(1u) : C_DIM, (int32_t)lvl * 1000 / 127, ICON_AUTO);
        param_format(&TP[P_SLEN], t->p[P_SLEN], val, &unit);
        draw_column(2, "LEN", val, unit, VAL(2u), RATIO(&TP[P_SLEN], t->p[P_SLEN]), ICON_AUTO);
        param_format(&TP[P_PAN], t->p[P_PAN], val, &unit);
        draw_column(3, "PAN", val, unit, VAL(3u), RATIO(&TP[P_PAN], t->p[P_PAN]), param_icon(&TP[P_PAN], t->p[P_PAN]));
        return;
    }
    if (pg->scope == SC_GRID && ui.srec) {             /* step record: STEP NOTE TIME FLAG of the cursor step */
        static const char *const TM[3] = {"NOTE", "TIE", "REST"}, *const FL[4] = {"-", "ACC", "SLD", "A+S"};
        const step_t *st = &TSEL->step[ui.sr_cur % NSTEP];
        char u[8];
        fmt_int(val, (int32_t)ui.sr_cur + 1);
        str_cpy(u, "/", 8);
        fmt_int(u + 1, (int32_t)view_len(TSEL));
        draw_column(0, "STEP", val, u, VAL(0u), -1, ICON_AUTO);
        note_text(val, 60 + st->nt);
        draw_column(1, "NOTE", val, "", VAL(1u), (st->nt - NT_MIN) * 1000 / (NT_MAX - NT_MIN), ICON_AUTO);
        draw_column(2, "TIME", TM[srec_time(st)], "", VAL(2u), -1, ICON_AUTO);
        draw_column(3, "FLAG", FL[st->acc | st->slide << 1], "", VAL(3u), -1, ICON_AUTO);
        return;
    }
    if (pg->scope == SC_GRID) {                         /* BANK; the rest: the grid on the keys */
        char u[8];
        fmt_int(val, (int32_t)ui.bank + 1);
        str_cpy(u, "/", 8);
        fmt_int(u + 1, (int32_t)bank_count());
        draw_column(0, "BANK", val, u, VAL(0u), -1, ICON_AUTO);
        for (c = 1; c < 4u; c++)
            draw_column(c, "", "", "", C_HI, -1, ICON_AUTO);
        return;
    }
    for (c = 0; c < 4u; c++) {
        int16_t *vp;
        const param_desc_t *d = page_desc(pg, c, &vp);
        if (!d || !d->label || d->label[0] == '-') {
            draw_column(c, "", "", "", C_HI, -1, ICON_AUTO);
            continue;
        }
        if (pg->id[c] == G_MIDI && pg->scope == SC_GLOBAL) {
            str_cpy(val, !usb.up ? "OFF" : usb.config ? "MIDI" : usb.setups ? "ENUM" : usb.sof_seen ? "BUS" : "WAIT", 12);
            draw_column(c, "USB", val, "USB", C_HI, -1, ICON_AUTO);
            continue;
        }
        if (pg->id[c] == G_NAME && pg->scope == SC_GLOBAL) {
            draw_column(c, "NAME", "EDIT", "", C_HI, -1, ICON_AUTO);
            continue;
        }
        if (pg->id[c] == G_INFO && pg->scope == SC_GLOBAL) {
            fmt_int(val, (int32_t)(song.cpu_q8 * 100u / 256u));
            unit = "%";
        } else {
            param_format(d, *vp, val, &unit);
        }
        {
            int32_t mv, mod = -1;
            if (pg->scope == SC_TRACK && lfo_live(TSEL, page_id(pg, c), &mv))
                mod = RATIO(d, mv);
            draw_column_m(c, d->label, val, unit, VAL(c), d->fmt == F_ENUM && d->max < 2 ? -1 : RATIO(d, *vp),
                          param_icon(d, *vp), mod);
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
            lcd_fill(0, 0, 240, 240, C_BLACK);
            draw_text_box(0, 92, 240, &FONT_L, "SAFE START", C_HI, 1);
            draw_text_box(0, 128, 240, &FONT_S, "NO AUDIO - USB UPDATE READY", C_WHITE, 1);
            draw_text_box(0, 148, 240, &FONT_S, "POWER OFF TO LEAVE", C_GRAY, 1);
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
            lcd_fill(0, 0, 240, 240, C_BLACK);
            draw_text_box(0, 84, 240, &FONT_S, b, C_WHITE, 1);
            if (ui.confirm == 5u)
                draw_text_box(0, 104, 240, &FONT_S, "START NEW PROJECT?", C_WHITE, 1);
            draw_text_box(0, 132, 240, &FONT_S, "OCT- NO    OCT+ YES", C_GRAY, 1);
            ui.force = 0;
        }
        return;
    }
    if (name_on()) {                                  /* NAME: its own screen */
        draw_name();
        return;
    }
    if (ui.mute) {                                    /* GLO held: the MUTE screen */
        draw_mute();
        return;
    }
    bank_fix();
    if (ui.force)
        draw_frame();
    felucca_dbg.stage = 3;
    draw_head();
    felucca_dbg.stage = 4;
    draw_columns();
    felucca_dbg.stage = 5;
    draw_graph();
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
}
