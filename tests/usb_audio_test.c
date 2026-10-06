/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* USB audio input in the audio ISR (audio.c's hooks, usb_app.c's ring) and USB LEVEL (fx.c, upstream 1.0.2 #42):
 * the recording gets the block the DAC gets (MASTER) or the full-level block while the DAC follows MASTER (FIXED);
 * nothing is fed while no program on the computer reads. */
#define FELUCCA_UAC 1
#include "drum_host.h"
#define FM1_AUDIO_HALF 0x80u
#define FM1_TICKS_PER_US 24u
static uint8_t fm1_audio_pending(void) { return 0; }
static uint32_t fm1_ticks(void) { return 0; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return 0; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *b, uint32_t n, void (*f)(void), int pri) { (void)b; (void)n; (void)f; (void)pri; }
void isr_alnk0(void) {}
#include "../firmware/src/audio.c"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static void stream(uint32_t open)                    /* the host's recorder: alt 1 and reading, or closed */
{
    usb.config = 1;
    uac.alt = (uint8_t)open;
    uac.flowing = (uint8_t)open;
}

/* one block of an 808 kick (deterministic) through audio_block, the stream open and primed; fills out, returns the
 * ring index the block was written at */
static int32_t out[2u * CTL];
static uint32_t kick_block(uint32_t fixed, uint32_t master)
{
    uint32_t w0;
    host_init();
    ua_w = ua_r = 0;
    stream(1);
    fx_usb_fixed = (uint8_t)fixed;
    song.master_q12 = master;
    drum_set_model(&trk[0], DM_K808);
    drum_hit(&trk[0], 127);
    uac_render_start();                              /* (re)start: silence ahead */
    w0 = ua_w;
    audio_block(out, CTL);
    return w0;
}

static int32_t ring_l(uint32_t w) { return (int16_t)(ua_ring[w & (UA_N - 1u)] & 0xFFFFu); }
static int32_t ring_r(uint32_t w) { return (int16_t)(ua_ring[w & (UA_N - 1u)] >> 16); }

int main(void)
{
    static int32_t full[2u * CTL];
    uint32_t i, w0, ok;
    int32_t peak = 0;

    host_init();
    ua_w = ua_r = 0;
    stream(0);
    uac_render_start();
    audio_block(out, CTL);
    check("USB audio closed (no program reads): the hooks feed nothing", ua_w == 0 && !uac.feed);

    w0 = kick_block(0, 4096);                        /* MASTER mode at the knob's top: the full-level block */
    ok = ua_w == w0 + CTL;
    for (i = 0; i < CTL; i++) {
        full[2u * i] = ring_l(w0 + i);
        full[2u * i + 1u] = ring_r(w0 + i);
        ok &= out[2u * i] == full[2u * i] * (1 << OUT_SHIFT) && out[2u * i + 1u] == full[2u * i + 1u] * (1 << OUT_SHIFT);
        peak = full[2u * i] > peak ? full[2u * i] : peak;
    }
    check("USB LEVEL MASTER: the recording is the DAC's block, sample for sample", ok && peak > 1000);

    w0 = kick_block(0, 1024);
    ok = 1;
    for (i = 0; i < CTL; i++)
        ok &= out[2u * i] == ring_l(w0 + i) * (1 << OUT_SHIFT);
    check("USB LEVEL MASTER, knob at a quarter: the recording follows the knob (= the DAC)", ok);

    w0 = kick_block(1, 1024);
    ok = 1;
    for (i = 0; i < CTL; i++) {
        ok &= ring_l(w0 + i) == full[2u * i] && ring_r(w0 + i) == full[2u * i + 1u];
        ok &= out[2u * i] == ((full[2u * i] * 1024) >> 12) * (1 << OUT_SHIFT);
    }
    check("USB LEVEL FIXED, knob at a quarter: the recording at full level, the DAC a quarter of it", ok);

    w0 = kick_block(1, 0);
    ok = 1;
    for (i = 0; i < CTL; i++)
        ok &= ring_l(w0 + i) == full[2u * i] && out[2u * i] == 0 && out[2u * i + 1u] == 0;
    check("USB LEVEL FIXED, knob at 0: the DAC silent, the recording still full level", ok);
    fx_usb_fixed = 0;

    printf(fails ? "usb_audio_test: %d FAILED\n" : "usb_audio_test: all passed\n", fails);
    return fails ? 1 : 0;
}
