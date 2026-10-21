/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The UI's LCD traffic on the host (spec: the redraw lag): the pixels each frame sends (lcd_fill + lcd_blit) in the
 * scenarios that lagged on the device (12 MHz SPI: ~1.33 us a pixel, a full screen ~77 ms), against their budgets;
 * and that what the frames left on the screen is what a full repaint draws (no stale region). Built with
 * FELUCCA_BLE=1 for the SMC-Mixer's knob (smc.c smc_knob: a message per click). */
#include "ui_host.h"
#if !FELUCCA_BLE
#error "build with -DFELUCCA_BLE=1"
#endif
#include "ble_ui_stub.h"
uint32_t ble_midi_room(void) { return 64u; }
int ble_midi_send(const uint8_t *m, uint32_t n) { (void)m; (void)n; return 1; }
#include "../firmware/src/smc.c"
#include "../firmware/src/bt_midi.c"
#include "../firmware/src/bt_page.c"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

#define FULL_PX (240u * 200u)                         /* a frame this big: a full repaint */
typedef struct {
    uint32_t px, wins;
} fr_t;

static fr_t frame(void)                              /* one UI frame (as ui_frame), its LCD traffic */
{
    fr_t f;
    uint32_t p0 = host_fill_px + host_blit_px, w0 = host_wins;
    ui_input();
    ui_leds();
    ui_draw();
    f.px = host_fill_px + host_blit_px - p0;
    f.wins = host_wins - w0;
    render_mix(0, 0, 704);                            /* ~15 ms of audio a UI frame */
    return f;
}

static void settle(uint32_t n)
{
    while (n--)
        frame();
}

typedef struct {
    uint32_t n, max, full;
    uint64_t sum;
} st_t;
static void add(st_t *s, fr_t f)
{
    s->n++;
    s->sum += f.px;
    s->full += f.px >= FULL_PX;
    if (f.px > s->max)
        s->max = f.px;
}
static void report(const char *name, const st_t *s)
{
    printf("     %-44s px/frame avg %6u  max %6u  full repaints %u  (~%.1f ms max at 12 MHz)\n", name,
           (unsigned)(s->n ? s->sum / s->n : 0), (unsigned)s->max, (unsigned)s->full, s->max * 16.0 / 12e3);
}

static void pattern(void)
{
    uint32_t i, s;
    for (i = 0; i < 4u; i++)
        for (s = 0; s < 16u; s++)
            trk[i].step[s].on = (uint8_t)((s + i) % 4u == 0u);
}

static void start(int home, int playing)
{
    ui_host_init();
    pattern();
    if (home)
        home_screen();
    else
        home_tracks();
    if (playing)
        transport_req = 1;
    settle(60);
}

/* stopped, everything run out (messages, the touched value, the meters): the screen must be what a full repaint
 * draws */
static uint16_t fb_inc[240 * 240];
static void same_as_full(const char *what)
{
    uint32_t i, diff = 0, y0 = 240, y1 = 0;
    char b[96];
    transport_req = 2;
    release_all();
    keys(0);
    settle(120);
    for (i = 0; i < 4000u && (ui.pr.pop || ui.msg_t || ui.bpm_t || ui.hot_t); i++)   /* (PRESETS' popup: 1.5 s) */
        frame();
    settle(60);
    memcpy(fb_inc, fb, sizeof fb);
    ui.force = 1;
    frame();
    for (i = 0; i < 240u * 240u; i++)
        if (fb[i] != fb_inc[i]) {
            diff++;
            if (i / 240u < y0)
                y0 = i / 240u;
            if (i / 240u > y1)
                y1 = i / 240u;
        }
    snprintf(b, sizeof b, "%s: the screen = a full repaint's (%u px differ, rows %u..%u)", what, (unsigned)diff,
             (unsigned)(diff ? y0 : 0), (unsigned)y1);
    check(b, diff == 0);
}

/* random use (knobs, SELECT, the preset knob, buttons, keys, SMC knob clicks, messages, start / stop), then stopped
 * and run out: the screen must be a full repaint's (a region left stale shows here) */
static uint32_t rnd_s;
static uint32_t rnd(uint32_t n)
{
    rnd_s = rnd_s * 1664525u + 1013904223u;
    return (rnd_s >> 8) % n;
}
static void fuzz(uint32_t seed, uint32_t steps)
{
    static const uint32_t BTN[] = {B_HOME, B_EDIT, B_FX, B_SEQ, B_LFO, B_ARP, B_ENV, B_GLO, B_SCL, B_OCTUP, B_OCTDN,
                                   B_REC, B_HOME, B_HOME};
    static const char *const MSG[] = {"OK", "A MUCH LONGER MESSAGE", "T1 TUNE 64", "SAVED"};
    uint32_t i, k;
    char b[48];
    rnd_s = seed;
    ui_host_init();
    pattern();
    if (seed & 1u)
        home_screen();
    for (i = 0; i < steps; i++) {
        switch (rnd(10)) {
        case 0:
        case 1:
            turn(EN_K1 + rnd(4), (int32_t)rnd(5) - 2);
            break;
        case 2:
            turn(EN_SELECT, rnd(2) ? 1 : -1);
            break;
        case 3:
            if (!rnd(3))
                turn(EN_PRESET, rnd(2) ? 1 : -1);
            break;
        case 4:
            press(BTN[rnd(sizeof BTN / sizeof BTN[0])]);
            break;
        case 5:
            keys(1u << rnd(8));
            break;
        case 6:
            smc_knob(rnd(4), rnd(2) ? 1 : -1);
            break;
        case 7:
            if (!rnd(4))
                ui_message(MSG[rnd(4)]);
            break;
        case 8:
            if (!rnd(6))
                transport_req = song.playing ? 2 : 1;
            break;
        default:
            break;
        }
        for (k = rnd(3) + 1u; k; k--) {
            frame();
            release_all();
            keys(0);
        }
    }
    snprintf(b, sizeof b, "random use (seed %u)", (unsigned)seed);
    same_as_full(b);
}

int main(void)
{
    st_t s;
    uint32_t i;
    fr_t f;

    /* a full repaint (a page change): at most the screen once, plus the 1 px rules (nothing drawn twice) */
    ui_host_init();
    pattern();
    home_tracks();
    memset(&s, 0, sizeof s);
    add(&s, frame());
    report("TRACKS full repaint", &s);
    check("TRACKS: a full repaint sends the screen about once (<= 58000 px)", s.max <= 58000u);
    ui_host_init();
    pattern();
    home_screen();
    memset(&s, 0, sizeof s);
    add(&s, frame());
    report("HOME full repaint", &s);
    check("HOME: a full repaint sends the screen about once (<= 58000 px)", s.max <= 58000u);

    /* TRACKS playing, nothing touched: the playheads and the meters only */
    start(0, 1);
    memset(&s, 0, sizeof s);
    for (i = 0; i < 120u; i++)
        add(&s, frame());
    report("TRACKS playing, idle", &s);
    check("TRACKS playing idle: <= 4000 px a frame on average", s.sum / s.n <= 4000u);
    check("TRACKS playing idle: no frame over 8000 px", s.max <= 8000u);
    memset(&s, 0, sizeof s);                          /* the 8 meters all moving: a hit on each every 4th frame */
    for (i = 0; i < 120u; i++) {
        uint32_t k;
        for (k = 0; k < NTRK; k++)
            if ((i + k) % 4u == 0u)
                trk[k].peak = (int16_t)(4000 + (int32_t)((i * 7919u + k * 104729u) % 28000u));
        add(&s, frame());
    }
    report("TRACKS playing, idle, 8 meters moving", &s);
    check("TRACKS playing idle, the meters moving: <= 4000 px a frame on average", s.sum / s.n <= 4000u);

    /* TRACKS stopped: an FM-1 knob (KNOB 2: LEVEL), a value a frame */
    start(0, 0);
    memset(&s, 0, sizeof s);
    for (i = 0; i < 30u; i++) {
        turn(EN_K1 + 1, (i & 8u) ? -1 : 1);
        add(&s, frame());
    }
    report("TRACKS stopped, FM-1 knob 2 (LEVEL) a value a frame", &s);
    check("TRACKS: an FM-1 knob value <= 7000 px", s.max <= 7000u);
    same_as_full("TRACKS after the FM-1 knob");

    /* TRACKS stopped: the SMC-Mixer's knob 1 (a message per click: "T1 TUNE 64"), a click a frame, then the
     * message runs out */
    start(0, 0);
    memset(&s, 0, sizeof s);
    for (i = 0; i < 30u; i++) {
        smc_knob(0, (i & 8u) ? -1 : 1);
        add(&s, frame());
    }
    report("TRACKS stopped, SMC knob a click a frame", &s);
    check("TRACKS: an SMC knob click <= 15000 px", s.max <= 15000u);
    check("TRACKS: an SMC knob click never repaints everything", !s.full);
    memset(&s, 0, sizeof s);
    for (i = 0; i < 60u; i++)
        add(&s, frame());
    report("TRACKS stopped, the SMC message running out", &s);
    check("TRACKS: the message's end <= 15000 px, no full repaint", s.max <= 15000u && !s.full);
    same_as_full("TRACKS after the SMC message");

    /* the same playing */
    start(0, 1);
    memset(&s, 0, sizeof s);
    for (i = 0; i < 30u; i++) {
        smc_knob(0, (i & 8u) ? -1 : 1);
        add(&s, frame());
    }
    for (i = 0; i < 60u; i++)
        add(&s, frame());
    report("TRACKS playing, SMC knob, the message out", &s);
    check("TRACKS playing: SMC knob clicks and the message's end never repaint everything", !s.full);
    same_as_full("TRACKS after the SMC message (playing)");

    /* HOME stopped: knob 1 (the model's first knob), a value a frame */
    start(1, 0);
    memset(&s, 0, sizeof s);
    for (i = 0; i < 30u; i++) {
        turn(EN_K1, (i & 8u) ? -1 : 1);
        add(&s, frame());
    }
    report("HOME stopped, knob 1 a value a frame", &s);
    check("HOME: a knob value <= 5000 px (the cell, the breadcrumb's text; the scope waits)", s.max <= 5000u);
    same_as_full("HOME after knob 1");

    /* HOME idle: the scope every other frame (reported) */
    start(1, 0);
    memset(&s, 0, sizeof s);
    for (i = 0; i < 60u; i++)
        add(&s, frame());
    report("HOME stopped, idle", &s);
    start(1, 1);
    memset(&s, 0, sizeof s);
    for (i = 0; i < 60u; i++)
        add(&s, frame());
    report("HOME playing, idle", &s);

    /* a message on HOME and its end; TEMPO's popup and its end */
    start(1, 0);
    memset(&s, 0, sizeof s);
    smc_knob(0, 1);
    for (i = 0; i < 60u; i++)
        add(&s, (f = frame()));
    report("HOME stopped, an SMC message and its end", &s);
    check("HOME: a message and its end never repaint everything", !s.full);
    same_as_full("HOME after a message");
    start(0, 0);
    memset(&s, 0, sizeof s);
    turn(EN_SELECT, 1);                               /* TRACKS: SELECT is the BPM (the TEMPO popup) */
    for (i = 0; i < 80u; i++)
        add(&s, frame());
    report("TRACKS stopped, TEMPO and its end", &s);
    check("TRACKS: TEMPO's popup and its end never repaint everything", !s.full);
    same_as_full("TRACKS after TEMPO");

    for (i = 1; i <= 12u; i++)
        fuzz(i * 7919u, 400);

    printf(fails ? "redraw_test: %d FAILED\n" : "redraw_test: all passed\n", fails);
    return fails != 0;
}
