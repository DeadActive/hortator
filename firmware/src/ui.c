/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Drum machine user interface: state, page navigation, track / model selection, the STEP grid.
 * Four columns map to KNOB 1..4; rendering (ui_draw.c) is lazy: every element remembers what it
 * last drew and is redrawn only on change. */
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "DRUM-DEV"         /* host tests; builds pass DRUM-<VERSION.txt> (tools/build.py) */
#endif
static void project_save(uint32_t slot);
static void project_load(uint32_t slot);
static void project_boot(void);
static int project_used(uint32_t slot);
static uint32_t chain_prepare(void);
static int project_name(uint32_t slot, char *b);
static void project_rename(uint32_t slot, const char *name);
static int transport_busy(void);
static int name_on(void);                  /* ui_name.c (NAME) */
static void draw_name(void);
static void panel_setup(void);
static void learn_save(void);              /* project.c: MIDI LEARN's map to flash (deferred while playing) */
static void ml_line(char *b);              /* ui_learn.c: MIDI LEARN's footer line */
static int pp_on(void);                    /* ui_preset.c: the PRESETS popup shows */
static int pl_on(void);                    /* .. the PRESETS list shows */
static void draw_popup(void);
static void draw_plist(void);
static int preset_user_save(uint32_t slot, const track_t *t);   /* project.c: the user presets' flash record */
static void preset_user_delete(uint32_t slot);

enum { CV_NORM, CV_HOT, CV_DIM };              /* a knob cell's value: plain, touched (inverted), inactive (dithered) */
#define HOT(c) ((c) == ui.hot_col && ui.hot_t ? CV_HOT : CV_NORM)
#define RATIO(d, v) ((d)->max > (d)->min ? (enum_rank((d), (int32_t)(v)) - (d)->min) * 1000 / ((d)->max - (d)->min) : -1)
/* layout A (spec 2026-10-19-md-ui §5): top bar 0..15, knob row 17..59, rule 60, graph 61..180 (a 20-row band over
 * the 100-row graph proper), trig strip 181..211 (dotted rule on its first row), breadcrumb 212..239 */
#define Y_HEAD 0
#define H_HEAD 16
#define Y_KNOB 17
#define H_KNOB 43
#define Y_GRAPH 61
#define H_GRAPH 120
#define LTR_N 108u                     /* LFO pages: a half's wave / trail width (one cycle; ui_draw.c graph_lfo) */
#define LG_X0 6u                       /* .. its x in the half */
#define LG_CY 46                       /* .. the zero line (graph y) and the full swing */
#define LG_A 30
#define G_OY 20
#define Y_TRIG 181
#define H_TRIG 31
#define Y_CRUMB 212
#define H_CRUMB 28
#define Y_LABEL Y_KNOB                 /* the old column's rows, until the knob cells (Task 5) */
#define Y_VALUE (Y_KNOB + 18)
#define Y_GAUGE (Y_KNOB + 38)
#define Y_SEP_END (Y_KNOB + H_KNOB)

static struct {
    uint8_t home;                /* HOME's second mode shown: the HOME screen (spec §3) */
    uint8_t hmode;               /* HOME's mode last shown: 0 TRACKS (the TRACKS page), 1 HOME (the HOME screen, COMP) */
    uint8_t page;                /* index into PAGES */
    uint8_t fam_last[FAM_COUNT]; /* last page used per family */
    uint8_t bank;                /* STEP grid: 16-step bank shown on the keys */
    uint8_t hot_col, hot_t;      /* column whose knob was just turned (drawn white) */
    uint8_t menu;                /* 0 off, 1 list, 2 about (HOME held), 3 Bluetooth (BLE build: bt_page.c) */
    uint8_t menu_sel;
    uint8_t about_top;           /* ABOUT: the first credits row shown (ui_menu.c; 0 whenever ABOUT opens) */
    uint32_t menu_sig, home_t0;
    uint8_t force;               /* full redraw pending */
    uint8_t msg_t;               /* transient message frames */
    uint8_t bpm_t;
    uint8_t arm, arm_t;          /* destructive action armed: param id, frames left to confirm */
    uint32_t rec_t0;             /* REC press time (btn_hold) */
    uint8_t rec_all, rec_keep;   /* REC ALL (a REC tap off TRACKS, ui_input.c rec_tap): on; the arm mask before it (back
                                  * when it is left). Never stored, as the arm state */
    uint32_t step_t0[16];        /* STEP grid: a white key's press time | 1; bit 1: no tap on its release (held
                                  * STEP_HOLD_MS, edited, accented or let go of: grid_drop_holds), bit 2: edited */
    uint16_t step_si[16];        /* the step it pressed, 0xFFFF none */
    uint8_t held;                /* the grid key pressed last (valid while step_t0[held] is set) */
    uint8_t confirm;             /* 1 "CLEAR TRACK n?", 2 CLEAR MOTION, 3 DELETE ROW, 4 CLEAR SONG, 5 the last project
                                  * failed to load (START NEW PROJECT?; confirm_trk its slot), 6 CLEAR ALL LEARNED?
                                  * (MIDI LEARN: ui_learn.c) */
    uint8_t confirm_trk;
    char msg[24];
    uint32_t enc_t[NE];
    uint8_t enc_c[NE];           /* knob acceleration: the turn's carried sixteenths of a step (ui_input.c accel) */
    char col[4][32];
    char focus_l[8], focus_v[8], focus_u[8];
    uint32_t graph_sig, head_sig, foot_sig, msg_sig, tempo_sig, col_sig, lev_sig, lev_m, frame;
    uint8_t graph_top;
    int16_t tr[2][LTR_N];        /* LFO pages, random waves: each LFO's trail (a ring, newest at tr_h - 1) */
    uint8_t tr_h[2], tr_n[2];
    uint32_t tr_ph[2], tr_key[2], tr_frame;   /* the phase at the last point; what the trail is of; the frame they
                                               * were fed */
    uint8_t lfo_l;               /* LFO pages: the LFO whose knob was turned last (OCT+ cycles its RATE MODE) */
    uint8_t persist_pending;     /* a settings save asked for while playing: written once stopped (project.c) */
    uint8_t cpu_on;              /* the top bar's CPU icon: since cpu_ms, after song.sheds moved past sheds (2 s) */
    uint32_t sheds, cpu_ms;
    uint32_t fx_t0;              /* PERFORM: FX's press time | FX_DOWN / FX_OPEN / FX_DEAD (ui_input.c fx_layer) */
    uint8_t layer;               /* PERFORM: the layer's map shows (FX held open, or a layer key still held) */
    uint8_t pg_open, pg_page, pg_home;   /* PERFORM PAGE: the screen open; the page / HOME under it when it opened */
    uint32_t glo_t0;             /* GLO's press time | GLO_DOWN OPEN DEAD PLAIN (ui_input.c glo_input) */
    uint8_t mute;                /* the MUTE screen shows (GLO held, spec §5) */
    uint8_t song_row;            /* SONG: the row selected (count = the + row) */
    uint8_t keys;                /* KEYS (spec §5): an SCL tap; off at power-on, never stored */
    uint8_t env;                 /* ENV (spec 2026-10-24 parameter locks): 1 STEPS (the white keys the selected track's
                                  * steps on every page), 0 TRACKS (as before); off at power-on, never stored */
    uint32_t env_t0;             /* ENV's press time | 1; bit 1: used (ENV + OCT+-, or pressed under a dialog): its
                                  * release toggles nothing */
    uint32_t ocd_t0;             /* locks: OCT- pressed with a step held: its time | 1; bit 1: the 0.6 s clear done */
    uint8_t ocd_si;              /* .. the step held then */
    uint8_t srec, sr_cur, sr_key;   /* step record (spec §6): on; its cursor; the key whose note is on it + 1 */
    struct {                     /* MIDI LEARN (ui_learn.c; GLO + G4): never stored */
        uint8_t on;              /* LEARN is on */
        uint8_t pick, trk, id;   /* something picked: track trk's P_* id (a CC is awaited: midi_learn.c ml_arm) .. */
        uint8_t act;             /* .. or, not 0, an action (midi_learn.c ML_A_*: a CC or a note awaited) */
        uint8_t save;            /* the map changed while playing: saved once stopped (project.c learn_save) */
    } ml;
    struct {                     /* PRESETS (ui_preset.c): never stored */
        uint8_t pop, pop_trk;    /* the popup shows, for that track */
        uint8_t lst;             /* the list shows */
        uint8_t arm;             /* the list: 1 SAVE over / 2 DELETE armed, on arm_id (the button again acts) */
        uint8_t e_st;            /* EDIT: 1 pressed (may become a hold), 2 the hold acted / dead until let go */
        uint8_t e_page, e_home, e_hmode, e_snd, e_lay;   /* the page EDIT's press left (the hold puts it back) */
        uint16_t pop_id, cur, arm_id;   /* the popup's preset; the list's cursor */
        uint16_t shown[5];       /* the popup's rows, top to bottom (the preset in the middle) */
        uint32_t now, pop_t0, arm_t0, e_t0, sig;
        uint8_t pv, pv_trk;      /* the list: track pv_trk auditions the cursor's preset (its own sound in sv_*) */
        uint8_t sv_model, sv_ed; /* .. that sound: MODEL, the 8 SOUND knobs (motion's base while it plays), the preset */
        uint16_t sv_pre;         /*    and its edited mark, put back unless LOAD keeps the audition */
        int16_t sv_k[8];
    } pr;
} ui;

static const page_t *cur_page(void) { return &PAGES[ui.page]; }

static uint32_t page_first(uint32_t fam)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == fam)
            return i;
    return 0;
}

static void ui_say(const char *a, const char *b)     /* transient message in the top bar: a + b */
{
    uint32_t n;
    str_cpy(ui.msg, a, sizeof ui.msg);
    n = str_len(ui.msg);
    str_cpy(ui.msg + n, b, sizeof ui.msg - n);
    ui.msg_t = 40;
}

static void ui_message(const char *s) { ui_say(s, ""); }

static int song_lock(void)                          /* a song plays (or is armed): its patterns stay as they are */
{
    if (!chain_busy())
        return 0;
    ui_message("STOP TO EDIT");
    return 1;
}

static int grid_mode(void) { return !ui.home && !ui.menu && cur_page()->scope == SC_GRID; }
static int mix_mode(void) { return !ui.home && !ui.menu && cur_page()->scope == SC_MIX; }   /* HOME's TRACKS mode */
static int comp_mode(void) { return !ui.home && !ui.menu && cur_page()->graph == GR_COMP; }   /* COMP: keys DUCK */
/* KEYS (spec §5): the keys a chromatic keyboard of the selected track on every page but TRACKS and COMP (their step /
 * duck keys); NAME and the menu keep theirs, and FX held takes its keys first (seq.c keyboard_block) */
static int keys_mode(void)
{
    return ui.keys && !ui.menu && !name_on() &&
           (ui.home || (cur_page()->scope != SC_MIX && cur_page()->graph != GR_COMP));
}
/* the white keys are the selected track's steps (the bank's 16): every SEQ page while KEYS is off, and TRACKS while
 * nothing is armed (user, 2026-10-08) */
static int seq_keys(void)
{
    return !ui.home && !ui.menu &&
           ((cur_page()->fam == FAM_SEQ && !keys_mode()) || (cur_page()->scope == SC_MIX && !song.rec));
}
/* ENV's STEPS mode (spec 2026-10-24 parameter locks): the white keys the selected track's steps on every page (HOME,
 * TRACKS armed, SOUND, FX, LFO, COMP ...); not in the menu or NAME */
static int env_steps(void) { return ui.env && !ui.menu && !name_on(); }
static int step_keys(void) { return seq_keys() || env_steps(); }
/* seq.c keyboard_block: whose the keys' presses are: 0 the drum keys (hits, MIDI), 1 the UI's (the STEP grid, COMP,
 * TRACKS, NAME), 2 TRACKS armed (hits and recording, as 0), 3 KEYS */
static uint8_t seq_mode_now(void)
{
    if (pl_on())                                       /* the PRESETS list: the keys play (and pick the track) */
        return 0u;
    if (keys_mode())
        return 3u;
    return (uint8_t)(step_keys() || comp_mode() ? 1u : mix_mode() ? (song.rec ? 2u : 1u) : 0u);
}

/* a held grid key lets go of its step (another track or page): keys still held edit nothing */
static void grid_drop_holds(void)
{
    uint32_t k;
    for (k = 0; k < 16u; k++) {
        ui.step_si[k] = 0xFFFFu;
        if (ui.step_t0[k])
            ui.step_t0[k] |= 6u;
    }
}

static void page_entered(void)
{
    ui.srec = 0;                                       /* step record ends (leaving the page, another track) */
    grid_drop_holds();
    song.seq_mode = seq_mode_now();                    /* seq.c: whose the keys are */
    ui.hot_t = 0;
    ui.force = 1;
}

/* SCL: KEYS on / off (a key still held: its release still ends its note, seq.c keyboard_block) */
static void keys_toggle(void)
{
    ui.keys ^= 1u;
    if (ui.keys)
        ui.env = 0;                                    /* one key role at a time: STEPS off */
    grid_drop_holds();                                 /* a grid key held edits nothing more */
    song.seq_mode = seq_mode_now();
    ui.force = 1;
}
/* ENV (its release): STEPS <-> TRACKS (KEYS off for STEPS: one key role at a time); a held key lets go of its step */
static void env_toggle(void)
{
    ui.env ^= 1u;
    if (ui.env)
        ui.keys = 0;
    grid_drop_holds();
    song.seq_mode = seq_mode_now();
    /* (no full repaint: the top bar's padlock, its KEYS line and the strip's marks are in their signatures) */
}
/* OCT- / OCT+ while KEYS has the keys: the keyboard an octave down / up, -2..+2 (seq.c keys_nt) */
static void keys_octave(int32_t d) { song.octave = (int8_t)clamp(song.octave + d, -2, 2); }
/* a note (MIDI, 60 = C4) by name: "C4", "F#3", "C-1" */
static void note_text(char *b, int32_t n)
{
    n = clamp(n, 0, 127);
    str_cpy(b, N_NOTE[n % 12], 4);
    fmt_int(b + str_len(b), n / 12 - 1);
}
/* step record's TIME of a step: 0 NOTE (ON), 1 TIE, 2 REST */
static uint32_t srec_time(const step_t *s) { return s->tie ? 1u : s->on ? 0u : 2u; }

/* a page with nothing to edit for the selected track: an EDIT page past the end of its engine's sound list */
static int page_hidden(uint32_t i)
{
    uint8_t ids[16];
    if (i >= NPAGES || PAGES[i].fam != FAM_SND || PAGES[i].id[0] < SND_SLOT)
        return 0;
    return PAGES[i].id[0] - SND_SLOT >= snd_list(TSEL, ids);
}

/* the pages a family shows (hidden ones left out), and where the current page is among them (1-based, 0 = not) */
static uint32_t fam_pages(uint32_t fam, uint32_t *pos)
{
    uint32_t i, n = 0;
    *pos = 0;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == fam && !page_hidden(i)) {
            n++;
            if (i == ui.page)
                *pos = n;
        }
    return n;
}

static void open_family(uint32_t fam)
{
    if (!ui.home && cur_page()->fam == fam) {          /* same button again: next page */
        uint32_t i = ui.page + 1u;
        while (i < NPAGES && PAGES[i].fam == fam && page_hidden(i))
            i++;
        if (i >= NPAGES || PAGES[i].fam != fam)
            i = page_first(fam);
        ui.page = (uint8_t)i;
    } else {
        ui.page = ui.fam_last[fam] && PAGES[ui.fam_last[fam]].fam == fam && !page_hidden(ui.fam_last[fam])
                      ? ui.fam_last[fam]
                      : (uint8_t)page_first(fam);
    }
    ui.fam_last[fam] = ui.page;
    ui.home = 0;
    page_entered();
}

/* HOME (spec §3): two modes, 1 TRACKS (the TRACKS page: the mixer, TRACK LEVEL LEN PAN, the keys as on TRACKS) and 2
 * HOME (the HOME screen: the scope, the selected track's macros; SELECT on to COMP 2/3, 3/3). ui.hmode: the one last
 * shown */
static void home_screen(void)                         /* HOME's second mode */
{
    ui.srec = 0;                                       /* step record ends (leaving the page, another track) */
    ui.home = 1;
    ui.hmode = 1;
    ui.hot_t = 0;
    song.seq_mode = seq_mode_now();
    ui.force = 1;
}

static void home_tracks(void)                         /* HOME's TRACKS mode: the TRACKS page (FAM_MIX: HOME's LED) */
{
    ui.page = (uint8_t)page_first(FAM_MIX);
    ui.home = 0;
    ui.hmode = 0;
    page_entered();
}

static void go_home(void)                             /* HOME in the mode last shown */
{
    if (ui.hmode)
        home_screen();
    else
        home_tracks();
}

static void home_tap(void)                            /* on HOME: TRACKS <-> HOME (COMP: HOME); elsewhere: HOME */
{
    if (ui.home)
        home_tracks();
    else if (cur_page()->fam == FAM_MIX || cur_page()->fam == FAM_HOME)
        home_screen();
    else
        go_home();
}

/* SELECT: the current section's next / previous page (its empty pages skipped), stopping at the ends; HOME 1/3 (the
 * HOME screen) is the HOME section's first page (then COMP 2/3, 3/3). (HOME's TRACKS mode: BPM, ui_input.c) */
static void page_turn(int32_t dir)
{
    uint32_t fam, i;
    if (ui.home) {
        if (dir > 0) {                                /* HOME 1/3 -> COMP 2/3 */
            ui.page = (uint8_t)page_first(FAM_HOME);
            ui.home = 0;
            page_entered();
        }
        return;
    }
    fam = cur_page()->fam;
    i = ui.page;
    do
        i = dir > 0 ? i + 1u : i - 1u;                /* (below 0: wraps above NPAGES, out of the section) */
    while (i < NPAGES && PAGES[i].fam == fam && page_hidden(i));
    if (i < NPAGES && PAGES[i].fam == fam) {
        ui.page = (uint8_t)i;
        ui.fam_last[fam] = ui.page;
        page_entered();
    } else if (dir < 0 && fam == FAM_HOME) {
        home_screen();                                /* COMP 2/3 back to HOME 1/3 */
    }
}

/* --------------------------------------------- what a track shows --- */
/* the STEP grid, PATTERN, the footer, TRACKS and the keys show a track's steps, or, when its SRC is a Grids
 * channel, that channel's pattern (read-only: Grids makes it) */
static uint32_t view_src(const track_t *t) { return t->p[P_SRC] >= 1 && t->p[P_SRC] <= 3 ? (uint32_t)t->p[P_SRC] : 0u; }
static uint32_t view_len(const track_t *t)
{
    uint32_t s = view_src(t);
    return s ? grids_len(s - 1u) : (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
}
static step_t view_step(const track_t *t, uint32_t si)
{
    uint32_t s = view_src(t), b;
    step_t r = {0};
    if (!s)
        return seq_steps(t)[si % NSTEP];             /* (a song: its row's) */
    b = grids_preview(s - 1u, si);
    r.on = (uint8_t)(b & 1u);
    r.acc = (uint8_t)((b >> 1) & 1u);
    return r;
}
static uint32_t view_idx(const track_t *t) { uint32_t s = view_src(t); return s ? grids_pos(s - 1u) : t->seq_idx; }

/* ------------------------------------------------------ STEP grid --- */
static uint32_t bank_count(void) { return (view_len(TSEL) + 15u) / 16u; }
static void bank_set(int32_t b) { ui.bank = (uint8_t)clamp(b, 0, (int32_t)bank_count() - 1); }
static void bank_fix(void)                             /* LEN shortened (knob, project): onto the last bank */
{
    if (ui.bank >= bank_count())
        bank_set((int32_t)bank_count() - 1);
}

/* the NOTE of a step the grid keys turn on (spec §2, user 2026-10-10): a gated (ACID) track's last KEYS / recorded
 * note; a drum's C4 (its steps transposed only by KEYS record, step record's NOTE or the roll) */
static int8_t new_step_nt(const track_t *t) { return trk_model(t)->gate ? t->key_nt : 0; }

/* a step key down this long is a hold, not a tap (owner 2026-10-23: holding a step is for editing it, never toggles
 * it). 0.3 s: a deliberate tap on these keys is well under it (~0.1 s down), a hold to edit is felt as one past it,
 * and the STEP n PROB RATCH line comes up without a wait; under GLO / FX's 0.4 s and HOME / REC's 0.7 s holds */
#define STEP_HOLD_MS 300u

/* the step a grid key holds for editing: held STEP_HOLD_MS, or edited / accented while down (step_t0 bit 1, its
 * page and track unchanged); NSTEP none. A tap shows nothing (its line would only flash) */
static uint32_t held_step(void)
{
    uint32_t k = ui.held;
    return k < 16u && (ui.step_t0[k] & 2u) && ui.step_si[k] < NSTEP ? ui.step_si[k] : NSTEP;
}

/* key k of the grid pressed: the step it holds, bank * 16 + k, inside LEN only (a song's lock: none, saying so).
 * The press changes nothing: a short one toggles the step on the release (step_tap); a hold turns an off step on
 * (step_hold_on) and edits it (step_edit) */
static void step_grab(uint32_t k)
{
    uint32_t si = ui.bank * 16u + k;
    ui.step_si[k] = 0xFFFFu;
    if (song_lock())
        return;
    if (k >= 16u || si >= (uint32_t)TSEL->p[P_SLEN] || view_src(TSEL))
        return;
    ui.step_si[k] = (uint16_t)si;
}

/* key k's step held for editing (STEP_HOLD_MS down, or a knob edit while down; owner 2026-10-23): an OFF step turns
 * on there and then (plain, the note a tap's new step gets), lit, its line up and its knobs live; an ON step stays
 * exactly as it is. Once a press (bit 2) */
static void step_hold_on(uint32_t k)
{
    step_t *s;
    if (k >= 16u || (ui.step_t0[k] & 4u))
        return;
    ui.step_t0[k] |= 4u;
    if (ui.step_si[k] >= (uint32_t)TSEL->p[P_SLEN] || view_src(TSEL) || chain_busy())
        return;
    s = &TSEL->step[ui.step_si[k]];
    if (!s->on) {
        s->nt = new_step_nt(TSEL);                    /* spec §2: ACID's last played / recorded note, a drum's C4 */
        s->on = 1;
        s->acc = 0;
        s->tie = 0;                                   /* a TIE step turned on plays (acid_step reads TIE first) */
    }
}

/* key k let go after a short press (down less than STEP_HOLD_MS, no held-step edit, its page and track unchanged):
 * its step turns on, or off (then plain again: no accent, PROB 100 %, 1 hit) */
static void step_tap(uint32_t k)
{
    uint32_t si = ui.step_si[k];
    step_t *s;
    if (si >= (uint32_t)TSEL->p[P_SLEN] || view_src(TSEL) || chain_busy())
        return;
    s = &TSEL->step[si];
    if (s->on) {
        memset(s, 0, sizeof *s);
    } else {
        s->on = 1;
        s->acc = 0;
        s->tie = 0;                                   /* a TIE step turned on plays (acid_step reads TIE first) */
        s->nt = new_step_nt(TSEL);                    /* spec §2: ACID's last played / recorded note, a drum's C4 */
    }
}

/* key k pressed with OCT+ held (fixes 2026-10-21 item 1: the accent's gesture; a held key is PROB / RATCH's): step
 * bank * 16 + k accented, an off one turned on with it (a TIE: plays), an accented one plain again (still on), inside
 * LEN only, at the press; held, the key's step (a knob turned edits it as it is now; its release toggles nothing) */
static void step_accent(uint32_t k)
{
    uint32_t si = ui.bank * 16u + k;
    step_t *s;
    ui.step_si[k] = 0xFFFFu;
    if (song_lock())
        return;
    if (k >= 16u || si >= (uint32_t)TSEL->p[P_SLEN] || view_src(TSEL))
        return;
    s = &TSEL->step[si];
    if (!s->on) {
        s->nt = new_step_nt(TSEL);                    /* (as a tap's new step) */
        s->on = 1;
        s->tie = 0;
        s->acc = 1;
    } else {
        s->acc = (uint8_t)!s->acc;
    }
    ui.step_si[k] = (uint16_t)si;
}

static void track_clear(track_t *t)               /* its steps, its motion and its locks; its last KEYS note C4 again */
{
    memset(t->step, 0, sizeof t->step);
    t->key_nt = 0;
    motion_clear((uint32_t)(t - trk));
    lock_clear_track((uint32_t)(t - trk));
}

/* TOOLS CLR*: every track's pattern cleared and its pattern settings (LEN DIV SWG SRC) at default; sounds, FX,
 * Grids, COMP and the globals untouched */
static void seq_clear_all(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        track_clear(t);
        t->p[P_SLEN] = TP[P_SLEN].def;
        t->p[P_SDIV] = TP[P_SDIV].def;
        t->p[P_SSWING] = TP[P_SSWING].def;
        t->p[P_SRC] = TP[P_SRC].def;
    }
    memset(&chain.cfg, 0, sizeof chain.cfg);           /* the song too */
    ui.song_row = 0;
    ui.bank = 0;
    ui.force = 1;
}

/* TOOLS INIT*: everything at power-on (the default kit, sounds, patterns, FX, Grids, COMP, BPM): stopped, nothing
 * armed; saved projects and the device settings untouched */
static void init_all(void)
{
    uint32_t master = song.master_q12;                  /* the volume knob's level (main.c reads it), kept */
    transport_req = 2;
    fm1_irq_off();                                      /* the audio ISR must not see half a kit */
    drum_tracks_init();
    song.master_q12 = master;
    song.playing = 0;
    song.rec = 0;
    ui.rec_all = 0;                                     /* (no REC ALL: nothing armed) */
    song.solo = 0;
    memset(&mo, 0, sizeof mo);                     /* no motion */
    memset(&chain, 0, sizeof chain);               /* no song, no name */
    perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;   /* PERFORM's knob effects off (spec §7) */
    fm1_irq_on();
    ui.bank = 0;
    ui.song_row = 0;
    ui.force = 1;
}

/* track k muted / unmuted (MUTE: NOW, or NEXT BAR); a mute also cuts what it is playing (declicked, in the ISR) */
static void track_mute_toggle(uint32_t k)
{
    if (k >= NTRK)
        return;
    if (settings.mutebar) {                            /* on the next bar (seq.c; at once when stopped) */
        fm1_irq_off();
        song.mute_q ^= (uint8_t)(1u << k);
        fm1_irq_on();
        return;
    }
    trk[k].p[P_MUTE] = (int16_t)!trk[k].p[P_MUTE];
    if (trk[k].p[P_MUTE])
        panic_req |= (uint8_t)(1u << k);
}

/* SOLO (the SMC-Mixer): track k soloed / not; the tracks this silences cut what they play, as a mute does */
static void track_solo_toggle(uint32_t k)
{
    uint32_t i, was = 0;
    if (k >= NTRK)
        return;
    for (i = 0; i < NTRK; i++)
        was |= (uint32_t)trk_silent(&trk[i]) << i;
    song.solo ^= (uint8_t)(1u << k);
    for (i = 0; i < NTRK; i++)
        if (trk_silent(&trk[i]) && !((was >> i) & 1u))
            panic_req |= (uint8_t)(1u << i);
}

/* COMP: white key k turns DUCK of track k on / off (not the source's: it is never ducked) */
static void track_duck_toggle(uint32_t k)
{
    if (k < NTRK && k != comp_src())
        trk[k].p[P_DUCK] = (int16_t)!trk[k].p[P_DUCK];
}

/* --------------------------------------------------- track, model --- */
static void track_select(uint32_t i)
{
    if (i >= NTRK || i == song.sel)
        return;
    ui.srec = 0;                                       /* step record ends (leaving the page, another track) */
    song.sel = (uint8_t)i;
    grid_drop_holds();
    ui.bank = 0;
    ui.force = 1;
}

/* power-on, before audio_init (IRQs still off): SEQ held -> safe start. The HAL's polled scan
 * (TIMER4 waits, GPIO only) runs FM1_DEB_RELEASE + 4 frames (the longest debounce, upstream 1.0), ~7 ms */
static void safe_start_check(void)
{
    uint32_t k;
    for (k = 0; k < FM1_DEB_RELEASE + 4u; k++)
        fm1_input_scan();
    safe_start = (uint8_t)((fm1_in.buttons >> panel.btn[B_SEQ]) & 1u);
    if (safe_start)
        for (k = 0; k < SMP_USER_SLOTS; k++)
            usr_nz[k] = 0;
}

/* felucca_init (main.c): everything the drum firmware sets up before audio and USB start */
static void drum_boot_init(void)
{
    safe_start_check();
#if USB8_OK
    usb10 = (uint8_t)(usbaud != UA_2CH && !safe_start);   /* MENU > USB AUDIO (usb_start follows; SAFE START: 2 CH) */
    usb_fxpair = (uint8_t)(usbaud == UA_10FX);
#endif
    drum_tracks_init();
    home_tracks();                                     /* HOME's TRACKS mode (spec §3) */
    ui.force = 1;
    if (!safe_start)
        project_boot();                                /* the last saved project (project.c; no IRQ enable) */
}

/* the MODEL knob's list (fix 8, owner 2026-10-21): MD -> MODERN -> CLASSIC, as the preset banks; within MD and CLASSIC
 * the enum's order, MODERN as its bank (ACID, SMPL last). Only the order shown: the models keep their numbers (projects
 * store them, dmodels.c) */
static const uint8_t MODEL_LIST[NMODELS] = {
    DM_KTRX, DM_SDTRX, DM_XTTRX, DM_XCTRX, DM_CPTRX, DM_RSTRX, DM_CBTRX, DM_CHTRX, DM_OHTRX, DM_CYTRX, DM_MATRX, DM_CLTRX,
    DM_B2TRX, DM_BDEFM, DM_SDEFM, DM_XTEFM, DM_CPEFM, DM_RSEFM, DM_CBEFM, DM_HHEFM, DM_CYEFM,
    DM_KBOOM, DM_KPUNC, DM_SSNAP, DM_SCRAK, DM_HMETL, DM_HNOIS, DM_MEMB, DM_FM, DM_ACID, DM_SMPL,
    DM_K808, DM_K909, DM_S808, DM_S909, DM_C808, DM_C909, DM_HATC, DM_HATO, DM_CYMB, DM_COWB, DM_TOM, DM_CONGA, DM_RIM,
    DM_CLAVE,
};
static uint32_t model_list_idx(uint32_t m)           /* m's place in MODEL_LIST, from 0 (0: not in it) */
{
    uint32_t i;
    for (i = 0; i < NMODELS && MODEL_LIST[i] != m; i++)
        ;
    return i == NMODELS ? 0u : i;
}
static uint32_t model_list_next(uint32_t m, int32_t dir)   /* the model after / before m in MODEL_LIST, wrapping */
{
    return MODEL_LIST[(model_list_idx(m) + (dir > 0 ? 1u : NMODELS - 1u)) % NMODELS];
}

/* the next / previous model on the selected track (MODEL_LIST's order), with its default sound; with the audio IRQ off,
 * so the ISR never renders a model with another model's parameters */
static void model_step(int32_t dir)
{
    uint32_t m = model_list_next((uint32_t)TSEL->p[P_MODEL] % NMODELS, dir);
    int capped = m == DM_ACID && acid_tracks(TSEL) >= ACID_MAXTRK;   /* ACID on 2 tracks at most: the list skips it */
    if (capped)
        m = model_list_next(m, dir);
    fm1_irq_off();
    drum_set_model(TSEL, m);
    motion_rebase(song.sel);                        /* motion.c: the new sound is the base */
    fm1_irq_on();
    if (capped)
        ui_message("ACID: 2 TRACKS MAX");
    else
        ui_say("MODEL ", N_MODEL[m]);
    ui.force = 1;
}

/* HOME: KNOB k edits the selected model's macro k (TUNE DECAY TONE CHAR) */
static const param_desc_t *home_param(uint32_t k, int16_t **vp)
{
    *vp = &TSEL->p[P_E0 + (k & 3u)];
    return track_desc(TSEL, P_E0 + (k & 3u));
}

/* ---- parameter locks (spec 2026-10-24): in STEPS, off the SEQ pages, a held step + a recordable knob locks it */
#define LOCK_CLR_MS 600u                               /* step held + OCT- held this long: all its locks cleared */
static int lock_page(void) { return env_steps() && (ui.home || cur_page()->fam != FAM_SEQ); }   /* HOME TRACKS SOUND
                                                                                                * LAYER FX LFO COMP */
/* the step of the last-pressed key still down (from the press: a turn right after it locks), NSTEP none (not a lock
 * page, no key, outside LEN, a Grids track, a song's lock: step_grab) */
static uint32_t lock_step(void)
{
    uint32_t k = ui.held;
    if (!lock_page() || k >= 16u || !ui.step_t0[k] || !((fm1_in.notes >> STEP_KEY[k]) & 1u) || ui.step_si[k] >= NSTEP)
        return NSTEP;
    return ui.step_si[k];
}
/* .. shown (cells, LOCK STEP n): once held STEP_HOLD_MS or turned (a tap shows nothing, as held_step) */
static uint32_t lock_view_step(void)
{
    uint32_t s = lock_step();
    return s < NSTEP && (ui.step_t0[ui.held] & 2u) ? s : NSTEP;
}
/* the P_* id KNOB slot + 1 edits here when MOTION can record it (motion.c motion_param): a track page's, HOME's macros,
 * TRACKS' LEVEL and PAN; P_COUNT none (the model, TYPE, WAVE, DEST, SRC, the timing, a global page ...) */
static uint32_t lock_id(uint32_t slot)
{
    const page_t *pg = cur_page();
    const param_desc_t *d;
    int16_t *vp = 0;
    uint32_t id;
    if (slot > 3u)
        return P_COUNT;
    if (ui.home)
        d = home_param(slot, &vp);
    else if (pg->scope == SC_MIX)
        return slot == 1u ? (uint32_t)P_LEVEL : slot == 3u ? (uint32_t)P_PAN : (uint32_t)P_COUNT;
    else if (pg->scope == SC_TRACK)
        d = page_desc(pg, slot, &vp);
    else
        return P_COUNT;
    if (!d || !vp || vp < TSEL->p || vp >= TSEL->p + P_COUNT || !d->label || d->label[0] == '-' || d->max <= d->min)
        return P_COUNT;
    id = (uint32_t)(vp - TSEL->p);
    return motion_param(id) ? id : P_COUNT;
}
/* what a cell of knob id shows: with a step held, its lock there (*locked 1) or the knob's own value; else live */
static int16_t lock_shown(uint32_t id, int16_t live, int *locked)
{
    uint32_t s = lock_view_step();
    int16_t v;
    *locked = 0;
    if (s >= NSTEP || id >= P_COUNT)
        return live;
    if (lock_get(song.sel, s, id, &v)) {
        *locked = 1;
        return v;
    }
    return motion_rest(song.sel, id);
}
