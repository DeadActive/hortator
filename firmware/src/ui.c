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
static int project_used(uint32_t slot);
static uint32_t chain_prepare(void);
static int project_name(uint32_t slot, char *b);
static void project_rename(uint32_t slot, const char *name);
static int transport_busy(void);
static int name_on(void);                  /* ui_name.c (NAME) */
static void draw_name(void);
static void panel_setup(void);

#define ACC C_HI
#define VAL(c) ((c) == ui.hot_col && ui.hot_t ? C_WHITE : C_HI)
#define RATIO(d, v) ((d)->max > (d)->min ? (enum_rank((d), (int32_t)(v)) - (d)->min) * 1000 / ((d)->max - (d)->min) : -1)
#define Y_HEAD 0
#define H_HEAD 20
#define Y_LABEL 26
#define Y_VALUE 44
#define Y_GAUGE 64
#define Y_SEP_END 70
#define Y_GRAPH 74
#define H_GRAPH 124
#define G_OY 24
#define Y_FOOT 202
#define H_FOOT 38

static struct {
    uint8_t home;
    uint8_t page;                /* index into PAGES */
    uint8_t fam_last[FAM_COUNT]; /* last page used per family */
    uint8_t bank;                /* STEP grid: 16-step bank shown on the keys */
    uint8_t hot_col, hot_t;      /* column whose knob was just turned (drawn white) */
    uint8_t menu;                /* 0 off, 1 list, 2 about (HOME held) */
    uint8_t menu_sel;
    uint32_t menu_sig, home_t0;
    uint8_t force;               /* full redraw pending */
    uint8_t msg_t;               /* transient message frames */
    uint8_t bpm_t;
    uint8_t arm, arm_t;          /* destructive action armed: param id, frames left to confirm */
    uint32_t rec_t0;             /* REC press time (btn_hold) */
    uint32_t step_t0[16];        /* STEP grid: a white key's press time (as btn_hold: bit1 = the hold acted) */
    uint16_t step_si[16];        /* the step it pressed, 0xFFFF none */
    step_t step_prev[16];        /* that step before the press (a hold or a knob turn restores it) */
    uint8_t held;                /* the grid key pressed last (valid while step_t0[held] is set) */
    uint8_t confirm;             /* 1 = "clear track n?" */
    uint8_t confirm_trk;
    char msg[24];
    uint32_t enc_t[NE];
    char col[4][32];
    char focus_l[8], focus_v[8], focus_u[8];
    uint32_t graph_sig, head_sig, foot_sig, frame;
    uint8_t graph_top;
    int16_t tr[136];             /* LFO pages, random waves: the shown LFO's trail (a ring, newest at tr_h - 1) */
    uint8_t tr_h, tr_n;
    uint32_t tr_ph, tr_key, tr_frame;   /* the phase at the last point; what the trail is of; the frame it was fed */
    uint8_t persist_pending;     /* a settings save asked for while playing: written once stopped (project.c) */
    uint32_t fx_t0;              /* PERFORM: FX's press time | FX_DOWN / FX_OPEN / FX_DEAD (ui_input.c fx_layer) */
    uint8_t layer;               /* PERFORM: the layer's map shows (FX held open, or a layer key still held) */
    uint8_t pg_open, pg_page, pg_home;   /* PERFORM PAGE: the screen open; the page / HOME under it when it opened */
    uint8_t song_row;            /* SONG: the row selected (count = the + row) */
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
static int mix_mode(void) { return !ui.home && !ui.menu && cur_page()->scope == SC_MIX; }   /* TRACKS: keys mute */
static int comp_mode(void) { return !ui.home && !ui.menu && cur_page()->graph == GR_COMP; }   /* COMP: keys DUCK */
/* the white keys are the selected track's steps (the bank's 16): every SEQ page, and TRACKS while nothing is armed
 * (user, 2026-10-08) */
static int step_keys(void)
{
    return !ui.home && !ui.menu && (cur_page()->fam == FAM_SEQ || (cur_page()->scope == SC_MIX && !song.rec));
}

/* a held grid key lets go of its step (another track or page): keys still held edit nothing, flip no accent */
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
    grid_drop_holds();
    song.seq_mode = (uint8_t)(step_keys() || comp_mode() ? 1u : mix_mode() ? (song.rec ? 2u : 1u) : 0u);   /* seq.c: the keys
                                                     * belong to the steps / the ducks / TRACKS (armed: play, REC held: mute) */
    ui.hot_t = 0;
    ui.force = 1;
}

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

static void go_home(void)
{
    ui.home = 1;
    ui.hot_t = 0;
    song.seq_mode = 0;
    ui.force = 1;
}

static void home_step(void)                           /* HOME: the HOME screen -> COMP 2/3 -> COMP 3/3 -> HOME */
{
    if (ui.home) {
        ui.page = (uint8_t)page_first(FAM_HOME);
        ui.home = 0;
        page_entered();
    } else if (cur_page()->fam == FAM_HOME && ui.page + 1u < NPAGES && PAGES[ui.page + 1u].fam == FAM_HOME) {
        ui.page++;
        page_entered();
    } else {
        go_home();
    }
}

/* PRESET: the current section's next / previous page (its empty pages skipped), stopping at the ends; HOME 1/3 is
 * the HOME section's first page (then COMP 2/3, 3/3) */
static void page_turn(int32_t dir)
{
    uint32_t fam, i;
    if (ui.home) {
        if (dir > 0)
            home_step();                              /* HOME 1/3 -> COMP 2/3 */
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
        go_home();                                    /* COMP 2/3 back to HOME 1/3 */
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
    step_t r = {0, 0, 0, 0};
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

/* key k of the grid pressed: step bank * 16 + k turns on, or off (then plain again: no accent, PROB 100 %,
 * 1 hit), inside LEN only; the step before the press is kept for a hold / a knob turn */
static void step_press(uint32_t k)
{
    uint32_t si = ui.bank * 16u + k;
    step_t *s;
    ui.step_si[k] = 0xFFFFu;
    if (song_lock())
        return;
    if (k >= 16u || si >= (uint32_t)TSEL->p[P_SLEN] || view_src(TSEL))
        return;
    s = &TSEL->step[si];
    ui.step_si[k] = (uint16_t)si;
    ui.step_prev[k] = *s;
    if (s->on) {
        memset(s, 0, sizeof *s);
    } else {
        s->on = 1;
        s->acc = 0;
    }
}

/* key k held STEP_HOLD: the step as before the press, on, its accent flipped */
static void step_hold(uint32_t k)
{
    step_t *s;
    if (k >= 16u || ui.step_si[k] >= (uint32_t)TSEL->p[P_SLEN] || view_src(TSEL))
        return;
    s = &TSEL->step[ui.step_si[k]];
    *s = ui.step_prev[k];
    s->on = 1;
    s->acc = (uint8_t)!ui.step_prev[k].acc;
}

static void track_clear(track_t *t)               /* its steps and its motion */
{
    memset(t->step, 0, sizeof t->step);
    motion_clear((uint32_t)(t - trk));
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
    memset(&mo, 0, sizeof mo);                     /* no motion */
    memset(&chain, 0, sizeof chain);               /* no song, no name */
    fm1_irq_on();
    ui.bank = 0;
    ui.song_row = 0;
    ui.force = 1;
}

/* TRACKS: white key k mutes / unmutes track k; a mute also cuts what it is playing (declicked, in the ISR) */
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
    drum_tracks_init();
    ui.home = 1;
    ui.force = 1;
}

/* the next / previous model on the selected track, with its default sound; with the audio IRQ off,
 * so the ISR never renders a model with another model's parameters */
static void model_step(int32_t dir)
{
    uint32_t m = ((uint32_t)TSEL->p[P_MODEL] + (dir > 0 ? 1u : NMODELS - 1u)) % NMODELS;
    fm1_irq_off();
    drum_set_model(TSEL, m);
    motion_rebase(song.sel);                        /* motion.c: the new sound is the base */
    fm1_irq_on();
    ui_say("MODEL ", N_MODEL[m]);
    ui.force = 1;
}

/* HOME: KNOB k edits the selected model's macro k (TUNE DECAY TONE CHAR) */
static const param_desc_t *home_param(uint32_t k, int16_t **vp)
{
    *vp = &TSEL->p[P_E0 + (k & 3u)];
    return track_desc(TSEL, P_E0 + (k & 3u));
}
