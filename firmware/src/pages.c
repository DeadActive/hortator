/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 Eugene Vech */
/* Drum UI pages: a family per page button (pressing it again steps through its pages), and what
 * the four knobs edit on each page. */
enum { FAM_HOME, FAM_SND, FAM_TRK, FAM_LAY, FAM_FX, FAM_SEQ, FAM_GLO, FAM_SAVE, FAM_MIX, FAM_COUNT };
enum { SC_TRACK, SC_GLOBAL, SC_GRID, SC_MIX };   /* knobs edit: the selected track, song.g, the STEP grid, the mixer */
enum { GR_NONE, GR_MODEL, GR_FX, GR_SLCR, GR_GRID, GR_STEPS, GR_SLOTS, GR_MIX };

typedef struct {
    const char *title;
    uint8_t fam, scope, graph;
    uint8_t id[4];               /* parameter ids; 0xFF = empty column */
} page_t;

static const page_t PAGES[] = {
    {"SOUND", FAM_SND, SC_TRACK, GR_MODEL, {P_E0, P_E1, P_E2, P_E3}},
    {"SOUND 2", FAM_SND, SC_TRACK, GR_MODEL, {P_E4, P_E5, P_E6, P_E7}},
    {"TRACK", FAM_TRK, SC_TRACK, GR_MODEL, {P_MODEL, P_LEVEL, P_PAN, P_CHOKE}},
    {"MIDI", FAM_TRK, SC_TRACK, GR_NONE, {P_NOTE, P_MUTE, 0xFF, 0xFF}},
    {"LAYER", FAM_LAY, SC_TRACK, GR_NONE, {P_LSET, P_LKEY, P_LLEVEL, P_LTUNE}},
    {"LAYER 2", FAM_LAY, SC_TRACK, GR_NONE, {P_LDEC, 0xFF, 0xFF, 0xFF}},
    {"FX", FAM_FX, SC_TRACK, GR_FX, {P_DIST, P_CHOR, P_DLY, P_REV}},
    {"SLICER", FAM_FX, SC_TRACK, GR_SLCR, {P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH}},
    {"DLY", FAM_FX, SC_GLOBAL, GR_NONE, {G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX}},
    {"REV/CHO", FAM_FX, SC_GLOBAL, GR_NONE, {G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH}},
    {"STEP", FAM_SEQ, SC_GRID, GR_GRID, {0xFF, 0xFF, 0xFF, 0xFF}},   /* KNOB 1: bank */
    {"PATTERN", FAM_SEQ, SC_TRACK, GR_STEPS, {P_SLEN, P_SDIV, P_SSWING, 0xFF}},
    {"GLOBAL", FAM_GLO, SC_GLOBAL, GR_NONE, {G_BPM, G_SWING, G_CLOCK, G_DRCH}},
    {"SYSTEM", FAM_GLO, SC_GLOBAL, GR_NONE, {G_MIDI, G_SYNC, G_ROUTE, G_INFO}},
    {"PROJECT", FAM_SAVE, SC_GLOBAL, GR_SLOTS, {G_SLOT, 0xFF, G_LOAD, G_SAVE}},
    {"TOOLS", FAM_SAVE, SC_GLOBAL, GR_NONE, {G_CLRSEQ, G_INITSND, 0xFF, 0xFF}},
    {"TRACKS", FAM_MIX, SC_MIX, GR_MIX, {0xFF, 0xFF, 0xFF, 0xFF}},   /* TRACK LEVEL LEN PAN */
};
#define NPAGES (sizeof(PAGES) / sizeof(PAGES[0]))

/* the button of each family (SCL and ARP have none) */
static const uint8_t FAM_BTN[FAM_COUNT] = {B_HOME, B_EDIT, B_ENV, B_LFO, B_FX, B_SEQ, B_GLO, B_SAVE, B_REC};

static const param_desc_t *page_desc(const page_t *pg, uint32_t slot, int16_t **valp)
{
    uint32_t id = pg->id[slot & 3u];
    *valp = 0;
    if (id == 0xFFu || pg->scope == SC_GRID || pg->scope == SC_MIX)
        return 0;
    if (pg->scope == SC_GLOBAL) {
        *valp = &song.g[id];
        return &GP[id];
    }
    *valp = &TSEL->p[id];
    return track_desc(TSEL, id);
}
