/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The factory banks (presets.c): every preset {MODEL, the 8 SOUND knobs P_E0..P_E7, name <= 8 characters}, in the banks'
 * order (Machinedrum, Classic 808 / 909, Modern / Plaits); a preset's id is its index here (then the 128 user slots).
 * A voice's first preset in its bank is its default sound (drum_set_model). For now one preset per voice: its default
 * knobs, named as the voice (the banks' real content: plan 2026-10-19-presets Task 5). Included once, by presets.c */
static const preset_t PRE_FACTORY[] = {
    /* MACHINEDRUM: the 21 voices measured from the Machinedrum (KTRX, the TRX voices, the EFM voices) */
    {DM_KTRX, {0, 64, 64, 32, 0, 0, 0, 0}, "KTRX"},
    {DM_SDTRX, {0, 64, 64, 64, 0, 64, 0, 0}, "SDTRX"},
    {DM_XTTRX, {0, 64, 0, 64, 0, 0, 0, 0}, "XTTRX"},
    {DM_XCTRX, {0, 64, 0, 64, 0, 0, 0, 0}, "XCTRX"},
    {DM_CPTRX, {0, 64, 64, 64, 64, 0, 64, 64}, "CPTRX"},
    {DM_RSTRX, {0, 64, 0, 0, 0, 0, 0, 0}, "RSTRX"},
    {DM_CBTRX, {0, 64, 0, 0, 64, 0, 64, 0}, "CBTRX"},
    {DM_CHTRX, {0, 64, 64, 0, 127, 64, 0, 0}, "CHTRX"},
    {DM_OHTRX, {0, 64, 64, 0, 127, 64, 0, 0}, "OHTRX"},
    {DM_CYTRX, {0, 64, 64, 64, 64, 64, 0, 0}, "CYTRX"},
    {DM_MATRX, {0, 64, 0, 0, 0, 0, 64, 64}, "MATRX"},
    {DM_CLTRX, {0, 64, 0, 0, 64, 0, 0, 0}, "CLTRX"},
    {DM_B2TRX, {0, 64, 0, 0, 0, 0, 0, 0}, "B2TRX"},
    {DM_BDEFM, {0, 64, 0, 64, 64, 64, 64, 0}, "BDEFM"},
    {DM_SDEFM, {0, 64, 64, 64, 64, 64, 64, 0}, "SDEFM"},
    {DM_XTEFM, {0, 64, 0, 64, 64, 64, 64, 0}, "XTEFM"},
    {DM_CPEFM, {0, 64, 64, 64, 64, 64, 64, 0}, "CPEFM"},
    {DM_RSEFM, {0, 64, 64, 0, 64, 64, 64, 64}, "RSEFM"},
    {DM_CBEFM, {0, 64, 0, 0, 64, 64, 64, 0}, "CBEFM"},
    {DM_HHEFM, {0, 64, 0, 64, 64, 64, 64, 0}, "HHEFM"},
    {DM_CYEFM, {0, 64, 0, 0, 64, 64, 64, 0}, "CYEFM"},
    /* CLASSIC 808 / 909 */
    {DM_K808, {0, 80, 40, 40, 30, 0, 0, 0}, "K808"},
    {DM_K909, {0, 64, 70, 50, 40, 20, 0, 0}, "K909"},
    {DM_S808, {0, 64, 64, 90, 64, 0, 0, 0}, "S808"},
    {DM_S909, {0, 64, 80, 80, 64, 0, 0, 0}, "S909"},
    {DM_C808, {0, 64, 64, 64, 0, 0, 0, 0}, "C808"},
    {DM_C909, {0, 64, 64, 40, 0, 0, 0, 0}, "C909"},
    {DM_HATC, {0, 64, 64, 64, 0, 0, 0, 0}, "HATC"},
    {DM_HATO, {0, 64, 64, 64, 0, 0, 0, 0}, "HATO"},
    {DM_CYMB, {0, 64, 64, 64, 0, 0, 0, 0}, "CYMB"},
    {DM_COWB, {0, 64, 64, 64, 0, 0, 0, 0}, "COWB"},
    {DM_TOM, {0, 64, 30, 50, 0, 0, 0, 0}, "TOM"},
    {DM_CONGA, {0, 64, 60, 30, 0, 0, 0, 0}, "CONGA"},
    {DM_RIM, {0, 64, 64, 40, 0, 0, 0, 0}, "RIM"},
    {DM_CLAVE, {0, 64, 30, 20, 0, 0, 0, 0}, "CLAVE"},
    /* MODERN / PLAITS */
    {DM_KBOOM, {0, 64, 64, 32, 0, 0, 0, 0}, "KBOOM"},
    {DM_KPUNC, {0, 64, 64, 64, 0, 0, 0, 0}, "KPUNC"},
    {DM_SSNAP, {0, 64, 64, 64, 0, 0, 0, 0}, "SSNAP"},
    {DM_SCRAK, {0, 64, 64, 64, 0, 0, 0, 0}, "SCRAK"},
    {DM_HMETL, {0, 40, 80, 40, 0, 0, 0, 0}, "HMETL"},
    {DM_HNOIS, {0, 40, 80, 40, 0, 0, 0, 0}, "HNOIS"},
    {DM_MEMB, {0, 60, 64, 20, 40, 30, 24, 0}, "MEMB"},
    {DM_FM, {0, 50, 60, 2, 40, 0, 0, 64}, "FM"},
    {DM_ACID, {0, 50, 40, 64, 64, 64, 0, 56}, "ACID"},
    {DM_SMPL, {0, 127, 127, 0, 0, 36, 0, 0}, "SMPL"},
};
#define PRE_NFACT ((uint32_t)(sizeof PRE_FACTORY / sizeof PRE_FACTORY[0]))
/* each bank's first id; PRE_BANK_START[PB_N]: the end of the user bank */
static const uint16_t PRE_BANK_START[PB_N + 1] = {0, 21, 35, PRE_NFACT, PRE_NFACT + PRE_NUSER};
