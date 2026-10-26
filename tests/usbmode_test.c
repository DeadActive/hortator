/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* MENU > USB AUDIO (ui_menu.c usb_mode_service, usb_app8.c) on the host, the USB hardware replaced by counters
 * (host_usb_offs / host_usb_ons):
 *   the row       KNOB 1 right 10 CH / left 2 CH, OCT+ toggles; nothing reaches USB while the menu is open
 *   reconnect     the menu closes: USB off at once (nothing streams, MIDI out dropped, the message), on again 1 s
 *                 later with the other descriptor set; the sequencer runs on throughout; MIDI in and out work again
 *                 once the host has configured the device; and back to 2 CH: usb_app.c's descriptors byte for byte
 *   CLK USB       the message says the clock pauses; playing stays on, nothing stops or crashes
 *   no switch     stepped there and back: no reconnect
 *   power-on      the saved 10 CH enumerates as 10 CH; SAFE START (SEQ held) as 2 CH, and never switches */
#define FELUCCA_UAC 1
#define FELUCCA_CDC 1
#include "ui_host.h"

int ref_get_desc(uint32_t wvalue, const uint8_t **d, uint16_t *l);   /* usb8_ref.c: the frozen usb_app.c */

static int fails;
static void check(const char *what, int ok)
{
    printf("%-78s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
}

static int same_as_frozen(void)                        /* every descriptor request as usb_app.c answers it */
{
    uint32_t t, i;
    for (t = 0; t < 0x100u; t++)
        for (i = 0; i < 24u; i++) {
            const uint8_t *a = 0, *b = 0;
            uint16_t la = 0, lb = 0;
            int ra = get_desc(t << 8 | i, &a, &la), rb = ref_get_desc(t << 8 | i, &b, &lb);
            if (ra != rb || (ra && (la != lb || memcmp(a, b, la))))
                return 0;
        }
    return 1;
}

static int product_is(const char *s)
{
    const uint8_t *d;
    uint16_t l;
    uint32_t n = (uint32_t)strlen(s), i;
    if (!get_desc(0x0302, &d, &l) || l != 2u + 2u * n)
        return 0;
    for (i = 0; i < n; i++)
        if (d[2 + 2 * i] != (uint8_t)s[i])
            return 0;
    return 1;
}

static void frames(uint32_t n, uint32_t ms_each)       /* UI frames, the clock and the audio moving on */
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        fm1_ms += ms_each;
        ui_frame();
        render_mix(0, 0, ms_each * FS / 1000u / CTL * CTL);   /* (the audio ISR meanwhile) */
    }
}

static void setup(void)
{
    ui_host_init();
    safe_start = 0;
    usbaud = 0;
    usb10 = 0;
    memset(&usbm, 0, sizeof usbm);
    host_usb_offs = host_usb_ons = 0;
    usb.up = 1;
    usb.config = 1;
    usb.detached = 0;
}

static void open_row(void)
{
    ui.menu = 1;
    ui.menu_sel = MI_USBAUD;
    ui.force = 1;
}

static void close_menu(void)                           /* OCT-: the menu closes (menu_close); the next UI frame */
{
    press(B_OCTDN);
    ui_frame();
    release_all();
    frames(1, 15);
}

static void test_row(void)
{
    int ok = 1;
    setup();
    open_row();
    ok &= str_eq(MI_NAME[MI_USBAUD], "USB AUDIO") && MI_USBAUD == MI_USB + 1;
    turn(EN_K1, 1);
    ui_frame();
    ok &= usbaud == 1u;
    turn(EN_K1, -1);
    ui_frame();
    ok &= usbaud == 0u;
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
    ok &= usbaud == 1u;
    turn(EN_K1, 1);                                     /* 10 CH MIX -> 10 CH FX, then stops there */
    ui_frame();
    ok &= usbaud == 2u;
    turn(EN_K1, 3);
    ui_frame();
    ok &= usbaud == 2u;
    press(B_OCTUP);                                     /* OCT+ wraps: FX -> 2 CH */
    ui_frame();
    release_all();
    ui_frame();
    ok &= usbaud == 0u;
    turn(EN_K1, -1);
    ui_frame();
    ok &= usbaud == 0u;
    turn(EN_K1, 1);                                     /* a detent a step */
    ui_frame();
    turn(EN_K1, 1);
    ui_frame();
    ok &= usbaud == 2u && str_eq(USBAUD_NAME[0], "2 CH") && str_eq(USBAUD_NAME[1], "10 CH MIX") &&
          str_eq(USBAUD_NAME[2], "10 CH FX");
    frames(200, 15);                                    /* 3 s in the menu */
    check("USB AUDIO: KNOB 1 steps 2 CH, 10 CH MIX, 10 CH FX (stops at the ends), OCT+ steps and wraps", ok);
    check("  nothing reaches USB while the menu is open (3 s)", host_usb_offs == 0 && host_usb_ons == 0 && usb10 == 0);
}

static void play_kit(void)                             /* T1 on every step, PLAY */
{
    uint32_t k;
    for (k = 0; k < NSTEP; k++)
        trk[0].step[k].on = 1;
    transport_req = 1;
    render_mix(0, 0, CTL);
}

static void test_reconnect(void)
{
    uint32_t c0, c1, c2, mo;
    int ok;
    setup();
    play_kit();
    open_row();
    turn(EN_K1, 1);
    ui_frame();
    c0 = trk[0].seq_cnt;
    close_menu();
    ok = host_usb_offs == 1 && host_usb_ons == 0 && usb.config == 0 && usb10 == 0 && str_eq(ui.msg, "USB RECONNECT: 10 CH");
    check("menu closed: USB off in the next UI frame (no host configuration), the message", ok);
    mo = mo_w;
    midi_out_event(0x00403C99u);                        /* a key's note while the cable is "out" */
    check("  MIDI out dropped while off (nothing stale for the next host)", mo_w == mo);
    frames(59, 15);                                     /* 0.9 s */
    check("  still off 0.9 s later", host_usb_ons == 0 && usb10 == 0);
    c1 = trk[0].seq_cnt;
    frames(10, 15);                                     /* past 1 s */
    check("  on again after 1 s, with the 10 CH set (usb10 1)", host_usb_ons == 1 && usb10 == 1 && host_usb_offs == 1);
    c2 = trk[0].seq_cnt;
    check("  the sequencer ran on throughout (steps played before, during and after)", c1 > c0 && c2 > c1 &&
          song.playing);
    check("  the host enumerates the other set: bcdDevice 3.81, \"Hortator 8ch\"",
          product_is("Hortator 8ch") && !same_as_frozen());
    usb.config = 1;                                     /* the host configures it */
    mo = mo_w;
    midi_out_event(0x00403C99u);
    {
        uint32_t a0 = hit_age(&trk[1]);
        midi_enqueue(0x7F269909u, 1u);                  /* note on 38, channel 10: T2 (the power-on kit) */
        render_mix(0, 0, CTL * 4);
        check("  MIDI works again once configured: out queued, a note in plays T2", mo_w == mo + 1u &&
              hit_age(&trk[1]) != a0);
    }
    frames(40, 15);
    check("  one reconnect only", host_usb_offs == 1 && host_usb_ons == 1);

    open_row();                                         /* and back */
    turn(EN_K1, -1);
    ui_frame();
    close_menu();
    frames(80, 15);
    check("back to 2 CH: off, on 1 s later; usb_app.c's descriptors byte for byte",
          host_usb_offs == 2 && host_usb_ons == 2 && usb10 == 0 && same_as_frozen() && product_is("Hortator"));
    transport_req = 2;
    render_mix(0, 0, CTL);
}

/* inside 10 CH, MIX <-> FX: the same descriptors, no reconnect; the routing at once (fx.c fades it) */
static void test_mix_fx(void)
{
    int ok = 1;
    setup();
    usbaud = UA_10MIX;
    usb10 = 1;
    usb_fxpair = 0;
    open_row();
    turn(EN_K1, 1);                                     /* MIX -> FX */
    ui_frame();
    ui_frame();
    ok &= usbaud == UA_10FX && usb_fxpair == 1;          /* at once, in the menu too */
    close_menu();
    frames(100, 15);
    ok &= host_usb_offs == 0 && host_usb_ons == 0 && usb10 == 1 && usb_fxpair == 1;
    open_row();
    turn(EN_K1, -1);                                    /* FX -> MIX */
    ui_frame();
    close_menu();
    frames(100, 15);
    ok &= host_usb_offs == 0 && host_usb_ons == 0 && usb10 == 1 && usb_fxpair == 0;
    check("10 CH MIX <-> 10 CH FX: no reconnect (same descriptors), the routing at once", ok);
    open_row();                                         /* FX -> 2 CH: a reconnect */
    turn(EN_K1, 1);
    ui_frame();
    turn(EN_K1, -1);
    ui_frame();
    turn(EN_K1, -1);
    ui_frame();
    close_menu();
    frames(100, 15);
    check("10 CH FX -> 2 CH: reconnects, as 10 CH MIX -> 2 CH", host_usb_offs == 1 && host_usb_ons == 1 && usb10 == 0 &&
          same_as_frozen());
    usb_fxpair = 0;
}

static void test_clock(void)
{
    uint32_t i;
    int ok = 1;
    setup();
    song.g[G_CLOCK] = 1;                                /* CLK USB */
    play_kit();
    song.playing = 1;
    open_row();
    turn(EN_K1, 1);
    ui_frame();
    close_menu();
    ok &= str_eq(ui.msg, "CLK USB PAUSED: 10 CH");
    for (i = 0; i < 100u; i++) {                        /* no clock for 1.5 s */
        fm1_ms += 15;
        ui_frame();
        ok &= song.playing == 1;
    }
    check("CLK USB: the message says so; playing stays on, the reconnect completes", ok && usb10 == 1 &&
          host_usb_ons == 1);
    for (i = 0; i < 80u; i++) {                         /* 1.2 s more (2.7 s off): inside the 2 s grace */
        fm1_ms += 15;
        ui_frame();
        ok &= song.playing == 1;
    }
    check("  the steps wait for the clock till 2 s after the reconnect", ok);
    for (i = 0; i < 100u; i++) {                        /* no clock after the grace: stopped as a pulled cable */
        fm1_ms += 15;
        ui_frame();
    }
    check("  no clock by then: stopped, as a pulled cable stops it", song.playing == 0);
    song.g[G_CLOCK] = 0;
    transport_req = 2;
    render_mix(0, 0, CTL);
}

static void test_no_switch(void)
{
    setup();
    open_row();
    turn(EN_K1, 1);
    ui_frame();
    turn(EN_K1, -1);
    ui_frame();
    close_menu();
    frames(100, 15);
    check("stepped to 10 CH and back to 2 CH, then closed: no reconnect", host_usb_offs == 0 && host_usb_ons == 0 &&
          usb10 == 0);
}

static void test_power_on(void)
{
    setup();
    usbaud = 1;                                         /* saved 10 CH */
    fm1_in.buttons = 0;
    drum_boot_init();
    check("power-on with 10 CH saved: enumerates as 10 CH", usb10 == 1 && usb_fxpair == 0 && product_is("Hortator 8ch"));
    setup();
    usbaud = UA_10FX;
    drum_boot_init();
    check("power-on with 10 CH FX saved: 10 CH, channels 1-2 the send effects", usb10 == 1 && usb_fxpair == 1);
    usb_fxpair = 0;
    setup();
    usbaud = 1;
    fm1_in.buttons = 1u << panel.btn[B_SEQ];            /* SEQ held: SAFE START */
    drum_boot_init();
    fm1_in.buttons = 0;
    check("SAFE START with 10 CH saved: 2 CH, usb_app.c's descriptors byte for byte", safe_start && usb10 == 0 &&
          same_as_frozen());
    ui.menu = 0;
    frames(100, 15);
    check("  SAFE START never switches (no reconnect)", host_usb_offs == 0 && host_usb_ons == 0 && usb10 == 0);
    safe_start = 0;
}

int main(void)
{
    printf("-- MENU > USB AUDIO\n");
    test_row();
    test_reconnect();
    test_mix_fx();
    test_clock();
    test_no_switch();
    test_power_on();
    printf(fails ? "USBMODE TEST FAILED (%d)\n" : "usbmode: all ok\n", fails);
    return fails != 0;
}
