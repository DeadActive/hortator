/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE
 * The breath's curve after upstream Felucca (hal/fm1_input.h fm1__breath_frame, #119):
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Dimmed and breathing LEDs (the key backlight, spec 2026-10-11-fixes §12; MENU BACKLIGHT's levels and the OCT
 * breath after upstream Felucca). The LEDs are only on / off and multiplexed by the TIMER5 scan (hal/fm1_input.h
 * fm1_input_tick, 10 kHz: one column per tick, column n lit with fm1_led[n] until the next tick). Dimming is PWM over
 * 20 scan frames (22 ms): an LED in a dim layer is lit in the frames of 20 its phase mask names (led_mid_ph,
 * led_glow_ph: bit f = frame f; at power-on LED_PH_25, frames 0 4 8 12 16, 25 % ~230 Hz pulses, and 0x1, frame 0,
 * 5 % 45 Hz: BACKLIGHT DIM LO, the old ON).
 * The UI (ui_input.c ui_leds) sets four layers per column in one word, written as one 32-bit store so the scan
 * never sees half a column: bits 0..7 full, 8..15 mid, 16..23 glow, 24..31 breath (packed row bits, as fm1_led).
 * While led_dim_on is 0 the UI writes fm1_led itself, as it always did, and the hook does nothing.
 * The breath (upstream Felucca's, #119): the breath layer's LEDs fade dark .. ~60 % of lit and back, all in step, in
 * LED_BR_FRAMES frames (~1.13 s); the frame's level B (of lit, /65536) is a smoothstep of a triangle, squared (the
 * eye), x the peak; a first order sigma-delta picks the frames lit (one accumulator: the lit frames as evenly spread
 * as can be). A breath starting from none starts at its peak (shown at once). Once a frame (at column 0): a few
 * multiplies, no division.
 * led_dim_tick runs in fm1_timer5_irq right after fm1_input_tick (main.c), also nested in the audio ISR: it composes
 * the column the NEXT tick lights (fm1__tick_col + 1, the column fm1_input_tick just lit being fm1__tick_col), so a
 * column's value is written a whole tick before it is used and never changes while it is lit. A handful of
 * instructions a tick: it runs 10000 times a second. Included before main.c (ui_input.c; frozen-base-8's felucca.c). */
#define LED_BR_FRAMES 1024u                        /* a breath: frames dark -> the peak -> dark (a power of 2) */
#define LED_BR_PK 154u                             /* its peak, /256 of lit: ~60 % (upstream FM1_LED_BREATH_PK) */
static volatile uint32_t led_lay[FM1_NCOL];
static volatile uint8_t led_dim_on;
#define LED_PH_25 (0x1111u | 1u << 16)             /* frames 0 4 8 12 16 of 20: 25 % */
static volatile uint32_t led_mid_ph = LED_PH_25, led_glow_ph = 0x1u;   /* the dim layers' frames of 20 (the UI's) */
static volatile uint8_t led_br_on;                 /* the UI: an LED in the breath layer */
static uint8_t led_dim_f;                          /* the frame's phase 0..19 (advanced at column 0) */
static uint8_t led_br_lit;                         /* this frame: the breath layer lit */
static uint16_t led_br_ph = LED_BR_FRAMES / 2u;    /* the breath: frames into it (0 dark, 512 the peak) */
static uint32_t led_br_acc;                        /* its sigma-delta, /65536 */

/* the breath's level at phase x (0..LED_BR_FRAMES - 1), of lit /65536 (upstream's curve: no division) */
static inline uint32_t led_br_level(uint32_t x)
{
    x >>= 1;                                       /* 0..511 */
    x = x & 256u ? 511u - x : x;                   /* a triangle 0..255 */
    x = x * x * (768u - 2u * x) >> 16;             /* smoothstep, 0..255 */
    return x * x * LED_BR_PK >> 8;                 /* squared (the eye), x the peak */
}

/* the bits lit in phase f of 20: full always, mid / glow in their masks' frames, breath in its lit frames */
static inline uint32_t led_dim_compose(uint32_t lay, uint32_t f)
{
    uint32_t v = lay & (0xFFu | ((led_mid_ph >> f) & 1u) * 0xFF00u | ((led_glow_ph >> f) & 1u) * 0xFF0000u |
                        (uint32_t)led_br_lit * 0xFF000000u);
    return (v | v >> 8 | v >> 16 | v >> 24) & 0xFFu;
}

static inline void led_br_frame(void)             /* (a new frame, before column 0 is composed) */
{
    uint32_t a;
    if (!led_br_on) {
        led_br_ph = LED_BR_FRAMES / 2u;            /* none: the next one starts at its peak */
        led_br_lit = 0;
        return;
    }
    a = led_br_acc + led_br_level(led_br_ph);
    led_br_ph = (uint16_t)((led_br_ph + 1u) & (LED_BR_FRAMES - 1u));
    led_br_lit = a >= 65536u;
    led_br_acc = a & 0xFFFFu;
}

static inline void led_dim_tick(void)
{
    uint32_t n, f;
    if (!led_dim_on)
        return;
    n = fm1__tick_col + 1u;
    f = led_dim_f;
    if (n == FM1_NCOL) {
        n = 0;
        f = f == 19u ? 0u : f + 1u;
        led_dim_f = (uint8_t)f;
        led_br_frame();
    }
    fm1_led[n] = (uint8_t)led_dim_compose(led_lay[n], f);
}
