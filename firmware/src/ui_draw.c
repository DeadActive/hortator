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

/* top bar: transport, BPM | track, USB, battery; messages replace it */
static void draw_head(void)
{
    char b[16];
    uint32_t i;
    int32_t x;
    uint32_t rec = (song.rec >> song.sel) & 1u ? 2u : song.rec != 0u;
    uint32_t sig = (uint32_t)song.playing * 3u + rec * 5u + song.sel * 13131u +
                   (ui.msg_t ? str_hash(7u, ui.msg) : 0u) + (uint32_t)song.g[G_BPM] * 101u + (ui.bpm_t != 0) * 31u +
                   (uint32_t)batt_shown() * 7777u + (usb.config && !usb.suspended) * 99991u;
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
    if (FELUCCA_ICONS) {
        cv_icon(x, 2, ICON_TEMPO, C_GRAY);
        x += 14;
    }
    cv_text(x, 1, &FONT_S, b, ui.bpm_t ? C_WHITE : C_HI);
    b[0] = 'T';
    b[1] = (char)('1' + song.sel);
    b[2] = 0;
    cv_text(140, 1, &FONT_S, b, C_HI);
    {
        int32_t lvl = batt_shown(), k, bx = 236 - 19;
        cv_rect(bx, 4, 17, 1, C_GRAY);
        cv_rect(bx, 12, 17, 1, C_GRAY);
        cv_rect(bx, 4, 1, 9, C_GRAY);
        cv_rect(bx + 16, 4, 1, 9, C_GRAY);
        cv_rect(bx + 17, 6, 2, 5, C_GRAY);
        for (k = 0; k < lvl; k++)
            cv_rect(bx + 2 + k * 5, 6, 3, 5, lvl == 1 && batt_level() <= 1 ? C_WHITE : C_HI);
        if (usb.config && !usb.suspended)
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
static void draw_column(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc,
                        int32_t ratio, uint32_t icon)
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
        key[n + 3] = 0;
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
    }
    cv_blit(c * 60u + 4u, Y_LABEL);
}

/* ---------------------------------------------------------- graphs --- */
static uint32_t steps_hash(const track_t *t)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < NSTEP; i++)
        h = (h ^ (t->step[i].on + t->step[i].acc * 2u + t->step[i].cond * 4u + t->step[i].rat * 256u)) * 16777619u;
    return h ^ (uint32_t)t->p[P_SLEN] * 7919u;
}

/* the selected track's model: name large, voices and choke group */
static void graph_model(const track_t *t, uint16_t c)
{
    const dmodel_t *m = &DMODELS[(uint32_t)t->p[P_MODEL] % NMODELS];
    char b[16];
    cv_text(6, 10, &FONT_L, m->name, c);
    str_cpy(b, m->voices > 1 ? "2 VOICES" : "1 VOICE", sizeof b);
    cv_text(6, 52, &FONT_S, b, C_GRAY);
    if (t->p[P_CHOKE]) {
        str_cpy(b, "CHOKE 1", sizeof b);
        b[6] = (char)('0' + t->p[P_CHOKE]);
        cv_text(100, 52, &FONT_S, b, C_AMB);
    }
    if (t->p[P_LLEVEL])
        cv_text(6, 70, &FONT_S, "+ SAMPLE LAYER", C_AMB);
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
    uint32_t i, len = (uint32_t)t->p[P_SLEN];
    for (i = 0; i < NSTEP && i < len; i++) {
        int32_t x = 6 + (int32_t)(i % 16u) * 14 + (int32_t)(i % 16u) / 4 * 4, y = 6 + (int32_t)(i / 16u) * 24;
        const step_t *st = &t->step[i];
        if (st->on)
            step_bar(x, st->acc ? y : y + 4, 2, st->acc ? 14 : 10, st, st->acc ? C_WHITE : c, 3);
        else
            cv_rect(x, y + 13, 2, 1, C_DIM);
        if (song.playing && i == t->seq_idx)
            cv_rect(x - 1, y + 16, 4, 3, C_WHITE);
    }
}

/* STEP page: the bank's 16 steps as the keys show them; bank n/m on the left */
static void graph_grid(const track_t *t, uint16_t c)
{
    uint32_t i, len = (uint32_t)t->p[P_SLEN];
    char b[8];
    for (i = 0; i < 16u; i++) {
        uint32_t si = ui.bank * 16u + i;
        int32_t x = 4 + (int32_t)i * 14 + (int32_t)(i / 4u) * 2, y = 30;
        const step_t *st = &t->step[si];
        if (si >= len) {
            cv_rect(x, y + 20, 11, 1, C_LINE);
            continue;
        }
        if (st->on)
            step_bar(x, st->acc ? y : y + 10, 11, st->acc ? 40 : 30, st, st->acc ? C_WHITE : c, 3);
        else
            cv_rect(x, y + 36, 11, 4, C_DIM);
        if (song.playing && si == t->seq_idx)
            cv_rect(x, y + 46, 11, 3, C_WHITE);
    }
    fmt_int(b, (int32_t)ui.bank + 1);
    str_cpy(b + str_len(b), "/", 4);
    fmt_int(b + str_len(b), (int32_t)bank_count());
    cv_text(4, 4, &FONT_S, "BANK", C_GRAY);
    cv_text(48, 4, &FONT_S, b, C_HI);
    if (ui.held < 16u && ui.step_t0[ui.held] && ui.step_si[ui.held] != 0xFFFFu) {   /* a step held: its settings */
        const step_t *hs = &t->step[ui.step_si[ui.held]];
        char h[32], cs[8];
        str_cpy(h, "STEP ", sizeof h);
        fmt_int(h + str_len(h), (int32_t)ui.step_si[ui.held] + 1);
        str_cpy(h + str_len(h), "  ", sizeof h - str_len(h));
        cond_format(hs->cond, cs);
        str_cpy(h + str_len(h), cs, sizeof h - str_len(h));
        str_cpy(h + str_len(h), "  RATCH ", sizeof h - str_len(h));
        fmt_int(h + str_len(h), (int32_t)hs->rat + 1);
        cv_text(4, 84, &FONT_S, h, C_WHITE);
    } else {
        cv_text(4, 84, &FONT_S, "TAP: ON/OFF  HOLD: ACCENT", C_DIM);
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
    for (i = 0; i < 4u; i++) {
        int32_t y = 8 + (int32_t)i * 26;
        char b[4];
        int sel = (int32_t)i + 1 == song.g[G_SLOT];
        b[0] = (char)('1' + i);
        b[1] = 0;
        if (sel)
            cv_rect(4, y + 6, 3, 3, C_WHITE);
        cv_text(14, y, &FONT_S, b, sel ? C_WHITE : C_GRAY);
        cv_text(40, y, &FONT_S, project_used(i) ? "USED" : "EMPTY", project_used(i) ? (sel ? C_WHITE : C_HI) : C_DIM);
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
        uint32_t sel = c == song.sel, lvl = (uint32_t)t->p[P_LEVEL] & 127u, mute = !lvl || t->p[P_MUTE];
        uint32_t arm = (song.rec >> c) & 1u, len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), bank, sig, k;
        int32_t m = mute ? 0 : meter_px(t->peak);
        uint16_t ink = sel ? C_HI : C_GRAY;
        char b[2] = {(char)('1' + c), 0};
        t->peak = 0;
        if (m < mx.meter[c] - 1)
            m = mx.meter[c] - 1;
        mx.meter[c] = (uint8_t)(m < 0 ? 0 : m);
        bank = song.playing ? (t->seq_idx % len) / 16u : sel ? ui.bank : 0u;
        sig = 1u + sel + arm * 2u + mute * 4u + lvl * 8u + mx.meter[c] * 1024u + bank * 65536u +
              (uint32_t)t->p[P_MODEL] * 7919u + steps_hash(t) * 131u +
              (song.playing ? t->seq_idx + 1u : 0u) * 2654435761u;
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
            const step_t *st = &t->step[si];
            int ph = song.playing && si == t->seq_idx;     /* the playhead: its step drawn white */
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
    if (pg->graph == GR_SLOTS)
        for (i = 0; i < 4u; i++)
            h ^= (uint32_t)project_used(i) << (20u + i);
    if (pg->graph == GR_STEPS || pg->graph == GR_GRID)
        h ^= steps_hash(t) + (song.playing ? t->seq_idx + 1u : 0u) * 31u;
        h ^= (ui.held < 16u && ui.step_t0[ui.held] ? ui.held + 1u : 0u) * 977u;
    return h;
}

static void draw_graph(void)
{
    const page_t *pg = cur_page();
    const track_t *t = TSEL;
    uint16_t c = ACC;
    uint32_t sig, top = 0;
    if (!ui.home && pg->graph == GR_MIX) {
        draw_mix();
        ui.graph_top = 1;
        return;
    }
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
            graph_grid(t, c);
            break;
        case GR_FX:
            graph_fx(t, c);
            break;
        case GR_SLCR:
            graph_slicer(t, c);
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

/* footer: the bank's 16 steps; the model, track and page */
static void draw_foot(void)
{
    char ti[20], tn[4];
    const track_t *t = TSEL;
    const page_t *pg = cur_page();
    const char *mn = N_MODEL[(uint32_t)t->p[P_MODEL] % NMODELS];
    uint32_t sig, i;
    if (ui.home) {
        str_cpy(ti, "HOME", sizeof ti);
    } else {
        uint32_t n = 0, k = 0;
        for (i = 0; i < NPAGES; i++)
            if (PAGES[i].fam == pg->fam) {
                n++;
                if (i == ui.page)
                    k = n;
            }
        str_cpy(ti, pg->title, 10);
        if (n > 1) {
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
          (song.playing && t->seq_idx / 16u == ui.bank ? t->seq_idx + 1u : 0u) * 97u;
    if (!ui.force && sig == ui.foot_sig)
        return;
    ui.foot_sig = sig;
    cv_begin(240, H_FOOT, C_BLACK);
    for (i = 0; i < 16u; i++) {
        uint32_t si = ui.bank * 16u + i;
        int32_t sx = 6 + (int32_t)i * 14 + (int32_t)(i / 4u) * 4;
        const step_t *st = &t->step[si];
        if (si >= (uint32_t)t->p[P_SLEN])
            continue;
        if (st->on)
            cv_rect(sx, st->acc ? 1 : 3, 2, st->acc ? 10 : 8, st->acc ? C_WHITE : C_HI);
        else
            cv_rect(sx, 10, 2, 1, C_DIM);
        if (song.playing && si == t->seq_idx)
            cv_rect(sx - 1, 13, 4, 3, C_WHITE);
    }
    cv_text(4, 20, &FONT_S, mn, C_HI);
    cv_text(60, 20, &FONT_S, tn, C_AMB);
    cv_text(236 - text_w(&FONT_S, ti), 20, &FONT_S, ti, C_GRAY);
    cv_blit(0, Y_FOOT);
}

static void draw_columns(void)
{
    uint32_t c;
    char val[12];
    const char *unit;
    const page_t *pg = cur_page();
    if (ui.home) {
        for (c = 0; c < 4u; c++) {
            int16_t *vp;
            const param_desc_t *d = home_param(c, &vp);
            if (!d->label || d->label[0] == '-') {
                draw_column(c, "", "", "", C_HI, -1, ICON_AUTO);
                continue;
            }
            param_format(d, *vp, val, &unit);
            draw_column(c, d->label, val, unit, VAL(c), RATIO(d, *vp), param_icon(d, *vp));
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
        draw_column(1, "LEVEL", val, unit, lvl && !t->p[P_MUTE] ? VAL(1u) : C_DIM, (int32_t)lvl * 1000 / 127, ICON_AUTO);
        param_format(&TP[P_SLEN], t->p[P_SLEN], val, &unit);
        draw_column(2, "LEN", val, unit, VAL(2u), RATIO(&TP[P_SLEN], t->p[P_SLEN]), ICON_AUTO);
        param_format(&TP[P_PAN], t->p[P_PAN], val, &unit);
        draw_column(3, "PAN", val, unit, VAL(3u), RATIO(&TP[P_PAN], t->p[P_PAN]), param_icon(&TP[P_PAN], t->p[P_PAN]));
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
        if (pg->id[c] == G_INFO && pg->scope == SC_GLOBAL) {
            fmt_int(val, (int32_t)(song.cpu_q8 * 100u / 256u));
            unit = "%";
        } else {
            param_format(d, *vp, val, &unit);
        }
        draw_column(c, d->label, val, unit, VAL(c), d->fmt == F_ENUM && d->max < 2 ? -1 : RATIO(d, *vp),
                    param_icon(d, *vp));
    }
}

static void ui_draw(void)
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
            char b[16] = "CLEAR TRACK 1?";
            b[12] = (char)('1' + ui.confirm_trk);
            lcd_fill(0, 0, 240, 240, C_BLACK);
            draw_text_box(0, 84, 240, &FONT_S, b, C_WHITE, 1);
            draw_text_box(0, 132, 240, &FONT_S, "OCT- NO    OCT+ YES", C_GRAY, 1);
            ui.force = 0;
        }
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
