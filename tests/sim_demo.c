/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The web simulator's first-visit demos, saved to project slots 1-4 with the firmware's own project_save; slot 1 is
 * then loaded (and remembered as the last project, so a returning visit opens it), so PLAY grooves at once. MD first,
 * as the MODEL list: 1 the TRX kit (the power-on kit's voices), 2 an EFM kit, 3 MODERN (Plaits-style) with an ACID
 * line, 4 the 808 (drumsim's kit, tests/drumsim.c).
 * 'x' = a hit, 'X' = an accent; TUNE (P_E0, KEEP = the voice's default sound's), DECAY (P_E1, -1 = the default's);
 * an ACID track plays acid[] (its notes from C4, slide where 's'). CLASSIC and the first MODERN models take their
 * knobs as up to drum 0.20.0 (SIM_OLD_DEF, as tests/old_defs.h: the 808 sounds as drumsim's, whatever the models'
 * defaults are now); the MD and ACID voices start on their first factory preset (drum_set_model). */
#define KEEP INT16_MIN
typedef struct {
    uint32_t model[NTRK];
    int16_t tune[NTRK], decay[NTRK];
    const char *pat[NTRK];
    int8_t acid[16];                                     /* an ACID track's notes, from C4 */
    const char *slide;                                   /* 's': that step slides into the next (0: none) */
} sim_kit_t;

static const sim_kit_t SIM_KITS[4] = {
    {{DM_KTRX, DM_SDTRX, DM_CPTRX, DM_CHTRX, DM_OHTRX, DM_XTTRX, DM_RSTRX, DM_CYTRX},
     {KEEP, KEEP, KEEP, KEEP, KEEP, KEEP, KEEP, KEEP}, {-1, -1, -1, -1, -1, -1, -1, -1},
     {"X..x..x...x..x..", "....X.......X...", "....x.......x..x", "x.x.x.x.x.x.x.x.", "..x...x...x...x.",
      "..........x.x.xX", ".x.....x...x....", "X..............."}, {0}, 0},
    {{DM_BDEFM, DM_SDEFM, DM_CPEFM, DM_HHEFM, DM_CYEFM, DM_XTEFM, DM_RSEFM, DM_CBEFM},
     {KEEP, KEEP, KEEP, KEEP, KEEP, KEEP, KEEP, KEEP}, {-1, -1, -1, -1, 40, -1, -1, -1},
     {"X...x...x...x...", "....X.......X..x", "....x.......x...", "xxxxxxxxxxxxxxXx", "..x...x...x...x.",
      "...x.....x....x.", ".x....x...x.....", "......x.x.....x."}, {0}, 0},
    {{DM_KBOOM, DM_SSNAP, DM_SCRAK, DM_HMETL, DM_HNOIS, DM_KPUNC, DM_ACID, DM_SSNAP},
     {0, 0, 0, 0, 0, 0, 0, 12}, {-1, -1, -1, -1, -1, 40, -1, 10},
     {"X..x..x...x..x..", "....X.......X...", "....x.......x..x", "x.x.x.x.x.x.x.x.", "..x...x...x...x.",
      "X.......X.......", "X.xxX.xxX.xxX.xX", ".x.....x...x...."},
     {-24, 0, -12, -24, -21, 0, -17, -12, -24, 0, -14, -24, -19, 0, -24, -12}, "...s.......s...."},
    {{DM_K808, DM_S808, DM_C808, DM_HATC, DM_HATO, DM_TOM, DM_COWB, DM_CYMB},
     {0, 0, 0, 0, 0, 0, 0, 0}, {-1, -1, -1, -1, -1, -1, -1, -1},
     {"X..x..x...x..x..", "....X.......X...", "....x.......x..x", "x.x.x.x.x.x.x.x.", "..x...x...x...x.",
      "..........x.x.xX", ".x.....x...x....", "X..............."}, {0}, 0},
};

#define SIM_OLD_N 21u                                    /* the kits' models: K808 .. HNOIS (enum order) */
static const int16_t SIM_OLD_DEF[SIM_OLD_N][8] = {
    {0, 80, 40, 40, 30, 0, 0, 0},  {0, 64, 70, 50, 40, 20, 0, 0}, {0, 64, 64, 90, 64, 0, 0, 0},   /* K808 K909 S808 */
    {0, 64, 80, 80, 64, 0, 0, 0},  {0, 64, 64, 64, 0, 0, 0, 0},   {0, 64, 64, 40, 0, 0, 0, 0},    /* S909 C808 C909 */
    {0, 64, 64, 64, 0, 0, 0, 0},   {0, 64, 64, 64, 0, 0, 0, 0},   {0, 64, 64, 64, 0, 0, 0, 0},    /* HATC HATO CYMB */
    {0, 64, 64, 64, 0, 0, 0, 0},   {0, 64, 30, 50, 0, 0, 0, 0},   {0, 64, 60, 30, 0, 0, 0, 0},    /* COWB TOM CONGA */
    {0, 64, 64, 40, 0, 0, 0, 0},   {0, 64, 30, 20, 0, 0, 0, 0},   {0, 127, 127, 0, 0, 36, 0, 0},  /* RIM CLAVE SMPL */
    {0, 64, 64, 32, 0, 0, 0, 0},   {0, 64, 64, 64, 0, 0, 0, 0},   {0, 64, 64, 64, 0, 0, 0, 0},    /* KBOOM KPUNC SSNAP */
    {0, 64, 64, 64, 0, 0, 0, 0},   {0, 40, 80, 40, 0, 0, 0, 0},   {0, 40, 80, 40, 0, 0, 0, 0},    /* SCRAK HMETL HNOIS */
};

static void sim_demo_install(void)
{
    uint32_t s, i, k;
    for (s = 0; s < 4u; s++) {
        const sim_kit_t *kit = &SIM_KITS[s];
        drum_tracks_init();
        for (i = 0; i < NTRK; i++) {
            track_t *t = &trk[i];
            drum_set_model(t, kit->model[i]);
            if (kit->model[i] < SIM_OLD_N)
                for (k = 0; k < 8u; k++)
                    t->p[P_E0 + k] = SIM_OLD_DEF[kit->model[i]][k];
            if (kit->tune[i] != KEEP)
                t->p[P_E0] = kit->tune[i];
            if (kit->decay[i] >= 0)
                t->p[P_E1] = kit->decay[i];
            for (k = 0; k < 16u; k++) {
                t->step[k].on = kit->pat[i][k] != '.';
                t->step[k].acc = kit->pat[i][k] == 'X';
                if (kit->model[i] == DM_ACID) {
                    t->step[k].nt = kit->acid[k];
                    t->step[k].slide = kit->slide && kit->slide[k] == 's';
                }
            }
        }
        trk[1].p[P_REV] = 40;                             /* drumsim's sends: snare and clap reverb, hat delay */
        trk[2].p[P_REV] = 50;
        trk[3].p[P_DLY] = 25;
        song.sel = 0;
        project_save(s);
    }
    lastp.set = 1;                                        /* the demos as one install: slot 1 is the one remembered
                                                           * (a returning visit opens it, as before), not slot 4 */
    lastp.slot = 0;
    settings_save();
    drum_tracks_init();
    project_load(0);
}
