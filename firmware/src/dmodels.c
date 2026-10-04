/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 Eugene Vech */
/* The drum model table. New models go above DM_SMPL in the enum, DMODELS and N_MODEL (same order). */
#include "dm_dsp.c"
#include "dm_sample.c"

enum { DM_SMPL, NMODELS };
static const dmodel_t DMODELS[NMODELS] = {DM_SMPL_DEF};
static const char *const N_MODEL[NMODELS] = {"SMPL"};
