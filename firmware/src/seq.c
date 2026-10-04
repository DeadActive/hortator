/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 Eugene Vech */
/* Drum sequencer and input. 8 tracks x 64 steps (hit / accent), per-track length, division and swing.
 * The 8 white keys F3..F4 hit tracks 1..8; MIDI notes on the drum channel hit every track whose NOTE
 * matches; an armed track records hits into the nearest step while playing. Runs in the audio ISR
 * before each block (events_block). */
static volatile uint8_t transport_req;   /* 1 start, 2 stop (from the UI) */
static volatile uint8_t panic_req;       /* bit per track: cut its voices */
static uint32_t kb_prev;
/* key index (0 = F3, the lowest key) of the white keys F3 G3 A3 B3 C4 D4 E4 F4 -> tracks 1..8 */
static const uint8_t KEY_TRK_KEY[NTRK] = {0, 2, 4, 6, 7, 9, 11, 12};

static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }
static uint32_t drum_ch(void) { return (uint32_t)clamp(song.g[G_DRCH], 1, 16) - 1u; }

/* the length of the step played as number cnt since PLAY: SWING (the track's + the global) makes the
 * even steps longer and the odd ones shorter, so every odd step starts late. Counted from PLAY, not from
 * the step index, so a pattern of any length (1, 3, ...) keeps the long / short pairs on the bar. */
static uint32_t step_samples(const track_t *t, uint32_t period, uint32_t cnt)
{
    int32_t sw = (t->p[P_SSWING] + song.g[G_SWING]) * (int32_t)period / 250;
    return period + (uint32_t)((cnt & 1u) ? -sw : sw);
}

/* live recording: into the nearest step, as swung (the playing one, or the next one past its middle) */
static void rec_hit(track_t *t, uint32_t vel)
{
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, idx = t->seq_idx % len;
    uint32_t period = div_samples((uint32_t)t->p[P_SDIV]);
    if (t->seq_pos > step_samples(t, period, t->seq_cnt) / 2u) {
        idx = (idx + 1u) % len;
        t->rskip = 1;                               /* it sounds now: that step must not hit again */
        t->rskip_idx = (uint8_t)idx;
    }
    t->step[idx].on = 1;
    if (vel > 110u)
        t->step[idx].acc = 1;
}

static void input_hit(track_t *t, uint32_t vel)
{
    if (((song.rec >> trk_index(t)) & 1u) && song.playing)
        rec_hit(t, vel);
    drum_hit(t, vel);
}

static void keyboard_block(void)
{
    uint32_t cur = fm1_in.notes, ch = cur ^ kb_prev, i;
    kb_prev = cur;
    for (i = 0; ch && i < NTRK; i++) {
        uint32_t k = KEY_TRK_KEY[i], note = (uint32_t)trk[i].p[P_NOTE] & 127u;
        if (!((ch >> k) & 1u))
            continue;
        if ((cur >> k) & 1u) {
            input_hit(&trk[i], 100u);
            midi_out_event(0x09u | (0x90u | drum_ch()) << 8 | note << 16 | 100u << 24);
        } else {
            midi_out_event(0x08u | (0x80u | drum_ch()) << 8 | note << 16);
        }
    }
}

static void midi_block(void)
{
    while (mi_r != mi_w) {
        uint32_t pkt = midi_in_q[mi_r % MQ], st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 0x0Fu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, i;
        mi_r++;
        if (st != 0x90u || !d2 || ch != drum_ch())
            continue;                               /* note-offs: one-shots ignore them */
        for (i = 0; i < NTRK; i++)
            if ((uint32_t)trk[i].p[P_NOTE] == d1)
                input_hit(&trk[i], d2);
    }
}

static void seq_start(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {                    /* every track from its step 0, together */
        track_t *t = &trk[i];
        t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
        t->seq_pos = 0x7FFFFFFF;                    /* step 0 fires on the first block */
        t->seq_cnt = 0xFFFFFFFFu;                   /* step 0 is count 0 */
        t->rskip = 0;
    }
    song.tick = 0;
    song.playing = 1;
    slicer_start();
}

static void seq_stop(void) { song.playing = 0; }

static void seq_tick(track_t *t, uint32_t n)
{
    uint32_t period = div_samples((uint32_t)t->p[P_SDIV]), len = (uint32_t)t->p[P_SLEN];
    if (!song.playing)
        return;
    t->seq_pos += n;
    for (;;) {
        uint32_t cur_len = step_samples(t, period, t->seq_cnt);
        if (t->seq_pos < cur_len && t->seq_pos != 0x7FFFFFFFu + n)
            break;
        t->seq_pos = t->seq_pos >= 0x7FFFFFFFu ? 0 : t->seq_pos - cur_len;
        t->seq_idx = (uint16_t)((t->seq_idx + 1u) % (len ? len : 1u));
        t->seq_cnt++;
        if (t->rskip && t->rskip_idx == t->seq_idx)
            t->rskip = 0;
        else if (t->step[t->seq_idx].on)
            drum_hit(t, t->step[t->seq_idx].acc ? 127u : 96u);
    }
}

/* everything that happens between two rendered blocks */
static void events_block(uint32_t n)
{
    uint32_t i, pr;
    if (transport_req == 1u) {
        seq_start();
        transport_req = 0;
    } else if (transport_req == 2u) {
        seq_stop();
        transport_req = 0;
    }
    pr = panic_req;
    panic_req = 0;
    for (i = 0; i < NTRK; i++)
        if ((pr >> i) & 1u)
            drum_cut(&trk[i]);
    keyboard_block();
    midi_block();
    for (i = 0; i < NTRK; i++)
        seq_tick(&trk[i], n);
    if (song.playing)
        song.tick++;
}
