/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* Dimmed LEDs (the key backlight, spec 2026-10-11-fixes §12). The LEDs are only on / off and multiplexed by the
 * TIMER5 scan (hal/fm1_input.h fm1_input_tick, 10 kHz: one column per tick, column n lit with fm1_led[n] until the
 * next tick). Dimming is PWM over 20 scan frames (22 ms): a 30 % LED is lit in 6 frames of 20, spread out (every 3rd or 4th:
 * ~270 Hz pulses), a 5 % one in 1 (45 Hz).
 * The UI (ui_input.c ui_leds) sets three layers per column in one word, written as one 32-bit store so the scan
 * never sees half a column: bits 0..7 full, 8..15 30 %, 16..23 5 % (packed row bits, as fm1_led). While led_dim_on
 * is 0 the UI writes fm1_led itself, as it always did, and the hook does nothing.
 * led_dim_tick runs in fm1_timer5_irq right after fm1_input_tick (main.c), also nested in the audio ISR: it composes
 * the column the NEXT tick lights (fm1__tick_col + 1, the column fm1_input_tick just lit being fm1__tick_col), so a
 * column's value is written a whole tick before it is used and never changes while it is lit. A handful of
 * instructions: it runs 10000 times a second. Included before main.c (ui_input.c; frozen-base-8's felucca.c). */
static volatile uint32_t led_lay[FM1_NCOL];
static volatile uint8_t led_dim_on;
static uint8_t led_dim_f;                          /* the frame's phase 0..19 (advanced at column 0) */

/* the bits lit in phase f of 20: full always, 30 % in phases 0 3 7 10 13 17, 5 % in phase 0 */
static inline uint32_t led_dim_compose(uint32_t lay, uint32_t f)
{
    uint32_t v = lay & (0xFFu | (0x22489u >> f & 1u) * 0xFF00u | (f == 0u) * 0xFF0000u);
    return (v | v >> 8 | v >> 16) & 0xFFu;
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
    }
    fm1_led[n] = (uint8_t)led_dim_compose(led_lay[n], f);
}
