/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Per-voice state of the M1-C models (dvoice_t.ms): one union member per model (Tasks 3-7 add them), and the
 * Q24 filter states of the toolkit (dm_dsp.c). */
typedef struct { int32_t g, r, h, s1, s2; } qsvf_t;     /* stmlib Svf: coefficients, two states */
typedef struct { int32_t g, gi, s; } qpole_t;           /* stmlib OnePole */
typedef union {
    int32_t raw[2];
} dm_state_t;
