/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* The drum model table. New models are appended at the end (projects store model numbers), in the enum, DMODELS and N_MODEL (same order). */
#include "dm_dsp.c"
#include "dm_sample.c"
#include "dm_kick.c"
#include "dm_snare.c"
#include "dm_metal.c"
#include "dm_perc.c"

enum { DM_K808, DM_K909, DM_S808, DM_S909, DM_C808, DM_C909, DM_HATC, DM_HATO, DM_CYMB, DM_COWB, DM_TOM, DM_CONGA, DM_RIM, DM_CLAVE, DM_SMPL, DM_KBOOM, DM_KPUNC, DM_SSNAP, DM_SCRAK, DM_HMETL, DM_HNOIS, NMODELS };
static const dmodel_t DMODELS[NMODELS] = {DM_K808_DEF, DM_K909_DEF, DM_S808_DEF, DM_S909_DEF, DM_C808_DEF, DM_C909_DEF, DM_HATC_DEF, DM_HATO_DEF, DM_CYMB_DEF, DM_COWB_DEF, DM_TOM_DEF, DM_CONGA_DEF, DM_RIM_DEF, DM_CLAVE_DEF, DM_SMPL_DEF, DM_KBOOM_DEF, DM_KPUNC_DEF, DM_SSNAP_DEF, DM_SCRAK_DEF, DM_HMETL_DEF, DM_HNOIS_DEF};
static const char *const N_MODEL[NMODELS] = {"K808", "K909", "S808", "S909", "C808", "C909", "HATC", "HATO", "CYMB", "COWB", "TOM", "CONGA", "RIM", "CLAVE", "SMPL", "KBOOM", "KPUNC", "SSNAP", "SCRAK", "HMETL", "HNOIS"};
