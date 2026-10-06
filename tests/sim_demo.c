/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The web simulator's first-visit demos: the kits of tests/drumsim.c (808, 909, Plaits, mixed), saved to project
 * slots 1-4 with the firmware's own project_save; slot 1 is then loaded, so PLAY grooves at once.
 * 'x' = a hit, 'X' = an accent; TUNE (P_E0), DECAY (P_E1, -1 = the model's default). */
typedef struct {
    uint32_t model[NTRK];
    int16_t tune[NTRK], decay[NTRK];
    const char *pat[NTRK];
} sim_kit_t;

static const sim_kit_t SIM_KITS[4] = {
    {{DM_K808, DM_S808, DM_C808, DM_HATC, DM_HATO, DM_TOM, DM_COWB, DM_CYMB},
     {0, 0, 0, 0, 0, 0, 0, 0}, {-1, -1, -1, -1, -1, -1, -1, -1},
     {"X..x..x...x..x..", "....X.......X...", "....x.......x..x", "x.x.x.x.x.x.x.x.", "..x...x...x...x.",
      "..........x.x.xX", ".x.....x...x....", "X..............."}},
    {{DM_K909, DM_S909, DM_C909, DM_HATC, DM_HATO, DM_RIM, DM_CLAVE, DM_CONGA},
     {0, 0, 0, 0, 0, 0, 0, 0}, {-1, -1, -1, -1, -1, -1, -1, -1},
     {"X...x...x...x...", "....X.......X..x", "....x.......x...", "xxxxxxxxxxxxxxXx", "..x...x...x...x.",
      "...x.....x....x.", ".x....x...x.....", "......x.x.....x."}},
    {{DM_KBOOM, DM_SSNAP, DM_SCRAK, DM_HMETL, DM_HNOIS, DM_KPUNC, DM_KBOOM, DM_SSNAP},
     {0, 0, 0, 0, 0, 0, 12, 12}, {-1, -1, -1, -1, -1, 40, 30, 10},
     {"X..x..x...x..x..", "....X.......X...", "....x.......x..x", "x.x.x.x.x.x.x.x.", "..x...x...x...x.",
      "X.......X.......", "..........x.x.x.", ".x.....x...x...."}},
    {{DM_KBOOM, DM_S808, DM_SCRAK, DM_HATC, DM_HNOIS, DM_K909, DM_TOM, DM_CYMB},
     {0, 0, 0, 0, 0, 0, 0, 0}, {-1, -1, -1, -1, -1, -1, -1, -1},
     {"X...x...x...x...", "....X.......X...", "....x.......x..x", "x.x.x.x.x.x.x.x.", "..x...x...x...x.",
      "..x.......x..x..", "..........x.x.xX", "X..............."}},
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
            t->p[P_E0] = kit->tune[i];
            if (kit->decay[i] >= 0)
                t->p[P_E1] = kit->decay[i];
            for (k = 0; k < 16u; k++) {
                t->step[k].on = kit->pat[i][k] != '.';
                t->step[k].acc = kit->pat[i][k] == 'X';
            }
        }
        trk[1].p[P_REV] = 40;                             /* drumsim's sends: snare and clap reverb, hat delay */
        trk[2].p[P_REV] = 50;
        trk[3].p[P_DLY] = 25;
        song.sel = 0;
        project_save(s);
    }
    drum_tracks_init();
    project_load(0);
}
