/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Parameter descriptions and value formatting. P_E0..P_E7 are described by the track's model. */
static const char *const N_DIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T"};
static const char *const N_ONOFF[] = {"OFF", "ON"};
static const char *const N_CLOCK[] = {"INT"};
static const char *const N_NOTE[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const N_DASH[] = {"--"};
static const char *const N_GO[] = {"--", "GO"};
static const char *const N_SLCR[] = {"OFF", "GATE", "STUT"};             /* SL_OFF .. SL_STUT (slicer.c) */
static const char *const N_SLDIV[] = {"1/8", "1/16", "1/32", "8T", "16T", "32T"};   /* SL_DEN */
static const char *const N_CHOKE[] = {"OFF", "1", "2", "3", "4"};
static const char *const N_SRC[] = {"STEP", "G-KCK", "G-SNR", "G-HAT"};   /* P_SRC: its steps or a Grids channel */
static const char *const N_GMODE[] = {"MAP", "EUCL"};
static const char *const N_CSRC[] = {"OFF", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8"};
static const char *const N_KNEE[] = {"HARD", "SOFT"};
static const char *const N_LWAVE[] = {"SQUAR", "SAW", "RSAW", "SINE", "TRI", "S&H", "WANDR", "EXP+", "EXP-", "RWALK"};
static const char *const N_LMODE[] = {"SYNC", "HZ", "TIME"};
static const char *const N_LTRIG[] = {"FREE", "HIT", "PLAY"};
static const char *const N_LSYNC[17] = {"8BAR", "4BAR", "2BAR", "1BAR", "1/2", "1/4.", "1/4", "1/4T", "1/8.", "1/8",
                                        "1/8T", "1/16.", "1/16", "1/16T", "1/32", "1/32T", "1/64"};

static const char *const N_RMODEL[] = {"OFF", "STRNG", "PIPE", "CHORD"};
static const char *const N_RCHORD[RS_NCHORD + 1] = {"OCT", "5TH", "4TH", "MAJ", "MIN", "SUS2", "SUS4", "DIM", "AUG",
                                                   "MAJ6", "MIN6", "MAJ7", "MIN7", "DOM7", "M7b5", "DIM7", "7SUS4",
                                                   "ADD9", "QUART", "CLUST", 0};
static const char *const N_RDEST[] = {"R.TUN", "R.DCY", "R.MIX", "R.TON", "R.STR", "R.POS"};   /* LFO DEST 11..16 */
#define LFO_TP(b, rf)                                                                                     \
    [b + LF_WAVE] = PE("WAVE", N_LWAVE, 3), [b + LF_MODE] = PE("MODE", N_LMODE, 0),                         \
    [b + LF_RATE] = PD("RATE", rf, 0, 127, 23), [b + LF_MORPH] = PD("MORPH", F_INT, 0, 127, 0),              \
    [b + LF_DEPTH] = PD("DEPTH", F_BIPCT, -64, 64, 0), [b + LF_DEST] = PD("DEST", F_LDEST, 0, 16, 0),        \
    [b + LF_TRIG] = PE("TRIG", N_LTRIG, 2), [b + LF_PHASE] = PD("PHASE", F_LPHASE, 0, 127, 0)
static const param_desc_t TP[P_COUNT] = {
    [P_MODEL] = PE("MODEL", N_MODEL, 0),
    [P_LEVEL] = PD("LVL", F_DB, 0, 127, 104),
    [P_PAN] = PD("PAN", F_BIPCT, -64, 63, 0),
    [P_MUTE] = PE("MUTE", N_ONOFF, 0),
    [P_CHOKE] = PE("CHOKE", N_CHOKE, 0),
    [P_NOTE] = PD("NOTE", F_INT, 0, 127, 36),
    [P_DIST] = PD("DST", F_PCT, 0, 127, 0),
    [P_CHOR] = PD("CHO", F_PCT, 0, 127, 0),
    [P_DLY] = PD("DLY", F_PCT, 0, 127, 0),
    [P_REV] = PD("REV", F_PCT, 0, 127, 0),
    [P_SLCR] = PE("SLCR", N_SLCR, 0),
    [P_SLPAT] = PD("PAT", F_INT, 1, 16, 1),        /* SL_PAT[] */
    [P_SLRATE] = PE("RATE", N_SLDIV, 1),
    [P_SLDEPTH] = PD("DEPTH", F_PCT, 0, 127, 127),
    [P_SLEN] = PD("LEN", F_STEPS, 1, NSTEP, 16),
    [P_SDIV] = PE("DIV", N_DIV, 2),
    [P_SSWING] = PD("SWG", F_PCT, 0, 100, 0),
    [P_LSET] = PE("LSET", SMP_ALL_NAMES, 0),
    [P_LKEY] = PD("LKEY", F_INT, 0, 127, 36),
    [P_LLEVEL] = PD("LLVL", F_PCT, 0, 127, 0),
    [P_LTUNE] = PD("LTUNE", F_SEMI, -24, 24, 0),
    [P_LDEC] = PD("LDEC", F_INT, 0, 127, 127),
    [P_SRC] = PE("SRC", N_SRC, 0),
    [P_DUCK] = PE("DUCK", N_ONOFF, 0),
    LFO_TP(P_LFO1, F_LRATE1),
    LFO_TP(P_LFO2, F_LRATE2),
    [P_RMODEL] = PE("MODEL", N_RMODEL, 0),
    [P_RTUNE] = PD("TUNE", F_NOTEO, 24, 96, 48),
    [P_RDECAY] = PD("DECAY", F_RDECAY, 0, 127, 72),
    [P_RMIX] = PD("MIX", F_PCT, 0, 127, 64),
    [P_RTONE] = PD("TONE", F_PCT, 0, 127, 100),
    [P_RSTRCT] = PD("STRCT", F_INT, 0, 127, 0),
    [P_RPOS] = PD("POS", F_PCT, 0, 127, 64),
};

static const param_desc_t GP[G_COUNT] = {
    [G_BPM] = PD("BPM", F_BPM, 40, 240, 120),
    [G_SWING] = PD("SWG", F_PCT, 0, 100, 0),
    [G_CLOCK] = PE("CLK", N_CLOCK, 0),
    [G_DTIME] = PE("TIME", N_DIV, 1),
    [G_DFDBK] = PD("FDBK", F_PCT, 0, 120, 60),
    [G_DCOLOR] = PD("COLR", F_PCT, 0, 127, 70),
    [G_DMIX] = PD("MIX", F_PCT, 0, 127, 90),
    [G_RSIZE] = PD("SIZE", F_PCT, 0, 127, 90),
    [G_RDAMP] = PD("DAMP", F_PCT, 0, 127, 60),
    [G_CRATE] = PD("CRT", F_LFOHZ, 0, 127, 40),
    [G_CDEPTH] = PD("CDP", F_PCT, 0, 127, 60),
    [G_MIDI] = PE("MIDI", N_DASH, 0),
    [G_SYNC] = PE("SYNC", N_DASH, 0),
    [G_ROUTE] = PE("ROUT", N_DASH, 0),
    [G_INFO] = PD("CPU", F_INT, 0, 0, 0),
    [G_SLOT] = PD("SLOT", F_INT, 1, 4, 1),
    [G_NAME] = PE("NAME", N_DASH, 0),
    [G_LOAD] = PE("LOAD", N_GO, 0),
    [G_SAVE] = PE("SAVE", N_GO, 0),
    [G_CLRSEQ] = PE("CLRSQ", N_GO, 0),
    [G_INITSND] = PE("INIT", N_GO, 0),
    [G_DRCH] = PD("CH", F_INT, 1, 16, 10),
    [G_GMODE] = PE("MODE", N_GMODE, 0),
    [G_GX] = PD("X", F_INT, 0, 127, 64),
    [G_GY] = PD("Y", F_INT, 0, 127, 64),
    [G_GCHAOS] = PD("CHAOS", F_PCT, 0, 127, 0),
    [G_GFILL1] = PD("FIL K", F_PCT, 0, 127, 64),
    [G_GFILL2] = PD("FIL S", F_PCT, 0, 127, 64),
    [G_GFILL3] = PD("FIL H", F_PCT, 0, 127, 64),
    [G_GLEN1] = PD("LEN K", F_STEPS, 1, 32, 16),
    [G_GLEN2] = PD("LEN S", F_STEPS, 1, 32, 12),
    [G_GLEN3] = PD("LEN H", F_STEPS, 1, 32, 8),
    [G_CSRC] = PE("SRC", N_CSRC, 0),
    [G_CTHR] = PD("THRSH", F_CTHR, 0, 127, 26),
    [G_CRAT] = PD("RATIO", F_CRAT, 0, 127, 45),
    [G_CREL] = PD("REL", F_CREL, 0, 127, 26),
    [G_CATK] = PD("ATK", F_CATK, 0, 127, 2),
    [G_CKNEE] = PE("KNEE", N_KNEE, 1),
    [G_CMKUP] = PD("MKUP", F_CMKUP, 0, 127, 0),
};

/* after G_COUNT: the TOOLS actions (G_CLRALL, G_INITALL) and GLOBAL 3/3's MUTE (G_MUTEBAR, the device setting
 * settings.mutebar); their knob values are song.act, not song.g / projects */
static const char *const N_MUTEBAR[] = {"NOW", "BAR"};
static const param_desc_t GP_ACT[3] = {PE("CLR*", N_GO, 0), PE("INIT*", N_GO, 0), PE("MUTE", N_MUTEBAR, 0)};

/* RESON STRCT on CHORD: the chord type, 0..127 split evenly over N_RCHORD (reson.c rs_chord) */
static const param_desc_t RS_CHORD_DESC = {"CHORD", F_INT, 0, 127, 0, N_RCHORD, 0};

static const param_desc_t *track_desc(const track_t *t, uint32_t id)
{
    if (id >= P_E0 && id <= P_E7)
        return &DMODELS[(uint32_t)t->p[P_MODEL] % NMODELS].edit[id - P_E0];
    if (id == P_RSTRCT && t->p[P_RMODEL] == RS_CHORD)
        return &RS_CHORD_DESC;
    return &TP[id];
}

static void fmt_ms10(char *val, const char **unit, uint32_t ms10)   /* a time in 0.1 ms: "4.5ms", "120ms", "1.20s" */
{
    if (ms10 < 100u) {
        fmt_fix(val, (int32_t)ms10, 1);
        *unit = "ms";
    } else if (ms10 < 10000u) {
        fmt_int(val, (int32_t)((ms10 + 5u) / 10u));
        *unit = "ms";
    } else {
        fmt_fix(val, (int32_t)(ms10 / 100u), 2);
        if (ms10 >= 100000u)
            fmt_fix(val, (int32_t)(ms10 / 1000u), 1);
        *unit = "s";
    }
}

#include "lfo_tables.h"
#include "reson_tables.h"
static uint32_t lfo_dest_param(uint32_t dest);    /* lfo.c */
/* an LFO DEST by name: OFF, the track's knob by its label ("--" where its engine has none), R.TUN .. R.POS */
static const char *lfo_dest_label(const track_t *t, int32_t dest)
{
    const param_desc_t *d;
    if (dest <= 0)
        return "OFF";
    if (dest >= 11)
        return N_RDEST[clamp(dest, 11, 16) - 11];
    d = track_desc(t, lfo_dest_param((uint32_t)clamp(dest, 1, 10)));
    return d->label && d->label[0] != '-' ? d->label : "--";
}

/* value string (<= 5 chars) and unit for a parameter value */
static void param_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    *unit = "";
    switch (d->fmt) {
    case F_PCT: {                                     /* the share of the knob's own range (0..100 swing, 0..127) */
        int32_t r = d->max - d->min > 0 ? d->max - d->min : 1;
        fmt_int(val, ((v - d->min) * 100 + r / 2) / r);
        *unit = "%";
        break;
    }
    case F_BIPCT:
        fmt_int(val, v * 100 / 64);
        if (v > 0) {
            char t[8];
            fmt_int(t, v * 100 / 64);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "%";
        break;
    case F_TIME:
        fmt_ms10(val, unit, TIME_MS_X10[v & 127]);
        break;
    case F_LRATE1:
    case F_LRATE2: {                                  /* by the LFO's RATE MODE (OCT+ on its page) */
        uint32_t mode = (uint32_t)clamp(TSEL->p[(d->fmt == F_LRATE1 ? P_LFO1 : P_LFO2) + LF_MODE], 0, 2), k = (uint32_t)clamp(v, 0, 127);
        if (mode == LM_SYNC) {
            str_cpy(val, N_LSYNC[k * 17u / 128u], 6);
        } else if (mode == LM_HZ) {                    /* "0.02" .. "9.99", "10.0" .. "40.0" (with the leading 0) */
            uint32_t h = LR_HZ_X100[k], n;
            fmt_int(val, (int32_t)(h / 100u));
            n = str_len(val);
            val[n++] = '.';
            val[n++] = (char)('0' + h / 10u % 10u);
            if (h < 1000u)
                val[n++] = (char)('0' + h % 10u);
            val[n] = 0;
            *unit = "Hz";
        } else {
            fmt_ms10(val, unit, LR_TIME_MS[k] * 10u);
        }
        break;
    }
    case F_LDEST:                                     /* the selected track's knob, by its label */
        str_cpy(val, lfo_dest_label(TSEL, v), 6);
        break;
    case F_LPHASE:
        fmt_int(val, (int32_t)clamp(v, 0, 127) * 360 / 128);
        *unit = "\xB0";                              /* the degree sign (Latin-1, in FONT_S) */
        break;
    case F_NOTEO:                                     /* a MIDI note as name + octave (60 = C4): "C3", "F#5" */
        str_cpy(val, N_NOTE[(uint32_t)clamp(v, 0, 127) % 12u], 6);
        fmt_int(val + str_len(val), clamp(v, 0, 127) / 12 - 1);
        break;
    case F_RDECAY:
        fmt_ms10(val, unit, RS_T60_MS10[clamp(v, 0, 127)]);
        break;
    case F_CTHR: {                                    /* whole dB from -10 down (the column fits "-24 dB") */
        int32_t d = comp_thr_dbx10(v);
        if (d <= -100)
            fmt_int(val, (d - 5) / 10);
        else
            comp_fmt_db10(val, d);
        *unit = "dB";
        break;
    }
    case F_CRAT:
        comp_ratio_text(v, val, unit);
        break;
    case F_CMKUP:
        comp_makeup_text(v, song.g[G_CTHR], val, unit);
        break;
    case F_CATK:
        fmt_ms10(val, unit, COMP_ATK_MS_X10[clamp(v, 0, 127)]);
        break;
    case F_CREL:
        fmt_ms10(val, unit, COMP_REL_MS_X10[clamp(v, 0, 127)]);
        break;
    case F_LFOHZ: {
        uint32_t h = LFO_HZ_X100[v & 127];
        if (h < 1000u)
            fmt_fix(val, (int32_t)h, 2);
        else
            fmt_fix(val, (int32_t)(h / 10u), 1);
        *unit = "Hz";
        break;
    }
    case F_CUTOFF: {
        uint32_t h = CUTOFF_HZ[v & 127];
        if (h < 1000u) {
            fmt_int(val, (int32_t)h);
            *unit = "Hz";
        } else {
            fmt_fix(val, (int32_t)(h / 100u), 1);
            *unit = "kHz";
        }
        break;
    }
    case F_DB:
        if (v <= 0) {
            str_cpy(val, "OFF", 6);
        } else {
            fmt_fix(val, LEVEL_DB_X10[v], 1);
            *unit = "dB";
        }
        break;
    case F_SEMI:
        fmt_int(val, v);
        if (v > 0) {
            char t[8];
            fmt_int(t, v);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "st";
        break;
    case F_ENUM:
        str_cpy(val, d->names[v < d->min ? d->min : v > d->max ? d->max : v], 6);
        if (d->unit)
            *unit = d->unit;
        break;
    case F_BPM:
        fmt_int(val, v);
        *unit = "BPM";
        break;
    case F_NOTE:
        str_cpy(val, N_NOTE[v % 12], 6);
        break;
    case F_ONOFF:
        str_cpy(val, N_ONOFF[v ? 1 : 0], 6);
        break;
    case F_STEPS:
        fmt_int(val, v);
        *unit = "STEP";
        break;
    default:
        if (d->names) {                               /* F_INT with a 0-terminated name list: the range */
            uint32_t k = 0;                           /* split evenly over the names (engine desc hooks) */
            while (d->names[k])
                k++;
            str_cpy(val, d->names[(uint32_t)(clamp(v, d->min, d->max) - d->min) * k / (uint32_t)(d->max - d->min + 1)], 6);
        } else {
            fmt_int(val, v);
        }
        if (d->unit)
            *unit = d->unit;
        break;
    }
}

/* PROB of a step (step_t.cond). The knob runs 0 %, 5 % .. 100 % (positions 0..20), 1-SHOT (21), then A/B
 * (22..56: 1/2, 2/2, 1/3 .. 8/8, "plays on loop A of every B"). Stored so that 0 is 100 % (a zeroed step is
 * plain): 0 = 100 %, 1..20 = 0 .. 95 %, 21.. as the knob. */
#define COND_POS_100 20u
#define COND_1SHOT 21u
#define COND_MAX 56u
static uint32_t cond_pos(uint32_t c) { return !c ? COND_POS_100 : c <= COND_POS_100 ? c - 1u : c < COND_MAX ? c : COND_MAX; }
static uint32_t cond_store(uint32_t pos)
{
    return pos == COND_POS_100 ? 0u : pos < COND_POS_100 ? pos + 1u : pos < COND_MAX ? pos : COND_MAX;
}

static void cond_ab(uint32_t c, uint32_t *a, uint32_t *b)   /* an A/B code (22..56): A of every B */
{
    uint32_t i = c > 22u ? (c < COND_MAX ? c : COND_MAX) - 22u : 0u, n = 2u;
    while (i >= n) {
        i -= n;
        n++;
    }
    *a = i + 1u;
    *b = n;
}

static void cond_format(uint32_t c, char *s)      /* "100%", "35%", "1-SHOT", "3/5"; s holds 8 bytes */
{
    uint32_t a, b;
    if (c < COND_1SHOT) {
        fmt_int(s, !c ? 100 : (int32_t)(c - 1u) * 5);
        str_cpy(s + str_len(s), "%", 2);
    } else if (c == COND_1SHOT) {
        str_cpy(s, "1-SHOT", 8);
    } else {
        cond_ab(c, &a, &b);
        s[0] = (char)('0' + a);
        s[1] = '/';
        s[2] = (char)('0' + b);
        s[3] = 0;
    }
}
