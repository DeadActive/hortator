/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Drum UI pages: a family per page button (pressing it again steps through its pages), and what
 * the four knobs edit on each page. */
enum { FAM_HOME, FAM_SND, FAM_LAY, FAM_FX, FAM_SEQ, FAM_GLO, FAM_SAVE, FAM_GRIDS, FAM_LFO, FAM_MIX, FAM_COUNT };
enum { SC_TRACK, SC_GLOBAL, SC_GRID, SC_MIX };   /* knobs edit: the selected track, song.g, the STEP grid, the mixer */
enum { GR_NONE, GR_MODEL, GR_FX, GR_SLCR, GR_GRID, GR_STEPS, GR_SLOTS, GR_MIX, GR_GRIDS, GR_COMP, GR_LFO, GR_RESON };

#define SND_SLOT 0xC0u                            /* EDIT page ids: SND_SLOT + n = the track's n-th sound knob */

typedef struct {
    const char *title;
    uint8_t fam, scope, graph;
    uint8_t id[4];               /* parameter ids; 0xFF = empty column */
} page_t;

static const page_t PAGES[] = {
    /* EDIT: the selected track's sound list (snd_list), 4 a page; a page past its end is not shown */
    {"SOUND", FAM_SND, SC_TRACK, GR_MODEL, {SND_SLOT + 0, SND_SLOT + 1, SND_SLOT + 2, SND_SLOT + 3}},
    {"SOUND", FAM_SND, SC_TRACK, GR_MODEL, {SND_SLOT + 4, SND_SLOT + 5, SND_SLOT + 6, SND_SLOT + 7}},
    {"SOUND", FAM_SND, SC_TRACK, GR_MODEL, {SND_SLOT + 8, SND_SLOT + 9, SND_SLOT + 10, SND_SLOT + 11}},
    {"SOUND", FAM_SND, SC_TRACK, GR_MODEL, {SND_SLOT + 12, SND_SLOT + 13, SND_SLOT + 14, SND_SLOT + 15}},
    {"LAYER", FAM_LAY, SC_TRACK, GR_NONE, {P_LSET, P_LKEY, P_LLEVEL, P_LTUNE}},
    {"LAYER", FAM_LAY, SC_TRACK, GR_NONE, {P_LDEC, 0xFF, 0xFF, 0xFF}},
    {"FX", FAM_FX, SC_TRACK, GR_FX, {P_DIST, P_CHOR, P_DLY, P_REV}},
    {"SLICER", FAM_FX, SC_TRACK, GR_SLCR, {P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH}},
    {"RESON", FAM_FX, SC_TRACK, GR_RESON, {P_RMODEL, P_RTUNE, P_RDECAY, P_RMIX}},
    {"RESON", FAM_FX, SC_TRACK, GR_RESON, {P_RTONE, P_RSTRCT, P_RPOS, 0xFF}},   /* STRCT: CHORD on CHORD */
    {"DLY", FAM_FX, SC_GLOBAL, GR_NONE, {G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX}},
    {"REV/CHO", FAM_FX, SC_GLOBAL, GR_NONE, {G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH}},
    {"COMP", FAM_HOME, SC_GLOBAL, GR_COMP, {G_CSRC, G_CTHR, G_CRAT, G_CREL}},   /* HOME 2/3; keys: DUCK per track */
    {"COMP", FAM_HOME, SC_GLOBAL, GR_COMP, {G_CATK, G_CKNEE, G_CMKUP, 0xFF}},   /* HOME 3/3 */
    {"STEP", FAM_SEQ, SC_GRID, GR_GRID, {0xFF, 0xFF, 0xFF, 0xFF}},   /* KNOB 1: bank */
    {"PATTERN", FAM_SEQ, SC_TRACK, GR_STEPS, {P_SLEN, P_SDIV, P_SSWING, P_SRC}},
    {"GLOBAL", FAM_GLO, SC_GLOBAL, GR_NONE, {G_BPM, G_SWING, G_CLOCK, G_DRCH}},
    {"SYSTEM", FAM_GLO, SC_GLOBAL, GR_NONE, {G_MIDI, G_SYNC, G_ROUTE, G_INFO}},
    {"GLOBAL", FAM_GLO, SC_GLOBAL, GR_NONE, {G_MUTEBAR, 0xFF, 0xFF, 0xFF}},   /* device setting (settings.mutebar) */
    {"PROJECT", FAM_SAVE, SC_GLOBAL, GR_SLOTS, {G_SLOT, 0xFF, G_LOAD, G_SAVE}},
    {"TOOLS", FAM_SAVE, SC_GLOBAL, GR_NONE, {G_CLRSEQ, G_INITSND, G_CLRALL, G_INITALL}},   /* track; all */
    {"GRIDS", FAM_GRIDS, SC_GLOBAL, GR_GRIDS, {G_GMODE, G_GX, G_GY, G_GCHAOS}},   /* EUCLID: MODE LEN K S H */
    {"GRIDS", FAM_GRIDS, SC_GLOBAL, GR_GRIDS, {G_GFILL1, G_GFILL2, G_GFILL3, 0xFF}},
    /* LFO: LFO 1's knobs; OCT- shows LFO 2 (song.lsel, page_id) */
    {"LFO", FAM_LFO, SC_TRACK, GR_LFO, {P_LFO1 + LF_WAVE, P_LFO1 + LF_RATE, P_LFO1 + LF_MORPH, P_LFO1 + LF_DEPTH}},
    {"LFO", FAM_LFO, SC_TRACK, GR_LFO, {P_LFO1 + LF_DEST, P_LFO1 + LF_TRIG, P_LFO1 + LF_PHASE, 0xFF}},
    {"TRACKS", FAM_MIX, SC_MIX, GR_MIX, {0xFF, 0xFF, 0xFF, 0xFF}},   /* TRACK LEVEL LEN PAN */
};
#define NPAGES (sizeof(PAGES) / sizeof(PAGES[0]))

/* the button (and LED) of each family; the LAYER pages are EDIT's (OCT+ from SOUND), the COMP pages HOME's (HOME 2/3,
 * 3/3); ENV and SCL are free */
static const uint8_t FAM_BTN[FAM_COUNT] = {B_HOME, B_EDIT, B_EDIT, B_FX, B_SEQ, B_GLO, B_SAVE, B_ARP, B_LFO, B_REC};

/* the EDIT list of track t: MODEL, its model's TUNE DECAY TONE and 4th knob, its extra knobs, then LVL PAN NOTE
 * CHOKE (at most 16); returns how many */
static uint32_t snd_list(const track_t *t, uint8_t *ids)
{
    const dmodel_t *m = &DMODELS[(uint32_t)t->p[P_MODEL] % NMODELS];
    uint32_t n = 0, k;
    ids[n++] = P_MODEL;
    for (k = 0; k < 8u; k++)
        if (k < 4u || (m->edit[k].label && m->edit[k].label[0] != '-'))
            ids[n++] = (uint8_t)(P_E0 + k);
    ids[n++] = P_LEVEL;
    ids[n++] = P_PAN;
    ids[n++] = P_NOTE;
    ids[n++] = P_CHOKE;
    return n;
}

/* the parameter of a column: an EDIT slot is the selected track's sound list; GRIDS 1/2 in EUCLID mode turns
 * LEN K / S / H where MAP has X / Y / CHAOS; an LFO page edits the LFO OCT- selected; 0xFF = empty */
static uint32_t page_id(const page_t *pg, uint32_t slot)
{
    uint32_t id = pg->id[slot & 3u];
    if (id >= SND_SLOT && id < SND_SLOT + 16u) {
        uint8_t ids[16];
        uint32_t n = snd_list(TSEL, ids);
        return id - SND_SLOT < n ? ids[id - SND_SLOT] : 0xFFu;
    }
    if (pg->graph == GR_GRIDS && id >= G_GX && id <= G_GCHAOS && song.g[G_GMODE])
        id = G_GLEN1 + (id - G_GX);
    if (pg->graph == GR_LFO && id != 0xFFu && song.lsel)
        id += LF_N;
    return id;
}

static const param_desc_t *page_desc(const page_t *pg, uint32_t slot, int16_t **valp)
{
    uint32_t id = page_id(pg, slot);
    *valp = 0;
    if (id == 0xFFu || pg->scope == SC_GRID || pg->scope == SC_MIX)
        return 0;
    if (pg->scope == SC_GLOBAL && id >= G_COUNT) {   /* TOOLS actions, MUTE */
        id = id - G_COUNT < 3u ? id - G_COUNT : 0u;
        *valp = &song.act[id];
        return &GP_ACT[id];
    }
    if (pg->scope == SC_GLOBAL) {
        *valp = &song.g[id];
        return &GP[id];
    }
    *valp = &TSEL->p[id];
    return track_desc(TSEL, id);
}
