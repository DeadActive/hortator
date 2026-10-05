/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The Grids fidelity cases (tests/grids_ref.cc, tests/grids_fidelity.c): MAP over X, Y, fills, chaos; EUCLID
 * over lengths and fills. Knob values (0..127, LEN 1..32). */
#define GRIDS_STEPS 96
typedef struct { int mode, x, y, chaos, fill[3], len[3]; } grids_case_t;
static const int GC_XY[5] = {0, 21, 64, 96, 127};
static const int GC_FILL[4][3] = {{0, 0, 0}, {64, 64, 64}, {127, 100, 30}, {127, 127, 127}};
static const int GC_CHAOS[3] = {0, 64, 127};
static const int GC_LEN[4][3] = {{1, 1, 1}, {16, 12, 8}, {32, 5, 7}, {13, 32, 3}};
static const int GC_EFILL[4][3] = {{0, 0, 0}, {64, 64, 64}, {127, 127, 127}, {10, 90, 50}};
#define GC_MAP (5 * 5 * 4 * 3)
#define GC_ALL (GC_MAP + 4 * 4)
static int grids_case(int i, grids_case_t *c)    /* case i (0 .. GC_ALL - 1); 0 past the end */
{
    int k;
    if (i < 0 || i >= GC_ALL)
        return 0;
    if (i < GC_MAP) {
        c->mode = 0;
        c->x = GC_XY[i % 5];
        c->y = GC_XY[i / 5 % 5];
        for (k = 0; k < 3; k++) {
            c->fill[k] = GC_FILL[i / 25 % 4][k];
            c->len[k] = 1;
        }
        c->chaos = GC_CHAOS[i / 100];
    } else {
        i -= GC_MAP;
        c->mode = 1;
        c->x = c->y = c->chaos = 0;
        for (k = 0; k < 3; k++) {
            c->len[k] = GC_LEN[i % 4][k];
            c->fill[k] = GC_EFILL[i / 4][k];
        }
    }
    return 1;
}
