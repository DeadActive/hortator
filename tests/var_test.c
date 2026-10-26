/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* Variations (spec docs/superpowers/specs/2026-10-26-cue-variations-fill-copy-design.md): the packed bank of a slot's
 * variations 2..8 (varpack.c): round trip, sizes, VARIATIONS FULL, in-place replace, validation of hostile banks
 * (built with ASan + UBSan: a read past the bank aborts). */
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

static step_t S[NTRK][NSTEP], R[NTRK][NSTEP];
static int16_t TM[NTRK][4], TR[NTRK][4];
static var_bank_t B, C;
static const step_t *SP[NTRK];
static step_t *RP[NTRK];

static void ptrs(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        SP[k] = S[k];
        RP[k] = R[k];
    }
}
static void clear_steps(void) { memset(S, 0, sizeof S); }
static void rnd_steps(uint32_t per_mille)          /* random fields on per_mille / 1000 of the steps */
{
    uint32_t k, i;
    clear_steps();
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NSTEP; i++) {
            step_t *s = &S[k][i];
            if ((rnd() >> 8) % 1000u >= per_mille)
                continue;
            s->on = (rnd() >> 9) & 1u;
            s->acc = (rnd() >> 9) & 1u;
            s->slide = (rnd() >> 9) & 1u;
            s->tie = (rnd() >> 9) & 1u;
            s->rat = (rnd() >> 9) & 3u;
            s->cond = (uint8_t)((rnd() >> 8) % (COND_MAX + 1u));
            s->nt = (int8_t)(NT_MIN + (int32_t)((rnd() >> 8) % (uint32_t)(NT_MAX - NT_MIN + 1)));
            if (!s->on && !s->acc && !s->slide && !s->tie && !s->rat && !s->cond && !s->nt)
                s->on = 1;                         /* (a set step: some field not 0) */
        }
}
static void dense_steps(void)
{
    uint32_t k, i;
    clear_steps();
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NSTEP; i++) {
            S[k][i].on = 1;
            S[k][i].rat = (uint8_t)(i & 3u);
            S[k][i].nt = (int8_t)(i - 30);
        }
}
static void per_track_on(uint32_t n)               /* steps 0..n-1 on, every track */
{
    uint32_t k, i;
    clear_steps();
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < n; i++)
            S[k][i].on = 1;
}
static void timing(uint32_t seed)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        TM[k][0] = (int16_t)(k == 0 ? 1 : k == 1 ? 64 : 1 + (seed + k) % 64u);
        TM[k][1] = (int16_t)(k == 2 ? 9 : (seed + k) % 10u);
        TM[k][2] = (int16_t)(k == 3 ? 100 : (seed * 7u + k) % 101u);
        TM[k][3] = (int16_t)((seed + k) % 4u);
    }
}
static int steps_equal(void)                       /* every field (rsv 0 in both) */
{
    uint32_t k, i;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NSTEP; i++) {
            const step_t *a = &S[k][i], *b = &R[k][i];
            if (a->on != b->on || a->acc != b->acc || a->slide != b->slide || a->tie != b->tie || a->rat != b->rat ||
                a->cond != b->cond || a->nt != b->nt || b->rsv)
                return 0;
        }
    return 1;
}
static int all_clear(void)
{
    static const step_t z;
    uint32_t k, i;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NSTEP; i++)
            if (memcmp(&R[k][i], &z, sizeof z))
                return 0;
    return 1;
}

static void var_round_trip(void)
{
    uint32_t v, ok = 1;
    var_empty(&B, 2);
    rs = 5;
    rnd_steps(400);
    timing(3);
    ok &= var_put(&B, 3, SP, TM) == 0;
    memset(R, 0x55, sizeof R);
    memset(TR, 0, sizeof TR);
    ok &= var_get(&B, 3, RP, TR) == 1 && steps_equal() && !memcmp(TM, TR, sizeof TM);
    for (v = 1; v < VAR_N; v++)
        if (v != 3u) {
            memset(R, 0x55, sizeof R);
            ok &= var_get(&B, v, RP, TR) == 0 && all_clear();
        }
    ok &= var_valid(&B, 2, 8u + B.used) && var_mask(&B) == 1u << 3;
    check("variation: random steps (every field, NOTE -60 / 67, RATCH 4, SLIDE, TIE, ACC, PROB) and LEN 1 / 64, DIV "
          "9, SWG 100 round-trip; the others stay empty", (int)ok);
}
static void var_sizes(void)
{
    uint32_t ok;
    var_empty(&B, 0);
    ok = B.used == 14u && B.magic == VAR_MAGIC && B.slot == 0u && var_mask(&B) == 0u;
    per_track_on(16);
    ok &= var_need(SP) == 2u + 8u * 12u + 3u * 128u && var_need(SP) == 482u;
    dense_steps();
    ok &= var_need(SP) == 1634u;
    clear_steps();
    ok &= var_need(SP) == 98u;                     /* (a used variation with no step set: its timing kept) */
    check("variation: empty bank 14 B; 16 steps a track 482 B, all 512 1634 B, none 98 B (a used one)", (int)ok);
}
static void var_full(void)
{
    uint32_t ok;
    var_empty(&B, 1);
    dense_steps();
    timing(1);
    ok = var_put(&B, 1, SP, TM) == 0 && var_put(&B, 2, SP, TM) == 0;
    ok &= B.used == 14u - 4u + 2u * 1634u;
    C = B;
    ok &= var_put(&B, 3, SP, TM) == -1 && !memcmp(&B, &C, sizeof B);
    check("variation: two dense variations fit, a third is VARIATIONS FULL (-1), the bank untouched", (int)ok);
}
static void var_replace_shrink(void)
{
    static step_t keep[VAR_N][NTRK][NSTEP];
    static int16_t ktm[VAR_N][NTRK][4];
    uint32_t ok = 1, v, before;
    var_empty(&B, 3);
    rs = 9;
    for (v = 1; v < VAR_N; v++) {                   /* 2..7 sparse, 1 dense */
        if (v == 2u)
            dense_steps();
        else
            rnd_steps(60);
        timing(v);
        ok &= var_put(&B, v, SP, TM) == 0;
        memcpy(keep[v], S, sizeof S);
        memcpy(ktm[v], TM, sizeof TM);
    }
    before = B.used;
    rs = 10;
    rnd_steps(50);
    timing(20);
    ok &= var_put(&B, 2, SP, TM) == 0 && B.used == before - 1634u + var_need(SP);
    memcpy(keep[2], S, sizeof S);
    memcpy(ktm[2], TM, sizeof TM);
    for (v = 1; v < VAR_N; v++) {
        memcpy(S, keep[v], sizeof S);
        ok &= var_get(&B, v, RP, TR) == 1 && steps_equal() && !memcmp(TR, ktm[v], sizeof TR);
    }
    ok &= var_valid(&B, 3, 8u + B.used) && var_mask(&B) == 0xFEu;
    rs = 11;                                        /* and grow it back: the tail moves the other way */
    dense_steps();
    timing(21);
    ok &= var_put(&B, 2, SP, TM) == 0 && B.used == before;
    memcpy(keep[2], S, sizeof S);
    memcpy(ktm[2], TM, sizeof TM);
    for (v = 1; v < VAR_N; v++) {
        memcpy(S, keep[v], sizeof S);
        ok &= var_get(&B, v, RP, TR) == 1 && steps_equal() && !memcmp(TR, ktm[v], sizeof TR);
    }
    check("variation: a dense one replaced by a sparse one shrinks the bank in place (and back), the others intact",
          (int)ok);
}
static void var_same_bytes(void)
{
    uint32_t ok;
    var_empty(&B, 0);
    rs = 12;
    rnd_steps(300);
    timing(4);
    ok = var_put(&B, 5, SP, TM) == 0;
    C = B;
    ok &= var_put(&B, 5, SP, TM) == 1 && !memcmp(&B, &C, sizeof B);
    TM[2][1] ^= 1;
    ok &= var_put(&B, 5, SP, TM) == 0;
    check("variation: the same content again: 1 (nothing changed); a changed DIV: 0", (int)ok);
}
static void var_clamps(void)
{
    uint32_t ok;
    var_empty(&B, 0);
    clear_steps();
    timing(0);
    S[0][0].on = 1;
    ok = var_put(&B, 1, SP, TM) == 0;
    {                                               /* poke out-of-range bytes into the entry: LEN 200, DIV 30, SWG
                                                     * 250, SRC 9, cond 250 (layout: n, then track 0's LEN DIV SWG SRC,
                                                     * its bitmap, ..., then step 0's flags cond nt) */
        uint8_t *e = B.d + 2u;                       /* entry 1 is the first */
        e[0] = 200;
        e[1] = 30;
        e[2] = 250;
        e[3] = 9;
        e[8u * 12u + 1u] = 250;
    }
    ok &= var_get(&B, 1, RP, TR) == 1 && TR[0][0] == NSTEP && TR[0][1] == 9 && TR[0][2] == 100 && TR[0][3] == 3 &&
          R[0][0].cond == COND_MAX && R[0][0].on == 1;
    check("variation: decoded values clamped (LEN, DIV, SWG, SRC, PROB)", (int)ok);
}
static void var_valid_rejects(void)
{
    static uint8_t ff[sizeof(var_bank_t)];
    uint32_t ok = 1, len, v;
    var_empty(&B, 2);
    rs = 13;
    rnd_steps(200);
    timing(5);
    var_put(&B, 1, SP, TM);
    var_put(&B, 4, SP, TM);
    len = 8u + B.used;
    ok &= var_valid(&B, 2, len) == 1;
    C = B; C.magic ^= 1u;           ok &= !var_valid(&C, 2, len);
    C = B;                           ok &= !var_valid(&C, 1, len);
    C = B;                           ok &= !var_valid(&C, 2, len + 1u) && !var_valid(&C, 2, len - 1u);
    C = B; C.used = VAR_BYTES;       ok &= !var_valid(&C, 2, 8u + C.used);
    C = B; C.used -= 1u;             ok &= !var_valid(&C, 2, 8u + C.used);   /* the last entry past used */
    C = B; C.d[0] ^= 0x01u;          ok &= !var_valid(&C, 2, len);           /* entry 1's n off by one */
    C = B; C.d[2u + 2u + 4u] ^= 0x80u; ok &= !var_valid(&C, 2, len);         /* a bitmap bit: popcount mismatch */
    memset(ff, 0xFF, sizeof ff);
    ok &= !var_valid((const var_bank_t *)ff, 2, sizeof ff);
    memset(&C, 0, sizeof C);
    ok &= !var_valid(&C, 0, 8);
    {                                                /* var_get on malformed banks: bounded (ASan), -1 or 0 */
        const var_bank_t *bad[3];
        uint32_t i;
        C = B;
        C.used = VAR_BYTES;                           /* past the end */
        bad[0] = &C;
        bad[1] = (const var_bank_t *)ff;
        bad[2] = &B;
        for (i = 0; i < 3u; i++)
            for (v = 1; v < VAR_N; v++) {
                int r = var_get(bad[i], v, RP, TR);
                ok &= r >= -1 && r <= 1;
            }
        memset(ff, 0xFF, sizeof ff);
        ((var_bank_t *)ff)->used = 3000;             /* entries of n 0xFFFF: past the end */
        for (v = 1; v < VAR_N; v++)
            ok &= var_get((const var_bank_t *)ff, v, RP, TR) == -1 && all_clear();
    }
    check("variation: wrong magic, slot, length, used past the end, an entry past used, a popcount mismatch, all "
          "0xFF, all 0: rejected; var_get on them bounded (malformed: -1, the steps cleared)", (int)ok);
}

int main(void)
{
    ptrs();
    check("variation bank: 3584 B (a sector's +0xF00.. stays erased)", sizeof(var_bank_t) == 3584u && VAR_N == 8u);
    var_round_trip();
    var_sizes();
    var_full();
    var_replace_shrink();
    var_same_bytes();
    var_clamps();
    var_valid_rejects();
    printf(fails ? "var_test: %d FAILED\n" : "var_test: all passed\n", fails);
    return fails ? 1 : 0;
}
