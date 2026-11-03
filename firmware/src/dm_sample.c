/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* One-shot IMA ADPCM playback (eng_sample.c decoder) for the SAMPLE model and the sample layer.
 *
 * SLOT (the SAMPLE model's P_E5, the LAYER's P_LKEY) picks one of its set's samples, 1..N in key order: a sample is
 * a zone with data of its own (zones sharing their data, offset length and rate, are one sample: PERC's 24 zones are
 * 16 samples), played at its root (TUNE pitches it). Stored as SMP_SLOT_V(i) = i - 16 (-16..-1): a value >= 0 is a
 * KEY from before SLOT (a MIDI note: the zone covering it, pitched by note - root), played as it always was; the
 * loads convert those (presets.c smp_slot_conv), LOCK / MOTION events keep theirs.
 * START / END (P_E6 / P_E7): the sample from (n * START) >> 7 to (n * (END + 1)) >> 7, END below START as START; END 0
 * (a SAMPLE stored before START / END: its "-" knob) as 127. The decoder's state at a START comes from a table:
 * gen_seek.py's for the built-in samples (flash), worked out in the main loop for each user slot's first sample
 * (smp_slots_sync); another sample of a user slot with several starts at its beginning (END still applies). The
 * zones' loops are not played (one-shot), so START / END are the only bounds. */
#include "felucca_seek.h"

/* zone index of key in set (built-in sets, then USR1..3), 0xFFFF = none */
static uint32_t smp_find(uint32_t set, uint32_t key)
{
    uint32_t i, zi = 0xFFFFu;
    set %= SMP_NALL;
    if (set < SMP_NSETS) {
        const smp_set_t *s = &SMP_SETS[set];
        for (i = 0; i < s->nz; i++)
            if (key >= SMP_ZONES[s->z0 + i].lo && key <= SMP_ZONES[s->z0 + i].hi)
                zi = s->z0 + i;
    } else {
        uint32_t k = set - SMP_NSETS;
        for (i = 0; i < usr_nz[k]; i++)
            if (key >= usr_zone[k][i].lo && key <= usr_zone[k][i].hi)
                zi = 0x8000u | k << 5 | i;
    }
    return zi;
}

/* ---- SLOT: a set's samples */
#define SMP_SLOT_N 16u                              /* SLOT's values: -16 .. -1 = sample 1 .. 16 */
#define SMP_SLOT_V(i) ((int32_t)(i) - (int32_t)SMP_SLOT_N)
_Static_assert(sizeof SMP_ZONES / sizeof SMP_ZONES[0] < 256u, "a built-in zone index is a byte");
static uint8_t sl_n[SMP_NALL], sl_z[SMP_NALL][SMP_SLOT_N];   /* set: its samples; sample i: its zone in the set */
static uint8_t sl_ok, sl_uok;                       /* the built-in sets' lists made; the user sets' (for sl_ugen) */
static uint32_t sl_ugen;
static uint32_t smp_set_nz(uint32_t set) { return set < SMP_NSETS ? SMP_SETS[set].nz : usr_nz[set - SMP_NSETS]; }
static uint32_t smp_set_zi(uint32_t set, uint32_t j)   /* zone j of set as a voice's zone index */
{
    return set < SMP_NSETS ? SMP_SETS[set].z0 + j : 0x8000u | (set - SMP_NSETS) << 5 | j;
}
static uint32_t smp_lo_of(uint32_t set, uint32_t j) { return smp_zone(smp_set_zi(set, j))->lo; }
static int smp_same(const smp_zone_t *a, const smp_zone_t *b) { return a->off == b->off && a->n == b->n && a->rate == b->rate; }
/* set's list: each sample by its lowest zone, in key order (then table order); made in locals, then copied */
static void sl_make(uint32_t set)
{
    uint8_t z[SMP_SLOT_N];
    uint32_t nz = smp_set_nz(set), c = 0, i, j;
    for (j = 0; j < nz; j++) {
        const smp_zone_t *a = smp_zone(smp_set_zi(set, j));
        if (!a->n)
            continue;
        for (i = 0; i < c && !smp_same(smp_zone(smp_set_zi(set, z[i])), a); i++)
            ;
        if (i < c) {
            if (a->lo < smp_lo_of(set, z[i]))
                z[i] = (uint8_t)j;
        } else if (c < SMP_SLOT_N) {
            z[c++] = (uint8_t)j;
        }
    }
    for (i = 1; i < c; i++) {
        uint8_t v = z[i];
        uint32_t lo = smp_lo_of(set, v), k = i;
        while (k && (smp_lo_of(set, z[k - 1u]) > lo || (smp_lo_of(set, z[k - 1u]) == lo && z[k - 1u] > v))) {
            z[k] = z[k - 1u];
            k--;
        }
        z[k] = v;
    }
    for (i = 0; i < c; i++)
        sl_z[set][i] = z[i];
    sl_n[set] = (uint8_t)c;
}
static void sl_builtin(void)                        /* any context: the built-in lists never change */
{
    uint32_t k;
    if (sl_ok)
        return;
    for (k = 0; k < SMP_NSETS; k++)
        sl_make(k);
    sl_ok = 1;
}
/* the samples set has now (0: none, an empty user slot) */
static uint32_t smp_slot_count(uint32_t set)
{
    set %= SMP_NALL;
    sl_builtin();
    return sl_n[set];
}
/* SLOT value v of set -> its sample 0..N-1 (a KEY from before SLOT: the zone covering it, else the nearest); -1 none */
static int32_t smp_slot_of(uint32_t set, int32_t v)
{
    uint32_t n = smp_slot_count(set), zi, i, j, best = 0xFFFFu, bd = 0xFFFFu;
    set %= SMP_NALL;
    if (!n)
        return -1;
    if (v < 0)
        return clamp(v + (int32_t)SMP_SLOT_N, 0, (int32_t)n - 1);
    zi = smp_find(set, (uint32_t)v);
    if (zi == 0xFFFFu)                              /* no zone there: the nearest (the first of equals) */
        for (j = 0; j < smp_set_nz(set); j++) {
            const smp_zone_t *z = smp_zone(smp_set_zi(set, j));
            uint32_t d = (uint32_t)v < z->lo ? z->lo - (uint32_t)v : (uint32_t)v > z->hi ? (uint32_t)v - z->hi : 0u;
            if (z->n && d < bd) {
                bd = d;
                best = smp_set_zi(set, j);
            }
        }
    else
        best = zi;
    if (best == 0xFFFFu)
        return -1;
    for (i = 0; i < n; i++)
        if (smp_same(smp_zone(smp_set_zi(set, sl_z[set][i])), smp_zone(best)))
            return (int32_t)i;
    return -1;
}
/* SLOT value v of set: the zone to play (0xFFFF none) and its pitch from the root, 1/16 semitones (*off16): a SLOT
 * at its root (0); a KEY from before SLOT as it played (note - root) */
static uint32_t smp_slot_zone(uint32_t set, int32_t v, int32_t *off16)
{
    uint32_t zi;
    int32_t i;
    set %= SMP_NALL;
    *off16 = 0;
    if (v >= 0) {
        zi = smp_find(set, (uint32_t)v);
        if (zi != 0xFFFFu)
            *off16 = v * 16 - smp_zone(zi)->root16;
        return zi;
    }
    i = smp_slot_of(set, v);
    if (i < 0)
        return 0xFFFFu;
    zi = smp_set_zi(set, sl_z[set][i]);
    return set >= SMP_NSETS && (zi & 31u) >= usr_nz[set - SMP_NSETS] ? 0xFFFFu : zi;   /* (a slot changing) */
}
/* "3/16" (a KEY from before SLOT: its sample's), "--" an empty set; val: 6 bytes */
static void smp_slot_text(uint32_t set, int32_t v, char *val)
{
    int32_t i = smp_slot_of(set, v), n = (int32_t)smp_slot_count(set);
    if (i < 0) {
        str_cpy(val, "--", 6);
        return;
    }
    fmt_int(val, i + 1);
    val[str_len(val) + 1u] = 0;
    val[str_len(val)] = '/';
    fmt_int(val + str_len(val), n);
}

/* ---- START: the decoder's states */
/* the state the decoder has (n * s) >> 7 nibbles into zone z, s 1..127, into p / ix: the main loop's (a user slot) */
static void smp_seek_fill(const smp_zone_t *z, int16_t *p, uint8_t *ix)
{
    const uint8_t *d = (const uint8_t *)((uintptr_t)SMP_DATA + z->off);   /* (a user zone's off: rebased onto SMP_DATA) */
    uint32_t pos = 0, s;
    int32_t pr = 0, x = 0;
    for (s = 1; s < 128u; s++) {
        uint32_t to = (z->n * s) >> 7;
        for (; pos < to; pos++) {
            uint32_t b = d[pos >> 1], c = (pos & 1u) ? b >> 4 : b & 15u;
            int32_t st = IMA_STEP[x], vd = st >> 3;
            if (c & 4u)
                vd += st;
            if (c & 2u)
                vd += st >> 1;
            if (c & 1u)
                vd += st >> 2;
            pr = clamp(pr + ((c & 8u) ? -vd : vd), -32768, 32767);
            x = clamp(x + IMA_IDX[c & 7u], 0, 88);
        }
        p[s - 1u] = (int16_t)pr;
        ix[s - 1u] = (uint8_t)x;
    }
}
#if defined(FELUCCA_BLE) && FELUCCA_BLE             /* (the Bluetooth build's .bss is full: the RAM-code region's spare,
                                                     * as the variations' banks; usr_seek_ok says what holds) */
static struct { int16_t p[SMP_USER_SLOTS][127]; uint8_t ix[SMP_USER_SLOTS][127]; } usr_seek
    __attribute__((section(".ram_bss"), aligned(4)));
#else
static struct { int16_t p[SMP_USER_SLOTS][127]; uint8_t ix[SMP_USER_SLOTS][127]; } usr_seek;
#endif
static uint8_t usr_seek_z[SMP_USER_SLOTS];          /* the zone usr_seek holds for, while usr_seek_ok's bit is set */
static uint8_t usr_seek_ok;
static uint32_t usr_seek_gen;                       /* smp_user_gen they were worked out for */
/* START s (1..127) of zone zi: the state there; 0 none (START plays from the beginning) */
static int smp_seek_at(uint32_t zi, uint32_t s, int32_t *p, int32_t *ix)
{
    if (zi < 0x8000u) {
        uint32_t r = zi < sizeof SMP_SEEK_ROW ? SMP_SEEK_ROW[zi] : 0xFFu;
        if (r >= SMP_SEEK_NROW)
            return 0;
        *p = SMP_SEEK_P[r][s - 1u];
        *ix = SMP_SEEK_I[r][s - 1u];
        return 1;
    } else {
        uint32_t k = (zi >> 5) & 3u;
        if (k >= SMP_USER_SLOTS || !((usr_seek_ok >> k) & 1u) || usr_seek_gen != smp_user_gen ||
            usr_seek_z[k] != (zi & 31u))
            return 0;
        *p = usr_seek.p[k][s - 1u];
        *ix = usr_seek.ix[k][s - 1u];
        return 1;
    }
}
/* main loop (and persist_boot): the user sets' lists and START states for the slots as they are now (after an upload,
 * a clear: smp_user_scan moved smp_user_gen). A trigger meanwhile: the old lists (a zone past the slot's: none), no
 * user START state (from the beginning) */
static void smp_slots_sync(void)
{
    uint32_t k, gen = smp_user_gen;
    sl_builtin();
    if (sl_uok && sl_ugen == gen && usr_seek_gen == gen)
        return;
    fm1_irq_off();
    usr_seek_ok = 0;
    for (k = SMP_NSETS; k < SMP_NALL; k++)
        sl_make(k);
    sl_ugen = gen;
    sl_uok = 1;
    usr_seek_gen = gen;
    fm1_irq_on();
    for (k = 0; k < SMP_USER_SLOTS; k++) {          /* (the IRQ on: a trigger finds no state, starts at 0) */
        if (!sl_n[SMP_NSETS + k])
            continue;
        usr_seek_z[k] = sl_z[SMP_NSETS + k][0];
        smp_seek_fill(&usr_zone[k][usr_seek_z[k]], usr_seek.p[k], usr_seek.ix[k]);
        RING_PUBLISH();
        usr_seek_ok |= (uint8_t)(1u << k);
    }
}

/* zone zi from START st to END en (0..127; en < st: st), pitched off16 + tune * 16 (1/16 semitones) */
static void smp_go(dvoice_t *v, uint32_t zi, int32_t off16, int32_t tune, uint32_t st, uint32_t en)
{
    const smp_zone_t *z = smp_zone(zi);
    voice_t *s = &v->sv;
    int32_t d16 = clamp(off16 + tune * 16, -1536, 576);   /* <= 3 octaves up */
    int32_t p, ix;
    s->ph[0] = s->ph[1] = 0;
    s->s[0] = s->s[1] = s->s[2] = s->s[6] = 0;
    if (st && smp_seek_at(zi, st, &p, &ix)) {
        s->ph[0] = (z->n * st) >> 7;
        s->s[0] = p;
        s->s[1] = ix;
    }
    en = en < st ? st : en;
    s->ph[2] = en >= 127u ? z->n : (z->n * (en + 1u)) >> 7;
    s->s[4] = (int32_t)zi;
    s->s[5] = (int32_t)((pow2_q16(d16) >> 8) * (z->rate >> 8));   /* Q16 source samples per output */
    s->s[3] = z->n ? sample_next(z, s, 0) : 0;
}
/* one block: amplitude env[0] (factor k[0]), gain Q15, one-pole tone lp (Q15 coefficient), drive 0..127.
 * Returns 0 when the sample (to its END) or its envelope has ended (the voice is then off). */
static int smp_render(dvoice_t *v, int32_t *out, uint32_t n, int32_t gain, int32_t lp, int32_t drv)
{
    voice_t *s = &v->sv;
    const smp_zone_t *z = smp_zone((uint32_t)s->s[4]);
    uint32_t i, frac = s->ph[1], stepq = (uint32_t)s->s[5], end = s->ph[2] < z->n ? s->ph[2] : z->n;   /* (a user
                                                     * slot being written: its zones' n 0, the voice ends) */
    for (i = 0; i < n; i++) {
        int32_t x;
        frac += stepq;
        while (frac >= 65536u) {
            frac -= 65536u;
            s->s[2] = s->s[3];
            if (s->ph[0] >= end) {
                dm_end(v);
                return 0;
            }
            s->s[3] = sample_next(z, s, 0);
        }
        x = s->s[2] + (((s->s[3] - s->s[2]) * (int32_t)(frac >> 1)) >> 15);
        s->s[6] += mulq15(x - s->s[6], lp);
        x = s->s[6];
        if (drv)
            x = softclip(x + (((x >> 2) * (drv * 150)) >> 11));
        x = mulq15(mulq15(x, env_q15(v->env[0])), gain);
        x = mulq15(x, VOICE_FS);
        out[i] += x;
        v->last = x;
        env_step(v, 0);
    }
    s->ph[1] = frac;
    if (v->env[0] < ENV_END) {
        dm_end(v);
        return 0;
    }
    return 1;
}

/* SAMPLE model: TUNE DECAY TONE DRIVE, SET SLOT START END */
static int smpl_playable(const track_t *t)          /* SET / SLOT name a sample (else a hit takes no voice) */
{
    int32_t o;
    uint32_t zi = smp_slot_zone((uint32_t)t->p[P_E4], t->p[P_E5], &o);
    return zi != 0xFFFFu && smp_zone(zi)->n;
}

static void smpl_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    int32_t o, en = p[7] <= 0 ? 127 : p[7];         /* (END 0: stored before START / END, the whole sample) */
    uint32_t zi = smp_slot_zone((uint32_t)p[4], p[5], &o);
    if (zi == 0xFFFFu || !smp_zone(zi)->n) {
        dm_end(v);
        return;
    }
    smp_go(v, zi, o, dm_tune(t), (uint32_t)clamp(p[6], 0, 127), (uint32_t)clamp(en, 0, 127));
    v->env[0] = ENV1;
    v->k[0] = dk(p[1]);
    v->x[0] = 4000 + p[2] * 28767 / 127;        /* tone lp coefficient */
}

static void smpl_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    smp_render(v, out, n, vel_gain(v), v->x[0], t->p[P_E3]);
}

#define DM_SMPL_DEF {"SAMPL", 2, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 127), \
    PD("TONE", F_INT, 0, 127, 127), PD("DRIVE", F_PCT, 0, 127, 0), PE("SET", SMP_ALL_NAMES, 0), \
    PD("SLOT", F_SLOT, -16, -1, -16), PD("START", F_INT, 0, 127, 0), PD("END", F_INT, 1, 127, 127)}, smpl_trigger, \
    smpl_render}
