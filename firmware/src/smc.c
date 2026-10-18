/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* FELUCCA_BLE (felucca.c includes it after the UI and ble/ble_core.h, before bt_midi.c): the M-VAVE SMC-Mixer in its
 * DAW (Mackie Control) mode (spec 2026-10-10 §5; smc-seq's mapping, checked on a unit). In: faders = pitch bend ch 1..8
 * -> track LEVEL with pickup; knobs 1..4 = CC 16..19 ch 1, sign-magnitude -> the selected track's P_E0..P_E3; buttons =
 * notes ch 1, presses only. Out (smc_frame, ble_midi_send): the LEDs as notes 127 / 0, only changes, all of them on a
 * new link; each fader's position as E0+n v v, which the mixer blinks the fader's LED against. Main loop only. */
typedef char smc_eight_strips[NTRK == 8u ? 1 : -1];
static uint32_t bt_mode_now(void);                    /* bt_midi.c */
static struct {
    uint32_t lit[2], known[2];      /* the LEDs as sent: [0] bit = note 0x00..0x1F (REC SOLO MUTE SELECT rows), [1]
                                     * bits 0..2 = STOP PLAY REC (0x5D..0x5F); known 0 = to send */
    uint8_t lv[NTRK];               /* each track's LEVEL as the fader set it or its position was sent */
    uint8_t last[NTRK];             /* each fader's last report (crossing; valid with its seen bit) */
    uint8_t up, on;                 /* linked; refreshed for SMC mode since the link (0: smc_fresh first) */
    uint8_t dark;                   /* SMC mode ended while linked: LEDs still to turn off (35 -> 0) */
    uint8_t picked, seen, pend;     /* bits per fader: picked up; last[] valid; position to send */
} smc;

/* a new link, or SMC mode again: every LED unknown, every fader unpicked, every position to send */
static void smc_fresh(void)
{
    uint32_t i;
    smc.known[0] = smc.known[1] = 0;
    smc.picked = smc.seen = 0;
    smc.pend = 0xFFu;
    smc.dark = 0;
    for (i = 0; i < NTRK; i++)
        smc.lv[i] = (uint8_t)clamp(trk[i].p[P_LEVEL], 0, 127);
    smc.on = 1;
}

/* track n's LEVEL changed by other means (the FM-1, LOAD, INIT, motion): its fader unpicked, the new position sent */
static void smc_notice(uint32_t n)
{
    uint32_t v = (uint32_t)clamp(trk[n].p[P_LEVEL], 0, 127);
    if (v != smc.lv[n]) {
        smc.lv[n] = (uint8_t)v;
        smc.picked &= (uint8_t)~(1u << n);
        smc.pend |= (uint8_t)(1u << n);
    }
}

/* fader n at v: an unpicked fader acts once within 2 of the LEVEL or across it (smc-seq's soft takeover) */
static void smc_fader(uint32_t n, uint32_t v)
{
    uint32_t b = 1u << n, s, crossed;
    smc_notice(n);
    s = smc.lv[n];
    crossed = (smc.seen & b) && ((smc.last[n] < s) != (v < s));
    smc.last[n] = (uint8_t)v;                         /* every report: a later unpick crosses from here */
    smc.seen |= (uint8_t)b;
    if (!(smc.picked & b)) {
        if (!crossed && (v + 2u < s || v > s + 2u))
            return;
        smc.picked |= (uint8_t)b;
    }
    if (v == s)
        return;
    smc.lv[n] = (uint8_t)v;
    smc.pend |= (uint8_t)b;                           /* echoed: the mixer's pickup LED follows (as smc-seq) */
    trk[n].p[P_LEVEL] = (int16_t)v;
    motion_capture(&trk[n], P_LEVEL);
}

/* knob k (0..3) turned delta clicks: the selected track's model knob k, one value step a click; "T3 DECAY 64" */
static void smc_knob(uint32_t k, int32_t delta)
{
    const param_desc_t *d = track_desc(TSEL, P_E0 + k);
    char m[24], val[8];
    const char *unit;
    uint32_t n;
    if (!d->label || !d->label[0] || d->label[0] == '-' || d->max <= d->min)
        return;                                       /* the model has no such knob */
    TSEL->p[P_E0 + k] = (int16_t)param_turn(d, TSEL->p[P_E0 + k], delta);
    preset_touch(TSEL);                               /* presets.c: the voice edited */
    motion_capture(TSEL, P_E0 + k);
    param_format(d, TSEL->p[P_E0 + k], val, &unit);
    m[0] = 'T';
    m[1] = (char)('1' + song.sel);
    m[2] = ' ';
    str_cpy(m + 3, d->label, sizeof m - 3u);
    n = str_len(m);
    str_cpy(m + n, " ", sizeof m - n);
    n = str_len(m);
    str_cpy(m + n, val, sizeof m - n);
    ui_say(m, unit);
}

/* a button pressed (note n): the rows REC SOLO MUTE SELECT 0x00 0x08 0x10 0x18 + track, the transport, track < > */
static void smc_button(uint32_t n)
{
    uint32_t k = n & 7u;
    if (n < 0x20u) {
        if (n < 0x08u) {
            if (!song_lock())                         /* as a REC tap */
                track_rec_toggle(k);
        } else if (n < 0x10u) {
            track_solo_toggle(k);
        } else if (n < 0x18u) {
            track_mute_toggle(k);
        } else {
            track_select(k);
        }
        return;
    }
    switch (n) {
    case 0x5Eu:                                       /* PLAY: stopped -> play (the FM-1's PLAY off SONG) */
        if (!song.playing && !chain_busy()) {
            ui.srec = 0;
            transport_req = 1;
        }
        break;
    case 0x5Du:                                       /* STOP: playing, or a song armed */
        if (song.playing || chain_busy())
            transport_req = 2;
        break;
    case 0x5Fu:                                       /* REC: the selected track */
        if (!song_lock())
            track_rec_toggle(song.sel);
        break;
    case 0x2Eu:
    case 0x30u:                                       /* BANK / CHANNEL <: the previous track */
        if (song.sel)
            track_select(song.sel - 1u);
        break;
    case 0x2Fu:
    case 0x31u:                                       /* BANK / CHANNEL >: the next (track_select: not past 8) */
        track_select(song.sel + 1u);
        break;
    default:                                          /* REW FF, the arrows: nothing */
        break;
    }
}

/* bt_midi_in in SMC mode: one USB-MIDI packet (CIN | status << 8 | d1 << 16 | d2 << 24) */
static void smc_in(uint32_t pkt)
{
    uint32_t st = (pkt >> 8) & 0xFFu, ch = st & 15u, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
    if (!smc.on)
        smc_fresh();
    switch (st >> 4) {
    case 0xEu:                                        /* fader ch + 1 (9: the master, ignored) */
        if (ch < NTRK)
            smc_fader(ch, (d2 << 7 | d1) >> 7);
        break;
    case 0xBu:                                        /* knobs 1..4 (5..8: nothing); bit 6 = counter-clockwise */
        if (!ch && d1 >= 16u && d1 <= 19u && (d2 & 0x3Fu))
            smc_knob(d1 - 16u, d2 & 0x40u ? -(int32_t)(d2 & 0x3Fu) : (int32_t)(d2 & 0x3Fu));
        break;
    case 0x9u:                                        /* velocity 0 = a release */
        if (!ch && d2)
            smc_button(d1);
        break;
    default:                                          /* note-offs (releases), the rest: nothing */
        break;
    }
}

/* the SMC's controller gone (the link down, SMC mode ended): solo off, as no FM-1 control unsolos. Nothing is cut:
 * the tracks it silenced become audible */
static void smc_unsolo(void) { song.solo = 0; }

/* core_ble_link: the link up / down (bt_link first: the mode). Up: everything sent again, the faders unpicked */
static void smc_link(int up)
{
    smc.up = (uint8_t)(up != 0);
    if (!up)
        smc_unsolo();
    smc.on = smc.dark = 0;
    ui_message(up ? "BT CONNECTED" : "BT LOST");
}

static int smc_send(uint32_t a, uint32_t b, uint32_t c)   /* 1 = queued (ble_midi_send); 0: try again next frame */
{
    uint8_t m[3];
    m[0] = (uint8_t)a;
    m[1] = (uint8_t)b;
    m[2] = (uint8_t)c;
    return ble_midi_room() && ble_midi_send(m, 3u) == 1;   /* only what fits: out_drops = real losses */
}

/* ed_service, after ble_service (may run several times a frame): linked in SMC mode, the LED changes and the fader
 * positions waiting; a refused message stays waiting. Nothing changed: no message. */
static void smc_frame(void)
{
    uint32_t w[2], i, j, b, m = 0;
    if (!smc.up || !bt_mode_now()) {
        if (smc.on && smc.up) {                       /* SMC mode ended while linked: unsolo, the LEDs off once */
            smc_unsolo();
            smc.dark = 35u;
        }
        smc.on = 0;                                   /* SMC mode again: everything again */
        for (; smc.dark; smc.dark--)                  /* notes 0x00..0x1F, 0x5D..0x5F at 0 */
            if (!smc_send(0x90u, 35u - smc.dark < 32u ? 35u - smc.dark : 0x5Du + 3u - smc.dark, 0))
                return;
        return;
    }
    if (!smc.on)
        smc_fresh();
    for (i = 0; i < NTRK; i++)
        smc_notice(i);
    fm1_irq_off();                                    /* the audio ISR flips P_MUTE and clears mute_q at the bar */
    for (i = 0; i < NTRK; i++)
        m |= (uint32_t)(trk[i].p[P_MUTE] != 0) << i;
    m ^= song.mute_q;                                 /* MUTE: lit = muted, xor a waiting flip */
    fm1_irq_on();
    w[0] = (uint32_t)song.rec | (uint32_t)song.solo << 8 | (m & 0xFFu) << 16 | 1u << (24u + song.sel);
    w[1] = (song.playing ? 2u : 1u) | (song.rec ? 4u : 0u);
    for (j = 0; j < 2u; j++) {
        uint32_t dirty = ((w[j] ^ smc.lit[j]) | ~smc.known[j]) & (j ? 7u : 0xFFFFFFFFu);
        for (i = 0; dirty && i < 32u; i++) {
            b = 1u << i;
            if (!(dirty & b))
                continue;
            if (!smc_send(0x90u, j ? 0x5Du + i : i, (w[j] & b) ? 127u : 0u))
                return;
            smc.lit[j] = (smc.lit[j] & ~b) | (w[j] & b);
            smc.known[j] |= b;
            dirty &= ~b;
        }
    }
    for (i = 0; smc.pend && i < NTRK; i++)
        if ((smc.pend >> i) & 1u) {
            if (!smc_send(0xE0u + i, smc.lv[i], smc.lv[i]))
                return;
            smc.pend &= (uint8_t)~(1u << i);
        }
}
