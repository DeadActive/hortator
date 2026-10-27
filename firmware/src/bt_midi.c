/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* FELUCCA_BLE (felucca.c includes it after the UI, ble/ble_core.h, bt_set.c and smc.c): the core's side of Bluetooth
 * MIDI (after felucca-ble's bt_midi.c). On the controller's link (ROLE HOST: the CTRL slot) or the host's (ROLE DEVICE).
 * In: each USB-MIDI packet the BLE unit received goes to the SMC-Mixer layout (SMC DAW mode, smc.c) or, in MIDI mode,
 *   through the USB filter into the MIDI input ring as source 3 (seq.c midi_block: notes on the drum channel, MIDI
 *   LEARN's CCs and notes, the realtime bytes with CLK BLE).
 * Out, MIDI mode: what the FM-1 sends to USB MIDI out (the keys' hits, seq.c keyboard_block) also goes to the
 *   connected device. Its caller runs in the audio interrupt, so it queues here and the main loop hands the queue to
 *   the BLE unit.
 * Clock out and THRU: GLO SYNC BLE / BOTH and THRU (midi_out.c, TIMER5 outside the render) queue here too
 *   (bt_sync_out): the one MIDI clock of the FM-1 (Start on PLAY, 24 a beat on the sequencer's grid, Stop; following a
 *   clock, the followed source's realtime), the same as USB's, so the device never gets a second one. A Clock only
 *   while this queue holds fewer than MOUT_CLOCK_ROOM (no backlog of stale Clocks). The queue's producers (the audio
 *   ISR's keys; TIMER5's SYNC / THRU) never run at once: TIMER5 does this only outside the render and the audio ISR
 *   never preempts TIMER5.
 * FM-LINK mode: no MIDI either way (received packets dropped, nothing sent, no clock out, no THRU): the link carries
 *   FM-Link only (bt_fml.c runs in every mode).
 * The link down: what the device held is released (CC 64 = 0 and CC 123 on each channel it played), as a pulled cable
 *   would otherwise leave it held. */
#define BT_MIDI_C 1
#define BT_OUT_Q 32u
static struct {
    uint32_t drops;                                  /* MIDI mode: packets refused, the ring's last 8 slots are TRS's */
    uint32_t out_drops;                              /* MIDI out: messages lost to a full queue here */
    uint16_t held, release;                          /* channels the device played / still to be released */
    uint8_t smc;                                     /* the linked device's name starts with "SMC" (bt_link) */
    uint8_t up;                                      /* linked (subscribed to the device's BLE-MIDI) */
    volatile uint8_t out_on;                         /* MIDI out wanted: linked, MIDI mode (bt_midi_frame) */
} bt_in;
static uint32_t bt_out_q[BT_OUT_Q];
static volatile uint32_t bt_out_w, bt_out_r;

/* 1 = the SMC-Mixer DAW layout, 0 = MIDI: MODE, or AUTO by the connected device's name */
static uint32_t bt_mode_now(void)
{
    bt_set_t s;
    bt_set_get(&s);
    if (s.mode != BT_MODE_AUTO)
        return s.mode == BT_MODE_SMC;
    return bt_in.smc;
}
static int bt_link_only(void)                        /* MODE FM-LINK: no MIDI either way */
{
    bt_set_t s;
    bt_set_get(&s);
    return s.mode == BT_MODE_LINK;
}

/* core_ble_link: the link up (the connected device's name read once) / down */
static void bt_link(int up)
{
    char n[24];
    bt_in.up = (uint8_t)(up != 0);
    bt_in.smc = (uint8_t)(up && ble_ui_conn_name(n) && n[0] == 'S' && n[1] == 'M' && n[2] == 'C');
    bt_in.out_on = (uint8_t)(up && !bt_mode_now() && !bt_link_only());   /* (at once: bt_midi_frame follows MODE from
                                                                           * then on) */
    if (!up) {
        bt_in.release |= bt_in.held;                 /* (bt_midi_frame: into the ring when it has room) */
        bt_in.held = 0;
        bt_out_r = bt_out_w;                         /* nothing queued for a device that is gone */
    }
}

/* usb_app.c midi_in_event's filter: the realtime F8 FA FB FC (as their status), channel voice with CIN = the status'
 * high nibble and data bytes < 0x80 (Cx Dx: one); 0 = dropped */
static uint32_t bt_midi_filter(uint32_t pkt)
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu;
    if (cin == 0xFu)
        return st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu ? 0xFu | st << 8 : 0u;
    if (cin >= 8u && cin <= 0xEu && (st >> 4) == cin && !((pkt >> 16) & 0x80u) &&
        ((cin == 0xCu || cin == 0xDu) || !(pkt & 0x80000000u)))
        return pkt;
    return 0;
}

/* one packet into the MIDI input ring as source 3; 0 = refused. 8 slots stay for TRS (usb_app.c EP1_ROOM), so BLE
 * never sets the overflow; while USB / TRS have set it, midi_enqueue refuses too. The TIMER5 ISR fills the ring as
 * well (USB, TRS): interrupts off */
static int bt_midi_put(uint32_t pkt)
{
    int took;
    fm1_irq_off();
    took = MQ - (mi_w - mi_r) > 8u && midi_enqueue(pkt, 3u);
    fm1_irq_on();
    return took;
}

/* two packets, both or neither (the release of a channel: the pedal up, then All Notes Off), in one interrupts-off
 * region so nothing from USB / TRS lands between them; 0 = refused */
static int bt_midi_put2(uint32_t a, uint32_t b)
{
    int took;
    fm1_irq_off();
    took = MQ - (mi_w - mi_r) > 9u && !midi_in_overflow && midi_enqueue(a, 3u) && midi_enqueue(b, 3u);
    fm1_irq_on();
    return took;
}

static void bt_midi_in(uint32_t pkt)
{
    uint32_t cin, st;
    if (bt_link_only())
        return;                                      /* FM-LINK: MIDI from the device ignored */
    if (bt_mode_now()) {
        smc_in(pkt);
        return;
    }
    pkt = bt_midi_filter(pkt);
    if (!pkt)
        return;
    if (!bt_midi_put(pkt)) {
        bt_in.drops++;
        return;
    }
    cin = pkt & 15u;
    st = (pkt >> 8) & 0xFFu;
    if ((cin == 9u && (pkt >> 24)) || (cin == 0xBu && ((pkt >> 16) & 0xFFu) == 64u && (pkt >> 24)))
        bt_in.held |= (uint16_t)(1u << (st & 15u));  /* a note-on or the pedal down: this channel has something held */
}

static void bt_out_push(uint32_t pkt)               /* the out ring (one producer at a time: the audio ISR, or with
                                                     * interrupts off). Full: dropped and counted */
{
    if (bt_out_w - bt_out_r >= BT_OUT_Q) {
        bt_in.out_drops++;
        return;
    }
    bt_out_q[bt_out_w % BT_OUT_Q] = pkt;
    RING_PUBLISH();
    bt_out_w++;
}

/* a channel voice message the FM-1 sent to USB MIDI out (seq.c keyboard_block), for the BLE device too. From the
 * audio interrupt (TIMER5's bt_sync_out never runs inside it). Full: dropped and counted */
static void bt_midi_out_isr(uint32_t pkt)
{
    uint32_t cin = pkt & 15u;
    if (!bt_in.out_on || cin < 8u || cin > 0xEu)
        return;
    bt_out_push(pkt);
}

/* GLO SYNC / THRU's packet for the device (midi_out.c mout_send; TIMER5 outside the render): rt = a realtime byte
 * (the clock: F8 FA FB FC), queued only while fewer than MOUT_CLOCK_ROOM wait; else a channel voice message (THRU).
 * Anything else, or not linked in MIDI mode (SMC DAW, FM-LINK, no link): nothing. Returns 1 when queued */
static int bt_sync_out(uint32_t pkt, int rt)
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu;
    if (!bt_in.out_on)
        return 0;
    if (rt ? cin != 0xFu || (st != 0xF8u && st != 0xFAu && st != 0xFBu && st != 0xFCu) : cin < 8u || cin > 0xEu)
        return 0;
    if (rt && bt_out_w - bt_out_r >= MOUT_CLOCK_ROOM) {
        bt_in.out_drops++;
        return 0;
    }
    bt_out_push(pkt);
    return 1;
}

/* the main loop (core_ble_api.c bt_service): the releases owed after a dropped link, then the out queue to the BLE
 * unit while it has room (ble_midi_send: main loop only) */
static void bt_midi_frame(void)
{
    uint32_t ch;
    for (ch = 0; ch < 16u && bt_in.release; ch++) {
        if (!((bt_in.release >> ch) & 1u))
            continue;
        if (!bt_midi_put2(0x0Bu | (0xB0u | ch) << 8 | 64u << 16,       /* the pedal up, All Notes Off */
                          0x0Bu | (0xB0u | ch) << 8 | 123u << 16))
            break;                                   /* still owed: the next pass */
        bt_in.release &= (uint16_t)~(1u << ch);
    }
    bt_in.out_on = (uint8_t)(bt_in.up && !bt_mode_now() && !bt_link_only());
    while (bt_out_r != bt_out_w && ble_midi_room()) {
        uint32_t pkt = bt_out_q[bt_out_r % BT_OUT_Q], cin = pkt & 15u;
        uint8_t m[3] = {(uint8_t)(pkt >> 8), (uint8_t)(pkt >> 16), (uint8_t)(pkt >> 24)};
        if (!ble_midi_send(m, cin == 0xFu ? 1u : cin == 0xCu || cin == 0xDu ? 2u : 3u))
            break;
        bt_out_r++;
    }
}
