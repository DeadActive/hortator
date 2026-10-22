/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* The drum model table. New models are appended at the end (projects store model numbers), in the enum, DMODELS and N_MODEL (same order). */
#include "dm_dsp.c"
#include "phys_dsp.c"          /* PHYS: the modal core (upstream 1.0, MIT DaisySP) */
#include "dm_sample.c"
#include "dm_kick.c"
#include "dm_snare.c"
#include "dm_metal.c"
#include "dm_perc.c"
#include "dm_memb.c"           /* MEMB: a struck drum head (PHYS) */
#include "dm_fm.c"             /* FM: 2-operator FM percussion */
#include "dm_ktrx.c"           /* KTRX: a measured digital sine kick */
#include "dm_sdtrx.c"          /* SDTRX: a measured digital snare */
#include "dm_xttrx.c"          /* XTTRX, XCTRX: a measured digital tom and conga */
#include "dm_trxn.c"           /* CPTRX, MATRX: measured digital noise voices (clap, maracas) */
#include "dm_rstrx.c"          /* RSTRX: a measured digital rim shot */
#include "dm_trxm.c"           /* CHTRX OHTRX CYTRX: measured digital hats and cymbal (one metal bank) */
#include "dm_trxcb.c"          /* CBTRX: a measured digital cowbell */
#include "dm_cltrx.c"          /* CLTRX: measured digital claves */
#include "dm_b2trx.c"          /* B2TRX: a measured digital bass drum 2 */
#include "dm_bdefm.c"          /* BDEFM, XTEFM: a measured FM bass drum and FM tom (one voice) */
#include "dm_efmn.c"           /* SDEFM CPEFM RSEFM: measured FM noise voices (snare, clap, rim) */
#include "dm_efmm.c"           /* CBEFM HHEFM CYEFM: measured FM cowbell, hat, cymbal */
#include "dm_acid.c"           /* ACID: a 303-style bass line */

enum { DM_K808, DM_K909, DM_S808, DM_S909, DM_C808, DM_C909, DM_HATC, DM_HATO, DM_CYMB, DM_COWB, DM_TOM, DM_CONGA, DM_RIM, DM_CLAVE, DM_SMPL, DM_KBOOM, DM_KPUNC, DM_SSNAP, DM_SCRAK, DM_HMETL, DM_HNOIS, DM_MEMB, DM_FM, DM_ACID, DM_KTRX, DM_SDTRX, DM_XTTRX, DM_XCTRX, DM_CPTRX, DM_RSTRX, DM_CBTRX, DM_CHTRX, DM_OHTRX, DM_CYTRX, DM_MATRX, DM_CLTRX, DM_B2TRX, DM_BDEFM, DM_SDEFM, DM_XTEFM, DM_CPEFM, DM_RSEFM, DM_CBEFM, DM_HHEFM, DM_CYEFM, NMODELS };
static const dmodel_t DMODELS[NMODELS] = {DM_K808_DEF, DM_K909_DEF, DM_S808_DEF, DM_S909_DEF, DM_C808_DEF, DM_C909_DEF, DM_HATC_DEF, DM_HATO_DEF, DM_CYMB_DEF, DM_COWB_DEF, DM_TOM_DEF, DM_CONGA_DEF, DM_RIM_DEF, DM_CLAVE_DEF, DM_SMPL_DEF, DM_KBOOM_DEF, DM_KPUNC_DEF, DM_SSNAP_DEF, DM_SCRAK_DEF, DM_HMETL_DEF, DM_HNOIS_DEF, DM_MEMB_DEF, DM_FM_DEF, DM_ACID_DEF, DM_KTRX_DEF, DM_SDTRX_DEF, DM_XTTRX_DEF, DM_XCTRX_DEF, DM_CPTRX_DEF, DM_RSTRX_DEF, DM_CBTRX_DEF, DM_CHTRX_DEF, DM_OHTRX_DEF, DM_CYTRX_DEF, DM_MATRX_DEF, DM_CLTRX_DEF, DM_B2TRX_DEF, DM_BDEFM_DEF, DM_SDEFM_DEF, DM_XTEFM_DEF, DM_CPEFM_DEF, DM_RSEFM_DEF, DM_CBEFM_DEF, DM_HHEFM_DEF, DM_CYEFM_DEF};
static const char *const N_MODEL[NMODELS] = {"K808", "K909", "S808", "S909", "C808", "C909", "HATC", "HATO", "CYMB", "COWB", "TOM", "CONGA", "RIM", "CLAVE", "SAMPL", "BOOM", "PUNCH", "SNAP", "CRACK", "METAL", "HISS", "SKIN", "FM", "ACID", "BDTRX", "SDTRX", "TMTRX", "CGTRX", "CPTRX", "RSTRX", "CBTRX", "CHTRX", "OHTRX", "CYTRX", "SHTRX", "CLTRX", "B2TRX", "BDEFM", "SDEFM", "TMEFM", "CPEFM", "RSEFM", "CBEFM", "HHEFM", "CYEFM"};