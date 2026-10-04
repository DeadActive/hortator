/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 Eugene Vech */
/* Drum UI input: LEDs, knobs and buttons, the STEP grid on the keys, panel setup. */
/* ----------------------------------------------------------- LEDs --- */
/* The LED picture is built off-line and copied one byte per column: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker. */
static uint8_t led_pos[41];                        /* (col << 3) | row bit, 0xFF = none */

static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}

static void led_put(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}

static uint32_t cur_fam(void) { return ui.home ? FAM_HOME : cur_page()->fam; }

static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0};
    uint32_t k, c, fam = cur_fam();
    static uint8_t ready;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    led_put(nl, panel.btn[FAM_BTN[fam]], 1);
    led_put(nl, panel.btn[B_PLAY], song.playing && ((song.tick / 64u) & 1u) == 0u);   /* blinks: intended */
    led_put(nl, panel.btn[B_REC], song.rec != 0u);
    if (grid_mode()) {                                /* the bank's steps; the playhead inverted */
        const track_t *t = TSEL;
        led_put(nl, panel.btn[B_OCTDN], ui.bank > 0u);
        led_put(nl, panel.btn[B_OCTUP], ui.bank + 1u < bank_count());
        for (k = 0; k < 16u; k++) {
            uint32_t si = ui.bank * 16u + k;
            int on = si < (uint32_t)t->p[P_SLEN] && t->step[si].on;
            if (song.playing && si == t->seq_idx)
                on = !on;
            led_put(nl, 14u + k, on);
        }
    } else {
        for (k = 0; k < 27u; k++)
            led_put(nl, 14u + k, (int)((fm1_in.notes >> k) & 1u));
    }
    for (c = 0; c < FM1_NCOL; c++)
        fm1_led[c] = nl[c];
}

/* ---------------------------------------------------------- input --- */
static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    uint32_t now = fm1_ticks(), dt = now - ui.enc_t[role];
    ui.enc_t[role] = now;
    if (range > 40 && dt < 60u * 1000u * FM1_TICKS_PER_US)
        return s * (range > 150 ? 6 : 3);
    return s;
}

/* TRACKS mixer: KNOB 1 TRACK, 2 LEVEL (a muted track: the first turn unmutes), 3 LEN, 4 PAN */
static void tracks_edit(uint32_t slot, int32_t steps)
{
    track_t *t = TSEL;
    uint32_t id;
    if (slot == 0u) {
        track_select((uint32_t)clamp((int32_t)song.sel + (steps > 0 ? 1 : -1), 0, NTRK - 1));
        return;
    }
    if (slot == 1u && t->p[P_MUTE]) {
        t->p[P_MUTE] = 0;
        return;
    }
    id = slot == 1u ? P_LEVEL : slot == 2u ? P_SLEN : P_PAN;
    t->p[id] = (int16_t)clamp(t->p[id] + accel(EN_K1 + slot, steps, TP[id].max - TP[id].min), TP[id].min, TP[id].max);
}

static void tracks_rec_tap(void)                       /* arm / disarm; arming while stopped starts play */
{
    uint8_t bit = (uint8_t)(1u << song.sel);
    song.rec ^= bit;
    if ((song.rec & bit) && !song.playing)
        transport_req = 1;
}

static void edit_param(uint32_t slot, int32_t steps)
{
    int16_t *vp;
    const page_t *pg = cur_page();
    const param_desc_t *d;
    uint32_t id = pg->id[slot];
    int32_t v;
    if (pg->scope == SC_GRID) {
        if (slot == 0u)
            bank_set((int32_t)ui.bank + (steps > 0 ? 1 : -1));
        return;
    }
    if (pg->scope == SC_MIX) {
        tracks_edit(slot, steps);
        return;
    }
    if (pg->scope == SC_TRACK && id == P_MODEL) {     /* a model comes with its default sound */
        model_step(steps);
        return;
    }
    d = page_desc(pg, slot, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    v = clamp(*vp + accel(EN_K1 + slot, steps, d->max - d->min), d->min, d->max);
    *vp = (int16_t)v;
    if (!v || pg->scope != SC_GLOBAL)
        return;
    if ((id == G_LOAD || id == G_SAVE || id == G_CLRSEQ || id == G_INITSND) && ui.arm != id) {
        *vp = 0;                                      /* one detent arms, a second one within ~1.5 s acts */
        ui.arm = (uint8_t)id;
        ui.arm_t = 90;
        ui_say("AGAIN: ", d->label);
        return;
    }
    ui.arm = 0;
    switch (id) {                                     /* GO buttons: act, then back to 0 */
    case G_LOAD:
        *vp = 0;
        project_load((uint32_t)song.g[G_SLOT] - 1u);
        break;
    case G_SAVE:
        *vp = 0;
        project_save((uint32_t)song.g[G_SLOT] - 1u);
        break;
    case G_CLRSEQ:
        *vp = 0;
        track_clear(TSEL);
        ui_message("PATTERN CLEARED");
        break;
    case G_INITSND:
        *vp = 0;
        fm1_irq_off();
        drum_set_model(TSEL, (uint32_t)TSEL->p[P_MODEL]);
        fm1_irq_on();
        ui_message("SOUND INIT");
        ui.force = 1;
        break;
    default:
        break;
    }
}

/* HOME / REC: tap on release, hold 0.7 s fires once. t0 = press time | 1,
 * bit 1 = fired (or swallowed: then the release is no tap either) */
enum { BT_NONE, BT_TAP, BT_HOLD };
static uint32_t btn_hold(uint32_t *t0, uint32_t label, uint32_t now, int hold_ok)
{
    uint32_t tap;
    if ((fm1_in.buttons >> panel.btn[label]) & 1u) {
        if (!*t0)
            *t0 = (now | 1u) & ~2u;
        else if (hold_ok && !(*t0 & 2u) && now - (*t0 & ~3u) > 700u * 1000u * FM1_TICKS_PER_US) {
            *t0 |= 2u;
            return BT_HOLD;
        }
        return BT_NONE;
    }
    tap = *t0 && !(*t0 & 2u);
    *t0 = 0;
    return tap ? BT_TAP : BT_NONE;
}

static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), now = fm1_ticks(), id, b, k, fam = cur_fam();
    uint32_t home = btn_hold(&ui.home_t0, B_HOME, now, 1);
    uint32_t rec = btn_hold(&ui.rec_t0, B_REC, now, !ui.menu && (fam == FAM_SEQ || fam == FAM_MIX));
    int32_t s;
    if (home == BT_HOLD) {                              /* HOME held: open the menu, or leave it */
        if (ui.menu) {
            menu_close();
        } else {
            ui.menu = 1;
            ui.menu_sel = 0;
            ui.confirm = 0;
            ui.force = 1;
            song.seq_mode = 0;
        }
    }
    if (ui.menu) {
        if (ui.rec_t0)
            ui.rec_t0 |= 2u;
        if (!ui.home_t0)
            menu_input(pressed);
        return;
    }
    if (rec == BT_HOLD) {                               /* REC held on SEQ / TRACKS: "clear track n?" */
        ui.confirm = 1;
        ui.confirm_trk = song.sel;
        ui.force = 1;
    } else if (rec == BT_TAP && !ui.confirm) {
        if (fam == FAM_MIX)
            tracks_rec_tap();
        else if (fam == FAM_SEQ)
            song.rec ^= (uint8_t)(1u << song.sel);
        else
            open_family(FAM_MIX);
    }
    if (ui.confirm) {                                   /* OCT- cancels, OCT+ clears; nothing else reacts */
        if ((pressed >> panel.btn[B_OCTUP]) & 1u) {
            char m[12] = "1 CLEARED";
            m[0] = (char)('1' + ui.confirm_trk);
            track_clear(&trk[ui.confirm_trk % NTRK]);
            ui_say("TRACK ", m);
            ui.confirm = 0;
            ui.force = 1;
        } else if ((pressed >> panel.btn[B_OCTDN]) & 1u) {
            ui.confirm = 0;
            ui.force = 1;
        }
        enc_drop();
        return;
    }
    if (home == BT_TAP)
        go_home();
    bank_fix();                                         /* LEN may have changed (knob, load) */
    for (id = 0; id < 14u; id++) {
        if (!((pressed >> id) & 1u))
            continue;
        b = panel_btn_of(id);
        switch (b) {
        case B_PLAY:
            transport_req = song.playing ? 2 : 1;
            break;
        case B_REC:
        case B_HOME:                                    /* tap / hold: above */
            break;
        case B_OCTDN:
        case B_OCTUP:                                   /* the STEP grid's bank; elsewhere nothing */
            if (grid_mode())
                bank_set((int32_t)ui.bank + (b == B_OCTUP ? 1 : -1));
            break;
        default: {
            uint32_t f;
            for (f = FAM_HOME + 1u; f < FAM_COUNT; f++)
                if (FAM_BTN[f] == b && f != FAM_MIX)
                    open_family(f);
            break;
        }
        }
    }
    if (grid_mode())
        for (k = 0; k < 16u; k++)
            if ((notes >> k) & 1u)
                step_tap(k);
    if ((s = panel_enc(EN_PRESET)) != 0 && (ui.home || cur_fam() == FAM_TRK))
        model_step(s);
    if ((s = panel_enc(EN_ALGO)) != 0)
        track_select((uint32_t)clamp((int32_t)song.sel + (s > 0 ? 1 : -1), 0, NTRK - 1));
    if ((s = panel_enc(EN_SELECT)) != 0) {
        song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
        ui.bpm_t = 40;
    }
    for (k = 0; k < 4u; k++) {
        const page_t *pg = cur_page();
        int16_t *hv;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        if (ui.home || pg->scope == SC_GRID || pg->scope == SC_MIX || page_desc(pg, k, &hv)) {
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
        if (ui.home) {
            int16_t *vp;
            const param_desc_t *d = home_param(k, &vp);
            if (d->max > d->min)
                *vp = (int16_t)clamp(*vp + accel(EN_K1 + k, s, d->max - d->min), d->min, d->max);
        } else {
            edit_param(k, s);
        }
    }
}

/* ---------------------------------------------------- panel setup --- */
/* 30 s without input: give up and keep the old table (a stuck key cannot hang the boot) */
#define SETUP_IDLE_MS 30000u
static void panel_setup(void)
{
    uint32_t i, used = 0, t0 = fm1_ms;
    const panel_t old = panel;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 10, 240, &FONT_S, "HARDWARE CALIBRATION", C_WHITE, 1);
    draw_text_box(0, 30, 240, &FONT_S, "TEACH EACH BUTTON AND KNOB", C_GRAY, 1);
    while (fm1_in.buttons) {                             /* wait for OCT-/OCT+ release */
        fm1_wdt_feed();
        if (fm1_ms - t0 > SETUP_IDLE_MS)
            goto timeout;
    }
    fm1_input_edges(0);
    for (i = 0; i < NB; i++) {
        uint32_t p = 0, id;
        draw_text_box(0, 80, 240, &FONT_S, "PRESS", C_GRAY, 1);
        draw_text_box(0, 100, 240, &FONT_L, B_NAME[i], C_WHITE, 1);
        t0 = fm1_ms;
        while (!(p & ~used)) {
            fm1_wdt_feed();
            p |= fm1_input_edges(0);
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
        }
        for (id = 0; id < 14u; id++)
            if (((p & ~used) >> id) & 1u)
                break;
        panel.btn[i] = (uint8_t)id;
        used |= 1u << id;
    }
    used = 0;
    for (i = 0; i < NE; i++) {
        uint32_t e;
        int32_t st = 0;
        draw_text_box(0, 80, 240, &FONT_S, "TURN RIGHT", C_GRAY, 1);
        draw_text_box(0, 100, 240, &FONT_L, E_NAME[i], C_WHITE, 1);
        for (e = 0; e < 7u; e++)
            fm1_enc_take(e);
        t0 = fm1_ms;
        for (;;) {
            fm1_wdt_feed();
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
            for (e = 0; e < 7u; e++)
                if (!((used >> e) & 1u) && (st = fm1_enc_take(e)) != 0)
                    break;
            if (e < 7u)
                break;
        }
        panel.enc[i] = (uint8_t)e;
        panel.dir[i] = (int8_t)(st > 0 ? 1 : -1);
        used |= 1u << e;
        fm1_delay_ms(300);
        fm1_enc_take(e);
    }
    panel.magic = PANEL_MAGIC;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.force = 1;
    return;
timeout:
    panel = old;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.force = 1;
    ui_message("SETUP CANCELLED");
}

