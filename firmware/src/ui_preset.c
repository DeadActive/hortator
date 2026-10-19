/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* PRESETS, the UI's side (spec docs/superpowers/specs/2026-10-19-presets-design.md; the core: presets.c, the user
 * bank's flash record: project.c). Included by ui_input.c.
 * The PRESETS encoder: each detent loads the next / previous preset onto the selected track at once (presets.c
 * preset_step: every bank in order, empty user slots and a capped ACID skipped, wrapping; no preset yet: the voice's
 * first). A popup over the graph shows the bank, the preset (large) and two loadable ones above and below; it goes
 * PP_SHOW_MS after the last detent (or with another track, a full screen).
 * EDIT held PP_HOLD_MS (as GLO_HOLD_MS): the PRESETS list. The press still acts at once, as a tap (SOUND, its next
 * page); the hold puts the page before the press back under the list. Step record keeps EDIT for its rest (no list);
 * while MIDI LEARN is on the hold does nothing (the list would end LEARN: ui_learn.c ml_guard).
 * The list: every preset by bank (MACHINEDRUM, CLASSIC, MODERN, USER with its 128 slots, empty ones too), the track's
 * own marked. KNOB 1 / PRESETS the cursor, KNOB 2 / SELECT the bank (its first), ALGORITHM / a white key the track (the
 * keys play, as on HOME). Each cursor move auditions the preset under it (owner, 2026-10-19): the track takes its MODEL
 * and 8 knobs for now and is hit once (its key and the pattern play it too; an empty slot or a capped ACID: said, the
 * last audition kept); OCT+ LOAD keeps it and closes the list; any other way out (EDIT, HOME) puts the track's own sound
 * back (MODEL, knobs, preset, its edited mark), as another track picked in the list does before that one auditions.
 * SAVE: the track's own sound (not the audition) into the user slot under the cursor (a used one: SAVE again within
 * PP_ARM_MS saves over it), elsewhere into the first free one; OCT- DELETE a used user slot (OCT- again); PLAY plays.
 * A save while playing is in the list at once, in flash at the next STOP (project.c). */
#define PP_HOLD_MS GLO_HOLD_MS                          /* EDIT held: the list */
#define PP_SHOW_MS 1500u                                /* the popup after the last detent */
#define PP_ARM_MS 1500u                                 /* SAVE over / DELETE: the second press within */
#define PP_T(ms) ((ms) * 1000u * FM1_TICKS_PER_US)
#define PL_ROWS 7u                                      /* the list's rows */
#define PL_RH 20                                        /* .. their height */
#define PL_HALF 4u                                      /* .. drawn 4 at a time (cv_begin's CV_MAX: 6 rows) */
static const char *const PB_TAB[PB_N] = {"MD", "CLASSIC", "MODERN", "USER"};

static int pp_on(void) { return ui.pr.pop; }
static int pl_on(void) { return ui.pr.lst; }

/* one detent of PRESETS (dir > 0 the next): the preset loaded onto the selected track at once (the audio IRQ off
 * meanwhile: presets.c preset_load), the popup up or kept PP_SHOW_MS longer */
static void preset_popup_turn(int dir)
{
    track_t *t = TSEL;
    uint32_t m = (uint32_t)t->p[P_MODEL], id = preset_step(t, t->pre, dir);
    if (preset_load(t, id) < 0)                        /* (nothing loadable: preset_step gave the one it came from) */
        return;
    if ((uint32_t)t->p[P_MODEL] != m)
        ui.force = 1;                                  /* another voice: its pages, as a MODEL change */
    if (!ui.pr.pop)
        ui.pr.sig = 0;
    ui.pr.pop = 1;
    ui.pr.pop_trk = song.sel;
    ui.pr.pop_id = (uint16_t)id;
    ui.pr.pop_t0 = ui.pr.now;
}

static void pp_close(void)
{
    ui.pr.pop = 0;
    ui.force = 1;                                      /* the graph back */
}

/* the list's cursor: preset id c, the arm off */
static void pl_cursor(int32_t c)
{
    ui.pr.cur = (uint16_t)clamp(c, 0, (int32_t)PRE_NID - 1);
    ui.pr.arm = 0;
}

/* the audition over: the track's own sound back (presets.c preset_put: the IRQ off, no hit) */
static void pl_restore(void)
{
    if (!ui.pr.pv)
        return;
    ui.pr.pv = 0;
    preset_put(&trk[ui.pr.pv_trk % NTRK], ui.pr.sv_model, ui.pr.sv_k, ui.pr.sv_pre, ui.pr.sv_ed);
}

/* the preset under the cursor on the selected track for now: MODEL and the 8 knobs as a load (presets.c preset_apply:
 * the audio IRQ off, another voice cut first, motion's base follows), the track's own sound kept first (the knobs as
 * motion's base while it plays: what SAVE stores). hit 1: hit once at its NOTE, velocity 100, as its key (no MIDI, no
 * recording); 2: only if the audio IRQ already played white key `key` (seq.c keyboard_block: else that press plays the
 * audition itself). Refused (an empty slot, a capped ACID): said, the track as it is */
static void pl_audition(uint32_t hit, uint32_t key)
{
    track_t *t = TSEL;
    uint32_t k = song.sel, i;
    preset_t p;
    if (ui.pr.pv && ui.pr.pv_trk != k)
        pl_restore();                                  /* (the track changed elsewhere) */
    if (!preset_ok(t, ui.pr.cur, &p)) {
        ui_message(!preset_get(ui.pr.cur, &p) ? "EMPTY SLOT" : "ACID: 2 TRACKS MAX");
        return;
    }
    fm1_irq_off();
    if (!ui.pr.pv) {
        ui.pr.pv = 1;
        ui.pr.pv_trk = (uint8_t)k;
        ui.pr.sv_model = (uint8_t)t->p[P_MODEL];
        for (i = 0; i < 8u; i++)
            ui.pr.sv_k[i] = (mo.base_ok >> k) & 1u ? mo.base[k][P_E0 + i] : t->p[P_E0 + i];
        ui.pr.sv_pre = t->pre;
        ui.pr.sv_ed = t->pre_ed;
    }
    preset_apply(t, &p, ui.pr.cur);                    /* (another voice: no full redraw, the list is its own
                                                        * screen; its pages are drawn anew as it closes) */
    motion_rebase(k);
    if (hit == 1u || (hit == 2u && ((kb_prev >> key) & 1u)))
        drum_hit(t, 100u);
    fm1_irq_on();
}

/* the cursor moved to c (PRESETS, KNOB 1, a bank): auditioned when it moved */
static void pl_move(int32_t c)
{
    uint32_t was = ui.pr.cur;
    pl_cursor(c);
    if (ui.pr.cur != was)
        pl_audition(1u, 0u);
}

/* track k picked in the list (ALGORITHM; a white key: key its key + 1): the one before back to its sound, k auditions */
static void pl_track(uint32_t k, uint32_t key)
{
    ui.pr.arm = 0;
    if (k >= NTRK || k == song.sel)
        return;
    pl_restore();
    track_select(k);
    pl_audition(key ? 2u : 1u, key ? key - 1u : 0u);
}

/* the track's own preset as the list marks it (its sound before an audition) */
static uint32_t pl_own(void)
{
    preset_t p;
    if (!ui.pr.pv)
        return preset_of(TSEL);
    return preset_get(ui.pr.sv_pre, &p) && p.model == ui.pr.sv_model ? ui.pr.sv_pre : PRE_NONE;
}

static void preset_list_open(void)
{
    uint32_t c = preset_of(TSEL);
    if (c >= PRE_NID)
        c = preset_first_of((uint32_t)TSEL->p[P_MODEL]);
    ui.pr.pop = 0;
    ui.pr.lst = 1;
    ui.pr.pv = 0;
    pl_cursor(c < PRE_NID ? (int32_t)c : 0);
    ui.pr.sig = 0;
    ui.srec = 0;                                       /* (step record ends) */
    grid_drop_holds();                                 /* a grid key held edits nothing under it */
    song.seq_mode = 0;                                 /* the keys play (seq_mode_now) */
    ui.msg_t = 0;
    ui.force = 1;
}

static void pl_close(void)                             /* (no LOAD: the track's own sound back) */
{
    pl_restore();
    ui.pr.lst = 0;
    ui.pr.arm = 0;
    page_entered();                                    /* the keys back to the page (seq_mode), redrawn */
}

/* frame start (ui_input, every frame): EDIT let go or pressed where it cannot hold; the popup's and the arm's time */
static void pre_frame(uint32_t now)
{
    ui.pr.now = now;
    if (!((fm1_in.buttons >> panel.btn[B_EDIT]) & 1u))
        ui.pr.e_st = 0;
    else if (ui.mute || ui.menu || ui.confirm || name_on() || ui.pr.lst || ui.ml.on)
        ui.pr.e_st = 2;                                /* dead until let go (MIDI LEARN: the list would end it) */
    if (ui.pr.pop && (now - ui.pr.pop_t0 >= PP_T(PP_SHOW_MS) || song.sel != ui.pr.pop_trk || ui.pr.lst || ui.menu ||
                      ui.confirm || name_on() || ui.mute))
        pp_close();
    if (ui.pr.arm && now - ui.pr.arm_t0 >= PP_T(PP_ARM_MS)) {
        ui.pr.arm = 0;
        ui.pr.sig = 0;
    }
}

/* EDIT pressed (ui_input, before it opens its page; not in step record): the page it leaves, kept for a hold */
static void edit_down(uint32_t now)
{
    ui.pr.e_st = 1;
    ui.pr.e_t0 = now;
    ui.pr.e_page = ui.page;
    ui.pr.e_home = ui.home;
    ui.pr.e_hmode = ui.hmode;
    ui.pr.e_snd = ui.fam_last[FAM_SND];
    ui.pr.e_lay = ui.fam_last[FAM_LAY];
}
/* every frame after the buttons: EDIT held PP_HOLD_MS since its press: the page before it, the list over it */
static void edit_hold(uint32_t now)
{
    if (ui.pr.e_st != 1u || now - ui.pr.e_t0 < PP_T(PP_HOLD_MS))
        return;
    ui.pr.e_st = 2;
    ui.page = ui.pr.e_page;
    ui.home = ui.pr.e_home;
    ui.hmode = ui.pr.e_hmode;
    ui.fam_last[FAM_SND] = ui.pr.e_snd;
    ui.fam_last[FAM_LAY] = ui.pr.e_lay;
    page_entered();
    preset_list_open();
}

static void pl_arm(uint32_t what)                      /* SAVE over (1) / DELETE (2) armed on the cursor */
{
    ui.pr.arm = (uint8_t)what;
    ui.pr.arm_id = ui.pr.cur;
    ui.pr.arm_t0 = ui.pr.now;
}
static int pl_armed(uint32_t what) { return ui.pr.arm == what && ui.pr.arm_id == ui.pr.cur; }

/* OCT+: the preset under the cursor onto the track, kept (the audition is the track's now), the list closed (refused:
 * an empty slot, ACID on 2 other tracks: the list stays) */
static void pl_load(void)
{
    preset_t p;
    char nb[9];
    ui.pr.arm = 0;
    if (ui.pr.pv && ui.pr.pv_trk != song.sel)
        pl_restore();
    if (preset_load(TSEL, ui.pr.cur) < 0) {
        ui_message(!preset_get(ui.pr.cur, &p) ? "EMPTY SLOT" : "ACID: 2 TRACKS MAX");
        return;
    }
    ui.pr.pv = 0;
    pl_close();
    preset_name_of(ui.pr.cur, nb);
    ui_say("LOADED ", nb);
}

/* SAVE: the track's own voice (an audition put back first) into the user slot under the cursor (used: armed first),
 * elsewhere the first free one; the track's preset now, not edited; the cursor on it */
static void pl_save(void)
{
    uint32_t slot;
    char nb[9];
    if (preset_bank_of(ui.pr.cur) == PB_USER) {
        slot = ui.pr.cur - PRE_NFACT;
        if (upr.s[slot].used == 1u && !pl_armed(1u)) {
            pl_arm(1u);
            return;
        }
    } else {
        int f = preset_user_free();
        if (f < 0) {
            ui.pr.arm = 0;
            ui_message("USER BANK FULL");
            return;
        }
        slot = (uint32_t)f;
    }
    ui.pr.arm = 0;
    pl_restore();
    if (preset_user_save(slot, TSEL) < 0)
        return;
    TSEL->pre = (uint16_t)(PRE_NFACT + slot);
    TSEL->pre_ed = 0;
    ui.pr.cur = TSEL->pre;
    preset_name_of(TSEL->pre, nb);
    if (transport_busy())
        ui_say(nb, ": FLASH AT STOP");                 /* (in the list now, written at the next STOP) */
    else
        ui_say("SAVED ", nb);
}

/* OCT-: the used user slot under the cursor emptied (armed first); tracks loaded from it keep their knobs */
static void pl_delete(void)
{
    uint32_t slot = ui.pr.cur - PRE_NFACT;
    char nb[9];
    if (preset_bank_of(ui.pr.cur) != PB_USER) {
        ui.pr.arm = 0;
        ui_message("ONLY USER PRESETS");
        return;
    }
    if (upr.s[slot].used != 1u) {
        ui.pr.arm = 0;
        ui_message("EMPTY SLOT");
        return;
    }
    if (!pl_armed(2u)) {
        pl_arm(2u);
        return;
    }
    ui.pr.arm = 0;
    preset_name_of(ui.pr.cur, nb);
    preset_user_delete(slot);
    ui_say("DELETED ", nb);
}

/* KNOB 2 / SELECT: the next / previous bank (wrapping), the cursor on its first */
static void pl_bank(int32_t dir)
{
    uint32_t b = preset_bank_of(ui.pr.cur);
    b = (b + (dir > 0 ? 1u : PB_N - 1u)) % PB_N;
    pl_move((int32_t)PRE_BANK_START[b]);
}

/* one UI frame of the list (ui_input): button edges, note edges, HOME's tap / hold */
static void preset_list_input(uint32_t pressed, uint32_t notes, uint32_t home)
{
    uint32_t k;
    int32_t s;
    song.seq_mode = 0;
    if (home) {                                        /* HOME: back */
        pl_close();
        return;
    }
    settings_poll();                                   /* (a save left for the STOP: written once stopped) */
    if ((pressed >> panel.btn[B_PLAY]) & 1u)
        play_press();
    if (ui.rec_t0)
        ui.rec_t0 |= 2u;                               /* (REC: nothing here) */
    for (k = 0; k < NTRK; k++)                         /* a white key: its track (it plays it too) */
        if ((notes >> KEY_TRK_KEY[k]) & 1u)
            pl_track(k, KEY_TRK_KEY[k] + 1u);
    if ((s = panel_enc(EN_ALGO)) != 0)
        pl_track((uint32_t)clamp((int32_t)song.sel + (s > 0 ? 1 : -1), 0, NTRK - 1), 0u);
    if ((s = panel_enc(EN_PRESET)) != 0)              /* one preset per detent */
        pl_move((int32_t)ui.pr.cur + s);
    if ((s = panel_enc(EN_K1)) != 0)
        pl_move((int32_t)ui.pr.cur + accel(EN_K1, s, (int32_t)PRE_NID));
    if ((s = panel_enc(EN_K2)) != 0)
        pl_bank(s);
    if ((s = panel_enc(EN_SELECT)) != 0)
        pl_bank(s);
    enc_drop();
    if ((pressed >> panel.btn[B_EDIT]) & 1u) {        /* EDIT: back */
        pl_close();
        return;
    }
    if ((pressed >> panel.btn[B_OCTUP]) & 1u) {
        pl_load();
        if (!pl_on())
            return;
    }
    if ((pressed >> panel.btn[B_SAVE]) & 1u)
        pl_save();
    if ((pressed >> panel.btn[B_OCTDN]) & 1u)
        pl_delete();
}

/* the popup's rows: the preset in the middle, the two loadable ones before and after it (preset_step) */
static void pp_rows(uint16_t *r)
{
    r[2] = ui.pr.pop_id;
    r[1] = (uint16_t)preset_step(TSEL, r[2], -1);
    r[0] = (uint16_t)preset_step(TSEL, r[1], -1);
    r[3] = (uint16_t)preset_step(TSEL, r[2], 1);
    r[4] = (uint16_t)preset_step(TSEL, r[3], 1);
}
/* a preset's name (8) and voice's, "" for an empty slot */
static int pre_label(uint32_t id, char *nb, const char **voice)
{
    preset_t p;
    if (!preset_get(id, &p)) {
        nb[0] = 0;
        *voice = "";
        return 0;
    }
    preset_name_of(id, nb);
    *voice = N_MODEL[p.model % NMODELS];
    return 1;
}

/* the popup (ui_draw, in the graph's place): a frame; the bank (amber) and the track; the preset large between two
 * neighbours above and below (smaller, dimmer the further), each with its voice at the right; a rule where a bank
 * ends */
static void draw_popup(void)
{
    static const int8_t RY[5] = {20, 37, 54, 89, 105};
    char nb[9], tn[3] = {'T', (char)('1' + song.sel), 0};
    const char *vo;
    uint32_t i, sig = 2166136261u + song.sel * 7919u;
    uint16_t r[5];
    pp_rows(r);
    for (i = 0; i < 5u; i++)
        sig = (sig ^ r[i]) * 16777619u;
    for (i = 0; i < 5u; i++)
        ui.pr.shown[i] = r[i];
    if (!ui.force && sig == ui.pr.sig)
        return;
    ui.pr.sig = sig;
    cv_begin(240, H_GRAPH, C_BLACK);
    cv_oy = 0;
    cv_rect(0, 0, 240, 1, C_LINE);
    cv_rect(0, H_GRAPH - 1, 240, 1, C_LINE);
    cv_rect(0, 0, 1, H_GRAPH, C_LINE);
    cv_rect(239, 0, 1, H_GRAPH, C_LINE);
    cv_text(8, 2, &FONT_S, N_PBANK[preset_bank_of(r[2]) % PB_N], C_AMB);
    cv_text(232 - text_w(&FONT_S, tn), 2, &FONT_S, tn, C_GRAY);
    for (i = 0; i < 5u; i++) {
        uint16_t ink = i == 2u ? C_WHITE : i == 1u || i == 3u ? C_GRAY : C_DIM;
        int32_t y = RY[i];
        if (i != 2u && r[i] == r[2])
            continue;                                  /* (nothing else loadable) */
        pre_label(r[i], nb, &vo);
        if (i == 2u) {
            cv_rect(3, y + 2, 3, 28, C_AMB);
            cv_text(10, y, &FONT_L, nb, ink);
            cv_text(232 - text_w(&FONT_S, vo), y + 12, &FONT_S, vo, C_HI);
        } else {
            cv_text(10, y, &FONT_S, nb, ink);
            cv_text(232 - text_w(&FONT_S, vo), y, &FONT_S, vo, C_DIM);
        }
        if (i && preset_bank_of(r[i]) != preset_bank_of(r[i - 1u]))   /* a bank's end between them */
            cv_rect(8, (RY[i - 1u] + (i - 1u == 2u ? 32 : 16) + y) / 2, 224, 1, C_LINE);
    }
    cv_blit(0, Y_GRAPH);
}

/* the list (its own screen, as NAME): the title (a message, or what an armed button does, while shown) and the
 * cursor's place in its bank; the banks as tabs (the cursor's lit); seven rows of presets (name, voice; an empty user
 * slot "EMPTY nn"; the track's own marked amber; the cursor's inverted; a rule where a bank ends); the controls */
static void draw_plist(void)
{
    char b[32], nb[9];
    const char *vo;
    uint32_t i, top, bank = preset_bank_of(ui.pr.cur) % PB_N, sig, used, own = pl_own();
    int32_t x;
    top = (uint32_t)clamp((int32_t)ui.pr.cur - 3, 0, (int32_t)(PRE_NID - PL_ROWS));
    used = bank == PB_USER && upr.s[ui.pr.cur - PRE_NFACT].used == 1u;
    sig = 37199u + ui.pr.cur * 31u + song.sel * 7919u + own * 131u + ui.pr.arm * 104729u + used * 3u +
          (ui.msg_t ? str_hash(5u, ui.msg) : 0u);
    for (i = 0; i < PL_ROWS; i++) {
        const upr_slot_t *u = &upr.s[(top + i - PRE_NFACT) % PRE_NUSER];
        if (top + i >= PRE_NFACT)
            sig = (sig ^ (u->used | (uint32_t)u->model << 8)) * 16777619u;
    }
    if (ui.msg_t)                                      /* the title shows a message for a while */
        ui.msg_t--;
    if (!ui.force && sig == ui.pr.sig)
        return;
    ui.pr.sig = sig;
    if (ui.force)
        lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.force = 0;
    cv_begin(240, 46, C_BLACK);                        /* the title and the banks */
    cv_oy = 0;
    if (ui.pr.arm) {
        cv_text(4, 4, &FONT_S, ui.pr.arm == 1u ? "SAVE AGAIN: OVERWRITE" : "OCT- AGAIN: DELETE", C_AMB);
    } else if (ui.msg_t) {
        cv_text(4, 4, &FONT_S, ui.msg, C_HI);
    } else {
        str_cpy(b, "PRESETS  T1", sizeof b);
        b[10] = (char)('1' + song.sel);
        cv_text(4, 4, &FONT_S, b, C_GRAY);
        fmt_int(b, (int32_t)(ui.pr.cur - PRE_BANK_START[bank] + 1u));
        str_cpy(b + str_len(b), "/", 4);
        fmt_int(b + str_len(b), (int32_t)(PRE_BANK_START[bank + 1u] - PRE_BANK_START[bank]));
        cv_text(236 - text_w(&FONT_S, b), 4, &FONT_S, b, C_DIM);
    }
    for (i = 0, x = 4; i < PB_N; i++) {
        int32_t w = text_w(&FONT_S, PB_TAB[i]);
        cv_text(x, 24, &FONT_S, PB_TAB[i], i == bank ? C_HI : C_DIM);
        if (i == bank)
            cv_rect(x, 42, w, 2, C_HI);
        x += w + 14;
    }
    cv_rect(0, 45, 240, 1, C_LINE);
    cv_blit(0, 0);
    for (i = 0; i < PL_ROWS; i++) {                    /* the rows, in two strips (the canvas: CV_MAX) */
        uint32_t id = top + i, cur = id == ui.pr.cur;
        int32_t y = (int32_t)(i % PL_HALF) * PL_RH;
        int full = pre_label(id, nb, &vo);
        uint16_t ink = cur ? C_BLACK : full ? C_HI : C_DIM;
        if (i % PL_HALF == 0u)
            cv_begin(240, (PL_ROWS - i < PL_HALF ? PL_ROWS - i : PL_HALF) * PL_RH, C_BLACK);
        if (cur)
            cv_rect(0, y, 240, PL_RH, C_HI);
        if (i && preset_bank_of(id) != preset_bank_of(id - 1u))
            cv_rect(0, y, 240, 1, C_LINE);
        if (id == own)
            cv_rect(4, y + 7, 6, 6, cur ? C_BLACK : C_AMB);   /* the track's own */
        if (!full) {
            str_cpy(b, id - PRE_NFACT < 9u ? "EMPTY 0" : "EMPTY ", sizeof b);   /* (as the names: "EMPTY 05") */
            fmt_int(b + str_len(b), (int32_t)(id - PRE_NFACT + 1u));
            cv_text(16, y + 2, &FONT_S, b, ink);
        } else {
            cv_text(16, y + 2, &FONT_S, nb, ink);
            cv_text(232 - text_w(&FONT_S, vo), y + 2, &FONT_S, vo, cur ? C_BLACK : C_GRAY);
        }
        if (i + 1u == PL_ROWS || (i + 1u) % PL_HALF == 0u)
            cv_blit(0, 48u + (i / PL_HALF) * PL_HALF * PL_RH);
    }
    cv_begin(240, 52, C_BLACK);                        /* the controls (the knobs move and pick the bank, unsaid) */
    cv_rect(0, 0, 240, 1, C_LINE);
    str_cpy(b, bank != PB_USER ? "SAVE: NEW SLOT" : used ? "SAVE: OVERWRITE" : "SAVE: HERE", sizeof b);
    x = (240 - text_w(&FONT_S, "OCT+ LOAD   ") - text_w(&FONT_S, b)) / 2;
    x = cv_text(x, 7, &FONT_S, "OCT+ LOAD   ", C_WHITE);
    cv_text(x, 7, &FONT_S, b, C_WHITE);
    x = (240 - text_w(&FONT_S, "OCT- DELETE   EDIT BACK")) / 2;
    x = cv_text(x, 29, &FONT_S, "OCT- DELETE   ", used ? C_WHITE : C_DIM);
    cv_text(x, 29, &FONT_S, "EDIT BACK", C_GRAY);
    cv_blit(0, 48 + PL_ROWS * PL_RH);
}
