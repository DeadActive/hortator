/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Drum machine user interface: state, page navigation, track / model selection, the STEP grid.
 * Four columns map to KNOB 1..4; rendering (ui_draw.c) is lazy: every element remembers what it
 * last drew and is redrawn only on change. */
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "DRUM-0.1"
#endif
static void project_save(uint32_t slot);
static void project_load(uint32_t slot);
static int project_used(uint32_t slot);
static void panel_setup(void);

#define ACC C_HI
#define VAL(c) ((c) == ui.hot_col && ui.hot_t ? C_WHITE : C_HI)
#define RATIO(d, v) ((d)->max > (d)->min ? ((int32_t)(v) - (d)->min) * 1000 / ((d)->max - (d)->min) : -1)
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

static int grid_mode(void) { return !ui.home && !ui.menu && cur_page()->scope == SC_GRID; }

static void page_entered(void)
{
    song.seq_mode = (uint8_t)grid_mode();            /* seq.c: the keys belong to the grid */
    ui.hot_t = 0;
    ui.force = 1;
}

static void open_family(uint32_t fam)
{
    if (!ui.home && cur_page()->fam == fam) {          /* same button again: next page */
        uint32_t i = ui.page + 1u;
        if (i >= NPAGES || PAGES[i].fam != fam)
            i = page_first(fam);
        ui.page = (uint8_t)i;
    } else {
        ui.page = ui.fam_last[fam] && PAGES[ui.fam_last[fam]].fam == fam ? ui.fam_last[fam]
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
        return t->step[si % NSTEP];
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
    if (k >= 16u || ui.step_si[k] == 0xFFFFu)
        return;
    s = &TSEL->step[ui.step_si[k]];
    *s = ui.step_prev[k];
    s->on = 1;
    s->acc = (uint8_t)!ui.step_prev[k].acc;
}

static void track_clear(track_t *t) { memset(t->step, 0, sizeof t->step); }

/* --------------------------------------------------- track, model --- */
static void track_select(uint32_t i)
{
    if (i >= NTRK || i == song.sel)
        return;
    song.sel = (uint8_t)i;
    ui.bank = 0;
    ui.force = 1;
}

/* power-on, before audio_init (IRQs still off): SEQ held -> safe start. The HAL's polled scan
 * (TIMER4 waits, GPIO only) runs FM1_DEBOUNCE + 4 frames, ~7 ms */
static void safe_start_check(void)
{
    uint32_t k;
    for (k = 0; k < FM1_DEBOUNCE + 4u; k++)
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
