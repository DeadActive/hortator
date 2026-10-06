/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
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

static int octdn_held(void) { return (int)((fm1_in.buttons >> panel.btn[B_OCTDN]) & 1u); }

static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0};
    uint32_t k, c, fam = cur_fam();
    static uint8_t ready;
    if (safe_start) {                                 /* safe start: LEDs off */
        uint32_t c0;
        for (c0 = 0; c0 < FM1_NCOL; c0++)
            fm1_led[c0] = 0;
        return;
    }
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    led_put(nl, panel.btn[FAM_BTN[fam]],                /* LAYER: EDIT blinks, LFO 2: LFO blinks, 250 ms on / off */
            (fam != FAM_LAY && (fam != FAM_LFO || !song.lsel)) || ((fm1_ticks() / (250u * 1000u * FM1_TICKS_PER_US)) & 1u));
    led_put(nl, panel.btn[B_PLAY], song.playing && ((song.tick / 64u) & 1u) == 0u);   /* blinks: intended */
    led_put(nl, panel.btn[B_REC], song.rec != 0u);
    if (ui.layer) {                                   /* PERFORM: effects blink, held lit, unavailable dark; the
                                                       * track keys lit while their track sounds */
        int blink = (int)((fm1_ticks() / (250u * 1000u * FM1_TICKS_PER_US)) & 1u);
        uint32_t held = perf_kill ? 0u : perf_held, ok = perf_avail();
        for (k = 0; k < 27u; k++) {
            uint32_t e = perf_key(k);
            led_put(nl, 14u + k, e < PF_M1 ? (int)(((ok >> e) & 1u) && (((held >> e) & 1u) || blink))
                               : e < PF_N ? (int)(!((held >> e) & 1u) && !trk[e - PF_M1].p[P_MUTE]) : 0);
        }
    } else if (grid_mode()) {                         /* the bank's steps; the playhead inverted */
        const track_t *t = TSEL;
        led_put(nl, panel.btn[B_OCTDN], ui.bank > 0u);
        led_put(nl, panel.btn[B_OCTUP], ui.bank + 1u < bank_count());
        for (k = 0; k < 16u; k++) {
            uint32_t si = ui.bank * 16u + k;
            int on = si < view_len(t) && view_step(t, si).on;
            if (song.playing && si == view_idx(t))
                on = !on;
            led_put(nl, 14u + STEP_KEY[k], on);
        }
    } else if (mix_mode() && octdn_held()) {          /* TRACKS, OCT- held: the playing tracks' keys; a mute waiting
                                                         * for the bar blinks */
        int blink = (int)((fm1_ticks() / (250u * 1000u * FM1_TICKS_PER_US)) & 1u);
        for (k = 0; k < NTRK; k++)
            led_put(nl, 14u + KEY_TRK_KEY[k], ((song.mute_q >> k) & 1u) ? blink : trk[k].p[P_MUTE] == 0);
    } else if (comp_mode()) {                         /* COMP: the ducked tracks' keys */
        for (k = 0; k < NTRK; k++)
            led_put(nl, 14u + KEY_TRK_KEY[k], trk[k].p[P_DUCK] && k != comp_src());
    } else {
        for (k = 0; k < 27u; k++)
            led_put(nl, 14u + k, (int)((fm1_in.notes >> k) & 1u));
    }
    for (c = 0; c < FM1_NCOL; c++)
        fm1_led[c] = nl[c];
}

/* ---------------------------------------------------------- input --- */
/* knob acceleration (upstream 1.0, #23 / #52): only a turn going on in one direction over a wide value (range > 32)
 * speeds up, by the longer of this read's and the previous read's time per detent (ACC_RATE / ms: x2 .. x4); a slow
 * turn, the first two detents of a turn, a single quick detent (a bounce) and a reversal are one step per detent.
 * MENU > KNOB ACCEL OFF: always one step. ui.enc_t[role]: bits 0..23 the ms of its last read, bit 24 its direction
 * (+1), 25..31 its ms per detent (127 slow) */
#define ACC_GAP 40u
#define ACC_RATE 50u
static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    uint32_t now = (fm1_ticks() / (1000u * FM1_TICKS_PER_US)) & 0xFFFFFFu, st = ui.enc_t[role], up = s > 0, pi = st >> 25;
    uint32_t a, i, m = 1;
    if (!settings.accel || range <= 32 || !s)
        return s;
    a = (uint32_t)(s < 0 ? -s : s);
    i = ((now - st) & 0xFFFFFFu) / a;                   /* ms per detent of this read */
    if (!st || ((st >> 24) & 1u) != up || i >= ACC_GAP)
        i = 127u;                                       /* a new turn, or reversed */
    else if (pi < ACC_GAP) {
        m = ACC_RATE / (i > pi ? i : pi ? pi : 1u);
        m = m < 1u ? 1u : m > 4u ? 4u : m;
    }
    ui.enc_t[role] = now | up << 24 | (i ? i : 1u) << 25;
    return s * (int32_t)m;
}

/* a step key held + KNOB 1: its PROB, KNOB 2: its RATCH. The first turn of a hold cancels the hold's accent
 * flip: the step is as before the press, and on. A held key outside LEN takes the turn and does nothing. */
static int step_edit(uint32_t knob, int32_t steps)
{
    uint32_t k = ui.held;
    step_t *s;
    if (knob > 1u || k >= 16u || !ui.step_t0[k])
        return 0;
    if (ui.step_si[k] >= (uint32_t)TSEL->p[P_SLEN] || view_src(TSEL))   /* outside LEN, or a Grids track */
        return 1;
    s = &TSEL->step[ui.step_si[k]];
    if (!(ui.step_t0[k] & 4u)) {
        *s = ui.step_prev[k];
        s->on = 1;
        ui.step_t0[k] |= 6u;                          /* edited: no hold accent from now on */
    }
    if (knob == 0u)
        s->cond = (uint8_t)cond_store((uint32_t)clamp((int32_t)cond_pos(s->cond) + accel(EN_K1, steps, COND_MAX), 0,
                                                      (int32_t)COND_MAX));
    else
        s->rat = (uint8_t)clamp((int32_t)s->rat + (steps > 0 ? 1 : -1), 0, 3);
    return 1;
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

static void mute_bar_set(uint32_t on)                  /* TRACKS' mutes: at once / on the next bar (device setting) */
{
    settings.mutebar = on & 1u;
    song.act[2] = (int16_t)settings.mutebar;
    settings_save();
    ui_message(settings.mutebar ? "MUTE: NEXT BAR" : "MUTE: NOW");
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
    uint32_t id = page_id(pg, slot);
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
    if (pg->scope == SC_GLOBAL && id == G_BPM && song.g[G_CLOCK]) {
        ui_message(song.g[G_CLOCK] == 1 ? "CLK USB" : "CLK TRS");
        return;
    }
    v = param_turn(d, *vp, accel(EN_K1 + slot, steps, d->max - d->min));
    if (pg->scope == SC_TRACK && id == P_RMODEL && v != RS_OFF && *vp == RS_OFF && reson_tracks(TSEL) >= RS_MAXTRK) {
        ui_message("RESON: 4 TRACKS MAX");            /* RESON on 4 tracks at most (CPU) */
        return;
    }
    if (pg->scope == SC_TRACK && id == P_RMODEL && v == RS_CHORD && *vp != RS_CHORD && reson_chords(TSEL) >= 2u) {
        ui_message("CHORD: 2 TRACKS MAX");            /* RESON CHORD on 2 tracks at most */
        return;
    }
    *vp = (int16_t)v;
    if (pg->scope == SC_GLOBAL && id == G_MUTEBAR) {
        mute_bar_set((uint32_t)v);
        return;
    }
    if (!v || pg->scope != SC_GLOBAL)
        return;
    if ((id == G_LOAD || id == G_SAVE || id == G_CLRSEQ || id == G_INITSND || id == G_CLRALL || id == G_INITALL) &&
        ui.arm != id) {
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
    case G_CLRALL:
        *vp = 0;
        seq_clear_all();
        ui_message("ALL PATTERNS CLEARED");
        break;
    case G_INITALL:
        *vp = 0;
        init_all();
        ui_message("ALL INIT");
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
#define STEP_HOLD_MS 400u                               /* a STEP grid key held this long: accent (user, 0.4 s) */
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

/* PERFORM (perform.c): FX tapped = its pages, on the release (nothing else touched); FX held = the layer: keys
 * pressed with it are its own (seq.c keyboard_block), KNOB 1..4 its macros FILTER CRUSH THROW DEPTH (never saved,
 * back to off when FX is let go). It opens at once with a key or a knob, else after FX_HOLD_MS (then the release
 * does nothing). No layer in the menu or a dialog: a press there stays dead until let go, and every effect is off
 * until its keys are let go */
#define FX_HOLD_MS 400u
#define FX_DOWN 1u
#define FX_OPEN 2u
#define FX_DEAD 4u
static int fx_allowed(void) { return !ui.menu && !ui.confirm; }
/* PERFORM PAGE: the screen closed (the screen under it changed, HOME, the menu, a dialog): the macros off; keys
 * still held stay the layer's until let go */
static void perf_page_close(void)
{
    ui.pg_open = 0;
    perf_mask &= ~PERF_PAGE;
    perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
}
static void fx_layer(uint32_t now, int home_tap)
{
    uint32_t bit = 1u << panel.btn[B_FX], *t0 = &ui.fx_t0, k, show, down = (fm1_in.buttons & bit) != 0u;
    int32_t s;
    if (!fx_allowed())
        perf_kill = 1;
    else if (!kb_layer)
        perf_kill = 0;
    if (ui.pg_open && (!fx_allowed() || home_tap || ui.page != ui.pg_page || ui.home != ui.pg_home))
        perf_page_close();
    if (down) {
        if (!*t0)
            *t0 = (now & ~7u) | FX_DOWN;
        if (!fx_allowed())
            *t0 |= FX_DEAD;
        if (!(*t0 & FX_DEAD) && (kb_layer || now - (*t0 & ~7u) >= FX_HOLD_MS * 1000u * FM1_TICKS_PER_US))
            *t0 |= FX_OPEN;
    } else if (*t0) {
        if (!(*t0 & (FX_OPEN | FX_DEAD)))
            open_family(FAM_FX);                        /* a tap: the FX pages, as the button always did */
        *t0 = 0;
        if (!ui.pg_open)
            perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;   /* HOLD: the macros snap back */
    }
    if ((down && !(*t0 & FX_DEAD)) || ui.pg_open)
        for (k = 0; k < 4u; k++)                        /* the macros: FILTER CRUSH THROW DEPTH */
            if ((s = panel_enc(EN_K1 + k)) != 0) {
                perf_k[k] = (int8_t)clamp(perf_k[k] + accel(EN_K1 + k, s, 200), k ? 0 : -100, 100);
                if (down)
                    *t0 |= FX_OPEN;
            }
    if (settings.perfpage && (*t0 & FX_OPEN) && !ui.pg_open && fx_allowed()) {   /* PAGE: the screen stays */
        ui.pg_open = 1;
        ui.pg_page = ui.page;
        ui.pg_home = ui.home;
    }
    perf_mask = (down && !(*t0 & FX_DEAD) ? bit : 0u) | (ui.pg_open ? PERF_PAGE : 0u);
    show = fx_allowed() && ((*t0 & FX_OPEN) || kb_layer || ui.pg_open);
    if (show != ui.layer) {
        ui.layer = (uint8_t)show;
        ui.force = 1;
    }
}

static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), now = fm1_ticks(), id, b, k, fam = cur_fam();
    uint32_t home = btn_hold(&ui.home_t0, B_HOME, now, 1);
    uint32_t rec = btn_hold(&ui.rec_t0, B_REC, now, !ui.menu && (fam == FAM_SEQ || fam == FAM_MIX));
    int32_t s;
    if (safe_start)                                     /* safe start: no pages, no edits */
        return;
    fx_layer(now, home == BT_TAP);                      /* PERFORM: FX tap / hold, the PAGE screen */
    if ((fm1_in.buttons & perf_mask) || (perf_mask & PERF_PAGE))   /* PERFORM: keys are the layer's */
        notes = 0;
    notes &= ~kb_layer;
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
        else
            open_family(FAM_MIX);                       /* arming is TRACKS' (a REC tap there) */
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
        home_step();
    settings_poll();
    song.octdn = panel.btn[B_OCTDN];
    if (mix_mode())                                     /* REC on TRACKS arms / disarms: the keys play / select */
        song.seq_mode = song.rec ? 2u : 1u;
    song.act[2] = (int16_t)settings.mutebar;
    bank_fix();                                         /* LEN may have changed (knob, load) */
    if (!ui.home && page_hidden(ui.page)) {             /* the engine changed: its last EDIT page */
        while (ui.page > page_first(FAM_SND) && page_hidden(ui.page))
            ui.page--;
        ui.fam_last[FAM_SND] = ui.page;
        page_entered();
    }
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
        case B_OCTUP:                                   /* the STEP grid's bank; EDIT: into / out of the layer */
            if (grid_mode()) {
                bank_set((int32_t)ui.bank + (b == B_OCTUP ? 1 : -1));
            } else if (!ui.home && b == B_OCTUP && cur_fam() == FAM_SND) {
                ui.fam_last[FAM_LAY] = 0;               /* LAYER 1/2 */
                open_family(FAM_LAY);
            } else if (!ui.home && b == B_OCTDN && cur_fam() == FAM_LAY) {
                open_family(FAM_SND);                   /* the SOUND page it came from */
            } else if (!ui.home && b == B_OCTUP && cur_fam() == FAM_LFO) {   /* the shown LFO: SYNC -> Hz -> TIME */
                int16_t *mp = &TSEL->p[(song.lsel ? P_LFO2 : P_LFO1) + LF_MODE];
                *mp = (int16_t)((clamp(*mp, 0, 2) + 1) % 3);
                ui.force = 1;
            } else if (!ui.home && b == B_OCTDN && cur_fam() == FAM_LFO) {   /* LFO 1 <-> 2, the same page */
                song.lsel ^= 1u;
                page_entered();
            }
            break;
        default: {
            uint32_t f;
            for (f = FAM_HOME + 1u; f < FAM_COUNT; f++)
                if (FAM_BTN[f] == b && f != FAM_MIX && f != FAM_LAY && b != B_FX)
                    open_family(f == FAM_SND && cur_fam() == FAM_LAY ? FAM_LAY : f);   /* in the layer: its pages */
            break;
        }
        }
    }
    if (grid_mode()) {
        for (k = 0; k < 16u; k++) {                     /* steps on the white keys (seq.c STEP_KEY): a press */
            uint32_t *t0 = &ui.step_t0[k];               /* turns the step on / off, a hold flips its accent */
            if ((notes >> STEP_KEY[k]) & 1u) {
                step_press(k);
                *t0 = (now | 1u) & ~6u;
                ui.held = (uint8_t)k;
            } else if (!((fm1_in.notes >> STEP_KEY[k]) & 1u)) {
                *t0 = 0;
            } else if (*t0 && !(*t0 & 2u) && now - (*t0 & ~7u) > STEP_HOLD_MS * 1000u * FM1_TICKS_PER_US) {
                *t0 |= 2u;
                step_hold(k);
            }
        }
    } else if (mix_mode()) {                            /* TRACKS: a white key selects its track; OCT- held: mutes
                                                         * it, top C# / D# (MONO / POLY): mutes at once / next bar */
        if (octdn_held() && ((notes >> 20) & 1u))
            mute_bar_set(0);
        if (octdn_held() && ((notes >> 22) & 1u))
            mute_bar_set(1);
        for (k = 0; k < NTRK; k++)
            if ((notes >> KEY_TRK_KEY[k]) & 1u) {
                if (octdn_held())
                    track_mute_toggle(k);
                else if (!song.rec)                     /* armed: the key plays and records (seq.c) */
                    track_select(k);
            }
    } else if (comp_mode()) {
        for (k = 0; k < NTRK; k++)                      /* COMP: a white key ducks / unducks its track */
            if ((notes >> KEY_TRK_KEY[k]) & 1u)
                track_duck_toggle(k);
    } else if (ui.home || (!ui.menu && cur_page()->scope == SC_TRACK && cur_fam() != FAM_SEQ)) {
        for (k = 0; k < NTRK; k++)                      /* HOME, a per-track page: a white key selects its track */
            if ((notes >> KEY_TRK_KEY[k]) & 1u)         /* (it plays it too: seq.c reads the keys itself) */
                track_select(k);
    }
    if ((s = panel_enc(EN_PRESET)) != 0)              /* PRESET: the section's pages (the engine: MODEL on EDIT 1) */
        page_turn(s > 0 ? 1 : -1);
    if ((s = panel_enc(EN_ALGO)) != 0)
        track_select((uint32_t)clamp((int32_t)song.sel + (s > 0 ? 1 : -1), 0, NTRK - 1));
    if ((s = panel_enc(EN_SELECT)) != 0) {
        if (song.g[G_CLOCK])                         /* following a clock: its tempo, not the knob */
            ui_message(song.g[G_CLOCK] == 1 ? "CLK USB" : "CLK TRS");
        else {
            song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
            ui.bpm_t = 40;
        }
    }
    for (k = 0; k < 4u; k++) {
        const page_t *pg = cur_page();
        int16_t *hv;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        if (grid_mode() && step_edit(k, s))
            continue;
        if (ui.home || pg->scope == SC_GRID || pg->scope == SC_MIX || page_desc(pg, k, &hv)) {
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
        if (ui.home) {
            int16_t *vp;
            const param_desc_t *d = home_param(k, &vp);
            if (d->max > d->min)
                *vp = (int16_t)param_turn(d, *vp, accel(EN_K1 + k, s, d->max - d->min));
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

