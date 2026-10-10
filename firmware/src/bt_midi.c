/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* FELUCCA_BLE (felucca.c includes it after the UI and ble/ble_core.h): the core's side of BLE MIDI in (spec 2026-10-10
 * §4). Each USB-MIDI packet the BLE unit received goes to the SMC-Mixer layout (SMC DAW mode) or, in MIDI mode,
 * through the USB filter into the MIDI input ring as source 3 (seq.c midi_block: notes on the drum channel, the
 * realtime messages with CLK BLE). Main loop only. */
static struct {
    uint32_t drops;                                  /* MIDI mode: packets refused, the ring's last 8 slots are TRS's */
    uint8_t smc;                                     /* the linked device's name starts with "SMC" (bt_link) */
} bt_in;

/* core_ble_link: the link up (the connected device's name read once) / down */
static void bt_link(int up)
{
    char n[24];
    bt_in.smc = (uint8_t)(up && ble_ui_conn_name(n) && n[0] == 'S' && n[1] == 'M' && n[2] == 'C');
}

/* 1 = the SMC-Mixer DAW layout, 0 = MIDI: MODE, or AUTO by the connected device's name */
static uint32_t bt_mode_now(void)
{
    if (btset.btmode)
        return btset.btmode == 1u;
    return bt_in.smc;
}

static void smc_in(uint32_t pkt) { (void)pkt; }     /* the SMC-Mixer layout (smc.c: not yet) */

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

static void bt_midi_in(uint32_t pkt)
{
    if (bt_mode_now()) {
        smc_in(pkt);
        return;
    }
    pkt = bt_midi_filter(pkt);
    if (!pkt)
        return;
    fm1_irq_off();                                   /* the TIMER5 ISR fills the ring too (USB, TRS) */
    if (MQ - (mi_w - mi_r) <= 8u || !midi_enqueue(pkt, 3u))   /* 8 slots stay for TRS (usb_app.c EP1_ROOM), so BLE
                                                               * never sets the overflow; while USB / TRS have set
                                                               * it, midi_enqueue refuses: counted too */
        bt_in.drops++;
    fm1_irq_on();
}
