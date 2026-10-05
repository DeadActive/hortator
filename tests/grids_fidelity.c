/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* M2 Grids fidelity: grids.c against the original's bitstreams (tests/grids_ref.cc): every case, every step,
 * triggers and accents identical.   grids_fidelity REF.bin */
#include "drum_host.h"
#include "grids_cases.h"

int main(int argc, char **argv)
{
    static uint8_t ref[GC_ALL * GRIDS_STEPS];
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : 0;
    grids_case_t c;
    int i, k, bad = 0, first = -1;
    if (!f || fread(ref, 1, sizeof ref, f) != sizeof ref) {
        printf("FAIL  grids fidelity: cannot read %s\n", argc > 1 ? argv[1] : "(none)");
        return 1;
    }
    fclose(f);
    host_init();
    for (i = 0; grids_case(i, &c); i++) {
        song.g[G_GMODE] = (int16_t)c.mode;
        song.g[G_GX] = (int16_t)c.x;
        song.g[G_GY] = (int16_t)c.y;
        song.g[G_GCHAOS] = (int16_t)c.chaos;
        for (k = 0; k < 3; k++) {
            song.g[G_GFILL1 + k] = (int16_t)c.fill[k];
            song.g[G_GLEN1 + k] = (int16_t)c.len[k];
        }
        grids_start();
        for (k = 0; k < GRIDS_STEPS; k++)
            if (grids_step() != ref[i * GRIDS_STEPS + k]) {
                bad++;
                if (first < 0)
                    first = i * GRIDS_STEPS + k;
            }
    }
    if (bad)
        printf("FAIL  grids fidelity: %d of %d steps differ (first: case %d step %d)\n", bad, GC_ALL * GRIDS_STEPS,
               first / GRIDS_STEPS, first % GRIDS_STEPS);
    else
        printf("ok    grids fidelity: %d cases x %d steps identical to the original (MAP + EUCLID)\n", GC_ALL, GRIDS_STEPS);
    return bad ? 1 : 0;
}
