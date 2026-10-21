/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Drum UI input: LEDs, knobs and buttons, the STEP grid on the keys, panel setup. */
/* ----------------------------------------------------------- LEDs --- */
/* The LED picture is built off-line and copied one byte per column: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker.
 * BACKLIGHT ON (spec 2026-10-11-fixes §12): one word per column instead, the full layer and the 25 % / 5 % ones,
 * which the scan's hook composes (led_dim.c). */
#include "led_dim.c"
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

/* fixes 2026-10-21 item 1: OCT+ where the keys are steps: 1 pressed (let go alone: the next bank), 2 a step key
 * accented with it (let go: nothing); 0 not held there (ui_input acc_oct_poll) */
static uint8_t acc_oct;
/* fixes 2026-10-21 item 2: the keys show the selected track (its white key lit, as the keys select it there) on the
 * HOME screen and every page of the EDIT (SOUND, LAYER), FX and LFO families; not under KEYS (the keyboard's), the
 * menu, NAME, the PRESETS list, the MUTE screen or PERFORM (ui_leds tests those first) */
static int track_keys(void)
{
    uint32_t f = cur_fam();
    return !ui.menu && !keys_mode() && (ui.home || f == FAM_SND || f == FAM_LAY || f == FAM_FX || f == FAM_LFO);
}

#include "ui_name.c"                               /* NAME: naming a project */
#include "ui_learn.c"                              /* MIDI LEARN: GLO + G4, a knob, a CC */

/* fixes 2026-10-21 item 5 (upstream Felucca's breath, #119): the OCT LEDs, OCT_LIT_* lit, OCT_BR_* breathing (dark ..
 * ~60 % and back, led_dim.c: it would do something if pressed); a way back (NO, CANCEL, BACK) lit; elsewhere dark.
 * NAME: OCT- cancels, OCT+ writes; the PRESETS list: OCT+ LOAD, OCT- DELETE where it would (a used user slot); a
 * dialog: OCT- NO, OCT+ YES; the menu and the Bluetooth page: OCT- BACK, OCT+ OK (ABOUT: OCT- back alone); MIDI LEARN:
 * OCT- CLEAR, OCT+ DONE; KEYS: lit off octave 0, as before (where it is); the steps: each breathes while there is a
 * bank that way, OCT+ lit while held (its accent: item 1); SOUND: OCT+ (LAYER); LAYER: OCT- (back to SOUND); the LFO
 * pages: OCT+ (a RATE MODE) */
#define OCT_LIT_DN 1u
#define OCT_LIT_UP 2u
#define OCT_BR_UP 4u
#define OCT_BR_DN 8u
static uint32_t oct_leds(void)
{
    if (ui.mute || ui.layer)
        return 0;
    if (name_on())
        return OCT_LIT_DN | OCT_BR_UP;
    if (pl_on())
        return OCT_BR_UP | (preset_bank_of(ui.pr.cur) == PB_USER && upr.s[ui.pr.cur - PRE_NFACT].used == 1u ? OCT_BR_DN
                                                                                                          : 0u);
    if (ui.confirm)
        return OCT_LIT_DN | OCT_BR_UP;
    if (ui.menu)
        return ui.menu == 2u ? OCT_LIT_DN : OCT_LIT_DN | OCT_BR_UP;
    if (ui.ml.on)
        return OCT_LIT_DN | OCT_BR_UP;
    if (keys_mode())
        return (song.octave < 0 ? OCT_LIT_DN : 0u) | (song.octave > 0 ? OCT_LIT_UP : 0u);
    if (step_keys())
        return (ui.bank > 0u ? OCT_BR_DN : 0u) |
               (acc_oct ? OCT_LIT_UP : ui.bank + 1u < bank_count() ? OCT_BR_UP : 0u);
    if (!ui.home && cur_fam() == FAM_SND)
        return OCT_BR_UP;
    if (!ui.home && cur_fam() == FAM_LAY)
        return OCT_LIT_DN;
    if (!ui.home && cur_fam() == FAM_LFO)             /* LFO: OCT+ the RATE MODE of the LFO turned last (item 3); */
        return OCT_BR_UP;                             /* OCT- does nothing there */
    return 0;
}

/* fixes 2026-10-21 item 9 (upstream Felucca's LEDS levels; MENU BACKLIGHT, panel.c BL_*): the picture nl (lit), nb
 * (breathing), beats (the STEP keys' beats, bit k = key k) out to the LEDs. OFF and nothing breathing: fm1_led as ever,
 * the scan's hook idle; else the hook's layers (led_dim.c), one store per column: DIM LO / HI the unlit keys and the
 * idle buttons in the glow, the unlit beats in the mid layer; INV / INV LO / INV HI the idle ones lit / in the mid
 * layer and the lit ones dark; a breathing LED only breathes (neither lit nor in the glow). The levels: the layers'
 * frames of 20 */
static const uint32_t BL_MID_PH[BL_COUNT] = {0u, LED_PH_25, 0x22489u, 0u, 0x401u, LED_PH_25};   /* 25 30 - 10 25 % */
static const uint32_t BL_GLOW_PH[BL_COUNT] = {0u, 0x1u, 0x401u, 0u, 0u, 0u};                   /* 5 10 % */
static void leds_out(uint8_t *nl, const uint8_t *nb, uint32_t beats)
{
    uint32_t lay[FM1_NCOL] = {0}, k, c, br = 0, m = backlight < BL_COUNT ? backlight : BL_OFF;
    for (c = 0; c < FM1_NCOL; c++) {
        nl[c] &= (uint8_t)~nb[c];
        br |= nb[c];
    }
    if (m == BL_OFF && !br) {
        led_dim_on = 0;                               /* (before fm1_led: the hook stops composing) */
        led_br_on = 0;
        for (c = 0; c < FM1_NCOL; c++)
            fm1_led[c] = nl[c];
        return;
    }
    for (k = 0; k < 41u; k++) {                       /* the buttons (0..13) and the keys (14..40) */
        uint8_t q = led_pos[k];
        uint32_t bit, lit;
        if (q == 0xFF)
            continue;
        bit = 1u << (q & 7u);
        lit = nl[q >> 3] & bit;
        if (nb[q >> 3] & bit)
            lay[q >> 3] |= bit << 24;                 /* breathing */
        else if (m == BL_DIM_LO || m == BL_DIM_HI)
            lay[q >> 3] |= lit ? bit : k >= 14u && ((beats >> (k - 14u)) & 1u) ? bit << 8 : bit << 16;
        else if (m == BL_INV)
            lay[q >> 3] |= lit ? 0u : bit;
        else if (m >= BL_INV_LO)
            lay[q >> 3] |= lit ? 0u : bit << 8;
        else
            lay[q >> 3] |= lit;
    }
    led_mid_ph = BL_MID_PH[m];
    led_glow_ph = BL_GLOW_PH[m];
    led_br_on = br != 0u;
    for (c = 0; c < FM1_NCOL; c++)
        led_lay[c] = lay[c];                          /* one store per column: the scan sees all of it or none */
    led_dim_on = 1;                                   /* (after the layers: the hook composes from them) */
}

static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nb[FM1_NCOL] = {0};
    uint32_t k, fam = cur_fam(), beats = 0;           /* beats: the keys of steps 1 5 9 13 (step keys) */
    static uint8_t ready;
    if (safe_start) {                                 /* safe start: LEDs off */
        uint32_t c0;
        led_dim_on = 0;
        for (c0 = 0; c0 < FM1_NCOL; c0++)
            fm1_led[c0] = 0;
        return;
    }
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    led_put(nl, panel.btn[FAM_BTN[fam]],                /* LAYER: EDIT blinks, 250 ms on / off */
            fam != FAM_LAY || ((fm1_ticks() / (250u * 1000u * FM1_TICKS_PER_US)) & 1u));
    led_put(nl, panel.btn[B_PLAY], song.playing && ((song.tick / 64u) & 1u) == 0u);   /* blinks: intended */
    led_put(nl, panel.btn[B_REC], song.rec != 0u || ui.srec);
    led_put(nl, panel.btn[B_SCL], ui.keys);           /* KEYS (spec §5) */
    if (ui.mute) {                                    /* MUTE screen: the tracks' keys lit while audible, blinking
                                                       * while a mute waits for the bar; top C# (NOW) / D# (NEXT BAR) */
        int blink = (int)((fm1_ticks() / (250u * 1000u * FM1_TICKS_PER_US)) & 1u);
        for (k = 0; k < NTRK; k++)
            led_put(nl, 14u + KEY_TRK_KEY[k], !trk[k].p[P_MUTE] ^ (((song.mute_q >> k) & 1u) && blink));
        led_put(nl, 14u + 20u, !settings.mutebar);
        led_put(nl, 14u + 22u, settings.mutebar);
        led_put(nl, 14u + KEY_LEARN, ui.ml.on);       /* G4: MIDI LEARN */
        led_put(nl, panel.btn[B_GLO], 1);
    } else if (name_on()) {                           /* NAME: the keys that type */
        uint32_t m = name_leds(fm1_ticks());
        for (k = 0; k < 27u; k++)
            led_put(nl, 14u + k, (int)((m >> k) & 1u));
    } else if (pl_on()) {                             /* the PRESETS list: the keys play (lit while held) */
        for (k = 0; k < 27u; k++)
            led_put(nl, 14u + k, (int)((fm1_in.notes >> k) & 1u));
    } else if (ui.layer) {                            /* PERFORM: effects blink, held lit, unavailable dark; the
                                                       * track keys lit while their track sounds */
        int blink = (int)((fm1_ticks() / (250u * 1000u * FM1_TICKS_PER_US)) & 1u);
        uint32_t held = perf_kill ? 0u : perf_held, ok = perf_avail();
        for (k = 0; k < 27u; k++) {
            uint32_t e = perf_key(k);
            led_put(nl, 14u + k, e < PF_M1 ? (int)(((ok >> e) & 1u) && (((held >> e) & 1u) || blink))
                               : e < PF_N ? (int)(!((held >> e) & 1u) && !trk[e - PF_M1].p[P_MUTE]) : 0);
        }
    } else if (step_keys()) {                         /* the bank's steps; the playhead inverted */
        const track_t *t = TSEL;
        for (k = 0; k < 16u; k++) {
            uint32_t si = ui.bank * 16u + k;
            int on = si < view_len(t) && view_step(t, si).on && (!acc_oct || view_step(t, si).acc);   /* OCT+ held:
                                                       * the accents */
            if (song.playing && si == view_idx(t))
                on = !on;
            led_put(nl, 14u + STEP_KEY[k], on);
            if ((k & 3u) == 0u)
                beats |= 1u << STEP_KEY[k];
        }
    } else if (comp_mode()) {                         /* COMP: the ducked tracks' keys */
        for (k = 0; k < NTRK; k++)
            led_put(nl, 14u + KEY_TRK_KEY[k], trk[k].p[P_DUCK] && k != comp_src());
    } else if (track_keys()) {                        /* HOME, EDIT, FX, LFO: the selected track's key (item 2) */
        led_put(nl, 14u + KEY_TRK_KEY[song.sel % NTRK], 1);
        for (k = 0; k < 27u; k++)
            led_put(nl, 14u + k, (int)((fm1_in.notes >> k) & 1u));
    } else if (keys_mode()) {                         /* KEYS: the keys held lit (OCT- / OCT+: oct_leds) */
        for (k = 0; k < 27u; k++)
            led_put(nl, 14u + k, (int)((fm1_in.notes >> k) & 1u));
    } else {
        for (k = 0; k < 27u; k++)
            led_put(nl, 14u + k, (int)((fm1_in.notes >> k) & 1u));
    }
    k = oct_leds();                                   /* OCT- / OCT+: lit, or breathing (item 5) */
    led_put(nl, panel.btn[B_OCTDN], (int)(k & OCT_LIT_DN));
    led_put(nl, panel.btn[B_OCTUP], (k & OCT_LIT_UP) != 0u);
    led_put(nb, panel.btn[B_OCTUP], (k & OCT_BR_UP) != 0u);
    led_put(nb, panel.btn[B_OCTDN], (k & OCT_BR_DN) != 0u);
    leds_out(nl, nb, beats);
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

/* a step key held + KNOB 1: its PROB, KNOB 2: its RATCH. The first turn of a hold undoes the press's on / off: the
 * step is as before the press, and on (pressed with OCT+: as the accent left it). A held key outside LEN takes the
 * turn and does nothing. */
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
        s->tie = 0;
        if (!ui.step_prev[k].on)
            s->nt = new_step_nt(TSEL);
        ui.step_t0[k] |= 6u;                          /* edited: the press undone once */
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
        ml_knob(song.sel, P_LEVEL);                 /* (MIDI LEARN: picked) */
        return;
    }
    if (slot == 2u && song_lock())                  /* LEN: a song's row */
        return;
    id = slot == 1u ? P_LEVEL : slot == 2u ? P_SLEN : P_PAN;
    t->p[id] = (int16_t)clamp(t->p[id] + accel(EN_K1 + slot, steps, TP[id].max - TP[id].min), TP[id].min, TP[id].max);
    motion_capture(t, id);                          /* motion.c: LEVEL / PAN recorded (LEN never) */
    ml_knob(song.sel, id);                          /* (MIDI LEARN: picked) */
}

static void mute_bar_set(uint32_t on)                  /* the mutes: at once / on the next bar (device setting) */
{
    settings.mutebar = on & 1u;
    song.act[2] = (int16_t)settings.mutebar;
    settings_save();
    ui_message(settings.mutebar ? "MUTE: NEXT BAR" : "MUTE: NOW");
}

static void track_rec_toggle(uint32_t k)               /* arm / disarm track k (REC, every screen; the SMC-Mixer);
                                                        * arming while stopped starts play */
{
    uint8_t bit = (uint8_t)(1u << k);
    song.rec ^= bit;
    if ((song.rec & bit) && !song.playing)
        transport_req = 1;
}

/* MOTION page: KNOB 1 PLAY (right ON, left OFF), KNOB 4 CLEAR (a turn right asks: "CLEAR MOTION T<n>?") */
static void motion_page_edit(uint32_t slot, int32_t steps)
{
    if (slot == 0u) {
        motion_set_play(song.sel, steps > 0);
    } else if (slot == 3u && steps > 0 && motion_count(song.sel)) {
        ui.confirm = 2;
        ui.confirm_trk = song.sel;
        ui.force = 1;
    }
}

/* SONG page: KNOB 1 ROW (up to the + row), 2 SLOT, 3 REPEAT, 4 LOOP; on the + row a turn of 2 / 3 adds a row */
static void song_edit(uint32_t slot, int32_t steps)
{
    chain_config_t *c = &chain.cfg;
    uint32_t r;
    if (ui.song_row > c->count)                       /* (a LOAD brought a shorter song) */
        ui.song_row = c->count;
    r = ui.song_row;
    if (slot == 0u) {
        ui.song_row = (uint8_t)clamp((int32_t)r + steps, 0, c->count < CHAIN_ROWS ? c->count : CHAIN_ROWS - 1u);
        return;
    }
    if (song_lock())
        return;
    if (slot == 3u) {
        c->loop = steps > 0;
        return;
    }
    if (r >= c->count) {                              /* the + row: a row at the end */
        r = c->count;
        c->row[r].slot = r ? c->row[r - 1u].slot : 0u;
        c->row[r].repeat = 1;
        c->count = (uint8_t)(r + 1u);
        return;
    }
    if (slot == 1u)
        c->row[r].slot = (uint8_t)clamp((int32_t)c->row[r].slot + steps, 0, 3);
    else
        c->row[r].repeat = (uint8_t)clamp((int32_t)c->row[r].repeat + steps, 1, 16);
}
static void song_play(void)                         /* PLAY on SONG */
{
    uint32_t rc = chain_prepare();
    if (rc >= 3u) {
        char b[16] = "PATTERN A EMPTY";
        b[8] = (char)('A' + rc - 3u);
        ui_message(b);
    } else if (rc == 1u) {
        ui_message("ADD A SONG ROW");
    } else if (rc == 2u) {
        ui_message("STOP FIRST");
    }
    ui.force = 1;
}
static void song_rec_hold(void)                     /* REC held on SONG: delete the row / clear the song */
{
    if (song_lock())
        return;
    if (ui.song_row < chain.cfg.count)
        ui.confirm = 3;
    else if (chain.cfg.count)
        ui.confirm = 4;
    else
        return;
    ui.confirm_trk = ui.song_row;
    ui.force = 1;
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
    if (pg->graph == GR_MOTION) {                     /* the MOTION page's own knobs */
        if (!song_lock())
            motion_page_edit(slot, steps);
        return;
    }
    if (pg->graph == GR_SONG) {
        song_edit(slot, steps);
        return;
    }
    if (pg->graph == GR_LDEST && slot == 0u) {        /* LFO DEST: KNOB 1 the slot */
        song.lslot = (uint8_t)clamp((int32_t)(song.lslot & 3u) + steps, 0, (int32_t)LS_N - 1);
        return;
    }
    if (pg->graph == GR_LFO)
        ui.lfo_l = (uint8_t)(slot >> 1);              /* OCT+: this LFO's RATE MODE */
    if (pg->scope == SC_MIX) {
        tracks_edit(slot, steps);
        return;
    }
    if (pg->scope == SC_TRACK && id == P_MODEL) {     /* a model comes with its default sound */
        model_step(steps);
        ml_knob(song.sel, P_MODEL);                   /* (MIDI LEARN: NOT LEARNABLE) */
        return;
    }
    if (pg->graph == GR_STEPS && song_lock())         /* PATTERN: a song's row */
        return;
    if (pg->graph == GR_SLOTS && id == G_NAME) {      /* PROJECT, NAME: rename the selected slot */
        if (steps > 0)
            name_rename();
        return;
    }
    d = page_desc(pg, slot, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    if (pg->scope == SC_GLOBAL && id == G_BPM && song.g[G_CLOCK]) {
        ui_say("CLK ", N_CLOCK[song.g[G_CLOCK]]);
        return;
    }
    v = param_turn(d, *vp, accel(EN_K1 + slot, steps, d->max - d->min));
    if (pg->scope == SC_TRACK && id == P_RMODEL && v != *vp) {   /* a capped model full: the next one (or why not) */
        int32_t dir = v > *vp ? 1 : -1;
        const char *why = reson_refused(TSEL, *vp, v);
        while (why && v + dir >= d->min && v + dir <= d->max) {
            v += dir;
            why = reson_refused(TSEL, *vp, v);
        }
        if (why) {
            ui_message(why);
            return;
        }
    }

    if (pg->scope == SC_TRACK && vp >= TSEL->p && vp < TSEL->p + P_COUNT && PRE_SOUND(vp - TSEL->p) && v != *vp)
        preset_touch(TSEL);                           /* presets.c: the voice edited (not a turn at its limit) */
    *vp = (int16_t)v;
    if (pg->scope == SC_TRACK && vp >= TSEL->p && vp < TSEL->p + P_COUNT) {
        motion_capture(TSEL, (uint32_t)(vp - TSEL->p));   /* motion.c: recorded when armed and playing */
        ml_knob(song.sel, (uint32_t)(vp - TSEL->p));      /* (MIDI LEARN: picked) */
    }
    if (pg->scope == SC_GLOBAL && id == G_MUTEBAR) {
        mute_bar_set((uint32_t)v);
        return;
    }
    if (pg->scope == SC_GLOBAL && id == G_METRO) {    /* METRO: the device setting (fx.c's click), saved */
        fx_metro = (uint8_t)v;
        settings_save();
        return;
    }
    if (!v || pg->scope != SC_GLOBAL)
        return;
    if ((id == G_CLRSEQ || id == G_CLRALL || id == G_INITALL) && song_lock()) {
        *vp = 0;
        return;
    }
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
    case G_SAVE:                                      /* (with the current name; NAME renames: ui_name.c) */
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
        for (id = P_FTYPE; id <= P_FDEC; id++)
            TSEL->p[id] = TP[id].def;                   /* the FILTER too (spec 2026-10-08 §3) */
        motion_rebase(song.sel);
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

/* PERFORM (perform.c): FX tapped = its pages, on the release (nothing else touched); FX held = the layer: keys
 * pressed with it are its own (seq.c keyboard_block), KNOB 1..4 its macros FILTER CRUSH THROW DEPTH (never saved;
 * spec §7: they stay on when the screen goes, until turned back to 0 on it, INIT ALL or BENCH). It opens at once
 * with a key or a knob, else after FX_HOLD_MS (then the release does nothing). No layer in the menu, a dialog or the
 * MUTE screen: a press there stays dead until let go, and every key effect is off until its keys are let go */
#define FX_HOLD_MS 400u
#define FX_DOWN 1u
#define FX_OPEN 2u
#define FX_DEAD 4u
static int fx_allowed(void) { return !ui.menu && !ui.confirm && !name_on() && !ui.mute && !pl_on(); }
/* fixes 2026-10-21 item 7: PERFORM has KNOB 1..4 (fx_layer reads them; set each pass): the page's loop leaves them
 * alone, so detents that come in after fx_layer's read wait for its next one instead of reaching the page under it.
 * 1 PERFORM's; 2 the pass it stops being (FX let go): the detents read then are still the macros', the page's loop
 * still leaves them (a knob turning while FX is let go moves nothing under it); 0 the page's */
static uint8_t fx_knobs;
/* PERFORM PAGE: the screen closed (the screen under it changed, HOME, the menu, a dialog): the macros stay on; keys
 * still held stay the layer's until let go */
static void perf_page_close(void)
{
    ui.pg_open = 0;
    perf_mask &= ~PERF_PAGE;
}
static void fx_layer(uint32_t now, int home_tap)
{
    uint32_t bit = 1u << panel.btn[B_FX], *t0 = &ui.fx_t0, k, show, down = (fm1_in.buttons & bit) != 0u;
    int32_t s;
    if (!fx_allowed()) {
        perf_kill = 1;                                  /* the buffer effects off (the macros stay on: spec §7) */
    } else if (!kb_layer) {
        perf_kill = 0;
    }
    if (ui.pg_open && (!fx_allowed() || home_tap || ui.page != ui.pg_page || ui.home != ui.pg_home))
        perf_page_close();
    if (down) {
        if (!*t0)
            *t0 = (now & ~7u) | FX_DOWN;
        if (!fx_allowed())
            *t0 = (*t0 | FX_DEAD) & ~FX_OPEN;           /* dead until let go: no map, no layer, no PAGE */
        if (!(*t0 & FX_DEAD) && (kb_layer || now - (*t0 & ~7u) >= FX_HOLD_MS * 1000u * FM1_TICKS_PER_US))
            *t0 |= FX_OPEN;
    } else if (*t0) {
        if (!(*t0 & (FX_OPEN | FX_DEAD)))
            open_family(FAM_FX);                        /* a tap: the FX pages, as the button always did */
        *t0 = 0;
    }
    fx_knobs = (down && !(*t0 & FX_DEAD)) || ui.pg_open || (kb_layer && fx_allowed()) ? 1u : fx_knobs == 1u ? 2u : 0u;
    if (fx_knobs)                                       /* the knobs follow the screen: */
        for (k = 0; k < 4u; k++)                        /* the macros (FILTER CRUSH THROW DEPTH) while it is PERFORM's */
            if ((s = panel_enc(EN_K1 + k)) != 0) {
                int32_t v = clamp(perf_k[k] + accel(EN_K1 + k, s, 200), k ? 0 : -100, 100);
                if (!k && perf_k[0] && (v < 0) != (perf_k[0] < 0))
                    v = 0;                              /* FILTER: a turn across 0 stops at 0, its off point */
                perf_k[k] = (int8_t)v;
                ml_pick_act(ML_A_MAC + k);              /* (MIDI LEARN: the macro picked, as a knob's parameter) */
                if (down && !(*t0 & FX_DEAD))
                    *t0 |= FX_OPEN;                     /* (a dead hold stays dead: a held layer key's turn
                                                         * opens no map that outlives the key) */
            }
    if (settings.perfpage && (*t0 & FX_OPEN) && !ui.pg_open && fx_allowed()) {   /* PAGE: the screen stays */
        ui.pg_open = 1;
        ui.pg_page = ui.page;
        ui.pg_home = ui.home;
    }
    perf_mask = (fx_allowed() && !(*t0 & FX_DEAD) ? bit : 0u) | (ui.pg_open ? PERF_PAGE : 0u);   /* armed before FX
                                                         * is pressed: a key struck with it is the layer's even before
                                                         * this pass has seen FX (keyboard_block tests the button) */
    show = fx_allowed() && ((*t0 & FX_OPEN) || kb_layer || ui.pg_open);   /* (the screen gone: the macros stay) */
    if (show != ui.layer) {
        ui.layer = (uint8_t)show;
        ui.force = 1;
    }
}

/* GLO (spec §5): a tap (let go within GLO_HOLD_MS, no key pressed meanwhile) = the GLOBAL pages, on the release; held
 * GLO_HOLD_MS, or a key pressed with it = the MUTE screen while GLO is held: the white keys F3..F4 toggle the tracks'
 * mutes (track_mute_toggle: NOW / NEXT BAR), top C# / D# the mode, G4 MIDI LEARN on / off (ui_learn.c). Every key
 * pressed with GLO is the screen's until let go (seq.c keyboard_block, mute_mask: no note, no MIDI, nothing
 * recorded). Pressed in the menu, NAME or a confirm: dead until let go; with the PERFORM layer up or FX held: a plain
 * button (the release opens GLOBAL) */
#define GLO_HOLD_MS 400u
#define GLO_DOWN 1u
#define GLO_OPEN 2u
#define GLO_DEAD 4u
#define GLO_PLAIN 8u
static int glo_dead(void) { return ui.menu || ui.confirm || name_on() || pl_on(); }
static void mute_keys(uint32_t hits)                   /* the keys the ISR gave the MUTE screen */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if ((hits >> KEY_TRK_KEY[k]) & 1u)
            track_mute_toggle(k);
    if ((hits >> 20) & 1u)                             /* top C#: NOW */
        mute_bar_set(0);
    if ((hits >> 22) & 1u)                             /* top D#: NEXT BAR */
        mute_bar_set(1);
    if ((hits >> KEY_LEARN) & 1u)                      /* G4: MIDI LEARN on / off (ui_learn.c) */
        ml_toggle();
}
static void glo_input(uint32_t now, uint32_t notes)
{
    uint32_t bit = 1u << panel.btn[B_GLO], *t0 = &ui.glo_t0, hits = 0;
    if (kb_mute_hit) {
        fm1_irq_off();
        hits = kb_mute_hit;
        kb_mute_hit = 0;
        fm1_irq_on();
    }
    if (fm1_in.buttons & bit) {
        if (!*t0)
            *t0 = (now & ~15u) | GLO_DOWN;
        if (!(*t0 & GLO_OPEN) && glo_dead())
            *t0 |= GLO_DEAD;
        else if (!(*t0 & GLO_OPEN) && (ui.layer || ui.fx_t0))
            *t0 |= GLO_PLAIN;
        if (!(*t0 & (GLO_OPEN | GLO_DEAD | GLO_PLAIN)) &&
            (notes || hits || now - (*t0 & ~15u) >= GLO_HOLD_MS * 1000u * FM1_TICKS_PER_US)) {
            *t0 |= GLO_OPEN;                            /* the MUTE screen */
            ui.mute = 1;
            ui.force = 1;
            grid_drop_holds();                          /* a grid key held edits nothing under it */
        }
    } else if (*t0) {
        if (!(*t0 & (GLO_OPEN | GLO_DEAD)) && !glo_dead())
            open_family(FAM_GLO);                       /* a tap: the GLOBAL pages */
        *t0 = 0;
        if (ui.mute) {
            ui.mute = 0;                                /* let go: the screen before */
            ui.force = 1;
        }
    }
    if (hits && !glo_dead())
        mute_keys(hits);
    mute_mask = (*t0 & GLO_OPEN) || (!(*t0 & (GLO_DEAD | GLO_PLAIN)) && !glo_dead() && !ui.layer && !ui.fx_t0) ? bit
                                                                                                              : 0u;
}

/* PLAY: start / stop (SONG: the song; PLAY / STOP during one stops it); step record ends */
static void play_press(void)
{
    ui.srec = 0;
    if (song.playing || chain_busy())
        transport_req = 2;
    else if (!ui.home && cur_page()->graph == GR_SONG)
        song_play();
    else
        transport_req = 1;
}

/* --------------------------------------------------- step record (spec §6) --- */
/* KEYS on the STEP page, stopped: a REC tap starts it at the bank's first step (top bar STEP REC nn). A key writes its
 * note to the cursor step (ON, NOTE; TIME = NOTE; ACC / SLIDE as FLAG left them) and the cursor moves on when it is
 * let go; a key pressed while the cursor's key is still held: SLIDE on that step, its note on the next one. KNOB 1 the
 * cursor, 2 NOTE, 3 TIME (NOTE / TIE / REST), 4 FLAG (- / ACC / SLD / A+S); an EDIT tap: a rest, the cursor on. A REC
 * tap, PLAY, leaving the page, KEYS off, another track, the transport starting, the menu, NAME or PERFORM's layer
 * (FX held) end it. The ISR sounds the keys. */
static int srec_ok(void)
{
    return keys_mode() && !ui.home && !ui.layer && cur_page()->scope == SC_GRID && !song.playing && !chain_busy() &&
           !view_src(TSEL);
}
static void srec_start(void)
{
    uint32_t len = view_len(TSEL);
    ui.srec = 1;
    ui.sr_cur = (uint8_t)(ui.bank * 16u < len ? ui.bank * 16u : len - 1u);
    ui.sr_key = 0;
    grid_drop_holds();
}
static void srec_next(void)                            /* the cursor on (LEN wraps), its bank shown */
{
    ui.sr_cur = (uint8_t)((ui.sr_cur + 1u) % view_len(TSEL));
    ui.bank = (uint8_t)(ui.sr_cur / 16u);
    ui.sr_key = 0;
}
static void srec_note(uint32_t key)                    /* a key pressed: its note on the cursor step */
{
    track_t *t = TSEL;
    step_t *s;
    if (ui.sr_key) {                                   /* the cursor's key still held: its step slides into this one */
        t->step[ui.sr_cur].slide = 1;
        srec_next();
    }
    s = &t->step[ui.sr_cur];
    s->on = 1;
    s->tie = 0;
    s->nt = (int8_t)clamp(keys_nt(key), NT_MIN, NT_MAX);
    t->key_nt = s->nt;
    ui.sr_key = (uint8_t)(key + 1u);
}
static void srec_keys(uint32_t notes)                  /* the frame's key edges: one key, the highest (the ISR's
                                                        * keyboard_block sounds that one of a frame's chord) */
{
    uint32_t k;
    if (ui.sr_key && !((fm1_in.notes >> (ui.sr_key - 1u)) & 1u))
        srec_next();                                   /* the cursor's key let go */
    for (k = 27u; k-- > 0u;)
        if ((notes >> k) & 1u) {
            srec_note(k);
            break;
        }
}
static void srec_rest(void)                            /* EDIT: the cursor step a plain rest (no PROB RATCH ACC SLIDE
                                                        * left for a later note), the cursor on */
{
    step_t *s = &TSEL->step[ui.sr_cur];
    s->on = 0;
    s->tie = 0;
    s->cond = 0;
    s->rat = 0;
    s->acc = 0;
    s->slide = 0;
    srec_next();
}
static void srec_knob(uint32_t k, int32_t s)
{
    track_t *t = TSEL;
    step_t *st;
    int32_t d = s > 0 ? 1 : -1, v, len = (int32_t)view_len(t);
    if (k == 0u) {
        ui.sr_cur = (uint8_t)clamp((int32_t)ui.sr_cur + accel(EN_K1, s, len), 0, len - 1);
        ui.bank = (uint8_t)(ui.sr_cur / 16u);
        ui.sr_key = 0;                                 /* a key still held moves nothing on its release */
        return;
    }
    st = &t->step[ui.sr_cur];
    if (k == 1u) {
        st->nt = (int8_t)clamp(st->nt + accel(EN_K2, s, NT_MAX - NT_MIN), NT_MIN, NT_MAX);
    } else if (k == 2u) {
        v = clamp((int32_t)srec_time(st) + d, 0, 2);
        st->on = v == 0;
        st->tie = v == 1;
    } else {
        v = clamp((int32_t)(st->acc | st->slide << 1) + d, 0, 3);
        st->acc = (uint8_t)(v & 1);
        st->slide = (uint8_t)(v >> 1);
    }
}

#include "ui_preset.c"                             /* PRESETS: the encoder's popup, the list on a long EDIT */

/* fixes 2026-10-21 item 1: OCT+ let go where it was pressed on the steps: alone (no step key accented with it), and the
 * keys still the steps, the next bank; anything else (an accent, another page, the menu, a dialog, the MUTE screen or
 * PERFORM up while it was held: acc_oct 2, ui_input) nothing */
static void acc_oct_poll(void)
{
    if (!acc_oct || ((fm1_in.buttons >> panel.btn[B_OCTUP]) & 1u))
        return;
    if (acc_oct == 1u && step_keys() && !ui.confirm && !name_on() && !pl_on() && !ui.mute && !ui.layer && !fx_knobs)
        bank_set((int32_t)ui.bank + 1);
    acc_oct = 0;
}

static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), now = fm1_ticks(), id, b, k, fam = cur_fam();
    uint32_t home = btn_hold(&ui.home_t0, B_HOME, now, 1);
    uint32_t rec = btn_hold(&ui.rec_t0, B_REC, now, !ui.menu && fam == FAM_SEQ), acts;
    int32_t s, dt;
    if (safe_start)                                     /* safe start: no pages, no edits */
        return;
    acc_oct_poll();                                     /* OCT+ let go on the steps: the next bank (item 1) */
    pre_frame(now);                                     /* PRESETS: EDIT's hold, the popup's time (ui_preset.c) */
#if FELUCCA_BLE
    bt_autostart();                                     /* AUTOSTART: BLUETOOTH ON once, 5 s after power-on (the
                                                         * main loop's UI only: gate 3 CALLERS) */
#endif
    glo_input(now, notes);                              /* GLO: tap / hold, the MUTE screen */
    fx_layer(now, home == BT_TAP);                      /* PERFORM: FX tap / hold, the PAGE screen */
    if (acc_oct && (ui.mute || ui.layer || fx_knobs))
        acc_oct = 2;                                    /* OCT+ held under MUTE / PERFORM: no bank on its release, there
                                                         * or after (they own the panel: item 7) */
    ml_guard();                                         /* MIDI LEARN: off in the menu, NAME, another confirm */
    ml_poll();                                          /* .. a CC / note came for the picked one */
    if (ui.ml.on && !ui.confirm) {                      /* .. PLAY / REC pick their action, and do nothing else */
        if ((pressed >> panel.btn[B_PLAY]) & 1u) {
            pressed &= ~(1u << panel.btn[B_PLAY]);
            ml_pick_act(ML_A_PLAY);
        }
        if ((pressed >> panel.btn[B_REC]) & 1u) {
            ui.rec_t0 |= 2u;                            /* (no tap, no hold) */
            ml_pick_act(ML_A_REC);
        }
    }
    acts = ml_drain(&dt);                               /* .. learned buttons pressed: PLAY and REC as their taps,
                                                         * NEXT / PREV TRACK as ALGORITHM (each where the button acts) */
    if (acts & 1u)
        pressed |= 1u << panel.btn[B_PLAY];
    if ((acts & 2u) && rec == BT_NONE)
        rec = BT_TAP;
    if (ui.mute) {                                      /* the MUTE screen owns the panel (its keys: glo_input), PLAY
                                                         * aside; a HOME / REC press under it is no tap or hold */
        if ((pressed >> panel.btn[B_PLAY]) & 1u)
            play_press();
        if (ui.home_t0)
            ui.home_t0 |= 2u;
        if (ui.rec_t0)
            ui.rec_t0 |= 2u;
        enc_drop();
        return;
    }
    if ((fm1_in.buttons & perf_mask) || (perf_mask & PERF_PAGE))   /* PERFORM: keys are the layer's */
        notes = 0;
    notes &= ~kb_layer;
    if (name_on()) {                                  /* NAME owns the panel: OCT+ / OCT- / HOME / PLAY, the keys, K1 K2 */
        ui.srec = 0;                                    /* (step record ends) */
        name_input(pressed, notes, now, home);
        return;
    }
    if (pl_on()) {                                    /* the PRESETS list owns the panel (ui_preset.c) */
        ui.srec = 0;
        preset_list_input(pressed, notes, home);
        return;
    }
    if (home == BT_HOLD) {                              /* HOME held: open the menu, or leave it */
        if (ui.menu) {
            menu_close();
        } else {
            ui.menu = 1;
            ui.srec = 0;                                /* step record ends */
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
    if (rec == BT_HOLD) {                               /* REC held on SEQ: "clear track n?"; SONG: its row */
        if (!ui.home && cur_page()->graph == GR_SONG) {
            song_rec_hold();
        } else if (!song_lock()) {
            ui.confirm = 1;
            ui.confirm_trk = song.sel;
            ui.force = 1;
        }
    } else if (rec == BT_TAP && !ui.confirm) {          /* spec §4: REC arms on every screen */
        if (ui.srec) {
            ui.srec = 0;                                /* step record ends */
        } else if (srec_ok()) {
            srec_start();                               /* KEYS, STEP, stopped: step record */
        } else if (!song_lock()) {
            track_rec_toggle(song.sel);                 /* the selected track armed / disarmed */
        }
    }
    if (ui.confirm) {                                   /* OCT- cancels (NO), OCT+ clears (YES); nothing else reacts */
        if ((pressed >> panel.btn[B_OCTUP]) & 1u) {
            char m[12] = "1 CLEARED";
            m[0] = (char)('1' + ui.confirm_trk);
            if (ui.confirm == 3u) {                     /* SONG: the row out, the later ones up */
                uint32_t r = ui.confirm_trk, j;
                for (j = r; j + 1u < chain.cfg.count; j++)
                    chain.cfg.row[j] = chain.cfg.row[j + 1u];
                if (r < chain.cfg.count)
                    chain.cfg.count--;
                ui_message("ROW DELETED");
            } else if (ui.confirm == 4u) {              /* SONG: no rows */
                memset(&chain.cfg, 0, sizeof chain.cfg);
                ui.song_row = 0;
                ui_message("SONG CLEARED");
            } else if (ui.confirm == 5u) {              /* the last project failed to load: forget it (saved) */
                lastp.set = 0;
                settings_save();
            } else if (ui.confirm == ML_CONFIRM) {      /* MIDI LEARN: CLEAR ALL LEARNED? (LEARN stays on) */
                ml_clear_all();
                ui_message("LEARN CLEARED");
            } else if (ui.confirm == 2u) {              /* the MOTION page's CLEAR */
                motion_clear(ui.confirm_trk % NTRK);
                ui_message("MOTION CLEARED");
            } else {
                track_clear(&trk[ui.confirm_trk % NTRK]);
                ui_say("TRACK ", m);
            }
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
        home_tap();                                     /* HOME: TRACKS <-> HOME; elsewhere HOME as last shown */
    settings_poll();
    song.act[2] = (int16_t)settings.mutebar;
    song.act[3] = (int16_t)fx_metro;
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
        if (ui.srec && b == B_EDIT) {                   /* step record: a rest, the cursor on */
            srec_rest();
            ui.pr.e_st = 2;                             /* (held: no PRESETS list) */
            continue;
        }
        if (b == B_EDIT)
            edit_down(now);                             /* PRESETS: held, the list (ui_preset.c edit_hold) */
        switch (b) {
        case B_PLAY:
            play_press();
            break;
        case B_REC:
        case B_HOME:                                    /* tap / hold: above */
            break;
        case B_SCL:                                     /* KEYS on / off */
            keys_toggle();
            break;
        case B_OCTDN:
        case B_OCTUP:                                   /* the STEP grid's bank; EDIT: into / out of the layer */
            if (ui.ml.on) {                             /* MIDI LEARN: OCT- clear, OCT+ done (the page's wait) */
                ml_oct(b == B_OCTUP);
            } else if (keys_mode()) {
                keys_octave(b == B_OCTUP ? 1 : -1);
            } else if (step_keys()) {                   /* OCT+: held + a step key its accent, let go alone the
                                                         * next bank (acc_oct_poll); OCT-: the bank before */
                if (b == B_OCTUP)
                    acc_oct = 1;
                else
                    bank_set((int32_t)ui.bank - 1);
            } else if (!ui.home && b == B_OCTUP && cur_fam() == FAM_SND) {
                ui.fam_last[FAM_LAY] = 0;               /* LAYER 1/2 */
                open_family(FAM_LAY);
            } else if (!ui.home && b == B_OCTDN && cur_fam() == FAM_LAY) {
                open_family(FAM_SND);                   /* the SOUND page it came from */
            } else if (!ui.home && b == B_OCTUP && cur_fam() == FAM_LFO) {   /* the LFO turned last (DEST: the slot's
                                                                               * source): SYNC -> Hz -> TIME */
                uint32_t l = cur_page()->graph == GR_LDEST ? (TSEL->p[LS_ID[song.lslot & 3u][LS_SRC]] ? 1u : 0u)
                                                           : ui.lfo_l & 1u;
                int16_t *mp = &TSEL->p[(l ? P_LFO2 : P_LFO1) + LF_MODE];
                *mp = (int16_t)((clamp(*mp, 0, 2) + 1) % 3);
                ui.force = 1;
            }
            break;
        default: {
            uint32_t f;
            for (f = FAM_HOME + 1u; f < FAM_COUNT; f++)
                if (FAM_BTN[f] == b && f != FAM_LAY && b != B_FX && b != B_GLO)   /* (FX, GLO: on the release) */
                    open_family(f == FAM_SND && cur_fam() == FAM_LAY ? FAM_LAY : f);   /* in the layer: its pages */
            break;
        }
        }
    }
    edit_hold(now);                                     /* EDIT held: the PRESETS list */
    if (pl_on())
        return;
    song.seq_mode = seq_mode_now();                   /* seq.c: whose the keys are (KEYS may have changed) */
    if (ui.srec && !srec_ok())
        ui.srec = 0;                                    /* KEYS off, another page, the transport started */
    if (ui.srec) {
        srec_keys(notes);
    } else if (step_keys()) {
        for (k = 0; k < 16u; k++) {                     /* steps on the white keys (seq.c STEP_KEY): a press */
            uint32_t *t0 = &ui.step_t0[k];               /* turns the step on / off; with OCT+ held, its accent */
            if ((notes >> STEP_KEY[k]) & 1u) {
                if (acc_oct) {
                    step_accent(k);
                    acc_oct = 2;
                    *t0 = (now | 1u | 4u) & ~2u;        /* (as it is now: a knob turned while held edits it) */
                } else {
                    step_press(k);
                    *t0 = (now | 1u) & ~6u;
                }
                ui.held = (uint8_t)k;
            } else if (!((fm1_in.notes >> STEP_KEY[k]) & 1u)) {
                *t0 = 0;
            }
        }
    } else if (comp_mode()) {
        for (k = 0; k < NTRK; k++)                      /* COMP: a white key ducks / unducks its track */
            if ((notes >> KEY_TRK_KEY[k]) & 1u)
                track_duck_toggle(k);
    } else if (!keys_mode() && (ui.home || (!ui.menu && cur_page()->scope == SC_TRACK && cur_fam() != FAM_SEQ))) {
        for (k = 0; k < NTRK; k++)                      /* HOME, a per-track page: a white key selects its track */
            if ((notes >> KEY_TRK_KEY[k]) & 1u)         /* (it plays it too: seq.c reads the keys itself) */
                track_select(k);
    }
    if ((s = panel_enc(EN_PRESET)) != 0 && (fx_knobs || ui.layer)) {
        /* PERFORM up: no preset loaded under it (item 7; the turn dropped) */
    } else if (s) {                                     /* PRESETS: each detent the next / previous preset, loaded */
        for (; s; s += s > 0 ? -1 : 1)                  /* (ui_preset.c: the popup) */
            preset_popup_turn(s > 0 ? 1 : -1);
    }
    if ((s = panel_enc(EN_ALGO)) != 0 && (fx_knobs || ui.layer)) {
        /* PERFORM up: no track change under it (item 7; the turn dropped) */
    } else if (s) {
        if (ui.ml.on)
            ml_pick_act(s > 0 ? ML_A_NEXT : ML_A_PREV);   /* MIDI LEARN: NEXT / PREV TRACK picked, no track change */
        else
            track_select((uint32_t)clamp((int32_t)song.sel + (s > 0 ? 1 : -1), 0, NTRK - 1));
    }
    if (dt)                                             /* NEXT / PREV TRACK learned (MIDI LEARN) */
        track_select((uint32_t)clamp((int32_t)song.sel + dt, 0, NTRK - 1));
    if ((s = panel_enc(EN_SELECT)) != 0 && (fx_knobs || ui.layer)) {
        /* PERFORM up: SELECT turns no page under it (item 7; the turn dropped) */
    } else if (s) {                                     /* SELECT: the section's pages; HOME's TRACKS mode: BPM */
        if (!mix_mode())
            page_turn(s > 0 ? 1 : -1);
        else if (song.g[G_CLOCK])                       /* following a clock: its tempo, not the knob */
            ui_say("CLK ", N_CLOCK[song.g[G_CLOCK]]);
        else {
            song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
            ui.bpm_t = 40;
        }
    }
    for (k = 0; k < 4u && !fx_knobs; k++) {            /* (PERFORM's: fx_layer's alone, item 7) */
        const page_t *pg = cur_page();
        int16_t *hv;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        if (ui.srec) {                                  /* step record: STEP NOTE TIME FLAG */
            srec_knob(k, s);
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
            continue;
        }
        if (grid_mode() && step_edit(k, s))
            continue;
        if (ui.home || pg->scope == SC_GRID || pg->scope == SC_MIX || pg->graph == GR_SONG || pg->graph == GR_LDEST ||
            page_desc(pg, k, &hv)) {
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
        if (ui.home) {
            int16_t *vp;
            const param_desc_t *d = home_param(k, &vp);
            if (d->max > d->min) {
                int32_t v = param_turn(d, *vp, accel(EN_K1 + k, s, d->max - d->min));
                if (v != *vp)
                    preset_touch(TSEL);                 /* (TUNE DECAY TONE CHAR: the voice edited; not at a limit) */
                *vp = (int16_t)v;
                if (vp >= TSEL->p && vp < TSEL->p + P_COUNT) {
                    motion_capture(TSEL, (uint32_t)(vp - TSEL->p));
                    ml_knob(song.sel, (uint32_t)(vp - TSEL->p));   /* (MIDI LEARN: picked) */
                }
            }
        } else {
            edit_param(k, s);
        }
    }
    if (mo.full) {                                      /* motion.c: a turn found no free place */
        mo.full = 0;
        ui_message("MOTION FULL");
    }
}

/* ---------------------------------------------------- panel setup --- */
/* 30 s without input: give up and keep the old table (a stuck key cannot hang the boot) */
#define SETUP_IDLE_MS 30000u
static void panel_setup(void)
{
    uint32_t i, used = 0, t0 = fm1_ms;
    const panel_t old = panel;
    lcd_fill(0, 0, 240, 240, C_BG);
    draw_text_box(0, 10, 240, &FONT_S, "HARDWARE CALIBRATION", C_INK, 1);
    draw_text_box(0, 30, 240, &FONT_S, "TEACH EACH BUTTON AND KNOB", C_INK, 1);
    while (fm1_in.buttons) {                             /* wait for OCT-/OCT+ release */
        fm1_wdt_feed();
        if (fm1_ms - t0 > SETUP_IDLE_MS)
            goto timeout;
    }
    fm1_input_edges(0);
    for (i = 0; i < NB; i++) {
        uint32_t p = 0, id;
        draw_text_box(0, 80, 240, &FONT_S, "PRESS", C_INK, 1);
        draw_text_box(0, 100, 240, &FONT_L, B_NAME[i], C_INK, 1);
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
        draw_text_box(0, 80, 240, &FONT_S, "TURN RIGHT", C_INK, 1);
        draw_text_box(0, 100, 240, &FONT_L, E_NAME[i], C_INK, 1);
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
    lcd_fill(0, 0, 240, 240, C_BG);
    ui.force = 1;
    return;
timeout:
    panel = old;
    lcd_fill(0, 0, 240, 240, C_BG);
    ui.force = 1;
    ui_message("SETUP CANCELLED");
}

