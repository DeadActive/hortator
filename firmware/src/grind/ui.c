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

static const char *const N_GSTYLE[] = {                /* gm_style_name's 22 styles, four letters: a knob column's width */
    "TRAD", "HAMR", "BOMB", "GRAV", "BLST", "DBET", "SKNK", "2XBS", "TECH", "BRUT", "FSLO",
    "ABRP", "STOP", "BRKD", "2STP", "HALF", "CHAO", "INDU", "POLY", "SLDG", "FUNR", "STNR"};
typedef char gm_style_names_complete[sizeof N_GSTYLE / sizeof N_GSTYLE[0] == GM_ALGORITHM_COUNT ? 1 : -1];
static const char *const N_GWHEN[] = {"STEP", "END"};

enum { GU_STYLE, GU_AGGR, GU_DENS, GU_CHAOS, GU_HUMAN, GU_MUT, GU_WHEN, GU_N };
static const param_desc_t GP_GRIND[GU_N] = {
    [GU_STYLE] = PE("STYLE", N_GSTYLE, 0),
    [GU_AGGR] = PD("AGGR", F_PCT, 0, 100, 75),
    [GU_DENS] = PD("DENS", F_PCT, 0, 100, 65),
    [GU_CHAOS] = PD("CHAOS", F_PCT, 0, 100, 20),
    [GU_HUMAN] = PD("HUMAN", F_PCT, 0, 100, 25),
    [GU_MUT] = PD("MUT", F_INT, 0, 99, 0),
    [GU_WHEN] = PE("WHEN", N_GWHEN, 1),
};
static int16_t gm_v[GU_N] = {0, 75, 65, 20, 25, 0, 1};   /* = the descriptors' defaults: the factory voicing of style 1 */

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
    gm_runtime_poll();
    n = gm_runtime_commits();
    if (n != seen) {
        seen = n;
        ui_message(gm_style_name((uint8_t)(gm_v[GU_STYLE] + 1)));   /* (the full name: the column has four letters) */
        return;
    }
    st = gm_runtime_status();
    if (st != GM_OK) {
        ui_message(st == GM_STALE ? "NOT APPLIED" : "GRIND ?");
        gm_runtime_ack_status();
    }
}

#endif
