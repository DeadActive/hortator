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
    "ABRP", "STOP", "BRKD", "2STP", "HALF", "CHAO", "INDU", "POLY", "SLDG", "FUNR", "STNR"};
typedef char gm_style_names_complete[sizeof N_GSTYLE / sizeof N_GSTYLE[0] == GM_ALGORITHM_COUNT ? 1 : -1];
static const char *const N_GWHEN[] = {"STEP", "END"};
static const char *const N_GKIT[] = {"--", "ROLE"};
static const char *const N_GUNDO[] = {"--", "UNDO"};
static const char *const N_GFREE[] = {"--", "ALL"};

enum { GU_STYLE, GU_AGGR, GU_DENS, GU_CHAOS, GU_HUMAN, GU_MUT, GU_WHEN, GU_KIT, GU_KEEP, GU_UNDO, GU_FREE, GU_N };
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
};
static int16_t gm_v[GU_N] = {0, 75, 65, 20, 25, 0, 1, 0, 0, 0, 0};   /* = the descriptors' defaults: the factory voicing of style 1 */

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
    if (style >= GM_ALGORITHM_COUNT)                   /* (a damaged record: the default style) */
        style = 0;
    gm_v[GU_STYLE] = (int16_t)style;
    gm_v[GU_WHEN] = (v >> 5) & 1u ? 0 : 1;
    if (gm_factory_config((uint8_t)(style + 1u), &f) == GM_OK) {
        gm_v[GU_AGGR] = f.aggression;
        gm_v[GU_DENS] = f.density;
        gm_v[GU_CHAOS] = f.chaos;
        gm_v[GU_HUMAN] = f.humanize;
    }
    gm_v[GU_MUT] = 0;
}
static uint8_t gm_save_t;                              /* frames until a changed style / WHEN is written (a turn through
                                                        * the list writes once, not at every detent) */

/* the knobs as a config: the style's factory grid and seed, the four controls, MUT as the mutation counter */
static gm_result gm_ui_config(gm_config *c)
{
    gm_result r = gm_factory_config((uint8_t)(gm_v[GU_STYLE] + 1), c);
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

/* a knob of the GRIND pages was turned (edit_param): the style brings its factory controls, then the request goes out */
static void gm_ui_changed(uint32_t id)
{
    gm_config c;
    gm_result r;
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
            gm_keep_track(song.sel, 1);
            ui_message("TRACK KEPT");
        } else if ((uint32_t)gm_v[GU_KEEP] < n) {
            gm_keep_track(song.sel, 0);
            ui_message("TRACK FREE");
        }
        gm_v[GU_KEEP] = (int16_t)gm_keep_count(song.sel);
        return;
    }
    if (id == GU_FREE) {
        if (gm_v[GU_FREE]) {
            gm_v[GU_FREE] = 0;
            gm_keep_free();
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
        if (gm_factory_config((uint8_t)(gm_v[GU_STYLE] + 1), &f) == GM_OK) {
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
    if (r == GM_UNSUPPORTED)
        ui_message(chain.running ? "STOP THE SONG" : "NOT NOW");
    else if (r != GM_OK)
        ui_message("GRIND ?");
}

/* every UI frame: the runtime's main-loop work, and what became of the requests (said once each) */
static void gm_ui_frame(void)
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
    n = gm_runtime_commits();
    if (n != seen) {
        seen = n;
        ui_message(gm_style_name((uint8_t)(gm_v[GU_STYLE] + 1)));   /* (the full name: the column has four letters) */
        return;
    }
    st = gm_runtime_status();
    if (st != GM_OK) {
        ui_message(st == GM_STALE ? "NOT APPLIED" : st == GM_LOCK_CONFLICT ? "KEEP DOES NOT FIT" : "GRIND ?");
        gm_runtime_ack_status();
    }
}

#endif
