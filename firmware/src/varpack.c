/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* Variations (spec docs/superpowers/specs/2026-10-26-cue-variations-fill-copy-design.md): a slot plays one of 8
 * variations, each its own steps and timing (LEN DIV SWG SRC, song.c CHAIN_TIMING); variation 1 is the slot's record's
 * steps (project.c), variations 2..8 are packed here, sparse, in one bank: the slot's flash object (storage.c
 * OBJ_VAR0 + slot; 3584 B at most, so its sector's +0xF00.. stays erased for the loader's scan). Pure data: no IRQ, no
 * flash; every read is bounded by the bank (a hostile flash copy is checked by var_valid, var_get never reads past it).
 *
 * The bank's d: variations 2..8 in order, each an entry: u16 n (little endian; 0 = empty, never used: 2 B), then n
 * bytes: per track LEN DIV SWG SRC (a byte each), its 64-bit step map (8 B, little endian, bit i = step i set: any
 * field not 0), then 3 B per set step in step order: flags (on | acc << 1 | slide << 2 | tie << 3 | rat << 4), cond, nt.
 * n = 96 + 3 x the set steps (a used variation keeps its timing even with no step set). */
#define VAR_N 8u                                /* variations a slot: 1 (the record's steps) + 7 in the bank */
#define VAR_BYTES 3584u                         /* the object, +0xF00 of its sector stays erased */
#define VAR_MAGIC 0x31524156u                   /* "VAR1" */
#define VAR_TRK 12u                             /* an entry's bytes a track: timing 4, the step map 8 */
typedef struct {
    uint32_t magic;
    uint8_t slot, rsv;
    uint16_t used;                              /* bytes of d in use: variations 2..8 in order */
    uint8_t d[VAR_BYTES - 8u];
} var_bank_t;                                   /* stored length: 8 + used */
_Static_assert(sizeof(var_bank_t) == VAR_BYTES && VAR_BYTES <= 3584u, "variation bank: one sector, +0xF00 erased");
_Static_assert(NSTEP == 64 && NTRK == 8, "variation entries: a 64-bit step map, 8 tracks");
#define VAR_CAP (VAR_BYTES - 8u)

static int var_set(const step_t *s) { return (s->on | s->acc | s->slide | s->tie | s->rat | s->cond | s->nt) != 0; }

static void var_empty(var_bank_t *b, uint32_t slot)   /* 7 empty entries (14 B), the rest 0 */
{
    memset(b, 0, sizeof *b);
    b->magic = VAR_MAGIC;
    b->slot = (uint8_t)slot;
    b->used = 2u * (VAR_N - 1u);
}

static uint32_t var_need(const step_t *const st[NTRK])   /* the bytes an entry of these steps takes */
{
    uint32_t k, i, n = 2u + NTRK * VAR_TRK;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NSTEP; i++)
            n += var_set(&st[k][i]) ? 3u : 0u;
    return n;
}

static uint32_t var_n_at(const var_bank_t *b, uint32_t o) { return (uint32_t)b->d[o] | (uint32_t)b->d[o + 1u] << 8; }

/* entry v's offset in d (v 1..7); its size in *sz (2 + n). -1: the entries before it run past used (malformed) */
static int32_t var_find(const var_bank_t *b, uint32_t v, uint32_t *sz)
{
    uint32_t o = 0, i, used = b->used < VAR_CAP ? b->used : VAR_CAP, n;
    for (i = 1; i <= v; i++) {
        if (o + 2u > used)
            return -1;
        n = 2u + var_n_at(b, o);
        if (n > used - o)
            return -1;
        if (i == v) {
            *sz = n;
            return (int32_t)o;
        }
        o += n;
    }
    return -1;
}

/* an entry's bytes, written to d (cmp 0) or compared with d (cmp 1: 1 the same, 0 not) */
static int var_enc(uint8_t *d, int cmp, const step_t *const st[NTRK], const int16_t tm[NTRK][4], uint32_t need)
{
    uint32_t k, i, j, o = 0;
    uint8_t x[4];
#define VAR_PUT(v)                                                                                                      \
    do {                                                                                                                \
        uint8_t v_ = (uint8_t)(v);                                                                                      \
        if (cmp ? d[o] != v_ : (d[o] = v_, 0))                                                                          \
            return 0;                                                                                                   \
        o++;                                                                                                            \
    } while (0)
    VAR_PUT((need - 2u) & 0xFFu);
    VAR_PUT((need - 2u) >> 8);
    for (k = 0; k < NTRK; k++) {
        x[0] = (uint8_t)clamp(tm[k][0], 1, NSTEP);
        x[1] = (uint8_t)clamp(tm[k][1], 0, 9);
        x[2] = (uint8_t)clamp(tm[k][2], 0, 100);
        x[3] = (uint8_t)clamp(tm[k][3], 0, 3);
        for (j = 0; j < 4u; j++)
            VAR_PUT(x[j]);
        for (j = 0; j < 8u; j++) {
            uint32_t m = 0;
            for (i = 0; i < 8u; i++)
                m |= (uint32_t)var_set(&st[k][j * 8u + i]) << i;
            VAR_PUT(m);
        }
    }
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NSTEP; i++) {
            const step_t *s = &st[k][i];
            if (!var_set(s))
                continue;
            VAR_PUT(s->on | s->acc << 1 | s->slide << 2 | s->tie << 3 | s->rat << 4);
            VAR_PUT(s->cond);
            VAR_PUT(s->nt);
        }
#undef VAR_PUT
    return 1;
}

/* variation v + 1 (v 1..7) <- st / tm: 0 stored (bytes changed), 1 the same bytes (nothing changed), -1 VARIATIONS
 * FULL or v / the bank malformed (b untouched). The tail after the entry moves in place (no memmove) */
static int var_put(var_bank_t *b, uint32_t v, const step_t *const st[NTRK], const int16_t tm[NTRK][4])
{
    uint32_t need = var_need(st), sz, used = b->used, i;
    int32_t o;
    if (v < 1u || v >= VAR_N || used > VAR_CAP)
        return -1;
    o = var_find(b, v, &sz);
    if (o < 0 || used - sz + need > VAR_CAP)
        return -1;
    if (sz == need && var_enc(&b->d[o], 1, st, tm, need))
        return 1;
    if (need > sz)                                   /* the tail out (from its end) */
        for (i = used; i > (uint32_t)o + sz; i--)
            b->d[i - 1u + need - sz] = b->d[i - 1u];
    else if (need < sz)                              /* the tail in (from its start) */
        for (i = (uint32_t)o + sz; i < used; i++)
            b->d[i - (sz - need)] = b->d[i];
    b->used = (uint16_t)(used - sz + need);
    for (i = b->used; i < used; i++)                 /* (a shrunk bank's spare bytes 0, as var_empty's) */
        b->d[i] = 0;
    var_enc(&b->d[o], 0, st, tm, need);
    return 0;
}

/* variation v + 1 (v 1..7) -> st / tm: 1 written (unset steps all 0, values in their ranges); 0 empty (the steps
 * cleared, tm untouched); -1 malformed (as empty). Reads only b, within VAR_BYTES (works on a flash copy) */
static int var_get(const var_bank_t *b, uint32_t v, step_t *const st[NTRK], int16_t tm[NTRK][4])
{
    uint32_t k, i, sz, n, p, q;
    int32_t o = v >= 1u && v < VAR_N ? var_find(b, v, &sz) : -1;
    for (k = 0; k < NTRK; k++)
        memset(st[k], 0, NSTEP * sizeof(step_t));
    if (o < 0)
        return -1;
    if (sz == 2u)
        return 0;
    n = sz - 2u;
    p = (uint32_t)o + 2u;
    q = p + NTRK * VAR_TRK;                          /* the steps' bytes */
    if (n < NTRK * VAR_TRK)
        return -1;
    for (k = 0; k < NTRK; k++) {
        const uint8_t *t = &b->d[p + k * VAR_TRK];
        tm[k][0] = (int16_t)clamp(t[0], 1, NSTEP);
        tm[k][1] = (int16_t)clamp(t[1], 0, 9);
        tm[k][2] = (int16_t)clamp(t[2], 0, 100);
        tm[k][3] = (int16_t)clamp(t[3], 0, 3);
        for (i = 0; i < NSTEP; i++) {
            step_t *s = &st[k][i];
            uint32_t f;
            if (!((t[4u + i / 8u] >> (i & 7u)) & 1u))
                continue;
            if (q + 3u > (uint32_t)o + sz) {         /* more set steps than the entry holds */
                for (k = 0; k < NTRK; k++)
                    memset(st[k], 0, NSTEP * sizeof(step_t));
                return -1;
            }
            f = b->d[q];
            s->on = f & 1u;
            s->acc = (f >> 1) & 1u;
            s->slide = (f >> 2) & 1u;
            s->tie = (f >> 3) & 1u;
            s->rat = (f >> 4) & 3u;
            s->cond = (uint8_t)(b->d[q + 1u] < COND_MAX ? b->d[q + 1u] : COND_MAX);
            s->nt = (int8_t)clamp((int8_t)b->d[q + 2u], NT_MIN, NT_MAX);
            q += 3u;
        }
    }
    return 1;
}

static uint32_t var_pop8(uint32_t x)
{
    uint32_t n = 0;
    while (x) {
        n += x & 1u;
        x >>= 1;
    }
    return n;
}

/* a bank (from flash) is whole: magic, slot, len == 8 + used, used inside, exactly 7 entries ending at used, each
 * empty or 96 + 3 x its step maps' bits long */
static int var_valid(const var_bank_t *b, uint32_t slot, uint32_t len)
{
    uint32_t o = 0, v, n, k, j, bits;
    if (b->magic != VAR_MAGIC || b->slot != slot || b->used > VAR_CAP || len != 8u + b->used)
        return 0;
    for (v = 1; v < VAR_N; v++) {
        if (o + 2u > b->used)
            return 0;
        n = var_n_at(b, o);
        o += 2u;
        if (!n)
            continue;
        if (n < NTRK * VAR_TRK || n > b->used - o)
            return 0;
        for (k = 0, bits = 0; k < NTRK; k++)
            for (j = 0; j < 8u; j++)
                bits += var_pop8(b->d[o + k * VAR_TRK + 4u + j]);
        if (n != NTRK * VAR_TRK + 3u * bits)
            return 0;
        o += n;
    }
    return o == b->used;
}

static uint32_t var_mask(const var_bank_t *b)   /* bit v (1..7): variation v + 1 not empty */
{
    uint32_t v, sz, m = 0;
    for (v = 1; v < VAR_N; v++)
        if (var_find(b, v, &sz) >= 0 && sz > 2u)
            m |= 1u << v;
    return m;
}
