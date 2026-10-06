/* Host build of the drum firmware's UI: the DSP harness, a 240x240 framebuffer for the LCD, and the
 * input / IRQ HAL replaced by state the tests set. Screens are written as PPM (tests convert to PNG). */
#include "drum_host.h"
#ifndef FELUCCA_FLASH
#define FELUCCA_FLASH 0
#endif
#define FELUCCA_OTA 0
#define FELUCCA_CDC 0
/* ---- LCD: framebuffer (RGB565, native order) */
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
/* ---- input HAL (hal/fm1_input.h API, host state) */
#define FM1_NCOL 11u
/* FM1_KEYMAP: copy the table byte for byte from hal/fm1_input.h (the LED positions depend on it) */
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PA0: encoders */
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},          /* PA5 */
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},          /* PA6 */
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},          /* PA7 */
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},          /* PA8 */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PB7: encoder 6 */
};
static uint8_t fm1_led[FM1_NCOL];
static uint32_t host_btn_edges, host_note_edges, host_ticks;
static int32_t host_enc[7];
#define FM1_TICKS_PER_US 24u
static uint32_t fm1_ticks(void) { return host_ticks += 24u * 1000u; }   /* 1 ms per call */
static int32_t fm1_enc_take(uint32_t e) { int32_t s = host_enc[e % 7u]; host_enc[e % 7u] = 0; return s; }
static uint32_t fm1_input_edges(uint32_t *released) { uint32_t p = host_btn_edges; (void)released; host_btn_edges = 0; return p; }
static uint32_t fm1_input_note_edges(void) { uint32_t p = host_note_edges; host_note_edges = 0; return p; }
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void fm1_wdt_feed(void) {}
#define FM1_DEB_PRESS 2u               /* as hal/fm1_input.h (upstream 1.0) */
#define FM1_DEB_RELEASE 8u
static uint32_t host_scans;                          /* the boot's polled scans (tests: SEQ held = fm1_in.buttons) */
static void fm1_input_scan(void) { host_scans++; }
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N];
static uint32_t scope_w;
static struct { uint32_t stage, page, home, ui_frames; } felucca_dbg;
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/pages.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/icons.c"
#ifndef UI_NO_DRAW
#include "../firmware/src/ui_draw.c"
#else
static void ui_draw(void) {}
#endif
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"
#if FELUCCA_FLASH
#include "flash_host.h"                               /* tests/boot_test.c: the boot path with flash */
#endif
#ifdef UI_NO_PROJECT                                  /* until Task 4 rewrites project.c */
static void project_save(uint32_t s) { (void)s; }
static void project_load(uint32_t s) { (void)s; }
static int project_used(uint32_t s) { (void)s; return 0; }
static void settings_save(void) {}
#else
#include "../firmware/src/project.c"
#endif

static void ui_host_init(void)
{
    host_init();
    memset(&ui, 0, sizeof ui);
    memset(fb, 0, sizeof fb);
    panel = PANEL_DEFAULT;
    settings.magic = 0;
    settings_init();
    host_ticks += 1000u * 1000u * FM1_TICKS_PER_US;   /* 1 s idle: the first turn of a test is not accelerated */
    ui.home = 1;
    ui.force = 1;
}

static void press(uint32_t label)                   /* one press edge + held for this frame */
{
    host_btn_edges |= 1u << panel.btn[label];
    fm1_in.buttons |= 1u << panel.btn[label];
}
static void release_all(void) { fm1_in.buttons = 0; }
static void turn(uint32_t role, int32_t steps) { host_enc[panel.enc[role]] += steps * panel.dir[role]; }
static void keys(uint32_t mask) { host_note_edges |= mask & ~fm1_in.notes; fm1_in.notes = mask; }
static void ui_frame(void) { ui_input(); ui_leds(); ui_draw(); render_mix(0, 0, CTL); }

static void shot(const char *path)                  /* the framebuffer as binary PPM */
{
    FILE *f = fopen(path, "wb");
    uint32_t i;
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = fb[i];
        uint8_t px[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63u) << 2), (uint8_t)((c & 31u) << 3)};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}
