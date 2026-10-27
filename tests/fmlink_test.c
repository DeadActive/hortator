/* SPDX-License-Identifier: GPL-3.0-only
 * FM-Link: 2026 DEADACTIVE */
/* The FM-Link engine (firmware/src/fmlink.c; docs/superpowers/specs/2026-10-21-fm-link-design.md): two FM-1s simulated
 * against one real time R (24 MHz ticks). Each has its own crystal (a few tens of ppm off) and its own tick count (a
 * random offset), renders 32-sample blocks heard at its own ticks (544 a sample), runs its main loop every millisecond,
 * and talks to the other over a simulated Bluetooth link: packets go out at the next connection event (every 15 ms),
 * reach the other side up to 1.5 ms later, and 5 % are lost. The leader is A (CLK INT), the follower B (CLK LINK).
 * Checked: both start at the same moment, their beat positions stay within 2 ms (at the same real time), through a
 * BPM change, a lost link, a STOP, a join on the next bar; a linked follower's own PLAY does nothing. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../firmware/src/fmlink.c"

#define FS_SIM 44118u                               /* 24 MHz / 544 */
#define TPS 544u
#define BLK 32u
#define MS 24000u                                   /* ticks a millisecond */
static int fails;
#define CHECK(c, ...) do { if (c) { printf("ok    "); } else { printf("FAIL  "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

typedef struct {
    fml_t f;
    int64_t off;                                    /* its ticks at R = 0 */
    int32_t ppm;
    uint32_t bpm, next_block, playing, started_R;
    uint32_t pos_tick, pos_spb;                     /* the last block's end (its ticks) and samples a beat */
    uint32_t pos_beats;                             /* its beats there (Q16) */
    uint32_t ext_spb;                               /* a leader on an external clock (CLK USB / TRS): its samples a
                                                     * beat (the clock's tempo, a fraction of a BPM off a whole one) */
    uint64_t ext_pulse_R;                           /* .. the clock's first pulse after its Start: it moves from then */
    uint32_t ext_acc;                               /* .. samples x 24 towards its next pulse */
} unit_t;
static unit_t U[2];
static uint64_t R;                                  /* real time, ticks */
static uint32_t rnd = 12345u;
static uint32_t rand32(void) { rnd = rnd * 1103515245u + 12345u; return rnd >> 8; }
static uint32_t clk(const unit_t *u) { return (uint32_t)(u->off + (int64_t)R + (int64_t)R * u->ppm / 1000000); }
static uint32_t spb_of(uint32_t bpm) { return FS_SIM * 60u / bpm; }

/* the link: packets queued with when they arrive; down = all lost; loss = per mille lost */
typedef struct { uint64_t at; int to; uint8_t len, b[20]; } pkt_t;
static pkt_t Q[4096];
static uint32_t qn, loss = 50, down;
static uint32_t phase;                              /* the connection events' phase */
static void send(int from, const uint8_t *b, uint32_t len)
{
    uint64_t ev = ((R - phase) / (15u * MS) + 1u) * (15u * MS) + phase;   /* the next connection event */
    if (down || rand32() % 1000u < loss || qn >= 4096u)
        return;
    Q[qn].at = ev + rand32() % (MS + MS / 2u);
    Q[qn].to = !from;
    Q[qn].len = (uint8_t)len;
    memcpy(Q[qn].b, b, len);
    qn++;
}
static void deliver(void)
{
    uint32_t i, k = 0;
    for (i = 0; i < qn; i++) {
        if (Q[i].at <= R)
            fml_rx(&U[Q[i].to].f, Q[i].b, Q[i].len, clk(&U[Q[i].to]));
        else
            Q[k++] = Q[i];
    }
    qn = k;
}
static uint32_t ui_req[2];                          /* the units' PLAY (1) / STOP (2) presses, taken by the next block */
static void step(void)                              /* R moves on by 1/24 ms; blocks, main loops, the link */
{
    int u;
    R += MS / 24u;
    deliver();
    for (u = 0; u < 2; u++) {
        unit_t *x = &U[u];
        uint8_t b[20];
        uint32_t len, seq_n, req, now = clk(x);
        if ((int32_t)(now + 256u * TPS - x->next_block) >= 0) {   /* rendered 256 samples before it is heard */
            uint32_t spb = x->ext_spb ? x->ext_spb : spb_of(u && x->f.l_valid ? x->f.l_bpm : x->bpm);
            req = fml_block(&x->f, x->next_block, BLK, u && x->f.l_valid ? x->f.l_bpm : x->bpm, spb, (int)x->playing,
                            ui_req[u], &seq_n);
            ui_req[u] = 0;
            if (x->ext_spb) {                       /* the clock's pulses (24 a beat) in this block */
                uint32_t k = 0;
                if (x->playing && R >= x->ext_pulse_R) {
                    x->ext_acc += BLK * 24u;
                    k = x->ext_acc / spb;
                    x->ext_acc %= spb;
                }
                fml_adv(&x->f, k, x->playing && R < x->ext_pulse_R);
            }
            if (req == 1u || req == 3u) {
                x->playing = 1;
                x->started_R = (uint32_t)(R / MS);
            } else if (req == 2u) {
                x->playing = 0;
            }
            x->pos_tick = x->ext_spb ? x->f.snap_t : x->next_block + BLK * TPS;
            x->pos_beats = x->ext_spb ? x->f.snap_beats : x->f.beats;   /* (a clock's: on its pulses' grid) */
            x->pos_spb = spb;
            x->next_block += BLK * TPS;
        }
        if ((R / (MS / 24u)) % 24u == (uint32_t)u * 12u)   /* each main loop every millisecond */
            while ((len = fml_poll(&x->f, now, b)) != 0u)
                send(u, b, len);
    }
}
static void run_ms(uint32_t ms) { uint64_t end = R + (uint64_t)ms * MS; while (R < end) step(); }
/* the unit's beat position at real time R, from its last block (Q16 beats) */
static int64_t pos_now(const unit_t *x)
{
    int32_t dt = (int32_t)(clk(x) - x->pos_tick);
    return (int64_t)x->pos_beats + (x->playing ? (int64_t)dt * 65536 / ((int64_t)x->pos_spb * TPS) : 0);
}
/* the largest |A - B| in ms over the next `ms` real milliseconds, sampled every 10 ms */
static double max_err_ms(uint32_t ms)
{
    double worst = 0;
    uint32_t k;
    for (k = 0; k < ms / 10u; k++) {
        double d;
        run_ms(10);
        d = (double)(pos_now(&U[0]) - pos_now(&U[1])) / 65536.0 * 60000.0 / U[0].bpm;
        if (d < 0)
            d = -d;
        if (d > worst)
            worst = d;
    }
    return worst;
}
static void power_on(int32_t ppm_a, int32_t ppm_b, uint32_t seed)
{
    int u;
    memset(U, 0, sizeof U);
    memset(Q, 0, sizeof Q);
    qn = 0;
    down = 0;
    R = 0;
    rnd = seed;
    phase = rand32() % (15u * MS);
    U[0].off = (int64_t)(rand32() % 1000000u) * 1000;
    U[1].off = (int64_t)(rand32() % 1000000u) * 1000;
    U[0].ppm = ppm_a;
    U[1].ppm = ppm_b;
    fml_init(&U[0].f, FML_LEADER);
    fml_init(&U[1].f, FML_FOLLOWER);
    for (u = 0; u < 2; u++) {
        U[u].bpm = 120;
        U[u].next_block = clk(&U[u]) + 256u * TPS;
    }
    U[1].bpm = 97;                                  /* the follower's own tempo before the link sets it */
}

int main(void)
{
    double e;
    uint32_t s;
    /* a fresh start, in time, for three minutes */
    power_on(30, -25, 7);
    run_ms(3000);                                   /* hello, pings: the offset known */
    CHECK(fml_linked(&U[0].f, clk(&U[0])) && fml_linked(&U[1].f, clk(&U[1])), "both see an FM-Link peer");
    CHECK(U[1].f.off_ok, "the follower knows the offset");
    ui_req[0] = 1;                                  /* PLAY on the leader */
    run_ms(5);
    CHECK(!U[0].playing, "the leader does not start at once with a follower linked (it announces the start)");
    run_ms(300);
    CHECK(U[0].playing && U[1].playing, "both play");
    CHECK(U[1].f.l_bpm == 120u, "the follower plays at the leader's BPM");
    e = max_err_ms(500);
    printf("      start: %.3f ms apart in the first half second\n", e);
    CHECK(e <= 1.0, "a fresh start: both within 1 ms from the start");
    e = max_err_ms(180000);
    printf("      three minutes: %.3f ms at most\n", e);
    CHECK(e <= 2.0, "three minutes at 120 BPM with crystals 55 ppm apart: within 2 ms");
    /* a BPM change on the leader */
    U[0].bpm = 133;
    run_ms(1000);
    CHECK(U[1].f.l_bpm == 133u, "the follower takes the leader's new BPM");
    e = max_err_ms(30000);
    printf("      after 120 -> 133 BPM: %.3f ms at most (from 1 s after the change)\n", e);
    CHECK(e <= 2.0, "a BPM change: within 2 ms again a second later");
    /* the link lost for five seconds, then back */
    down = 1;
    e = max_err_ms(5000);
    printf("      the link lost for 5 s: %.3f ms at most\n", e);
    CHECK(U[1].playing && e <= 2.0, "the link lost: the follower plays on, still within 2 ms");
    down = 0;
    e = max_err_ms(20000);
    CHECK(e <= 2.0, "the link back: within 2 ms");
    /* the follower's own PLAY / STOP: nothing while linked */
    ui_req[1] = 2;
    run_ms(50);
    CHECK(U[1].playing && U[1].f.msg_follow, "a linked follower's own STOP does nothing (FOLLOWING LINK)");
    /* STOP on the leader */
    ui_req[0] = 2;
    run_ms(200);
    CHECK(!U[0].playing && !U[1].playing, "STOP on the leader stops both");
    /* a join on the next bar: the leader plays alone, then the link comes */
    power_on(-40, 20, 99);
    down = 1;
    run_ms(500);
    ui_req[0] = 1;
    run_ms(4321);                                   /* the leader alone: no peer, it starts at once */
    CHECK(U[0].playing && !U[1].playing, "the leader alone starts at once");
    down = 0;
    run_ms(6000);
    CHECK(U[1].playing, "the follower joins a playing leader");
    s = (uint32_t)(U[1].f.joined_beats >> 16);
    CHECK(U[1].f.joined_beats % (4u << 16) == 0u && s > 0u, "it joined on a bar: beat %u", s);
    e = max_err_ms(20000);
    printf("      joined: %.3f ms at most\n", e);
    CHECK(e <= 2.0, "joined: within 2 ms");
    /* heavier loss */
    loss = 300;
    e = max_err_ms(30000);
    printf("      30 %% of the packets lost: %.3f ms at most\n", e);
    CHECK(e <= 2.0, "30 %% of the packets lost: within 2 ms");
    loss = 50;
    /* the leader on an external clock (CLK USB / TRS): its transport and tempo are the clock's */
    power_on(30, -25, 21);
    U[0].f.ext = 1;
    U[0].bpm = 123;
    U[0].ext_spb = FS_SIM * 600u / 1234u;           /* 123.4 BPM */
    run_ms(3000);
    ui_req[0] = 1;                                  /* its own PLAY: the clock's business (a Start, then pulses) */
    run_ms(5);
    CHECK(U[0].playing, "an external clock's leader: its own PLAY is not held back");
    U[0].playing = 0;
    run_ms(2000);
    U[0].playing = 1;                               /* the clock's Start */
    U[0].ext_pulse_R = R + 10u * MS;                /* .. its first pulse 10 ms on */
    U[0].ext_acc = U[0].ext_spb;
    run_ms(5);
    CHECK(!U[1].playing, "the follower waits for the clock's first pulse");
    run_ms(150);
    CHECK(U[1].playing && U[1].f.joined_beats == 0u, "the follower starts from beat 1 with the clock's leader");
    s = U[1].started_R - (uint32_t)(U[0].ext_pulse_R / MS);
    printf("      the follower started %u ms after the clock's first pulse\n", s);
    CHECK(s <= 60u, "the follower's first step within 60 ms of the leader's");
    e = max_err_ms(500);
    printf("      after the start: %.3f ms apart\n", e);
    CHECK(e <= 2.0, "caught up: within 2 ms 150 ms after the start");
    e = max_err_ms(180000);
    printf("      three minutes at 123.4 BPM: %.3f ms at most\n", e);
    CHECK(e <= 2.0, "three minutes at a clock's 123.4 BPM: within 2 ms");
    U[0].bpm = 100;                                 /* the clock's tempo changes */
    U[0].ext_spb = FS_SIM * 60u / 100u;
    run_ms(1000);
    e = max_err_ms(20000);
    printf("      after 123.4 -> 100 BPM: %.3f ms at most\n", e);
    CHECK(U[1].playing && U[1].f.l_bpm == 100u && e <= 2.0, "a clock's tempo change: followed, within 2 ms");
    U[0].playing = 0;                               /* the clock's Stop */
    run_ms(200);
    CHECK(!U[1].playing, "the clock's Stop stops the follower");
    printf(fails ? "fmlink: %d FAILED\n" : "fmlink: all checks passed\n", fails);
    return fails != 0;
}
