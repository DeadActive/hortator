/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* M1-C fidelity (docs/superpowers/specs/2026-10-05-m1c-drum-models-design.md §5): each new model against
 * the reference metrics tests/drum_ref.cc wrote (build/drum_ref/metrics.bin), over the grid of
 * tests/drum_ref_grid.h, each render taken REF_NVAR times on both sides (dmm_pairs). Two tiers: a render that
 * misses the loose tolerances fails; strict misses are counted and listed in build/drum_fidelity.txt.
 *   drum_fidelity METRICS.bin */
#define DM_QCHECK                                    /* every Q24 result range-checked (dm_qover) */
#include "drum_host.h"
#include "drum_ref_grid.h"
#include "drum_metrics.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

#define NREN ((uint32_t)(REF_SECONDS * FS))
static dmm_t ref[REF_NMODELS][REF_NGRID][2][REF_NVAR];
static int32_t buf[NREN + CTL];
static float y[NREN + CTL];

/* the render matches if any take of ours is within tolerance of any take of the original (both are random
 * processes: start phases, noise; user decision, M1-C); why: the first takes' miss */
static int dmm_pairs(const dmm_t *ref_takes, const dmm_t *our_takes, int kick, const dmm_tol_t *t, char *why, size_t wn)
{
    char w2[160];
    int a, b;
    for (a = 0; a < REF_NVAR; a++)
        for (b = 0; b < REF_NVAR; b++)
            if (dmm_compare(&ref_takes[a], &our_takes[b], kick, t, a || b ? w2 : why, a || b ? sizeof w2 : wn))
                return 1;
    return 0;
}

static int model_of(const char *name)
{
    uint32_t mi;
    for (mi = 0; mi < NMODELS; mi++)
        if (str_eq(N_MODEL[mi], name))
            return (int)mi;
    return -1;
}

/* our take tk of grid render (m, g, v), model mi: another hit count, so other noise seeds and start phases */
static void render_ours(int m, int mi, int g, int v, uint32_t tk, dmm_t *out, int *bounded)
{
    uint32_t i;
    int k;
    host_init();
    dvage += tk * 7919u;
    drum_set_model(&trk[0], (uint32_t)mi);
    for (k = 0; k < 4; k++)
        trk[0].p[P_E0 + k] = (int16_t)ref_knob(m, g, k);
    drum_hit(&trk[0], (uint32_t)REF_VEL[v]);
    render_track(&trk[0], buf, NREN);
    for (i = 0; i < NREN; i++) {                         /* back to "1.0 = a reference sample of 1.0" */
        y[i] = (float)(buf[i] * (32768.0 / ((double)DM_FLOAT1 * VOICE_FS)));
        *bounded &= fabsf(y[i]) < 4.0f;
    }
    dmm_measure(y, (int)NREN, FS, REF_NOISY[m], ref_hz(REF_NOTE[m] + ref_knob(m, g, 0)), out);
}

int main(int argc, char **argv)
{
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : 0, *log = fopen("build/drum_fidelity.txt", "w");
    uint32_t hdr[4];
    int m, g, v, k, built = 0;
    if (!f || fread(hdr, sizeof hdr, 1, f) != 1 || hdr[0] != 0x32524D44u || hdr[1] != REF_NMODELS ||
        hdr[2] != REF_NGRID * REF_NVAR || hdr[3] != sizeof(dmm_t) || fread(ref, sizeof ref, 1, f) != 1) {
        check("fidelity: the reference metrics are readable (tests/drum_ref.cc grid)", 0);
        return 1;
    }
    fclose(f);
    for (m = 0; m < REF_NMODELS; m++) {
        int mi = model_of(REF_NAME[m]), ns = 0, nl = 0, shown = 0, defs = 1, bounded = 1;
        char what[160], why[160];
        if (mi < 0) {
            printf("     fidelity: %s not built yet\n", REF_NAME[m]);
            continue;
        }
        built++;
        for (k = 0; k < 4; k++)
            defs &= DMODELS[mi].edit[k].def == REF_DEF[m][k];
        snprintf(what, sizeof what, "fidelity: %s knob defaults match tests/drum_ref_grid.h", REF_NAME[m]);
        check(what, defs);
        for (g = 0; g < REF_NGRID; g++)
            for (v = 0; v < 2; v++) {
                dmm_t ours[REF_NVAR];
                uint32_t tk;
                for (tk = 0; tk < REF_NVAR; tk++)
                    render_ours(m, mi, g, v, tk, &ours[tk], &bounded);
                if (dmm_pairs(ref[m][g][v], ours, REF_KICK[m], &DMM_STRICT, why, sizeof why)) {
                    ns++;
                    nl++;
                    continue;
                }
                if (log)
                    fprintf(log, "%s TUNE %d DECAY %d TONE %d CHAR %d vel %d: strict miss: %s\n", REF_NAME[m],
                            ref_knob(m, g, 0), ref_knob(m, g, 1), ref_knob(m, g, 2), ref_knob(m, g, 3), REF_VEL[v], why);
                if (dmm_pairs(ref[m][g][v], ours, REF_KICK[m], &DMM_LOOSE, why, sizeof why)) {
                    nl++;
                    continue;
                }
                if (shown++ < 5)
                    printf("     %s TUNE %d DECAY %d TONE %d CHAR %d vel %d: %s\n", REF_NAME[m], ref_knob(m, g, 0),
                           ref_knob(m, g, 1), ref_knob(m, g, 2), ref_knob(m, g, 3), REF_VEL[v], why);
                if (log)
                    fprintf(log, "%s TUNE %d DECAY %d TONE %d CHAR %d vel %d: LOOSE MISS: %s\n", REF_NAME[m],
                            ref_knob(m, g, 0), ref_knob(m, g, 1), ref_knob(m, g, 2), ref_knob(m, g, 3), REF_VEL[v], why);
            }
        printf("     %s: strict %d / %d, loose %d / %d (strict misses: build/drum_fidelity.txt)\n", REF_NAME[m], ns,
               2 * REF_NGRID, nl, 2 * REF_NGRID);
        snprintf(what, sizeof what, "fidelity: %s within the loose tolerances on all %d renders, bounded", REF_NAME[m],
                 2 * REF_NGRID);
        check(what, nl == 2 * REF_NGRID && bounded);
    }
    if (log)
        fclose(log);
#ifdef DM_QCHECK
    check("fidelity: no Q24 product overflowed in any render", dm_qover == 0);
#endif
    printf("fidelity: %d of %d models built\n", built, REF_NMODELS);
    printf(fails ? "drum_fidelity: %d FAILED\n" : "drum_fidelity: all passed\n", fails);
    return fails ? 1 : 0;
}
