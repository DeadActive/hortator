/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 Eugene Vech */
/* The drum model table. New models go above DM_SMPL in the enum, DMODELS and N_MODEL (same order). */
#include "dm_dsp.c"
#include "dm_sample.c"
#include "dm_kick.c"
#include "dm_snare.c"
#include "dm_metal.c"

enum { DM_K808, DM_K909, DM_S808, DM_S909, DM_C808, DM_C909, DM_HATC, DM_HATO, DM_CYMB, DM_COWB, DM_SMPL, NMODELS };
static const dmodel_t DMODELS[NMODELS] = {DM_K808_DEF, DM_K909_DEF, DM_S808_DEF, DM_S909_DEF, DM_C808_DEF, DM_C909_DEF, DM_HATC_DEF, DM_HATO_DEF, DM_CYMB_DEF, DM_COWB_DEF, DM_SMPL_DEF};
static const char *const N_MODEL[NMODELS] = {"K808", "K909", "S808", "S909", "C808", "C909", "HATC", "HATO", "CYMB", "COWB", "SMPL"};
