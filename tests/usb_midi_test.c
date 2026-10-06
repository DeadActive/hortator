/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Host test of the app's USB-MIDI driver, firmware/src/usb_app.c (upstream Felucca 1.0's usb.c): EP1 back-pressure,
 * packet checks, SysEx ends, the update commands and frames, realtime queued, the ring overflow flag. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 1
#define FELUCCA_CDC 0
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"   /* SIE register macros (never touched here) */
#include "../firmware/src/usb_app.c"

static uint32_t now_ms;
static uint32_t ota_now_ms(void) { return now_ms; }
static void ota_idle(void) { now_ms++; }

static int check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static uint32_t ev(uint32_t cin, uint32_t st, uint32_t d1, uint32_t d2) { return cin | st << 8 | d1 << 16 | d2 << 24; }

static void put(uint8_t *b, uint32_t i, uint32_t pkt)       /* event i of an EP1 packet buffer */
{
    b[4u * i] = (uint8_t)pkt;
    b[4u * i + 1u] = (uint8_t)(pkt >> 8);
    b[4u * i + 2u] = (uint8_t)(pkt >> 16);
    b[4u * i + 3u] = (uint8_t)(pkt >> 24);
}

static void reset(void)
{
    mi_r = mi_w = 0;
    midi_in_overflow = 0;
    memset(&usb, 0, sizeof usb);
    sx_ready = sx_collect = 0;
    sx_pos = sx_frame_len = 0;
}

static void sysex_usb(const uint8_t *s, uint32_t n)         /* as USB-MIDI SysEx packets (CIN 4, then 5 / 6 / 7) */
{
    while (n > 3u) {
        midi_in_event(ev(4u, s[0], s[1], s[2]));
        s += 3;
        n -= 3u;
    }
    midi_in_event(ev(n == 1u ? 5u : n == 2u ? 6u : 7u, s[0], n > 1u ? s[1] : 0u, n > 2u ? s[2] : 0u));
}

int main(void)
{
    static const uint8_t UBOOT[6] = {0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7};
    static const uint8_t UPGRADE[6] = {0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7};
    uint8_t pk[64], fr[102];
    uint32_t i, k, w0;
    int bad = 0;

    /* back-pressure: 41 queued leaves 23 free (< 16 + 8): the packet stays; one more free: taken whole */
    reset();
    for (i = 0; i < 41u; i++)
        midi_enqueue(ev(9u, 0x99, 36, 100), 1u);
    for (i = 0; i < 16u; i++)
        put(pk, i, ev(9u, 0x99, 40u + i, 1u + i));
    w0 = mi_w;
    bad += check("EP1: a full ring holds the packet back (host NAKed), nothing queued",
                 ep1_take(pk, 64) == 0 && mi_w == w0);
    mi_r++;
    k = ep1_take(pk, 64);
    for (i = 0; i < 16u && k; i++)
        k = midi_in_q[(w0 + i) % MQ] == ev(9u, 0x99, 40u + i, 1u + i);
    bad += check("EP1: once 24 slots are free the same packet is taken whole, in order", k && mi_w == w0 + 16u);

    /* packet checks: a note-on, a program change (one data byte) kept; the rest dropped */
    reset();
    put(pk, 0, ev(9u, 0x90, 38, 90));
    put(pk, 1, ev(8u, 0x90, 38, 90));                       /* CIN 8 with a note-on status */
    put(pk, 2, ev(9u, 0x90, 0x80, 90));                     /* a data byte >= 0x80 */
    put(pk, 3, ev(9u, 0x90, 38, 0x80));
    put(pk, 4, ev(0xCu, 0xC0, 5, 0x80));                    /* program change: its 2nd byte is not data */
    put(pk, 5, ev(2u, 0xF2, 1, 2));                         /* CIN 2: system common, not queued */
    put(pk, 6, ev(0u, 0x90, 38, 90));                       /* CIN 0: reserved */
    put(pk, 7, ev(0xFu, 0xFE, 0, 0));                       /* active sensing: not queued */
    bad += check("packets: only the valid note-on and program change are queued",
                 ep1_take(pk, 32) == 1 && mi_w == 2u && midi_in_q[0] == ev(9u, 0x90, 38, 90) &&
                     (midi_in_q[1] & 0xFFFFu) == (0xCu | 0xC0u << 8));

    /* SysEx: a status byte ends an unfinished frame (the rest of the key then means nothing) */
    reset();
    for (i = 0; i < 4u; i++)
        sysex_byte(UBOOT[i]);
    sysex_byte(0x90);
    sysex_byte(0x7D);
    sysex_byte(0xF7);
    bad += check("SysEx: a status byte aborts an unfinished frame (no UBOOT request)", !usb.uboot_req && !usb.sx_on);
    reset();
    sysex_usb(UBOOT, 3);
    midi_in_event(ev(9u, 0x90, 38, 90));                    /* a note between the SysEx packets */
    sysex_usb(UBOOT + 3, 3);
    bad += check("SysEx over USB: a channel message ends it; the note is queued", !usb.uboot_req && mi_w == 1u);

    /* realtime inside SysEx is skipped; the UBOOT key and the M-UPGRADE command still work */
    reset();
    for (i = 0; i < 6u; i++) {
        sysex_byte(UBOOT[i]);
        if (i == 2u)
            sysex_byte(0xF8);
    }
    bad += check("SysEx: a clock byte inside is skipped, the UBOOT key works", usb.uboot_req == 1);
    reset();
    sysex_usb(UPGRADE, 3);
    midi_in_event(ev(0xFu, 0xF8, 0, 0));                    /* a clock packet between the SysEx packets */
    sysex_usb(UPGRADE + 3, 3);
    bad += check("M-UPGRADE command over USB with a clock packet in between: ota_req, clock queued",
                 usb.ota_req == 1 && mi_w == 1u && midi_in_q[0] == ev(0xFu, 0xF8, 0, 0));
    reset();
    for (i = 0; i < 3u; i++)
        sysex_byte(UPGRADE[i]);
    sysex_byte(0xB0);                                       /* aborted */
    for (i = 0; i < 6u; i++)
        sysex_byte(UPGRADE[i]);
    bad += check("M-UPGRADE command right after an aborted frame: ota_req", usb.ota_req == 1);

    /* an update / editor frame (100 data bytes) with a clock packet inside arrives whole */
    reset();
    fr[0] = 0xF0;
    for (i = 0; i < 100u; i++)
        fr[1 + i] = (uint8_t)((i * 37u + 5u) & 0x7Fu);
    fr[101] = 0xF7;
    sysex_usb(fr, 51);
    midi_in_event(ev(0xFu, 0xF8, 0, 0));
    sysex_usb(fr + 51, 51);
    bad += check("a 100-byte SysEx frame with a clock packet inside: ready, whole",
                 sx_ready == 1 && sx_frame_len == 100u && !memcmp(sx_frame, fr + 1, 100));

    /* realtime: Clock / Start / Continue / Stop queued with their source (USB = 1) */
    reset();
    midi_in_event(ev(0xFu, 0xF8, 0, 0));
    midi_in_event(ev(0xFu, 0xFA, 0, 0));
    midi_in_event(ev(0xFu, 0xFB, 0, 0));
    midi_in_event(ev(0xFu, 0xFC, 0, 0));
    bad += check("realtime: Clock, Start, Continue, Stop queued (CIN F), source USB",
                 mi_w == 4u && midi_in_q[1] == ev(0xFu, 0xFA, 0, 0) && midi_in_q[3] == ev(0xFu, 0xFC, 0, 0) &&
                     midi_in_source[0] == 1u && midi_in_source[3] == 1u);

    /* overflow: the 65th event sets the flag; nothing is accepted until the reader clears it */
    reset();
    for (i = 0; i < MQ; i++)
        midi_enqueue(ev(9u, 0x99, 36, 100), 1u);
    k = midi_enqueue(ev(9u, 0x99, 36, 100), 1u);
    mi_r = mi_w;                                            /* drained, but the flag is still set */
    bad += check("overflow: a full ring sets the flag and refuses until it is cleared",
                 k == 0 && midi_in_overflow == 1 && midi_enqueue(ev(9u, 0x99, 36, 100), 1u) == 0);

    if (!bad)
        printf("usb_midi_test: all passed\n");
    return bad;
}
