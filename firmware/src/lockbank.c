/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The lock bank (spec docs/superpowers/specs/2026-10-24-parameter-locks-design.md): a project's recorded MOTION
 * events (hold until the knob's next event) and its parameter locks (that step only, then snap back), up to 1024 of
 * them, kept sorted by (key, kind) so the audio ISR finds a step's events by a binary search (motion.c motion_step).
 * An event: a 16-bit key (track 3 bits, step 6 bits, knob 7 bits), an int8 value (every recordable range: -64..127)
 * and a kind bit (the lock bitmap: set = a lock). One knob on one step may hold both kinds (motion first).
 * Pure data: no IRQ handling here, the callers (motion.c, project.c) guard. Each project slot's bank is its own flash
 * object (storage.c OBJ_LOCKS0 + slot): 3208 B, so a sector's +0xF00 (the update loader's erase scan) stays erased. */
#define LK_MAX 1024u
#define LK_MAGIC 0x31424B4Cu                   /* "LKB1" */
enum { LK_MOTION = 1, LK_LOCK = 2, LK_BOTH = 3 };   /* kinds */
typedef struct {
    uint32_t magic;                             /* LK_MAGIC (lk_seal) */
    uint8_t slot, on;                           /* owning project slot 0..3; MOTION PLAY bits */
    uint16_t count;
    uint16_t key[LK_MAX];                       /* trk << 13 | step << 7 | param, ascending by (key, lock) */
    int8_t val[LK_MAX];
    uint8_t lock[LK_MAX / 8u];                  /* bit i: event i is a lock (snap back); else motion (hold) */
} lk_bank_t;
_Static_assert(sizeof(lk_bank_t) == 3208u && sizeof(lk_bank_t) <= 3584u, "lock bank: 3208 B, +0xF00 erased");
_Static_assert(NTRK == 8 && NSTEP == 64 && P_COUNT <= 128, "lock key: track 3 bits, step 6, knob 7");

/* the motion store as FDR8..FDRE projects saved it (128 events shared by the 8 tracks): read by the converter */
#define MOTION_MAX 128u
typedef struct { uint8_t trk, step, param; int8_t value; } motion_event_t;   /* every recordable range: -64..127 */
typedef struct { uint8_t count, on, rsv[2]; motion_event_t ev[MOTION_MAX]; } motion_store_t;   /* on: PLAY bits */
typedef char motion_store_size[sizeof(motion_store_t) == 516u ? 1 : -1];
#define MO_W ((P_COUNT + 31u) / 32u)            /* words of a track's knob-id bits */
typedef char motion_ids_fit[P_COUNT <= 96 && MO_W == 3u ? 1 : -1];

static int motion_param(uint32_t id);           /* motion.c: the recordable knobs */

static uint32_t lk_key(uint32_t trk, uint32_t step, uint32_t param) { return trk << 13 | step << 7 | param; }
static int lk_is_lock(const lk_bank_t *b, uint32_t i) { return (int)((b->lock[i >> 3] >> (i & 7u)) & 1u); }
static void lk_set_lock(lk_bank_t *b, uint32_t i, int on)
{
    if (on)
        b->lock[i >> 3] |= (uint8_t)(1u << (i & 7u));
    else
        b->lock[i >> 3] &= (uint8_t)~(1u << (i & 7u));
}
static uint32_t lk_ord(const lk_bank_t *b, uint32_t i) { return (uint32_t)b->key[i] << 1 | (uint32_t)lk_is_lock(b, i); }
static uint32_t lk_lower(const lk_bank_t *b, uint32_t ord)   /* the first i with lk_ord(i) >= ord (11 probes at 1024) */
{
    uint32_t lo = 0, hi = b->count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) >> 1;
        if (lk_ord(b, mid) < ord)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return lo;
}
/* the first event of track trk at step >= step (count: none) */
static uint32_t lk_seek(const lk_bank_t *b, uint32_t trk, uint32_t step) { return lk_lower(b, lk_key(trk, step, 0) << 1); }
static int lk_find(const lk_bank_t *b, uint32_t key, int lock)   /* the event's index, -1 none */
{
    uint32_t ord = key << 1 | (lock ? 1u : 0u), i = lk_lower(b, ord);
    return i < b->count && lk_ord(b, i) == ord ? (int)i : -1;
}
/* key's event of that kind = v: 0 inserted, 1 replaced, -1 the bank full (unchanged) */
static int lk_put(lk_bank_t *b, uint32_t key, int32_t v, int lock)
{
    uint32_t ord = key << 1 | (lock ? 1u : 0u), i = lk_lower(b, ord), j;
    if (i < b->count && lk_ord(b, i) == ord) {
        b->val[i] = (int8_t)v;
        return 1;
    }
    if (b->count >= LK_MAX)
        return -1;
    memmove(&b->key[i + 1u], &b->key[i], (b->count - i) * sizeof b->key[0]);
    memmove(&b->val[i + 1u], &b->val[i], (b->count - i) * sizeof b->val[0]);
    for (j = b->count; j > i; j--)
        lk_set_lock(b, j, lk_is_lock(b, j - 1u));
    b->key[i] = (uint16_t)key;
    b->val[i] = (int8_t)v;
    lk_set_lock(b, i, lock);
    b->count++;
    return 0;
}
static void lk_del(lk_bank_t *b, uint32_t i)    /* event i out (the tail stays zero) */
{
    uint32_t j;
    if (i >= b->count)
        return;
    memmove(&b->key[i], &b->key[i + 1u], (b->count - i - 1u) * sizeof b->key[0]);
    memmove(&b->val[i], &b->val[i + 1u], (b->count - i - 1u) * sizeof b->val[0]);
    for (j = i; j + 1u < b->count; j++)
        lk_set_lock(b, j, lk_is_lock(b, j + 1u));
    b->count--;
    b->key[b->count] = 0;
    b->val[b->count] = 0;
    lk_set_lock(b, b->count, 0);
}
/* events [a, z) whose drop(i) is set out, the rest moved down in order; returns how many went */
static uint32_t lk_compact(lk_bank_t *b, uint32_t a, uint32_t z, uint32_t trk, uint32_t step, uint32_t kinds,
                           const uint32_t *ids)
{
    uint32_t i, n = a, gone;
    for (i = a; i < b->count; i++) {
        uint32_t k = b->key[i], id = k & 127u;
        int lock = lk_is_lock(b, i);
        int drop = i < z && (k >> 13) == trk && (step >= NSTEP || ((k >> 7) & 63u) == step) &&
                   ((kinds >> (lock ? 1 : 0)) & 1u) && (!ids || ((ids[id >> 5] >> (id & 31u)) & 1u));
        if (drop)
            continue;
        b->key[n] = b->key[i];
        b->val[n] = b->val[i];
        lk_set_lock(b, n, lock);
        n++;
    }
    gone = b->count - n;
    for (i = n; i < b->count; i++) {
        b->key[i] = 0;
        b->val[i] = 0;
        lk_set_lock(b, i, 0);
    }
    b->count = (uint16_t)n;
    return gone;
}
/* track trk's events of the kinds out; returns how many */
static uint32_t lk_clear_track(lk_bank_t *b, uint32_t trk, uint32_t kinds)
{
    return lk_compact(b, lk_seek(b, trk, 0), lk_seek(b, trk + 1u, 0), trk, NSTEP, kinds, 0);
}
/* track trk's locks at step out (ids: a MO_W-word mask of knob ids, 0 every knob); motion events stay */
static uint32_t lk_clear_step(lk_bank_t *b, uint32_t trk, uint32_t step, const uint32_t *ids)
{
    return lk_compact(b, lk_seek(b, trk, step), lk_seek(b, trk, step + 1u), trk, step, LK_LOCK, ids);
}
static uint32_t lk_count(const lk_bank_t *b, uint32_t trk, uint32_t kinds)   /* track trk's events of the kinds */
{
    uint32_t i, z = lk_seek(b, trk + 1u, 0), n = 0;
    for (i = lk_seek(b, trk, 0); i < z; i++)
        n += (kinds >> (lk_is_lock(b, i) ? 1 : 0)) & 1u;
    return n;
}
/* bit j: step first + j (j < 16) of track trk has a lock */
static uint32_t lk_step_mask(const lk_bank_t *b, uint32_t trk, uint32_t first)
{
    uint32_t i, z, m = 0;
    if (first >= NSTEP)
        return 0;
    z = lk_seek(b, trk, first + 16u > NSTEP ? NSTEP : first + 16u);
    for (i = lk_seek(b, trk, first); i < z; i++)
        if (lk_is_lock(b, i))
            m |= 1u << (((uint32_t)(b->key[i] >> 7) & 63u) - first);
    return m;
}
/* a bank as stored for slot: its magic and slot, the count in range, every event a recordable knob with a value in
 * range, strictly ascending by (key, kind) (so no place twice). One pass */
static int lk_valid(const lk_bank_t *b, uint32_t slot)
{
    uint32_t i;
    if (b->magic != LK_MAGIC || b->slot != slot || b->count > LK_MAX)
        return 0;
    for (i = 0; i < b->count; i++) {
        uint32_t id = b->key[i] & 127u;
        if (id >= P_COUNT || !motion_param(id) || b->val[i] < -64 || (i && lk_ord(b, i - 1u) >= lk_ord(b, i)))
            return 0;
    }
    return 1;
}
/* ready to store: the magic, the slot, everything past the count zero (the same events: the same bytes) */
static void lk_seal(lk_bank_t *b, uint32_t slot)
{
    uint32_t i;
    if (b->count > LK_MAX)
        b->count = 0;
    b->magic = LK_MAGIC;
    b->slot = (uint8_t)slot;
    for (i = b->count; i < LK_MAX; i++) {
        b->key[i] = 0;
        b->val[i] = 0;
        lk_set_lock(b, i, 0);
    }
}
/* an old project's motion (FDR8..FDRE: 128 events) -> b as motion events, its PLAY bits kept: 1; a broken block (a
 * count past 128, a place out of range or twice, a knob not recordable, a value below -64): 0, b empty */
static int lk_from_motion(lk_bank_t *b, uint32_t slot, const motion_store_t *m)
{
    uint32_t i;
    memset(b, 0, sizeof *b);
    lk_seal(b, slot);
    if (m->count > MOTION_MAX)
        return 0;
    for (i = 0; i < m->count; i++) {
        const motion_event_t *e = &m->ev[i];
        if (e->trk >= NTRK || e->step >= NSTEP || e->param >= P_COUNT || !motion_param(e->param) || e->value < -64 ||
            lk_put(b, lk_key(e->trk, e->step, e->param), e->value, 0) != 0) {
            memset(b, 0, sizeof *b);
            lk_seal(b, slot);
            return 0;
        }
    }
    b->on = m->on;
    return 1;
}
