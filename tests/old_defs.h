/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The models' knob defaults as they were up to drum 0.20.0 (DMODELS[].edit[].def, copied here): the tests that render
 * or measure "at the default" use these, so a later change of a model's defaults (the presets: every voice's default
 * = its bank's first preset) changes no fidelity metric, reference, threshold, demo or test outcome (owner's
 * decision, docs/superpowers/specs/2026-10-19-presets-design.md). Included by drum_host.h after the firmware. */

#define OLD_NMODELS 45                                /* the models up to drum 0.20.0 (enum order, as DMODELS) */
_Static_assert(NMODELS >= OLD_NMODELS, "tests/old_defs.h: a model went missing");

static const int16_t OLD_DEF[OLD_NMODELS][8] = {      /* P_E0..P_E7 */
    {0, 80, 40, 40, 30, 0, 0, 0},       /* K808 */
    {0, 64, 70, 50, 40, 20, 0, 0},      /* K909 */
    {0, 64, 64, 90, 64, 0, 0, 0},       /* S808 */
    {0, 64, 80, 80, 64, 0, 0, 0},       /* S909 */
    {0, 64, 64, 64, 0, 0, 0, 0},        /* C808 */
    {0, 64, 64, 40, 0, 0, 0, 0},        /* C909 */
    {0, 64, 64, 64, 0, 0, 0, 0},        /* HATC */
    {0, 64, 64, 64, 0, 0, 0, 0},        /* HATO */
    {0, 64, 64, 64, 0, 0, 0, 0},        /* CYMB */
    {0, 64, 64, 64, 0, 0, 0, 0},        /* COWB */
    {0, 64, 30, 50, 0, 0, 0, 0},        /* TOM */
    {0, 64, 60, 30, 0, 0, 0, 0},        /* CONGA */
    {0, 64, 64, 40, 0, 0, 0, 0},        /* RIM */
    {0, 64, 30, 20, 0, 0, 0, 0},        /* CLAVE */
    {0, 127, 127, 0, 0, 36, 0, 0},      /* SMPL */
    {0, 64, 64, 32, 0, 0, 0, 0},        /* KBOOM */
    {0, 64, 64, 64, 0, 0, 0, 0},        /* KPUNC */
    {0, 64, 64, 64, 0, 0, 0, 0},        /* SSNAP */
    {0, 64, 64, 64, 0, 0, 0, 0},        /* SCRAK */
    {0, 40, 80, 40, 0, 0, 0, 0},        /* HMETL */
    {0, 40, 80, 40, 0, 0, 0, 0},        /* HNOIS */
    {0, 60, 64, 20, 40, 30, 24, 0},     /* MEMB */
    {0, 50, 60, 2, 40, 0, 0, 64},       /* FM */
    {0, 50, 40, 64, 64, 64, 0, 56},     /* ACID */
    {0, 64, 64, 32, 0, 0, 0, 0},        /* KTRX */
    {0, 64, 64, 64, 0, 64, 0, 0},       /* SDTRX */
    {0, 64, 0, 64, 0, 0, 0, 0},         /* XTTRX */
    {0, 64, 0, 64, 0, 0, 0, 0},         /* XCTRX */
    {0, 64, 64, 64, 64, 0, 64, 64},     /* CPTRX */
    {0, 64, 0, 0, 0, 0, 0, 0},          /* RSTRX */
    {0, 64, 0, 0, 64, 0, 64, 0},        /* CBTRX */
    {0, 64, 64, 0, 127, 64, 0, 0},      /* CHTRX */
    {0, 64, 64, 0, 127, 64, 0, 0},      /* OHTRX */
    {0, 64, 64, 64, 64, 64, 0, 0},      /* CYTRX */
    {0, 64, 0, 0, 0, 0, 64, 64},        /* MATRX */
    {0, 64, 0, 0, 64, 0, 0, 0},         /* CLTRX */
    {0, 64, 0, 0, 0, 0, 0, 0},          /* B2TRX */
    {0, 64, 0, 64, 64, 64, 64, 0},      /* BDEFM */
    {0, 64, 64, 64, 64, 64, 64, 0},     /* SDEFM */
    {0, 64, 0, 64, 64, 64, 64, 0},      /* XTEFM */
    {0, 64, 64, 64, 64, 64, 64, 0},     /* CPEFM */
    {0, 64, 64, 0, 64, 64, 64, 64},     /* RSEFM */
    {0, 64, 0, 0, 64, 64, 64, 0},       /* CBEFM */
    {0, 64, 0, 64, 64, 64, 64, 0},      /* HHEFM */
    {0, 64, 0, 0, 64, 64, 64, 0},       /* CYEFM */
};

static void old_knobs(track_t *t)                     /* t's 8 knobs = its model's old defaults */
{
    uint32_t i, mi = (uint32_t)t->p[P_MODEL] % NMODELS;
    if (mi < OLD_NMODELS)
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = OLD_DEF[mi][i];
}

static void old_set_model(track_t *t, uint32_t mi)    /* drum_set_model at the old knob defaults */
{
    drum_set_model(t, mi);
    old_knobs(t);
}

/* the power-on kit up to drum 0.22.0 (drum_core.c KIT_DEF then; since fix 4 the power-on kit is the TRX classic kit):
 * model and MIDI note of each track. The tests' "power-on kit" stays this one, so no metric moves */
static const uint8_t OLD_KIT[NTRK][2] = {
    {DM_K909, 36}, {DM_S808, 38}, {DM_C808, 39}, {DM_HATC, 42},
    {DM_HATO, 46}, {DM_TOM, 45}, {DM_RIM, 37}, {DM_CYMB, 49},
};

static void old_kit(void)                             /* the old power-on kit (OLD_KIT) at the old defaults */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        drum_set_model(&trk[k], OLD_KIT[k][0]);       /* (as drum_tracks_init did: model, its first preset) */
        trk[k].p[P_NOTE] = OLD_KIT[k][1];
        old_knobs(&trk[k]);
        if (trk[k].model == DM_SMPL)
            trk[k].p[P_E5] = OLD_KIT[k][1];           /* (as drum_tracks_init: the GM kit's sound for the note) */
    }
}
