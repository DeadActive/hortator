/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* Host test of the key backlight's dimming (firmware/src/led_dim.c, spec 2026-10-11-fixes §12) against the real
 * scan: hal/fm1_input.h's fm1_input_tick runs on host GPIO registers, led_dim_tick right after it as in main.c's
 * fm1_timer5_irq, and the LED lines each tick drives are decoded (row bit 1 PA9, 2 PA10, 3 PH6, 4 PH9). Checked:
 *   - OFF: the hook writes nothing; every column is lit with fm1_led bit for bit;
 *   - ON: per column, the full layer lit in every frame, the 25 % layer in 5 of 20, the 5 % layer in 1 of 20;
 *   - a column is lit with the value composed for it from its layers (the next-column write: never another
 *     column's, never half of a change); a layer change shows from the next frame on;
 *   - led_dim_compose: phase by phase;
 *   - fixes 2026-10-21: the dim layers' frame masks (item 9), the OCT breath (item 5). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#define fm1_ticks fm1_ticks_hw                     /* the HAL's TIMER4 read, unused: host_now instead */
#include "../firmware/hal/fm1_time.h"
#include "../firmware/hal/fm1_gpio.h"
#undef fm1_ticks
static uint32_t host_now;
static uint32_t fm1_ticks(void) { return host_now += 2400u; }   /* 100 us per read */
static volatile uint32_t host_gpio[8][8];          /* port, register / 4 */
#undef FM1_PR
#define FM1_PR(p, r) host_gpio[(p)][(r) >> 2]
#define FM1_INPUT_NOW() host_now
#include "../firmware/hal/fm1_input.h"
#include "../firmware/src/led_dim.c"

static int fails;
static int check(const char *what, int ok)
{
    printf("%-78s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
    return ok;
}

static uint32_t lines(void)                        /* the row bits the LED lines drive now */
{
    uint32_t a = host_gpio[FM1_PA][0], h = host_gpio[FM1_PH][0];
    return ((a >> 9) & 1u) << 1 | ((a >> 10) & 1u) << 2 | ((h >> 6) & 1u) << 3 | ((h >> 9) & 1u) << 4;
}

/* one TIMER5 tick as main.c runs it; returns the column lit until the next tick, *lit its row bits */
static uint32_t tick(uint32_t *lit)
{
    fm1_input_tick();
    led_dim_tick();
    *lit = lines();
    return fm1__tick_col;
}

static void reset(void)
{
    memset((void *)host_gpio, 0, sizeof host_gpio);
    memset(fm1_led, 0, sizeof fm1_led);
    memset((void *)led_lay, 0, sizeof led_lay);
    fm1__tick_col = 0;
    led_dim_f = 0;
    led_dim_on = 0;
}

int main(void)
{
    uint32_t c, i, f, lit, ok;
    static uint32_t cnt[FM1_NCOL][5];              /* per column: frames lit, per row bit 1..4 */

    /* led_dim_compose, phase by phase */
    ok = 1;
    for (f = 0; f < 20u; f++) {
        uint32_t v = led_dim_compose(0x040201u, f), want = 0x01u | (f % 4u == 0u ? 0x02u : 0u) | (f == 0u ? 0x04u : 0u);
        ok &= v == want;
        ok &= led_dim_compose(0x1E1E00u, f) == (f % 4u == 0u ? 0x1Eu : 0u);   /* a bit in both dim layers: 25 % */
        ok &= led_dim_compose(0x1Eu, f) == 0x1Eu;                     /* full only: every phase */
    }
    check("compose: full in every phase, 25 % in phases 0 4 8 12 16, 5 % in phase 0", ok);

    /* OFF: the hook does nothing; the scan lights fm1_led bit for bit */
    reset();
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led[c] = (uint8_t)((c * 7u + 3u) & 0x1Eu);
        led_lay[c] = 0x1E1E00u;                    /* (layers set, ignored while OFF) */
    }
    ok = 1;
    for (i = 0; i < 5u * FM1_NCOL * 20u; i++) {
        uint8_t before[FM1_NCOL];
        memcpy(before, fm1_led, sizeof before);
        c = tick(&lit);
        ok &= lit == fm1_led[c] && memcmp(before, fm1_led, sizeof before) == 0;
    }
    check("OFF: the hook writes nothing; every column lit with fm1_led exactly", ok);

    /* ON: duty per layer over 20 frames, every column, from any starting phase of the scan */
    for (i = 0; i < FM1_NCOL; i++) {
        uint32_t t, start;
        reset();
        for (start = 0; start < i; start++)        /* the scan somewhere mid-frame when the backlight turns on */
            tick(&lit);
        for (c = 0; c < FM1_NCOL; c++)             /* row bit 1 full, 2 at 25 %, 3 at 5 %, 4 off */
            led_lay[c] = 0x02u | 0x04u << 8 | 0x08u << 16;
        led_dim_on = 1;
        tick(&lit);                                /* (the column composed before ON: the next tick shows it) */
        memset(cnt, 0, sizeof cnt);
        ok = 1;
        for (t = 0; t < 5u * FM1_NCOL * 40u; t++) {
            uint32_t r;
            c = tick(&lit);
            ok &= lit == fm1_led[c];
            for (r = 1; r < 5u; r++)
                cnt[c][r] += (lit >> r) & 1u;
        }
        for (c = 0; c < FM1_NCOL; c++)
            ok &= cnt[c][1] == 200u && cnt[c][2] == 50u && cnt[c][3] == 10u && cnt[c][4] == 0u;   /* (200 frames) */
        if (!ok)
            break;
    }
    check("ON: full lit every frame, 25 % in 5 of 20, 5 % in 1 of 20, per column, any start", ok);

    /* the next-column write: each tick lights the value composed for that column from its own layers; a column's
     * layers changed while it is lit or just composed show from its next frame, never torn or one column over */
    reset();
    for (c = 0; c < FM1_NCOL; c++)
        led_lay[c] = (uint32_t)(1u << (1u + c % 4u));   /* full: a different row bit per column */
    led_dim_on = 1;
    ok = 1;
    {
        uint32_t want[FM1_NCOL], have = 0;         /* the value composed for each column at its tick (the model) */
        for (i = 0; i < 5u * FM1_NCOL * 10u; i++) {
            uint32_t n;
            c = tick(&lit);
            if (have & 1u << c)
                ok &= lit == want[c];
            n = c + 1u == FM1_NCOL ? 0u : c + 1u;  /* composed now, from its layers now, in the phase now */
            want[n] = led_dim_compose(led_lay[n], led_dim_f);
            have |= 1u << n;
            ok &= fm1_led[n] == want[n];
            if (i % 13u == 0u)                     /* the UI changes a column: the one lit, then the next */
                led_lay[(c + (i & 1u)) % FM1_NCOL] = (uint32_t)((i * 0x0B0D07u) & 0x1E1E1Eu);
        }
        ok &= have == (1u << FM1_NCOL) - 1u;
    }
    check("each tick lights its own column's composed value (written one tick ahead)", ok);
    {                                              /* one column changed: the next tick still shows the old value
                                                    * if it was composed already, the frame after the new one */
        uint32_t n, before;
        reset();
        led_dim_on = 1;
        for (c = 0; c < FM1_NCOL; c++)
            led_lay[c] = 0x02u;
        tick(&lit);
        tick(&lit);
        n = (fm1__tick_col + 1u) % FM1_NCOL;       /* composed already, lit next */
        before = fm1_led[n];
        led_lay[n] = 0x10u;
        c = tick(&lit);
        ok = c == n && lit == before;
        for (i = 0; i < FM1_NCOL; i++)
            c = tick(&lit);
        ok &= c == n && lit == 0x10u;
        check("a layer change: the column already composed keeps its value; the new one from its next frame", ok);
    }
    /* fixes 2026-10-21 item 9: the dim layers' frames from their masks (DIM HI: mid 6 of 20, glow 2 of 20) */
    reset();
    led_mid_ph = 0x22489u;
    led_glow_ph = 0x401u;
    for (c = 0; c < FM1_NCOL; c++)
        led_lay[c] = 0x02u | 0x04u << 8 | 0x08u << 16;
    led_dim_on = 1;
    tick(&lit);
    memset(cnt, 0, sizeof cnt);
    for (i = 0; i < 5u * FM1_NCOL * 40u; i++) {
        uint32_t r;
        c = tick(&lit);
        for (r = 1; r < 5u; r++)
            cnt[c][r] += (lit >> r) & 1u;
    }
    ok = 1;
    for (c = 0; c < FM1_NCOL; c++)
        ok &= cnt[c][1] == 200u && cnt[c][2] == 60u && cnt[c][3] == 20u && cnt[c][4] == 0u;
    check("item 9: the masks: DIM HI's mid in 6 of 20 frames, its glow in 2 of 20, per column", ok);
    led_mid_ph = 0x11111u;
    led_glow_ph = 0x1u;

    /* item 5: the breath (upstream Felucca's curve): dark at its ends, ~60 % at its peak, a smooth rise; while it is on
     * the breath layer lit in the frames its sigma-delta picks (spread: no long dark run near the peak), every column
     * alike; off: never lit, and the next one starts at its peak */
    {
        uint32_t x, up = 1, lv;
        ok = led_br_level(0) == 0u && led_br_level(LED_BR_FRAMES - 1u) < 64u;
        lv = led_br_level(LED_BR_FRAMES / 2u);
        ok &= lv > 38000u && lv < 40000u;             /* 154 / 256 of lit */
        for (x = 1; x <= LED_BR_FRAMES / 2u; x++)
            up &= led_br_level(x) >= led_br_level(x - 1u);
        check("item 5: the breath's level: 0 at its ends, ~60 % at the peak, rising to it", ok && up);
    }
    reset();
    for (c = 0; c < FM1_NCOL; c++)
        led_lay[c] = 0x10u << 24 | 0x02u;           /* row 4 breathing, row 1 full */
    led_dim_on = 1;
    led_br_on = 1;
    led_br_ph = 0;
    led_br_acc = 0;
    {
        uint32_t litf[FM1_NCOL] = {0}, gap = 0, maxgap = 0, peak = 0, full_ok = 1;
        tick(&lit);
        for (i = 0; i < LED_BR_FRAMES * FM1_NCOL; i++) {
            c = tick(&lit);
            full_ok &= (lit & 0x02u) != 0u;
            litf[c] += (lit >> 4) & 1u;
            if (c == 0u) {
                if (led_br_ph > 400u && led_br_ph < 624u) {   /* around the peak: lit at least every 3rd frame */
                    gap = (lit >> 4) & 1u ? 0u : gap + 1u;
                    maxgap = gap > maxgap ? gap : maxgap;
                    peak += (lit >> 4) & 1u;
                }
            }
        }
        ok = full_ok && maxgap <= 2u && peak > 100u;
        for (c = 1; c < FM1_NCOL; c++)
            ok &= litf[c] + 1u >= litf[0] && litf[c] <= litf[0] + 1u;
        ok &= litf[0] > LED_BR_FRAMES / 8u && litf[0] < LED_BR_FRAMES / 3u;   /* the average: ~20 % of the frames */
        check("item 5: breathing: lit in the frames the sigma-delta picks (around the peak never 3 dark in a row), "
              "every column alike, ~20 % on average; the full layer unaffected", ok);
        led_br_on = 0;
        tick(&lit);                                /* (the column composed before: the next tick) */
        ok = 1;
        for (i = 0; i < 4u * FM1_NCOL; i++) {
            c = tick(&lit);
            ok &= (lit & 0x10u) == 0u;
        }
        check("item 5: no LED breathing: the layer dark, the next breath from its peak", ok &&
              led_br_ph == LED_BR_FRAMES / 2u);
    }
    printf(fails ? "led_dim_test: %d FAILED\n" : "led_dim_test: all passed\n", fails);
    return fails != 0;
}
