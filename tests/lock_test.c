/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* Parameter locks (spec docs/superpowers/specs/2026-10-24-parameter-locks-design.md): the lock bank (lockbank.c:
 * sorted packed events, binary-search lookup, O(n) validation) and the locks' playback (motion.c). */
#include "drum_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static uint32_t rs = 1;
static uint32_t rnd(void) { return rs = rs * 1664525u + 1013904223u; }
static uint32_t rnd_param(void)                     /* a recordable knob */
{
    uint32_t id;
    do
        id = (rnd() >> 8) % P_COUNT;
    while (!motion_param(id));
    return id;
}
static uint32_t rnd_key(void) { return lk_key((rnd() >> 8) % NTRK, (rnd() >> 8) % NSTEP, rnd_param()); }
static uint32_t ord(const lk_bank_t *b, uint32_t i) { return (uint32_t)b->key[i] << 1 | (uint32_t)lk_is_lock(b, i); }
static int sorted(const lk_bank_t *b)
{
    uint32_t i;
    for (i = 1; i < b->count; i++)
        if (ord(b, i - 1u) >= ord(b, i))
            return 0;
    return 1;
}
static void empty(lk_bank_t *b, uint32_t slot)
{
    memset(b, 0, sizeof *b);
    lk_seal(b, slot);
}

static lk_bank_t B, C;

/* ---- the bank (lockbank.c) */
static void lk_size(void) { check("bank: 3208 B (<= 3584: the loader scan's +0xF00 stays erased)", sizeof(lk_bank_t) == 3208u); }

static void lk_put_keeps_order(void)
{
    uint32_t i, n = 0;
    int r, ok = 1;
    empty(&B, 0);
    rs = 11;
    for (i = 0; i < 500u; i++) {
        r = lk_put(&B, rnd_key(), (int32_t)((rnd() >> 8) % 128u), (int)((rnd() >> 9) & 1u));
        ok &= r == 0 || r == 1;
        n += r == 0;
    }
    check("bank: 500 random puts stay sorted by (key, kind), the count right", ok && sorted(&B) && B.count == n);
}
static void lk_put_replaces(void)
{
    uint32_t k = lk_key(2, 5, P_E1);
    int r;
    empty(&B, 0);
    lk_put(&B, k, 10, 1);
    r = lk_put(&B, k, 99, 1);
    check("bank: the same key and kind again replaces the value (returns 1, count kept)",
          r == 1 && B.count == 1u && B.val[lk_find(&B, k, 1)] == 99);
}
static void lk_motion_and_lock_coexist(void)
{
    uint32_t k = lk_key(2, 5, P_E1);
    int im, il;
    empty(&B, 0);
    lk_put(&B, k, 20, 1);
    lk_put(&B, k, 30, 0);
    im = lk_find(&B, k, 0);
    il = lk_find(&B, k, 1);
    check("bank: a motion event and a lock on one knob and step coexist, motion first",
          B.count == 2u && im == 0 && il == 1 && B.val[im] == 30 && B.val[il] == 20 && !lk_is_lock(&B, 0) &&
              lk_is_lock(&B, 1));
}
static void lk_full_at_1024(void)
{
    uint32_t i;
    int ok = 1;
    empty(&B, 1);
    for (i = 0; i < LK_MAX; i++)                    /* 1024 distinct places: track, step, the first 2 knobs, kind */
        ok &= lk_put(&B, lk_key(i >> 7, (i >> 1) & 63u, (i & 1u) ? P_E1 : P_E0), (int32_t)(i & 63u), 0) == 0;
    memcpy(&C, &B, sizeof B);
    ok &= lk_put(&B, lk_key(0, 0, P_E2), 5, 0) == -1 && !memcmp(&B, &C, sizeof B);
    ok &= lk_put(&B, lk_key(0, 0, P_E0), 7, 1) == -1 && !memcmp(&B, &C, sizeof B);
    ok &= lk_put(&B, lk_key(3, 9, P_E1), 44, 0) == 1 && B.count == LK_MAX;
    check("bank: 1024 events, the 1025th refused (bank unchanged), a replace still works", ok);
}
static void lk_seek_matches_scan(void)
{
    uint32_t t, s, i, lin;
    int ok = 1;
    empty(&B, 0);
    rs = 12;
    while (B.count < LK_MAX)
        lk_put(&B, rnd_key(), (int32_t)((rnd() >> 8) % 100u), (int)((rnd() >> 9) & 1u));
    for (t = 0; t < NTRK; t++)
        for (s = 0; s < NSTEP; s++) {
            for (lin = 0; lin < B.count && B.key[lin] < lk_key(t, s, 0); lin++)
                ;
            ok &= lk_seek(&B, t, s) == lin;
        }
    for (i = 0; i < B.count; i++)                    /* (a step's events: key >> 7 is track << 6 | step) */
        ok &= (uint32_t)B.key[i] >> 7 == (((uint32_t)B.key[i] >> 13) << 6 | (((uint32_t)B.key[i] >> 7) & 63u));
    check("bank: lk_seek finds every track / step's first event as a scan does (1024 events)", ok && sorted(&B));
}
static void lk_valid_rejects(void)
{
    int ok;
    uint32_t i;
    empty(&B, 2);
    rs = 13;
    for (i = 0; i < 300u; i++)
        lk_put(&B, rnd_key(), (int32_t)((rnd() >> 8) % 128u) - 64, (int)((rnd() >> 9) & 1u));
    lk_seal(&B, 2);
    ok = lk_valid(&B, 2);
#define BAD(what, change) do { memcpy(&C, &B, sizeof B); change; ok &= !lk_valid(&C, 2); if (lk_valid(&C, 2)) printf("  not caught: %s\n", what); } while (0)
    BAD("magic", C.magic ^= 1u);
    BAD("slot", C.slot = 1);
    ok &= !lk_valid(&B, 1);
    BAD("count 1025", C.count = 1025);
    BAD("unsorted", { uint16_t x = C.key[3]; C.key[3] = C.key[4]; C.key[4] = x; });
    BAD("duplicate", { C.key[4] = C.key[3]; if (lk_is_lock(&C, 3) != lk_is_lock(&C, 4)) C.lock[4 >> 3] ^= 1u << 4; });
    BAD("param", { C.count = 1; C.key[0] = (uint16_t)lk_key(3, 3, P_MODEL); });
    BAD("param past P_COUNT", { C.count = 1; C.key[0] = (uint16_t)lk_key(3, 3, 127); });
    BAD("value", C.val[5] = -65);
#undef BAD
    check("bank: lk_valid rejects magic, slot, count, order, duplicates, a knob not recordable, value -65", ok);
}
static void lk_clear_kinds(void)
{
    uint32_t n;
    empty(&B, 0);
    lk_put(&B, lk_key(1, 2, P_E1), 1, 0);
    lk_put(&B, lk_key(1, 3, P_E1), 2, 1);
    lk_put(&B, lk_key(1, 9, P_E2), 3, 0);
    lk_put(&B, lk_key(4, 3, P_E1), 4, 1);
    lk_put(&B, lk_key(4, 5, P_E1), 5, 0);
    n = lk_clear_track(&B, 1, LK_MOTION);
    check("bank: clearing a track's motion keeps its locks",
          n == 2u && B.count == 3u && lk_find(&B, lk_key(1, 3, P_E1), 1) >= 0 && sorted(&B));
    n = lk_clear_track(&B, 4, LK_BOTH);
    check("bank: clearing both kinds of a track keeps the other tracks'",
          n == 2u && B.count == 1u && lk_find(&B, lk_key(1, 3, P_E1), 1) == 0 && lk_count(&B, 1, LK_LOCK) == 1u &&
              lk_count(&B, 4, LK_BOTH) == 0u);
}
static void lk_clear_step_ids(void)
{
    uint32_t ids[MO_W] = {0}, n;
    empty(&B, 0);
    lk_put(&B, lk_key(2, 4, P_E1), 1, 1);
    lk_put(&B, lk_key(2, 4, P_E1), 2, 0);
    lk_put(&B, lk_key(2, 4, P_FCUT), 3, 1);
    lk_put(&B, lk_key(2, 5, P_E1), 4, 1);
    ids[P_E1 >> 5] |= 1u << (P_E1 & 31u);
    n = lk_clear_step(&B, 2, 4, ids);
    check("bank: clearing a step's locks on some knobs: only those locks, not motion, not other knobs or steps",
          n == 1u && B.count == 3u && lk_find(&B, lk_key(2, 4, P_E1), 1) < 0 && lk_find(&B, lk_key(2, 4, P_E1), 0) >= 0 &&
              lk_find(&B, lk_key(2, 4, P_FCUT), 1) >= 0 && lk_find(&B, lk_key(2, 5, P_E1), 1) >= 0);
    n = lk_clear_step(&B, 2, 4, 0);
    check("bank: clearing all of a step's locks keeps its motion",
          n == 1u && B.count == 2u && lk_find(&B, lk_key(2, 4, P_E1), 0) >= 0 && sorted(&B));
}
static void lk_step_mask_bank(void)
{
    empty(&B, 0);
    lk_put(&B, lk_key(3, 3, P_E1), 1, 1);
    lk_put(&B, lk_key(3, 18, P_FCUT), 1, 1);
    lk_put(&B, lk_key(3, 7, P_E1), 1, 0);            /* motion: no mark */
    lk_put(&B, lk_key(2, 5, P_E1), 1, 1);            /* another track */
    check("bank: a track's locked steps of a bank of 16 as a mask (motion events not)",
          lk_step_mask(&B, 3, 0) == 1u << 3 && lk_step_mask(&B, 3, 16) == 1u << 2 && lk_step_mask(&B, 3, 32) == 0u);
}
static void lk_from_motion_ok(void)
{
    static motion_store_t m;
    uint32_t i, ok = 1;
    memset(&m, 0, sizeof m);
    for (i = 0; i < MOTION_MAX; i++) {
        m.ev[i].trk = (uint8_t)(i & 7u);
        m.ev[i].step = (uint8_t)(63u - (i >> 3));
        m.ev[i].param = (uint8_t)P_E0 + (uint8_t)(i % 3u);
        m.ev[i].value = (int8_t)(i - 64);
    }
    m.count = MOTION_MAX;
    m.on = 0x5A;
    memset(&B, 0xEE, sizeof B);
    ok = lk_from_motion(&B, 3, &m) == 1 && B.count == MOTION_MAX && B.on == 0x5A && lk_valid(&B, 3);
    for (i = 0; ok && i < MOTION_MAX; i++) {
        int j = lk_find(&B, lk_key(m.ev[i].trk, m.ev[i].step, m.ev[i].param), 0);
        ok &= j >= 0 && B.val[j] == m.ev[i].value;
    }
    ok &= lk_count(&B, 0, LK_LOCK) == 0u;
    check("bank: an old project's 128 motion events convert (motion kind, PLAY bits kept)", ok);
}
static void lk_from_motion_broken(void)
{
    static motion_store_t m;
    int ok;
    memset(&m, 0, sizeof m);
    m.count = 2;
    m.on = 1;
    m.ev[0].trk = 9;
    m.ev[0].param = P_E1;
    m.ev[1].param = P_E1;
    ok = lk_from_motion(&B, 0, &m) == 0 && B.count == 0u && B.on == 0u && lk_valid(&B, 0);
    m.ev[0].trk = 1;
    m.ev[1].trk = 1;                                   /* the same place twice */
    ok &= lk_from_motion(&B, 0, &m) == 0 && B.count == 0u && lk_valid(&B, 0);
    m.count = 129;
    ok &= lk_from_motion(&B, 0, &m) == 0 && B.count == 0u;
    check("bank: a broken old motion block (track 9, a place twice, count 129) converts to an empty bank", ok);
}
static void lk_seal_deterministic(void)
{
    uint32_t i;
    memset(&B, 0x11, sizeof B);
    memset(&C, 0x77, sizeof C);
    B.count = C.count = 0;
    for (i = 0; i < 40u; i++) {
        uint32_t k = lk_key(i & 7u, i, P_E3);
        lk_put(&B, k, (int32_t)i, (int)(i & 1u));
        lk_put(&C, k, (int32_t)i, (int)(i & 1u));
    }
    B.on = C.on = 3;
    lk_seal(&B, 1);
    lk_seal(&C, 1);
    check("bank: sealed banks with the same events are byte-identical (garbage past the count zeroed)",
          !memcmp(&B, &C, sizeof B) && lk_valid(&B, 1));
}

/* ---- playback (motion.c): T1 a kick on every 1/4, E1's base 64 (as motion_test's fresh) */
#define STEP16 (22050u / 4u)
static void fresh(void)
{
    uint32_t k;
    host_init();
    song.g[G_BPM] = 120;
    for (k = 0; k < 16u; k += 4u)
        trk[0].step[k].on = 1;
    trk[0].p[P_E1] = 64;
    song.sel = 0;
}
static void play(void) { transport_req = 1; render_mix(0, 0, CTL); }
static void stop(void) { transport_req = 2; render_mix(0, 0, CTL); }
static void to_step(uint32_t s, uint32_t into)
{
    uint32_t guard = 0;
    while ((trk[0].seq_idx != s || trk[0].seq_pos < into) && guard++ < 200000u)
        render_mix(0, 0, CTL);
}
static void bar(void) { to_step(15, 100); to_step(0, 100); }

static void lock_snaps_back(void)
{
    int ok;
    fresh();
    lock_set(0, 4, P_E1, 100);
    play();
    to_step(4, 100);
    ok = trk[0].p[P_E1] == 100;
    to_step(5, 100);
    ok &= trk[0].p[P_E1] == 64;
    to_step(0, 100);
    ok &= trk[0].p[P_E1] == 64;
    to_step(4, 2000);
    ok &= trk[0].p[P_E1] == 100;
    stop();
    check("lock: step 4's value plays on step 4 only, the knob's own value again from step 5 (snap back)",
          ok && trk[0].p[P_E1] == 64);
}
static void lock_plays_motion_off(void)
{
    int ok;
    fresh();
    lock_set(0, 4, P_E1, 100);
    motion_add(0, 9, P_E2, 3);
    motion_set_play(0, 0);
    play();
    to_step(4, 100);
    ok = trk[0].p[P_E1] == 100 && !motion_on(0);
    stop();
    motion_clear(0);
    check("lock: plays with MOTION PLAY off; MOTION CLEAR keeps the locks",
          ok && lock_count(0) == 1u && motion_count(0) == 0u);
}
static void lock_over_motion(void)
{
    int ok;
    fresh();
    motion_add(0, 2, P_E1, 90);
    lock_set(0, 4, P_E1, 110);
    play();
    to_step(3, 100);
    ok = trk[0].p[P_E1] == 90;
    to_step(4, 100);
    ok &= trk[0].p[P_E1] == 110;
    to_step(5, 100);
    ok &= trk[0].p[P_E1] == 90;
    bar();
    ok &= trk[0].p[P_E1] == 64;
    stop();
    check("lock over motion: motion's 90 holds, the lock's 110 on its step, back to 90, the base at step 0", ok);
}
/* Review Focus 3: LFO 1 on E1, motion and a lock on E1 at step 4 */
static void lock_lfo_motion_same_knob(void)
{
    int ok, lo = 999, hi = -999;
    int32_t v;
    uint32_t b;
    fresh();
    trk[0].p[P_LFO1 + LF_DEST] = 2;                   /* (lfo.c lfo_dest_param: 2 = P_E1) */
    trk[0].p[P_LFO1 + LF_DEPTH] = 10;                 /* (+-20 on E1's 0..127) */
    trk[0].p[P_LFO1 + LF_TRIG] = LT_FREE;
    trk[0].p[P_LFO1 + LF_MODE] = LM_HZ;
    trk[0].p[P_LFO1 + LF_RATE] = 127;                 /* (fast: it swings within the step) */
    motion_add(0, 4, P_E1, 30);
    lock_set(0, 4, P_E1, 100);
    play();
    to_step(4, 100);
    ok = trk[0].p[P_E1] == 100;
    for (b = 0; b < 40u && trk[0].seq_idx == 4u; b++) {
        render_mix(0, 0, CTL);
        if (lfo_live(&trk[0], P_E1, &v)) {
            lo = v < lo ? (int)v : lo;
            hi = v > hi ? (int)v : hi;
        }
        ok &= trk[0].seq_idx != 4u || trk[0].p[P_E1] == 100;
    }
    ok &= lo < 100 && hi > 100 && lo >= 100 - 22 && hi <= 100 + 22;
    to_step(5, 100);
    ok &= trk[0].p[P_E1] == 30;
    stop();
    ok &= trk[0].p[P_E1] == 64;
    if (!ok)
        printf("     LFO on a lock: live %d..%d, E1 %d\n", lo, hi, trk[0].p[P_E1]);
    check("lock + motion + LFO on one knob: the LFO round the lock's 100 on its step, motion's 30 after, STOP the base", ok);
}
static void lock_dormant_off_step(void)
{
    int ok;
    fresh();
    lock_set(0, 4, P_E1, 100);
    trk[0].step[4].on = 0;
    play();
    to_step(4, 100);
    ok = trk[0].p[P_E1] == 64 && lock_count(0) == 1u;
    trk[0].step[4].on = 1;
    bar();
    to_step(4, 100);
    ok &= trk[0].p[P_E1] == 100 && lock_count(0) == 1u;
    stop();
    check("lock: dormant on a step turned off (kept), plays when it is on again", ok);
}
/* Review Focus 4: song rows of two slots, a lock in slot A's bank only */
static step_t ST[2][NTRK][NSTEP];
static lk_bank_t SB[2];
static void lock_song_row_change(void)
{
    static const uint32_t RS[2] = {0, 1};
    uint32_t s, k, r, b, ok = 1, seen[2] = {0, 0};
    fresh();
    memset(ST, 0, sizeof ST);
    memset(SB, 0, sizeof SB);
    for (s = 0; s < 2u; s++) {
        lk_seal(&SB[s], s);
        chain.src[s].m = &SB[s];
        for (k = 0; k < NTRK; k++) {
            chain.src[s].step[k] = ST[s][k];
            chain.src[s].timing[k][0] = 16;
            chain.src[s].timing[k][1] = 2;
            chain.src[s].model[k] = (uint8_t)trk[k].p[P_MODEL];
        }
        for (k = 0; k < 16u; k++)
            ST[s][0][k].on = 1;
    }
    chain.src[0].timing[0][0] = 5;                    /* slot A: T1 LEN 5, T2 (the ref) 16 */
    lk_put(&SB[0], lk_key(0, 4, P_E1), 100, 1);
    chain.cfg.count = 2;
    chain.cfg.loop = 1;
    for (r = 0; r < 2u; r++)
        chain.cfg.row[r] = (chain_row_t){(uint8_t)RS[r], 1};
    chain.armed = 1;
    play();
    for (b = 0; b < 3000u; b++) {                     /* two passes of both rows */
        render_mix(0, 0, CTL);
        if (chain.running && trk[0].seq_pos > 300u && trk[0].seq_pos < 5000u) {
            int want = chain.slot == 0u && trk[0].seq_idx == 4u ? 100 : 64;
            ok &= trk[0].p[P_E1] == want;
            seen[want == 100] += chain.slot == 1u || want == 100;
        }
    }
    stop();
    ok &= trk[0].p[P_E1] == 64 && mo.b.count == 0u && SB[1].count == 0u && seen[0] && seen[1];
    check("lock in a song: each row plays its slot's locks; none leaks into the next row; the working bank untouched", ok);
    memset(&chain, 0, sizeof chain);
}
static void lock_turn_while_held(void)
{
    int ok;
    fresh();
    lock_set(0, 4, P_E1, 100);
    play();
    to_step(4, 100);
    trk[0].p[P_E1] = 80;                              /* a turn (unarmed) during the locked step */
    motion_capture(&trk[0], P_E1);
    to_step(5, 100);
    ok = trk[0].p[P_E1] == 80;
    stop();
    check("lock: a turn during the locked step is the knob's value after it (80, not 64)", ok && trk[0].p[P_E1] == 80);
}
static void lock_stop_releases(void)
{
    fresh();
    lock_set(0, 4, P_E1, 100);
    play();
    to_step(4, 100);
    stop();
    check("lock: STOP during the locked step puts the knob's value back", trk[0].p[P_E1] == 64);
}
static void lock_model_change(void)
{
    int ok = 1, i;
    fresh();
    lock_set(0, 4, P_E1, 100);
    play();
    to_step(4, 100);
    fm1_irq_off();
    drum_set_model(&trk[0], (uint32_t)(trk[0].p[P_MODEL] + 1) % NMODELS);
    motion_rebase(0);
    fm1_irq_on();
    i = trk[0].p[P_E1];
    to_step(5, 100);
    ok = trk[0].p[P_E1] == i && i != 100;
    to_step(4, 100);                                   /* (the lock's model is gone: E events skipped? no, locks follow
                                                        * the track's own model: still the lock's value) */
    stop();
    check("lock: a model change during the locked step: the new model's own values, no 100 in its base", ok &&
          mo.base[0][P_E1] != 100);
}
static void lock_preset_rest(void)                  /* (what project.c preset_user_save stores: tests/preset_test.c) */
{
    int ok;
    fresh();
    motion_add(0, 2, P_E2, 5);                         /* (motion plays too: its base taken) */
    lock_set(0, 4, P_E1, 100);
    play();
    to_step(4, 100);
    ok = trk[0].p[P_E1] == 100 && motion_rest(0, P_E1) == 64;
    if (!ok)
        printf("     rest with motion: E1 %d rest %d base_ok %u\n", trk[0].p[P_E1], motion_rest(0, P_E1), mo.base_ok);
    motion_set_play(0, 0);                             /* (PLAY off) */
    stop();
    play();
    to_step(4, 100);
    ok &= trk[0].p[P_E1] == 100 && motion_rest(0, P_E1) == 64;
    if (!ok)
        printf("     rest without motion: E1 %d rest %d base_ok %u\n", trk[0].p[P_E1], motion_rest(0, P_E1), mo.base_ok);
    stop();
    check("lock: a knob's rest value during its locked step is its own (64), with or without motion", ok);
}

int main(void)
{
    lk_size();
    lk_put_keeps_order();
    lk_put_replaces();
    lk_motion_and_lock_coexist();
    lk_full_at_1024();
    lk_seek_matches_scan();
    lk_valid_rejects();
    lk_clear_kinds();
    lk_clear_step_ids();
    lk_step_mask_bank();
    lk_from_motion_ok();
    lk_from_motion_broken();
    lk_seal_deterministic();
    lock_snaps_back();
    lock_plays_motion_off();
    lock_over_motion();
    lock_lfo_motion_same_knob();
    lock_dormant_off_step();
    lock_song_row_change();
    lock_turn_while_held();
    lock_stop_releases();
    lock_model_change();
    lock_preset_rest();
    printf(fails ? "lock_test: %d FAILED\n" : "lock_test: all passed\n", fails);
    return fails ? 1 : 0;
}
