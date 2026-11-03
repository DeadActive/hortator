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
 * zones' loops are not played (one-shot), so START / END are the only bounds.
 * ATK SUS REL LOOP (P_SATK..P_SLOOP, SOUND 5/5; read at the hit): the envelope env = env * k + c each sample, k and c
 * set a block at a time (smpl_env): ATK a ramp from silence (-84 dB) to full, then DECAY's fall towards SUS's level
 * (SUS 0: to silence, the one-shot as before), held while the note's gate is open, then REL's fall. The gate: a hit's
 * is one step of the track (DIV at the tempo then), longer through the TIE steps after it (seq.c step_fire); a KEYS
 * key, a pad, a MIDI note: while held (SMPL_HOLD_MAX at most). LOOP between START and END (smp_edge, called from the
 * decode loop only where the sample's bound is reached): FWD jumps back to START with a 64-sample crossfade from the
 * sample past END to the loop's start (a short loop: half its length); PING-PONG turns at END and at START (the
 * sample mirrored: no jump to click), its backward leg decoded forward a chunk at a time from states kept on the way
 * (smpl_loop_t), exactly the samples the forward leg played; a loop longer than SPP_K1 x SPP_K2 x SPP_B samples plays
 * FWD. At the defaults (ATK 0, SUS 0, LOOP OFF; REL does nothing then) a hit plays as before, bit for bit. */
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
/* a user set's START table (the web editor writes it into a set of several samples: web/EDITOR_PROTOCOL.md §5): the
 * header's rsv2[0] "SEEK", rsv2[1] its offset in the data; zone j's states at START s 1..127 (its decoder (n * s) >> 7
 * nibbles in, smp_seek_fill's) at 3 x (127 j + s - 1): predictor (2 bytes), step index. Read from flash at the hit (no
 * RAM); a set without it (one sample, an older page's, a backup): the main loop's state of its first sample only */
#define SMP_SEEK_MAGIC 0x4B454553u                  /* "SEEK" */
static const uint8_t *smp_user_seek(uint32_t k, uint32_t j, uint32_t s)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(k);
    uint32_t off = h->rsv2[1];
    if (h->rsv2[0] != SMP_SEEK_MAGIC || j >= usr_nz[k] || off > h->data_len || (h->data_len - off) / 381u < usr_nz[k])
        return 0;
    return smp_user_xip(k) + SMP_USER_DATA + off + (j * 127u + s - 1u) * 3u;
}
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
        const uint8_t *q = k < SMP_USER_SLOTS ? smp_user_seek(k, zi & 31u, s) : 0;
        if (q) {
            *p = (int16_t)(q[0] | q[1] << 8);
            *ix = q[2] <= 88u ? q[2] : 88;
            return 1;
        }
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
/* ---- LOOP (smpl_loop_t in the voice's model state; smp_edge is called by smp_render's decode loop at its bound only,
 * and does a bounded amount of work each time: no loop of its own) */
_Static_assert(sizeof(smpl_loop_t) <= sizeof(dm_state_t) && SPP_K1 * SPP_K2 <= 256u && SPP_B * 2u <= 255u,
               "LOOP's state: in the model state, chunk indexes a byte");
#define SPP_XF 64u                                  /* FWD: the crossfade over the seam, source samples */
/* one nibble of data d at pos onto the state p / x (sample_next's rule); the sample */
static int32_t smp_nib(const uint8_t *d, uint32_t pos, int32_t *p, int32_t *x)
{
    uint32_t b = d[pos >> 1], c = (pos & 1u) ? b >> 4 : b & 15u;
    int32_t st = IMA_STEP[*x], vd = st >> 3;
    if (c & 4u)
        vd += st;
    if (c & 2u)
        vd += st >> 1;
    if (c & 1u)
        vd += st >> 2;
    *p = clamp(*p + ((c & 8u) ? -vd : vd), -32768, 32767);
    *x = clamp(*x + IMA_IDX[c & 7u], 0, 88);
    return *p;
}
static void spp_put(uint8_t *q, int32_t p, int32_t x)
{
    q[0] = (uint8_t)p;
    q[1] = (uint8_t)((uint32_t)p >> 8);
    q[2] = (uint8_t)x;
}
static void spp_get(const uint8_t *q, int16_t *p, uint8_t *x)
{
    *p = (int16_t)(q[0] | q[1] << 8);
    *x = q[2];
}
/* PING's chunks, counted down from END: chunk k is [spp_lo(k), END - k * SPP_B), the lowest one may be shorter */
static uint32_t spp_lo(const smpl_loop_t *l, uint32_t k)
{
    return l->le - l->ls > (k + 1u) * SPP_B ? l->le - (k + 1u) * SPP_B : l->ls;
}
static uint32_t spp_len(const smpl_loop_t *l, uint32_t k) { return (k ? spp_lo(l, k - 1u) : l->le) - spp_lo(l, k); }
static uint32_t spp_slot(uint32_t k, uint32_t t) { return (k & 1u) ? SPP_B - 1u - t : t; }   /* sample t of chunk k */
/* the state before chunk k's first nibble (p, x): the forward leg keeps its segment's (the lowest chunk's) and the top
 * segment's chunks' */
static void spp_keep(smpl_loop_t *l, uint32_t k, int32_t p, int32_t x)
{
    uint32_t sg = k / SPP_K2, kl = sg * SPP_K2 + SPP_K2 - 1u;
    if (k == (kl < l->nch ? kl : l->nch - 1u))
        spp_put(l->co[sg], p, x);
    if (!sg)
        spp_put(l->fi[l->fcur][k], p, x);
}
/* PING: the fill of chunk k (the one below the chunk playing) from its state; one nibble of it into the slot the
 * chunk playing freed */
static void spp_fill_start(smpl_loop_t *l, uint32_t k)
{
    uint32_t a = k / SPP_K2 == l->rk / SPP_K2 ? l->fcur : l->fcur ^ 1u;
    spp_get(l->fi[a][k % SPP_K2], &l->fp, &l->fx);
    l->fpos = spp_lo(l, k);
    l->fill = (uint8_t)spp_len(l, k);
}
static void spp_fill_one(smpl_loop_t *l, const uint8_t *d)
{
    uint32_t k = l->rk + 1u, t = spp_len(l, k) - l->fill;
    int32_t p = l->fp, x = l->fx;
    l->buf[spp_slot(k, t)] = (int16_t)smp_nib(d, l->fpos++, &p, &x);
    l->fp = (int16_t)p;
    l->fx = (uint8_t)x;
    l->fill--;
}
/* PING: the look-ahead works out segment sg's chunk states (into the fi[] not playing), from its lowest chunk up */
static void spp_look_start(smpl_loop_t *l, uint32_t sg)
{
    uint32_t kl = sg * SPP_K2 + SPP_K2 - 1u;
    kl = kl < l->nch ? kl : l->nch - 1u;
    spp_get(l->co[sg], &l->ap, &l->ax);
    l->apos = spp_lo(l, kl);
    memcpy(l->fi[l->fcur ^ 1u][kl % SPP_K2], l->co[sg], 3);
    l->aj = (uint8_t)(kl % SPP_K2);
    l->ac = l->aj;                                  /* chunks still to reach: kl - 1 .. sg * SPP_K2 */
}
static void spp_look_one(smpl_loop_t *l, const uint8_t *d)
{
    int32_t p = l->ap, x = l->ax;
    if (!l->ac)
        return;
    smp_nib(d, l->apos++, &p, &x);
    l->ap = (int16_t)p;
    l->ax = (uint8_t)x;
    if (l->apos == l->le - ((uint32_t)(l->rk / SPP_K2 + 1u) * SPP_K2 + l->aj) * SPP_B) {
        l->aj--;                                    /* (the segment below the one playing: its chunk aj - 1's start) */
        spp_put(l->fi[l->fcur ^ 1u][l->aj], p, x);
        l->ac--;
    }
}
/* the forward leg at pos (a chunk's start, or inside the top chunk: there every sample, kept for the turn) */
static uint32_t spp_fwd(smpl_loop_t *l, voice_t *s, const uint8_t *d)
{
    uint32_t pos = s->ph[0], top = spp_lo(l, 0);
    int32_t p = s->s[0], x = s->s[1];
    if (pos >= top) {
        if (pos == top)
            spp_keep(l, 0, p, x);
        l->buf[pos - top] = (int16_t)smp_nib(d, pos, &p, &x);
        s->s[0] = p;
        s->s[1] = x;
        s->s[3] = p;
        s->ph[0] = pos + 1u;
        return (pos + 1u) << 1 | 1u;
    }
    spp_keep(l, (l->le - pos) / SPP_B - 1u, p, x);
    return (pos + SPP_B) << 1;
}
/* PING backward: the next sample down (a chunk done: the next one, filled meanwhile; START reached: forward again) */
static uint32_t spp_back(smpl_loop_t *l, voice_t *s, const uint8_t *d)
{
    if (!l->rj) {
        if (l->rk + 1u >= l->nch) {                 /* START: forward from the sample after it */
            int32_t p = l->p0, x = l->x0, v0 = smp_nib(d, l->ls, &p, &x);
            uint32_t nx;
            s->s[0] = p;
            s->s[1] = x;
            s->ph[0] = l->ls + 1u;
            l->dir = 0;
            l->fcur = 0;
            spp_keep(l, l->nch - 1u, l->p0, l->x0);
            if (l->nch == 1u)
                l->buf[0] = (int16_t)v0;
            nx = l->nch == 1u ? l->ls + 1u : spp_lo(l, l->nch - 2u);
            return nx == s->ph[0] ? spp_fwd(l, s, d) : nx << 1;
        }
        l->rk++;
        l->rj = (uint8_t)spp_len(l, l->rk);
        if (!(l->rk % SPP_K2)) {                    /* a segment down: its states the look-ahead's */
            l->fcur ^= 1u;
            if (l->rk / SPP_K2 + 1u < l->nseg)
                spp_look_start(l, l->rk / SPP_K2 + 1u);
        }
        if (l->rk + 1u < l->nch)
            spp_fill_start(l, l->rk + 1u);
    }
    l->rj--;
    s->s[3] = l->buf[spp_slot(l->rk, l->rj)];
    if (l->fill)
        spp_fill_one(l, d);
    spp_look_one(l, d);
    spp_look_one(l, d);
    return 1;
}
/* FWD's crossfade: the sample past END (frozen at the zone's end) fading into the loop from START */
static uint32_t spp_xfade(smpl_loop_t *l, voice_t *s, const smp_zone_t *z, const uint8_t *d)
{
    int32_t p = s->s[0], x = s->s[1], h = smp_nib(d, s->ph[0], &p, &x), tv = l->fp;
    s->s[0] = p;
    s->s[1] = x;
    s->ph[0]++;
    if (l->fpos < z->n) {
        int32_t tp = l->fp, tx = l->fx;
        tv = smp_nib(d, l->fpos++, &tp, &tx);
        l->fp = (int16_t)tp;
        l->fx = (uint8_t)tx;
    }
    s->s[3] = h + (tv - h) * (int32_t)l->xf / (int32_t)(l->xfn + 1u);
    l->xf--;
    return l->xf ? 1u : l->le << 1 | 1u;
}
/* smp_render's bound reached (decode loop; the IRQ's): 0 the sample ends (LOOP OFF, END; the slot being rewritten);
 * else the next bound << 1 (kept in ph[2] for the next block) | 1, s[3] the next source sample (made here) */
static uint32_t smp_edge_at(dvoice_t *v, const smp_zone_t *z);
#if defined(DRUM_HOST) || defined(SIM_HOST)
#define SMP_COLD __attribute((noinline))            /* (the hosts define __attribute__ away: their cost checks then see
                                                     * the decode loop the firmware has, the edge a call out of it) */
#else
#define SMP_COLD __attribute__((noinline))
#endif
SMP_COLD static uint32_t smp_edge(dvoice_t *v, const smp_zone_t *z)
{
    uint32_t r = smp_edge_at(v, z);
    if (r && !(r & 1u)) {                           /* (the next sample here too: the decode loop takes it as made) */
        v->sv.s[3] = sample_next(z, &v->sv, 0);
        r |= 1u;
    }
    v->sv.ph[2] = r >> 1;
    return r;
}
static uint32_t smp_edge_at(dvoice_t *v, const smp_zone_t *z)
{
    smpl_loop_t *l = &v->ms.sl;
    voice_t *s = &v->sv;
    const uint8_t *d = (const uint8_t *)((uintptr_t)SMP_DATA + z->off);
    if (!l->mode || l->le > z->n)
        return 0;
    if (l->xf)
        return spp_xfade(l, s, z, d);
    if (l->dir)
        return spp_back(l, s, d);
    if (s->ph[0] < l->le)
        return spp_fwd(l, s, d);
    if (l->mode == 1u) {                            /* FWD: back to START, the crossfade from here */
        uint32_t len = l->le - l->ls;
        l->xfn = (uint8_t)(len / 2u < SPP_XF ? len / 2u : SPP_XF);
        l->xf = l->xfn;
        l->fpos = l->le;
        l->fp = (int16_t)s->s[0];
        l->fx = (uint8_t)s->s[1];
        s->ph[0] = l->ls;
        s->s[0] = l->p0;
        s->s[1] = l->x0;
        return spp_xfade(l, s, z, d);
    }
    l->dir = 1;                                     /* PING: END: backward from the sample before it */
    l->rk = 0;
    l->rj = (uint8_t)(spp_len(l, 0) - 1u);
    l->fill = 0;
    l->ac = 0;
    if (l->nch > 1u) {
        spp_fill_start(l, 1);
        spp_fill_one(l, d);
    }
    if (l->nseg > 1u)
        spp_look_start(l, 1);
    return spp_back(l, s, d);
}
/* the hit's LOOP: zone zi from START st (its decoder state there, or the beginning: smp_go's), to END s->ph[2] */
static void smp_loop_init(dvoice_t *v, uint32_t zi, uint32_t st, uint32_t mode)
{
    smpl_loop_t *l = &v->ms.sl;
    voice_t *s = &v->sv;
    const smp_zone_t *z = smp_zone(zi);
    int32_t p = 0, ix = 0;
    uint32_t len;
    if (!mode)
        return;
    l->ls = st && smp_seek_at(zi, st, &p, &ix) ? (z->n * st) >> 7 : 0u;
    l->le = s->ph[2] < z->n ? s->ph[2] : z->n;
    l->p0 = (int16_t)p;
    l->x0 = (uint8_t)ix;
    len = l->le - l->ls;
    if (len < 2u || l->le > z->n)
        return;
    l->nch = (uint16_t)((len + SPP_B - 1u) / SPP_B);
    if (mode == 2u && l->nch > SPP_K1 * SPP_K2)
        mode = 1;                                   /* longer than PING-PONG holds: FWD */
    l->mode = (uint8_t)mode;
    if (mode == 1u)
        return;                                     /* (the bound: END) */
    l->nseg = (uint8_t)((l->nch + SPP_K2 - 1u) / SPP_K2);
    l->fcur = 0;
    spp_keep(l, l->nch - 1u, p, ix);                /* (START: the lowest chunk's start) */
    if (l->nch == 1u)
        l->buf[0] = (int16_t)s->s[3];
    s->ph[2] = l->nch == 1u ? l->ls + 1u : spp_lo(l, l->nch - 2u);
}

/* one source step at the bound's side (smp_render's slow path): 0 the voice ended */
SMP_COLD static int smp_step1(dvoice_t *v, const smp_zone_t *z)
{
    voice_t *s = &v->sv;
    s->s[2] = s->s[3];
    if (s->ph[0] >= (s->ph[2] < z->n ? s->ph[2] : z->n))
        return smp_edge(v, z) != 0;
    s->s[3] = sample_next(z, s, 0);
    return 1;
}
/* an output sample's m source steps (1..32: TUNE 3 octaves up on a 4x zone at most), one at a time (written out: no
 * loop here, the decode loop stays smp_render's own); 0 the voice ended */
#define SMP_S1 if (!smp_step1(v, z)) return 0   /* (falls through: each case one step more) */
SMP_COLD static int smp_steps(dvoice_t *v, const smp_zone_t *z, uint32_t m)
{
    switch (m) {
    case 32: SMP_S1; /* fall through */ case 31: SMP_S1; /* fall through */ case 30: SMP_S1; /* fall through */
    case 29: SMP_S1; /* fall through */ case 28: SMP_S1; /* fall through */ case 27: SMP_S1; /* fall through */
    case 26: SMP_S1; /* fall through */ case 25: SMP_S1; /* fall through */ case 24: SMP_S1; /* fall through */
    case 23: SMP_S1; /* fall through */ case 22: SMP_S1; /* fall through */ case 21: SMP_S1; /* fall through */
    case 20: SMP_S1; /* fall through */ case 19: SMP_S1; /* fall through */ case 18: SMP_S1; /* fall through */
    case 17: SMP_S1; /* fall through */ case 16: SMP_S1; /* fall through */ case 15: SMP_S1; /* fall through */
    case 14: SMP_S1; /* fall through */ case 13: SMP_S1; /* fall through */ case 12: SMP_S1; /* fall through */
    case 11: SMP_S1; /* fall through */ case 10: SMP_S1; /* fall through */ case 9: SMP_S1; /* fall through */
    case 8: SMP_S1; /* fall through */ case 7: SMP_S1; /* fall through */ case 6: SMP_S1; /* fall through */
    case 5: SMP_S1; /* fall through */ case 4: SMP_S1; /* fall through */ case 3: SMP_S1; /* fall through */
    case 2: SMP_S1; /* fall through */ case 1: SMP_S1; /* fall through */
    default: return 1;
    }
}
#undef SMP_S1

/* one block: amplitude env[0] (env * k[0] + c a sample), gain Q15, one-pole tone lp (Q15 coefficient), drive 0..127.
 * Returns 0 when the sample (to its END) or its envelope has ended (the voice is then off). An output sample whose
 * source steps reach the bound (ph[2]: END, or LOOP's next point) takes them one at a time (smp_steps, smp_edge) */
static int smp_render(dvoice_t *v, int32_t *out, uint32_t n, int32_t gain, int32_t lp, int32_t drv, int32_t c)
{
    voice_t *s = &v->sv;
    const smp_zone_t *z = smp_zone((uint32_t)s->s[4]);
    const uint8_t *d = (const uint8_t *)((uintptr_t)SMP_DATA + z->off);
    uint32_t i, frac = s->ph[1], stepq = (uint32_t)s->s[5], end = s->ph[2] < z->n ? s->ph[2] : z->n;   /* (a user
                                                     * slot being written: its zones' n 0, the voice ends) */
    uint32_t k = v->k[0], pos = s->ph[0];
    int32_t env = v->env[0], tone = s->s[6], last = v->last;   /* (the block's state in registers, back after it) */
    int32_t pr = s->s[0], ix = s->s[1], s2 = s->s[2], s3 = s->s[3];
    for (i = 0; i < n; i++) {
        int32_t x;
        uint32_t m;
        frac += stepq;
        m = frac >> 16;
        if (m && pos + m > end) {                   /* the bound (END, LOOP's) in this sample's steps: one at a time,
                                                     * out of the loop (smp_steps; smp_edge moves the state) */
            s->ph[0] = pos;
            s->s[0] = pr;
            s->s[1] = ix;
            s->s[3] = s3;
            frac &= 0xFFFFu;
            if (!smp_steps(v, z, m)) {
                dm_end(v);
                return 0;
            }
            pos = s->ph[0];
            pr = s->s[0];
            ix = s->s[1];
            s2 = s->s[2];
            s3 = s->s[3];
            end = s->ph[2] < z->n ? s->ph[2] : z->n;
        } else {
            while (frac >= 65536u) {                /* sample_next, in registers (the step index always 0..88 here:
                                                     * smp_go's, smp_edge's, each clamped: no clamp on its read) */
                uint32_t b = d[pos >> 1], code = (pos & 1u) ? (b >> 4) : (b & 15u);
                int32_t step = IMA_STEP[ix], vd = step >> 3;
                frac -= 65536u;
                s2 = s3;
                if (code & 4u)
                    vd += step;
                if (code & 2u)
                    vd += step >> 1;
                if (code & 1u)
                    vd += step >> 2;
                pr = clamp(pr + ((code & 8u) ? -vd : vd), -32768, 32767);
                ix = clamp(ix + IMA_IDX[code & 7u], 0, 88);
                pos++;
                s3 = pr;
            }
        }
        x = s2 + (((s3 - s2) * (int32_t)(frac >> 1)) >> 15);
        tone += mulq15(x - tone, lp);
        x = tone;
        if (drv)
            x = softclip(x + (((x >> 2) * (drv * 150)) >> 11));
        x = mulq15(mulq15(x, env_q15(env)), gain);
        x = mulq15(x, VOICE_FS);
        out[i] += x;
        last = x;
        env = mulq16(env, k) + c;
    }
    s->ph[0] = pos;
    s->ph[1] = frac;
    s->s[0] = pr;
    s->s[1] = ix;
    s->s[2] = s2;
    s->s[3] = s3;
    s->s[6] = tone;
    v->env[0] = env;
    v->last = last;
    if (env < ENV_END) {
        dm_end(v);
        return 0;
    }
    return 1;
}

/* SAMPLE model: TUNE DECAY TONE DRIVE, SET SLOT START END; PLAY (KEY / CHOP), ATK SUS REL LOOP.
 * PLAY CHOP: a hit's note picks the set's sample, a chop: chop (note - C4) mod 16 + 1 (a step's NOTE, the keys' (the
 * KEYS white keys: chops 1..16 on any octave, seq.c keys_note), a MIDI note's), played at its root + TUNE (the note does
 * not pitch it); SLOT unused; a chop past the set's samples plays nothing. KEY: as before (SLOT's sample, the note
 * pitching it) */
static int32_t smpl_chop(const track_t *t)          /* the hit's chop 0..15 (PLAY CHOP), -1 KEY */
{
    return t->p[P_SPLAY] == 1 ? (int32_t)(((uint32_t)(int32_t)t->nt) & 15u) : -1;
}
/* the zone a hit of t plays (0xFFFF none) and its pitch from the root (1/16 semitones) */
static uint32_t smpl_zone(const track_t *t, int32_t *o)
{
    int32_t c = smpl_chop(t);
    uint32_t set = (uint32_t)t->p[P_E4];
    if (c >= 0 && (uint32_t)c >= smp_slot_count(set)) {
        *o = 0;
        return 0xFFFFu;                             /* (a chop the set does not have: nothing) */
    }
    return smp_slot_zone(set, c >= 0 ? SMP_SLOT_V(c) : t->p[P_E5], o);
}
static int smpl_playable(const track_t *t)          /* SET / SLOT (CHOP: the chop) name a sample (else no voice) */
{
    int32_t o;
    uint32_t zi = smpl_zone(t, &o);
    return zi != 0xFFFFu && smp_zone(zi)->n;
}

/* ---- ATK SUS REL: the voice's spare fields: k[1] DECAY's factor, inc[0] REL's, k[2] the SE_ bits, inc[1] who holds
 * the note (smpl_hold: 0 nobody), x[1] the gate's samples left, x[2] ATK's, x[3] SUS's level (Q24) */
#define SE_ATK 1u                                   /* attacking */
#define SE_SUS 2u                                   /* SUS above 0: the gate holds the note */
#define SE_REL 4u                                   /* the gate closed: releasing */
#define SE_ON 8u                                    /* ATK or SUS: smpl_env runs (else DECAY alone, as before) */
#define SMPL_HOLD_MAX (10u * 44100u)                /* a held key / note's gate at most (a note-off lost) */
static uint32_t smpl_gate_len(const track_t *t);    /* seq.c: one step of the track */
/* ATK 1..127: 1 ms .. 2 s, exponential (2^(11 x (ATK - 1) / 126) ms) */
static uint32_t smpl_atk_len(int32_t a) { return ((pow2_q16((a - 1) * 2106 / 126) >> 8) * 44u) >> 8; }
static void smpl_env_init(const track_t *t, dvoice_t *v)
{
    int32_t a = clamp(t->p[P_SATK], 0, 127), su = clamp(t->p[P_SSUS], 0, 127);
    if (!a && !su)
        return;                                     /* (the default: DECAY alone, bit for bit as before) */
    v->k[1] = v->k[0];
    v->inc[0] = dk(t->p[P_SREL]);
    v->x[3] = su * (ENV1 / 127);
    v->x[1] = (int32_t)smpl_gate_len(t);
    v->k[2] = SE_ON | (su ? SE_SUS : 0u) | (a ? SE_ATK : 0u);
    if (a) {
        v->env[0] = ENV_END;                        /* (-84 dB: above the voice's end) */
        v->x[2] = (int32_t)smpl_atk_len(a);
        v->k[0] = 65536u;
    }
}
/* the envelope's block of n: the gate counted, this block's factor into k[0]; returns c (env = env * k + c): ATK the
 * ramp's step, DECAY towards SUS the constant that settles it there, REL 0 */
static int32_t smpl_env(dvoice_t *v, uint32_t n)
{
    uint32_t f = v->k[2];
    if ((f & (SE_SUS | SE_REL)) == SE_SUS) {
        if ((uint32_t)v->x[1] > n)
            v->x[1] -= (int32_t)n;
        else
            f = (f | SE_REL) & ~SE_ATK;
    }
    v->k[2] = f;
    if (f & SE_REL) {
        v->k[0] = v->inc[0];
        return 0;
    }
    if (f & SE_ATK) {
        int32_t left = v->x[2], c;
        v->k[0] = 65536u;
        if ((uint32_t)left > n) {
            c = (ENV1 - v->env[0]) / left;
            v->x[2] = left - (int32_t)n;
        } else {
            c = (ENV1 - v->env[0]) / (int32_t)n;
            v->k[2] = f & ~SE_ATK;
        }
        return c;
    }
    v->k[0] = v->k[1];
    return v->x[3] - mulq16(v->x[3], v->k[1]);
}

static void smpl_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    int32_t o, en = p[7] <= 0 ? 127 : p[7];         /* (END 0: stored before START / END, the whole sample) */
    uint32_t zi = smpl_zone(t, &o), st = (uint32_t)clamp(p[6], 0, 127);
    if (zi == 0xFFFFu || !smp_zone(zi)->n) {
        dm_end(v);
        return;
    }
    smp_go(v, zi, o, smpl_chop(t) >= 0 ? p[0] : dm_tune(t), st, (uint32_t)clamp(en, 0, 127));   /* (a chop: TUNE alone) */
    v->env[0] = ENV1;
    v->k[0] = dk(p[1]);
    v->x[0] = 4000 + p[2] * 28767 / 127;        /* tone lp coefficient */
    smpl_env_init(t, v);
    smp_loop_init(v, zi, st, (uint32_t)clamp(t->p[P_SLOOP], 0, 2));
}

static void smpl_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    smp_render(v, out, n, vel_gain(v), v->x[0], t->p[P_E3], v->k[2] ? smpl_env(v, n) : 0);
}

#define DM_SMPL_DEF {"SAMPL", 2, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 127), \
    PD("TONE", F_INT, 0, 127, 127), PD("DRIVE", F_PCT, 0, 127, 0), PE("SET", SMP_ALL_NAMES, 0), \
    PD("SLOT", F_SLOT, -16, -1, -16), PD("START", F_INT, 0, 127, 0), PD("END", F_INT, 1, 127, 127)}, smpl_trigger, \
    smpl_render}
