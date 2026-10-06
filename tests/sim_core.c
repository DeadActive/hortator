/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The web simulator's core (docs/SIMULATOR.md): the firmware (tests/sim_host.h) behind a small API the page
 * drives. One instance = one power-on. The browser builds this file as wasm (tools/build_sim.sh) and runs it
 * in an AudioWorklet; tests/sim_test.c includes it. sim_render is the clock: it runs the audio ISR once per
 * half buffer and the main loop's UI frame every 15 ms of audio, as the device does. */
#include "sim_host.h"

#define SIM_MAX_FRAMES 1024u
#define SIM_FRAME_SAMPLES (FS * 15u / 1000u)             /* main.c: ~60 UI frames a second at most */

static float sim_out[2u * SIM_MAX_FRAMES];
static uint32_t sim_pos = HALF_FRAMES;                   /* frames of the current half already handed out */
static uint64_t sim_next_frame;
static int32_t sim_knob = 512 * 16;                       /* main.c's MASTER filter state */
static uint32_t sim_master_raw = 800;

static void sim_demo_install(void);

void sim_frame(void)                                     /* main.c's loop body, minus the hardware */
{
    int32_t a = (int32_t)sim_master_raw;
    uint32_t k10;
    song.batt_raw = 600;                                  /* a full battery */
    sim_knob += (a * 16 - sim_knob) / 8;
    k10 = (uint32_t)(sim_knob / 16);
    song.master_q12 = (k10 * k10) >> 8;
    felucca_dbg.ui_frames++;
    felucca_dbg.page = ui.page;
    felucca_dbg.home = ui.home;
    ui_input();
    ui_leds();
    ui_draw();
}

void sim_flash_reset(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    memset(sim_dirty, 0, sizeof sim_dirty);
}

void sim_init(int fresh)                                 /* main.c's boot order */
{
    if (fresh)
        sim_flash_reset();
    persist_boot();
    settings_init();
    memset(&felucca_dbg, 0, sizeof felucca_dbg);
    felucca_dbg.magic = DBG_MAGIC;
    felucca_dbg.boots = 1;
    panel_init();
    drum_boot_init();
    audio_init();
    sim_knob = (int32_t)sim_master_raw * 16;
    sim_frame();                                          /* settles MASTER and draws the first screen */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.force = 1;
    if (fresh)
        sim_demo_install();
    else if (project_used(0))                             /* a returning visitor: their slot 1, not the device's empty
                                                           * power-on pattern (PLAY should groove on a public demo) */
        project_load(0);
    sim_next_frame = sim_samples + SIM_FRAME_SAMPLES;
}

uint32_t sim_render(uint32_t frames)                     /* interleaved float stereo into sim_audio(); UI frames run */
{
    uint32_t i, ui_frames = 0;
    if (frames > SIM_MAX_FRAMES)
        frames = SIM_MAX_FRAMES;
    for (i = 0; i < frames; i++) {
        const int32_t *h;
        if (sim_pos == HALF_FRAMES) {
            if (sim_samples >= sim_next_frame) {          /* between halves, as the main loop runs between ISRs */
                sim_frame();
                ui_frames++;
                sim_next_frame += SIM_FRAME_SAMPLES;
            }
            sim_half ^= 1u;
            fm1_alnk0_irq();
            sim_samples += HALF_FRAMES;
            sim_ms_update();
            sim_pos = 0;
        }
        h = &abuf[sim_half * HALF_WORDS];
        sim_out[2u * i] = (float)h[2u * sim_pos] * (1.0f / 8388608.0f);   /* 24-bit */
        sim_out[2u * i + 1u] = (float)h[2u * sim_pos + 1u] * (1.0f / 8388608.0f);
        sim_pos++;
    }
    return ui_frames;
}

void sim_btn(uint32_t label, int down)                    /* label = B_FX .. B_OCTUP (panel.c) */
{
    uint32_t bit;
    if (label >= NB)
        return;
    bit = 1u << panel.btn[label];
    if (down) {
        fm1_in.pressed |= bit & ~fm1_in.buttons;
        fm1_in.buttons |= bit;
    } else
        fm1_in.buttons &= ~bit;
}

void sim_key(uint32_t n, int down)                        /* note key 0 (F3) .. 26 (G5) */
{
    uint32_t bit;
    if (n >= 27u)
        return;
    bit = 1u << n;
    if (down) {
        fm1_in.notes_pressed |= bit & ~fm1_in.notes;
        fm1_in.notes |= bit;
    } else
        fm1_in.notes &= ~bit;
}

void sim_enc(uint32_t role, int32_t steps)                /* role = EN_SELECT .. EN_K4, + = clockwise */
{
    if (role < NE)
        sim_enc_acc[panel.enc[role]] += steps * panel.dir[role];
}

void sim_master(uint32_t v) { sim_master_raw = v > 1023u ? 1023u : v; }

static int sim_led_lit(uint32_t id)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id)
                return (fm1_led[p] >> r) & 1u;
    return 0;
}

uint32_t sim_leds(void)                                   /* bit b = button label b (B_FX .. B_OCTUP) */
{
    uint32_t b, m = 0;
    for (b = 0; b < NB; b++)
        m |= (uint32_t)sim_led_lit(panel.btn[b]) << b;
    return m;
}

uint32_t sim_key_leds(void)                               /* bit n = note key n (0 = F3 .. 26 = G5) */
{
    uint32_t n, m = 0;
    for (n = 0; n < 27u; n++)
        m |= (uint32_t)sim_led_lit(14u + n) << n;
    return m;
}

uint32_t sim_playing(void) { return song.playing != 0u; }
uint16_t *sim_fb(void) { return fb; }
float *sim_audio(void) { return sim_out; }
uint8_t *sim_flash(void) { return hflash; }
uint32_t *sim_flash_dirty(void) { return sim_dirty; }
void sim_flash_clean(void) { memset(sim_dirty, 0, sizeof sim_dirty); }

#include "sim_demo.c"
