/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The M1-C fidelity grid, shared by tests/drum_ref.cc (the reference renders) and tests/drum_fidelity.c
 * (ours): per model its base MIDI note and knob defaults (= its DMODELS edit[] defaults, checked by
 * drum_fidelity.c), and the grid TUNE / DECAY / TONE / CHAR in {min, default, max}, velocity 127 / 64. */
#include <math.h>
#define REF_NMODELS 6
#define REF_SECONDS 1.5
#define REF_NGRID 81
/* each grid render is taken REF_NVAR times, the reference idling this long first: other start phases and noise
 * (the hats' sources run freely; user decision, M1-C) */
#define REF_NVAR 4
static const double REF_IDLE[REF_NVAR] = {0.1, 0.1137, 0.1291, 0.1503};
static const char *const REF_NAME[REF_NMODELS] = {"KBOOM", "KPUNC", "SSNAP", "SCRAK", "HMETL", "HNOIS"};
static const int REF_NOTE[REF_NMODELS] = {31, 31, 55, 55, 60, 72};          /* MIDI note at TUNE 0 */
static const int REF_DEF[REF_NMODELS][4] = {                                 /* TUNE DECAY TONE CHAR */
    {0, 64, 64, 32}, {0, 64, 64, 64}, {0, 64, 64, 64}, {0, 64, 64, 64}, {0, 40, 80, 40}, {0, 40, 80, 40}};
static const int REF_VEL[2] = {127, 64};
static const int REF_KICK[REF_NMODELS] = {1, 1, 0, 0, 0, 0};                  /* pitch track checked */
static const int REF_NOISY[REF_NMODELS] = {0, 0, 1, 1, 1, 1};                 /* noise-based: longer windows */
static double ref_hz(int note) { return 440.0 * pow(2.0, (note - 69) / 12.0); }
/* grid point g (0..80) of model m: knob k's value */
static int ref_knob(int m, int g, int k)
{
    static const int D[4] = {1, 3, 9, 27};
    int lv = (g / D[k]) % 3;                     /* 0 min, 1 default, 2 max */
    if (k == 0)
        return lv == 0 ? -24 : lv == 1 ? REF_DEF[m][0] : 24;
    return lv == 0 ? 0 : lv == 1 ? REF_DEF[m][k] : 127;
}
