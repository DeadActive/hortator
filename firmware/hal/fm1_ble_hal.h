/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE (Hortator, from fm1-lsdj 548ce73) */
/* App-only HAL for the BLE build (FELUCCA_BLE=1); the loader never includes it, and no frozen HAL file changes for it.
 * Two parts, picked by whether fm1_irq.h is already in (FM1_CRASH_MAGIC):
 *  - the core's unit (firmware/src/core_ble_api.c, after fm1_irq.h and fm1_guard.h):
 *      fm1_guard_stack_window(lo, hi)   move the EMU stack-limit window to [lo, hi] (the BLE task stacks)
 *      fm1_irq_unmask(n) / fm1_irq_is_masked(n)
 *  - the BLE unit (firmware/src/ble/ble.c), self-contained (no fm1_irq.h: it defines fm1_fault_c, one per image):
 *   fm1_clk_hz(which)      READ-ONLY decode of the clock tree: sys / hsb / lsb / sfc in Hz (registers and decode
 *                          from the AC79 SDK: asm/WL82.h, clock_hw.h, cpu.a clock.c clk_early_init)
 *   fm1_irq_off_save() / fm1_irq_restore(s)    nesting-safe cli / sti for the BT libraries' local_irq_* */
#pragma once
#include <stdint.h>

#ifdef FM1_CRASH_MAGIC
/* as fm1_guard_enable's stack part, with the window given: both SSP and USP */
static inline void fm1_guard_stack_window(uint32_t lo, uint32_t hi)
{
    fm1__dbg_unlock();
    FM1_EMU_CON &= ~(1u << 3);
    FM1_EMU_SSP_L = lo;
    FM1_EMU_SSP_H = hi;
    FM1_EMU_USP_L = lo;
    FM1_EMU_USP_H = hi;
    FM1_EMU_CON |= 1u << 3;
    fm1__dbg_lock();
}
static inline void fm1_irq_unmask(uint32_t n) { FM1_ICFG(n) |= 1u << ((n & 7u) * 4u); }       /* (prio kept) */
static inline uint32_t fm1_irq_is_masked(uint32_t n) { return !(FM1_ICFG(n) & (1u << ((n & 7u) * 4u))); }
#else

#define FM1B_SYS_DIV   (*(volatile const uint32_t *)0x10008u)
#define FM1B_CLK_CON2  (*(volatile const uint32_t *)0x10014u)
#define FM1B_PLL_CON0  (*(volatile const uint32_t *)0x119A0u)
#define FM1B_PLL2_CON0 (*(volatile const uint32_t *)0x119A8u)
#define FM1B_PLL2_CON1 (*(volatile const uint32_t *)0x119ACu)
#define FM1B_HUSB_CON0 (*(volatile const uint32_t *)0x16A00u)
#define FM1B_SFC_BAUD  (*(volatile const uint32_t *)0x40204u)

/* the 24 MHz tick counter (TIMER4, fm1_time.h), always inlined: the RAM-run delays during the radio calibration
 * must not call into XIP */
static inline __attribute__((always_inline)) uint32_t fm1_ble_ticks(void) { return *(volatile uint32_t *)0x10804u; }

/* one nop, for the counted delay loops (the compiler must not drop or merge them) */
static inline __attribute__((always_inline)) void fm1_ble_nop(void) { __asm__ volatile("nop"); }

enum { FM1_CLK_SYS, FM1_CLK_HSB, FM1_CLK_LSB, FM1_CLK_SFC };

static uint32_t fm1_clk_hz(uint32_t which)
{
    static const uint16_t TAP[5] = {192, 137, 320, 480, 107};
    static const uint8_t D1[4] = {1, 3, 5, 7}, D2[4] = {1, 2, 4, 8};
    uint32_t c2 = FM1B_CLK_CON2, div = FM1B_SYS_DIV, src = c2 & 15u, sys_khz, hsb, lsb;
    uint32_t osc_khz = ((FM1B_PLL_CON0 >> 26) & 3u) == 2u ? 40000u : 24000u;
    if (src < 5u) {
        sys_khz = TAP[src] * 1000u / D1[(c2 >> 4) & 3u] / D2[(c2 >> 6) & 3u];
    } else if (src < 8u) {
        uint32_t pll2 = osc_khz / (((FM1B_PLL2_CON0 >> 2) & 31u) + 2u) * ((FM1B_PLL2_CON1 & 0xFFFu) + 2u);
        sys_khz = 2u * pll2 / (4u - (src - 5u));
    } else if (src < 10u) {
        sys_khz = ((((FM1B_HUSB_CON0 >> 18) & 0x3FFu) + 2u) >> (src == 9u)) * 1000u;
    } else {
        sys_khz = 480000u;
    }
    hsb = sys_khz / (((div >> 16) & 3u) + 1u);
    lsb = hsb / (((div >> 8) & 7u) + 1u);
    switch (which) {
    case FM1_CLK_SYS: return sys_khz * 1000u;
    case FM1_CLK_HSB: return hsb * 1000u;
    case FM1_CLK_LSB: return lsb * 1000u;
    default: return hsb / ((FM1B_SFC_BAUD & 0xFFu) + 1u) * 1000u;
    }
}

static volatile uint32_t fm1_ble_irq_depth;
static inline __attribute__((always_inline)) void fm1_ble_irq_off(void)
{
    __asm__ volatile("cli" ::: "memory");
    fm1_ble_irq_depth++;
}
static inline __attribute__((always_inline)) void fm1_ble_irq_on(void)
{
    if (fm1_ble_irq_depth && !--fm1_ble_irq_depth)
        __asm__ volatile("csync\n\tsti" ::: "memory");
}
#endif
