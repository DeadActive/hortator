/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* Web simulator host (docs/SIMULATOR.md): the drum firmware's sources, felucca.c's list in its order, with the
 * HAL replaced by state the simulator drives: the audio sample count as the clock, input levels and latched
 * edges, the LCD as a framebuffer, the audio ISR run on demand, the flash in RAM with dirty sectors.
 * Built with cc (tests/sim_test.c) and emcc (the browser). Left out: lcd.c (fb), main.c (sim_core.c runs its
 * boot and loop body), ota.c, console.c. tests/sim_test.c checks this list against felucca.c. */
#include <stdint.h>
#include <stddef.h>
#define __attribute__(x)
#define memset felucca_memset
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "felucca_tables.h"
#include "../firmware/src/libc.c"
#undef memset
#undef memcpy
#undef memcmp
#include <string.h>

/* ---- clock: TIMER4 (24 MHz) from the audio sample count; time stops when the audio does */
#define FM1_TICKS_PER_US 24u
static uint64_t sim_samples;
static volatile uint32_t fm1_ms;                       /* main.c's millisecond count (also usb_app.c's tentative one) */
static uint32_t sim_ms_skew;                            /* the ms a busy-wait burned (fm1_wdt_feed) */
static uint32_t fm1_ticks(void) { return (uint32_t)(sim_samples * 24000000u / 44100u); }
static void sim_ms_update(void) { fm1_ms = (uint32_t)(sim_samples * 1000u / 44100u) + sim_ms_skew; }
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
/* only firmware busy-waits feed the watchdog outside the main loop (panel_setup waits for input): no input or
 * audio can arrive inside one here, so each feed moves the clock a second on and the wait times out */
static void fm1_wdt_feed(void)
{
    sim_ms_skew += 1000u;
    sim_ms_update();
}

/* ---- input (hal/fm1_input.h API): levels set by the page, press edges latched until the UI takes them */
#define FM1_NCOL 11u
#include "sim_keymap.h"                                /* FM1_KEYMAP, from hal/fm1_input.h (tools/build_sim.sh) */
#define FM1_DEB_PRESS 2u
#define FM1_DEB_RELEASE 8u
static struct { volatile uint32_t notes, buttons, pressed, notes_pressed; } fm1_in;
static int32_t sim_enc_acc[7];
static uint8_t fm1_led[FM1_NCOL];
static int32_t fm1_enc_take(uint32_t e) { int32_t s = sim_enc_acc[e % 7u]; sim_enc_acc[e % 7u] = 0; return s; }
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t p = fm1_in.pressed;
    if (released)
        *released = 0;
    fm1_in.pressed = 0;
    return p;
}
static uint32_t fm1_input_note_edges(void) { uint32_t p = fm1_in.notes_pressed; fm1_in.notes_pressed = 0; return p; }
static void fm1_input_scan(void) {}
static void fm1_led_key(uint32_t id, int on)          /* as hal/fm1_input.h */
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                if (on)
                    fm1_led[p] |= (uint8_t)(1u << r);
                else
                    fm1_led[p] &= (uint8_t)~(1u << r);
            }
}

/* ---- LCD: the 240x240 RGB565 framebuffer (as tests/ui_host.h) */
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_init(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fb[(y + j) * 240u + x + i] = (uint16_t)((px[j * w + i] >> 8) | (px[j * w + i] << 8));   /* canvas is byte-swapped */
}

/* ---- audio (hal/fm1_audio.h API for audio.c): sim_core.c calls the ISR once per half, the halves alternate */
#define FM1_AUDIO_HALF 0x80u
static uint32_t sim_half;
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return sim_half; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_stop(void) {}
static void fm1_audio_init(int32_t *buf, uint32_t half_words, void (*isr)(void), uint32_t prio)
{
    (void)buf, (void)half_words, (void)isr, (void)prio;
}
void isr_alnk0(void) {}

/* ---- flash: 1 MiB in RAM (erased = 0xFF); erase / program mark their 4 KB sectors dirty for the page */
#define FELUCCA_FLASH 1
#define FELUCCA_OTA 0
#define FELUCCA_CDC 0
#define SIM_FLASH_SIZE 0x100000u
static uint8_t hflash[SIM_FLASH_SIZE];
static uint32_t sim_dirty[SIM_FLASH_SIZE / 4096u / 32u];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
static void sim_mark(uint32_t off, uint32_t n)
{
    uint32_t s;
    for (s = off / 4096u; s <= (off + n - 1u) / 4096u && s < SIM_FLASH_SIZE / 4096u; s++)
        sim_dirty[s / 32u] |= 1u << (s % 32u);
}

#include "../firmware/src/gfx.c"
#include "../firmware/src/core.h"
#include "../firmware/src/dsp.c"
#include "../firmware/src/comp.c"
#include "../firmware/src/eng_sample.c"
#include "../firmware/src/dmodels.c"
#include "../firmware/src/params.c"
#include "../firmware/src/reson.c"
#include "../firmware/src/lfo.c"
#include "../firmware/src/drum_core.c"
#include "../firmware/src/slicer.c"
#include "../firmware/src/fx.c"
#include "../firmware/src/usb_app.c"
#include "../firmware/src/midi_uart.c"
#include "../firmware/src/grids.c"
#include "../firmware/src/seq.c"
#include "../firmware/src/bench.c"
#include "../firmware/src/audio.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/pages.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/icons.c"
#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"

/* the flash glue felucca.c gives storage.c and persist_boot (as tests/flash_host.h, plus the dirty sectors) */
static uint8_t flash_ok;
#define FL_FAR(fn) (fn)
static uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* the FM-1's flash id */
static void fl_plain_window_init(void) {}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off > SIM_FLASH_SIZE || n > SIM_FLASH_SIZE - off)
        return -1;
    memcpy(dst, hflash + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (off > SIM_FLASH_SIZE - 4096u)
        return -8;
    memset(hflash + off, 0xFF, 4096);
    sim_mark(off, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (off > SIM_FLASH_SIZE || n > SIM_FLASH_SIZE - off)
        return -8;
    for (i = 0; i < n; i++)
        hflash[off + i] &= s[i];
    if (n)
        sim_mark(off, n);
    return 0;
}
#include "../firmware/src/storage.c"
#include "../firmware/src/project.c"
