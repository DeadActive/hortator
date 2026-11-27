/* SPDX-License-Identifier: GPL-3.0-only */
/* The GRIND pages (GRIDS family: press ARP again to step on from GRIDS): the style and the four controls of the portable
 * rhythm engine as ordinary knob pages. A turned knob builds a config from the style's factory values and the knobs, and
 * hands it to the runtime, which generates in the main loop and publishes at the native cue boundary (WHEN: STEP the
 * next step, END the end of the longest pattern). GRIND_NATIVE only; included once, after runtime.c and before pages.c.
 * The values are screen state: never stored in a project, never an editor parameter. */
#ifndef GRIND_UI_C
#define GRIND_UI_C

static void ui_say(const char *a, const char *b);      /* ui.c */
static void ui_message(const char *s);
static void settings_save(void);                       /* project.c: the device settings, written once stopped */

static const char *const N_GSTYLE[] = {                /* gm_style_name's 22 styles, four letters: a knob column's width */
    "TRAD", "HAMR", "BOMB", "GRAV", "BLST", "DBET", "SKNK", "2XBS", "TECH", "BRUT", "FSLO",
    "ABRP", "STOP", "BRKD", "2STP", "HALF", "CHAO", "INDU", "POLY", "SLDG", "FUNR", "STNR", "MATH"};
/* The STYLE list: the 22 styles, then MATH = Technical (9) on a 7/4 grid (3+2+2 quarters, 28 sixteenths). The extra
 * entry is a UI choice over the same core, so the core's ids, the saved bytes and the golden patterns stay as they were. */
#define GM_UI_STYLES (GM_ALGORITHM_COUNT + 1u)
typedef char gm_style_names_complete[sizeof N_GSTYLE / sizeof N_GSTYLE[0] == GM_UI_STYLES ? 1 : -1];
static gm_result gm_ui_style_config(uint32_t idx, gm_config *c)
{
    gm_result r;
    if (idx >= GM_UI_STYLES)
        return GM_INVALID;
    if (idx < GM_ALGORITHM_COUNT)
        return gm_factory_config((uint8_t)(idx + 1u), c);
    r = gm_factory_config(9, c);
    if (r == GM_OK)
        c->grid = (gm_grid){7, 4, 16, 1};
    return r;
}
static const char *gm_ui_style_name(uint32_t idx)
{
    return idx < GM_ALGORITHM_COUNT ? gm_style_name((uint8_t)(idx + 1u)) : idx == GM_ALGORITHM_COUNT ? "Mathcore 7/4" : "";
}
static const char *const N_GWHEN[] = {"STEP", "END"};
static const char *const N_GKIT[] = {"--", "ROLE"};
static const char *const N_GUNDO[] = {"--", "UNDO"};
static const char *const N_GFREE[] = {"--", "ALL"};
static const char *const N_GOK[] = {"--", "OK"};

enum { GU_STYLE, GU_AGGR, GU_DENS, GU_CHAOS, GU_HUMAN, GU_MUT, GU_WHEN, GU_KIT, GU_KEEP, GU_UNDO, GU_FREE, GU_BROWSE, GU_PVBANK, GU_OK, GU_VARY, GU_N };
static const param_desc_t GP_GRIND[GU_N] = {
    [GU_STYLE] = PE("STYLE", N_GSTYLE, 0),
    [GU_AGGR] = PD("AGGR", F_PCT, 0, 100, 75),
    [GU_DENS] = PD("DENS", F_PCT, 0, 100, 65),
    [GU_CHAOS] = PD("CHAOS", F_PCT, 0, 100, 20),
    [GU_HUMAN] = PD("HUMAN", F_PCT, 0, 100, 25),
    [GU_MUT] = PD("MUT", F_INT, 0, 99, 0),
    [GU_WHEN] = PE("WHEN", N_GWHEN, 1),
    [GU_KEEP] = PD("KEEP", F_INT, 0, NSTEP, 0),       /* how many steps of the selected track the generator leaves alone: up keeps
                                                       * the whole track, down frees it (one step: hold its key in STEPS) */
    [GU_UNDO] = PE("UNDO", N_GUNDO, 0),               /* an action, and its own undo: the last GRIND change back, again: forward */
    [GU_FREE] = PE("FREE", N_GFREE, 0),               /* an action: no step of any track kept */
    [GU_KIT] = PE("KIT", N_GKIT, 0),                  /* an action: ROLE sets the eight sounds (twice to act) */
    [GU_BROWSE] = PE("STYLE", N_GSTYLE, 0),           /* the style browser: the style looked at, nothing live changes */
    [GU_PVBANK] = PD("BANK", F_INT, 1, 4, 1),         /* which 16 steps of a longer pattern the preview shows */
    [GU_VARY] = PD("VARY", F_INT, 0, 100, 0),         /* natural hit variation: a velocity swing per hit (seq.c), no pattern change */
    [GU_OK] = PE("APPLY", N_GOK, 0),                  /* an action: the browsed style becomes the committed one */
};
static int16_t gm_v[GU_N] = {0, 75, 65, 20, 25, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0};   /* = the descriptors' defaults: the factory voicing of style 1 */

/* KIT: the eight tracks' sounds for the eight roles the generator writes (gm_role order: K S H R C X TH TL), from the
 * models the default kit already has. The cymbals share one model and the toms another, told apart by TUNE and
 * DECAY (semitones, 0..127): these offsets are starting points, not tuned by ear. It replaces the tracks' models and
 * sound knobs, so it asks twice and works only while stopped; levels, pans, FX sends and the patterns stay. */
static const struct { uint8_t model, note; int8_t tune, decay; } GM_ROLE_KIT[GM_TRACKS] = {
    {DM_KTRX, 36, 0, -1},   /* kick */
    {DM_SDTRX, 38, 0, -1},  /* snare */
    {DM_CHTRX, 42, 0, -1},  /* hi-hat */
    {DM_CYTRX, 51, -5, 55}, /* ride */
    {DM_CYTRX, 49, 0, 70},  /* crash */
    {DM_CYTRX, 52, 5, 45},  /* china */
    {DM_XTTRX, 50, 5, -1},  /* tom, high */
    {DM_XTTRX, 45, -5, -1}, /* tom, low */
};
static uint8_t gm_kit_arm;                             /* frames left to confirm ROLE (counted down in gm_ui_frame) */

static void gm_kit_roles(void)
{
    uint32_t k;
    fm1_irq_off();                                     /* the audio ISR must not see half a kit */
    for (k = 0; k < GM_TRACKS; k++) {
        track_t *t = &trk[k];
        const dmodel_t *m = &DMODELS[GM_ROLE_KIT[k].model];
        drum_set_model(t, GM_ROLE_KIT[k].model);
        t->p[P_NOTE] = GM_ROLE_KIT[k].note;
        t->p[P_E0] = (int16_t)clamp(m->edit[0].def + GM_ROLE_KIT[k].tune, m->edit[0].min, m->edit[0].max);
        if (GM_ROLE_KIT[k].decay >= 0)
            t->p[P_E1] = (int16_t)clamp(GM_ROLE_KIT[k].decay, m->edit[1].min, m->edit[1].max);
        t->pre_ed = 1;                                 /* (a SOUND knob moved since the preset: presets.c) */
    }
    fm1_irq_on();
}

/* Saved with the device settings (project.c persist_t.palette, bits 18..23: free until now), not in a project, so no
 * stored format grows or changes: STYLE (5 bits) and WHEN (1 bit, stored inverted: a record from a firmware without the
 * engine has these bits clear and so reads STYLE 1, WHEN END, the defaults). AGGR DENS CHAOS HUMAN are the style's
 * factory values at power-on (MUT 0): the style is what is remembered, not the last knob positions. */
#define GM_SET_SHIFT 18u
static uint32_t gm_settings_pack(void)
{
    return ((uint32_t)gm_v[GU_STYLE] & 31u) | (uint32_t)(gm_v[GU_WHEN] ? 0u : 1u) << 5;
}
static void gm_settings_unpack(uint32_t v)
{
    gm_config f;
    uint32_t style = v & 31u;
    if (style >= GM_UI_STYLES)                         /* (a damaged record: the default style) */
        style = 0;
    gm_v[GU_STYLE] = (int16_t)style;
    gm_v[GU_WHEN] = (v >> 5) & 1u ? 0 : 1;
    if (gm_ui_style_config(style, &f) == GM_OK) {
        gm_v[GU_AGGR] = f.aggression;
        gm_v[GU_DENS] = f.density;
        gm_v[GU_CHAOS] = f.chaos;
        gm_v[GU_HUMAN] = f.humanize;
    }
    gm_v[GU_MUT] = 0;
}
static gm_result gm_last_result;                     /* retained for the USB editor after the panel reports it */
static uint8_t gm_save_t;                              /* frames until a changed style / WHEN is written (a turn through
                                                        * the list writes once, not at every detent) */

/* the knobs as a config: the style's factory grid and seed, the four controls, MUT as the mutation counter */
static gm_result gm_ui_config(gm_config *c)
{
    gm_result r = gm_ui_style_config((uint32_t)gm_v[GU_STYLE], c);
    if (r != GM_OK)
        return r;
    c->aggression = (uint8_t)gm_v[GU_AGGR];
    c->density = (uint8_t)gm_v[GU_DENS];
    c->chaos = (uint8_t)gm_v[GU_CHAOS];
    c->humanize = (uint8_t)gm_v[GU_HUMAN];
    c->mutation = (uint32_t)gm_v[GU_MUT];
    c->bpm = (uint16_t)clamp(song.g[G_BPM], 40, 350);   /* (the tempo stays the player's: it only scales the micro timing) */
    return GM_OK;
}

/* STYLE BROWSER (the fourth GRIND page): look at a style on the live pattern before it goes live. The preview is made by
 * the runtime's own gm_runtime_make (the live pattern and the KEEP mask as the base, the style's factory controls), so it
 * is what APPLY would publish; it is made in the main loop only while no stage is in use (the base shares the stage's
 * bytes), else it says BUSY and tries again. Only compact row bitmaps are kept; nothing is published while browsing, and
 * leaving the page puts the cursor back on the committed style. The style's tempo is shown, never applied. */
enum { PV_NONE, PV_READY, PV_BUSY, PV_FAIL };
static struct {
    uint8_t on[GM_TRACKS][8], acc[GM_TRACKS][8], rat[GM_TRACKS][8];   /* bit s of row t: the step is on / accented / ratcheted */
    uint8_t len, div, num, den, state, idx, res;
    uint16_t bpm;                                      /* the style's own (a label) */
    uint32_t rev;                                      /* the live pattern's revision it was made from */
} gm_pv;

static void gm_pv_update(void)
{
    gm_config c;
    uint32_t idx = (uint32_t)gm_v[GU_BROWSE], k, s, rev = gmrt.ctl.native_revision;
    gm_result r;
    if ((gm_pv.state == PV_READY || gm_pv.state == PV_FAIL) && gm_pv.idx == idx && gm_pv.rev == rev)
        return;
    gm_pv.idx = (uint8_t)idx;
    if (gmrt.ctl.stage_state != GM_STAGE_FREE || gmrt.ctl.has_pending) {   /* a request is staged: wait, never share its bytes */
        gm_pv.state = PV_BUSY;
        return;
    }
    r = gm_ui_style_config(idx, &c);
    if (r == GM_OK) {
        gm_pv.bpm = c.bpm;
        gm_pv.num = c.grid.num;
        gm_pv.den = c.grid.den;
        c.bpm = (uint16_t)clamp(song.g[G_BPM], 40, 350);   /* (as a submitted request: the tempo stays the player's) */
        r = gm_runtime_make(&c);
    }
    gm_pv.rev = rev;
    gm_pv.res = (uint8_t)r;
    if (r != GM_OK) {
        gm_pv.state = PV_FAIL;
        return;
    }
    memset(gm_pv.on, 0, sizeof gm_pv.on);
    memset(gm_pv.acc, 0, sizeof gm_pv.acc);
    memset(gm_pv.rat, 0, sizeof gm_pv.rat);
    gm_pv.len = gmrt.out.timing[0].length;
    gm_pv.div = gmrt.out.timing[0].division;
    for (k = 0; k < GM_TRACKS; k++)
        for (s = 0; s < gmrt.out.timing[k].length && s < GM_STEPS; s++) {
            gm_cell a = gmrt.out.cell[k][s];
            if (a.flags & GM_ON)
                gm_pv.on[k][s >> 3] |= (uint8_t)(1u << (s & 7u));
            if (a.flags & GM_ACC)
                gm_pv.acc[k][s >> 3] |= (uint8_t)(1u << (s & 7u));
            if (a.flags >> GM_RAT_SHIFT)
                gm_pv.rat[k][s >> 3] |= (uint8_t)(1u << (s & 7u));
        }
    gm_pv.state = PV_READY;
}

/* a knob of the GRIND pages was turned (edit_param): the style brings its factory controls, then the request goes out */
static void gm_ui_changed(uint32_t id)
{
    gm_config c;
    gm_result r;
    if (id == GU_VARY) {                               /* a feel, not a pattern: read by the sequencer per hit */
        gm_vary = (uint8_t)gm_v[GU_VARY];
        return;
    }
    if (id == GU_BROWSE || id == GU_PVBANK)
        return;                                        /* looking: nothing live changes, the frame makes the preview */
    if (id == GU_OK) {                                 /* APPLY: the browsed style, as a STYLE turn (its factory controls) */
        if (!gm_v[GU_OK])
            return;
        gm_v[GU_OK] = 0;
        gm_v[GU_STYLE] = gm_v[GU_BROWSE];
        id = GU_STYLE;
    }
    if (id == GU_KIT) {                                /* a GO button: one detent arms, a second one within ~1.5 s acts */
        if (!gm_v[GU_KIT])
            return;
        gm_v[GU_KIT] = 0;
        if (!gm_kit_arm) {
            gm_kit_arm = 90;
            ui_say("AGAIN: ", "ROLE KIT");
            return;
        }
        gm_kit_arm = 0;
        if (song.playing || transport_req == 1u) {
            ui_message("STOP FIRST");
            return;
        }
        gm_kit_roles();
        ui_message("ROLE KIT SET");
        return;
    }
    if (id == GU_KEEP) {                               /* the knob shows the count: up keeps the track, down frees it */
        uint32_t n = gm_keep_count(song.sel);
        if ((uint32_t)gm_v[GU_KEEP] > n) {
            fm1_irq_off();
            gm_keep_track(song.sel, 1);
            fm1_irq_on();
            ui_message("TRACK KEPT");
        } else if ((uint32_t)gm_v[GU_KEEP] < n) {
            fm1_irq_off();
            gm_keep_track(song.sel, 0);
            fm1_irq_on();
            ui_message("TRACK FREE");
        }
        gm_v[GU_KEEP] = (int16_t)gm_keep_count(song.sel);
        return;
    }
    if (id == GU_FREE) {
        if (gm_v[GU_FREE]) {
            gm_v[GU_FREE] = 0;
            fm1_irq_off();
            gm_keep_free();
            fm1_irq_on();
            ui_message("ALL FREE");
        }
        return;
    }
    if (id == GU_UNDO) {
        int u;
        if (!gm_v[GU_UNDO])
            return;
        gm_v[GU_UNDO] = 0;
        u = gm_runtime_undo();
        ui_message(u < 0 ? "STOP THE SONG" : !u ? "NOTHING TO UNDO" : gmrt.undo_was_undo ? "UNDONE" : "REDONE");
        return;
    }
    if (id == GU_STYLE) {
        gm_config f;
        if (gm_ui_style_config((uint32_t)gm_v[GU_STYLE], &f) == GM_OK) {
            gm_v[GU_AGGR] = f.aggression;
            gm_v[GU_DENS] = f.density;
            gm_v[GU_CHAOS] = f.chaos;
            gm_v[GU_HUMAN] = f.humanize;
            gm_v[GU_MUT] = 0;
        }
    }
    if (id == GU_STYLE || id == GU_WHEN)
        gm_save_t = 90;                                /* (about 1.5 s after the last turn) */
    r = gm_ui_config(&c);
    if (r == GM_OK)
        r = gm_runtime_submit(&c, gm_v[GU_WHEN] ? GM_BOUNDARY_PATTERN : GM_BOUNDARY_STEP,
                              id == GU_STYLE ? GM_REQUEST_STYLE : id == GU_MUT ? GM_REQUEST_MUTATE : GM_REQUEST_MORPH);
    gm_last_result = r;
    if (r == GM_UNSUPPORTED)
        ui_message(chain.running ? "STOP THE SONG" : "NOT NOW");
    else if (r != GM_OK)
        ui_message("GRIND ?");
}

/* every UI frame: the runtime's main-loop work, and what became of the requests (said once each) */
static void gm_ui_frame(int browsing)                  /* browsing: the STYLE BROWSER page is the one shown */
{
    static uint32_t seen;
    uint32_t n;
    gm_result st;
    if (gm_kit_arm)
        gm_kit_arm--;
    if (gm_save_t && !--gm_save_t)
        settings_save();
    gm_v[GU_KEEP] = (int16_t)gm_keep_count(song.sel);  /* (the selected track's: it follows the selection) */
    gm_runtime_poll();
    if (browsing) {
        gm_pv_update();
        if (gm_pv.state == PV_READY && gm_v[GU_PVBANK] > (int16_t)((gm_pv.len + 15u) / 16u))
            gm_v[GU_PVBANK] = (int16_t)((gm_pv.len + 15u) / 16u);   /* (a 16-step style has one bank) */
    } else {
        gm_v[GU_BROWSE] = gm_v[GU_STYLE];                /* (leaving the page cancels: the cursor is the committed style) */
    }
    n = gm_runtime_commits();
    if (n != seen) {
        seen = n;
        ui_message(gm_ui_style_name((uint32_t)gm_v[GU_STYLE]));   /* (the full name: the column has four letters) */
        return;
    }
    st = gm_runtime_status();
    if (st != GM_OK) {
        gm_last_result = st;
        ui_message(st == GM_STALE ? "NOT APPLIED" : st == GM_LOCK_CONFLICT ? "KEEP DOES NOT FIT" : "GRIND ?");
        gm_runtime_ack_status();
    }
}

#endif
