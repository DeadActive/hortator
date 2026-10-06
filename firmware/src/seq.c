/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Drum sequencer and input. 8 tracks x 64 steps (hit / accent / PROB / RATCH), per-track length, division and swing.
 * The 8 white keys F3..F4 hit tracks 1..8; MIDI notes on the drum channel hit every track whose NOTE
 * matches; an armed track records hits into the nearest step while playing. Runs in the audio ISR
 * before each block (events_block). Grids (grids.c) runs on its own 1/32 clock and plays the tracks whose SRC
 * is one of its channels. */
static volatile uint8_t transport_req;   /* 1 start, 2 stop (from the UI) */
static volatile uint8_t panic_req;       /* bit per track: cut its voices */
static uint32_t kb_prev;
/* key index (0 = F3, the lowest key) of the white keys F3 G3 A3 B3 C4 D4 E4 F4 -> tracks 1..8 */
static const uint8_t KEY_TRK_KEY[NTRK] = {0, 2, 4, 6, 7, 9, 11, 12};
/* the STEP grid: steps 1..16 of a bank on the 16 white keys F3 G3 A3 B3 C4 D4 E4 F4 G4 A4 B4 C5 D5 E5 F5 G5
 * (key index from F3); the black keys do nothing there */
static const uint8_t STEP_KEY[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};

static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }
static uint32_t drum_ch(void) { return (uint32_t)clamp(song.g[G_DRCH], 1, 16) - 1u; }

/* a division (N_DIV id) as num / den beats: ids 0..5 are 1 / DEN (fx.c), the slow ids 6..9 are 2, 4, 8, 16 beats.
 * A step's period carries the remainder of the one before (rem < den), so den steps last exactly num beats: no
 * division drifts against another, or against an external clock (each 1/16 at 120 BPM: 5512, 5513, 5512, 5513) */
static uint32_t div_num_den(uint32_t div, uint32_t *den)
{
    if (div >= 6u && div < 10u) {
        *den = 1u;
        return 1u << (div - 5u);
    }
    *den = DIV_DEN[div % 6u];
    return 1u;
}
static uint32_t div_period(uint32_t div, uint32_t rem)
{
    uint32_t den, num = div_num_den(div, &den);
    return (beat_samples() * num + rem) / den;
}
static uint32_t div_rem_next(uint32_t div, uint32_t rem)
{
    uint32_t den, num = div_num_den(div, &den);
    return (beat_samples() * num + rem) % den;
}
/* the length of the step played as number cnt since PLAY: SWING (the track's + the global) makes the
 * even steps longer and the odd ones shorter, so every odd step starts late. Counted from PLAY, not from
 * the step index, so a pattern of any length (1, 3, ...) keeps the long / short pairs on the bar. */
static uint32_t step_samples(const track_t *t, uint32_t period, uint32_t cnt)
{
    int32_t sw = track_swing(t) * (int32_t)div_samples((uint32_t)t->p[P_SDIV]) / 250;
    return period + (uint32_t)((cnt & 1u) ? -sw : sw);
}

/* Grids' clock: one Grids step per 1/32, counted on the 1/16 grid exactly as a 1/16 step track (no track swing)
 * counts its steps: the global swing makes every other 1/16 long; the even 1/32 plays at the 1/16, the odd one at
 * its half. So a tempo change keeps Grids on the step tracks' 1/16s, and no Grids step is ever skipped (the chaos
 * sequence stays the original's). */
#define MUTE_LEAD 256u   /* samples ahead of the bar: the COMP source mute fade (5 ms) and a declick end there */
static struct { uint32_t pos, cnt; uint8_t half, rem; } gclk;   /* samples into the 1/16, 1/16s since PLAY, odd 1/32 done */
static uint32_t grids_l16(uint32_t cnt)
{
    uint32_t p16 = div_period(2u, gclk.rem);
    int32_t sw = song.g[G_SWING] * (int32_t)div_samples(2u) / 250;
    return p16 + (uint32_t)((cnt & 1u) ? -sw : sw);
}

static void grids_fire(void)                        /* one Grids step: the tracks on a channel that fired play */
{
    uint32_t bits = grids_step(), i;
    for (i = 0; bits & 7u && i < NTRK; i++) {       /* accent 127, else 96 */
        uint32_t ch = (uint32_t)trk[i].p[P_SRC];
        if (ch >= 1u && ch <= 3u && ((bits >> (ch - 1u)) & 1u))
            drum_hit(&trk[i], ((bits >> (ch + 2u)) & 1u) ? 127u : 96u);
    }
}

static void grids_tick(uint32_t n)
{
    if (!song.playing)
        return;
    gclk.pos += n;
    for (;;) {
        uint32_t l16 = grids_l16(gclk.cnt);
        int start = gclk.pos == 0x7FFFFFFFu + n;    /* PLAY: the first 1/16 starts in this block */
        if (!start && !gclk.half && gclk.pos >= l16 / 2u) {
            gclk.half = 1;                          /* the odd 1/32 */
            grids_fire();
            continue;
        }
        if (gclk.pos < l16 && !start)
            break;
        if (!start)
            gclk.rem = (uint8_t)div_rem_next(2u, gclk.rem);
        gclk.pos = start ? 0 : gclk.pos - l16;
        gclk.cnt++;
        gclk.half = 0;
        grids_fire();                               /* the even 1/32, on the 1/16 */
    }
}

/* PROB of step s coming up on track t (LEN len): 100 % plays; a percentage plays when the track's next random
 * number is below it; A/B when loop mod B = A - 1 and 1-SHOT on loop 0, loop = steps since PLAY / LEN */
static int step_plays(track_t *t, const step_t *s, uint32_t len)
{
    uint32_t c = s->cond, loop = t->seq_cnt / (len ? len : 1u), a, b;
    if (!c)
        return 1;
    if (c < COND_1SHOT) {
        t->rng = t->rng * 1664525u + 1013904223u;
        return ((t->rng >> 16) * 100u >> 16) < (c - 1u) * 5u;
    }
    if (c == COND_1SHOT)
        return loop == 0u;
    cond_ab(c, &a, &b);
    return loop % b == a - 1u;
}

/* the step that came up: decided once (step_plays), then RATCH hits spread over its length len_s, the first
 * now (rat_due plays the others). A track following Grids plays nothing from its steps. */
static void step_fire(track_t *t, uint32_t len_s)
{
    const step_t *s = &t->step[t->seq_idx];
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u;
    t->rat_n = 0;
    if (t->p[P_SRC] || !s->on || !step_plays(t, s, len))
        return;
    t->rat_vel = (uint8_t)(s->acc ? 127u : 96u);
    drum_hit(t, t->rat_vel);
    t->rat_n = (uint8_t)(s->rat < 3u ? s->rat + 1u : 4u);
    t->rat_k = 1;
    t->rat_len = len_s;
}

/* the roll's hits whose time has come; one not played before the step ends (the tempo changed) is dropped */
static void rat_due(track_t *t, uint32_t cur_len)
{
    while (t->rat_k < t->rat_n) {
        uint32_t at = t->rat_k * t->rat_len / t->rat_n;
        if (at > t->seq_pos || at >= cur_len)
            break;
        drum_hit(t, t->rat_vel);
        t->rat_k++;
    }
}

/* live recording: into the nearest step, as swung (the playing one, or the next one past its middle) */
static void rec_hit(track_t *t, uint32_t vel)
{
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, idx = t->seq_idx % len;
    uint32_t period = div_period((uint32_t)t->p[P_SDIV], t->seq_rem);
    if (t->seq_pos > step_samples(t, period, t->seq_cnt) / 2u) {
        idx = (idx + 1u) % len;
        t->rskip = 1;                               /* it sounds now: that step must not hit again */
        t->rskip_idx = (uint8_t)idx;
    }
    t->step[idx].on = 1;
    t->step[idx].cond = 0;                          /* a recorded step always plays, once */
    t->step[idx].rat = 0;
    if (vel > 110u)
        t->step[idx].acc = 1;
}

static void input_hit(track_t *t, uint32_t vel)
{
    if (((song.rec >> trk_index(t)) & 1u) && song.playing && !t->p[P_SRC])
        rec_hit(t, vel);
    drum_hit(t, vel);
}

static void keyboard_block(void)
{
    uint32_t cur = fm1_in.notes, ch = cur ^ kb_prev, i;
    kb_prev = cur;
    if (song.seq_mode == 1u || (song.seq_mode == 2u && ((fm1_in.buttons >> song.octdn) & 1u)))
        ch &= ~cur;                                 /* the STEP grid / TRACKS / COMP ducks (ui_input.c) own presses; releases
                                                     * still send their note-off (no hung notes). TRACKS while armed
                                                     * (seq_mode 2): the keys play and record, OCT- held: they mute */
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

/* the MIDI ring emptied and open again (usb_app.c's overflow flag cleared) */
static void midi_drop(void)
{
    mi_r = mi_w;
    midi_in_overflow = 0;
}

static void midi_block(void)
{
    if (midi_in_overflow) {                         /* (upstream 1.0, usb_app.c) the ring overflowed: the stream is
                                                     * broken, drop the backlog */
        midi_drop();
        return;
    }
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
        t->seq_rem = 0;
        t->rskip = 0;
        t->rat_n = 0;
        t->rng = 0x9E3779B9u * (i + 1u);           /* PROB: the same variations after every PLAY */
    }
    gclk.pos = 0x7FFFFFFF;                          /* Grids step 0 on the first block too */
    gclk.cnt = 0xFFFFFFFFu;
    gclk.rem = 0;
    grids_start();
    lfo_start();
    song.tick = 0;
    song.playing = 1;
    slicer_start();
}

static void seq_stop(void) { song.playing = 0; }

static void seq_tick(track_t *t, uint32_t n)
{
    uint32_t div = (uint32_t)t->p[P_SDIV], len = (uint32_t)t->p[P_SLEN];
    if (!song.playing)
        return;
    t->seq_pos += n;
    for (;;) {
        uint32_t cur_len = step_samples(t, div_period(div, t->seq_rem), t->seq_cnt);
        rat_due(t, cur_len);
        if (t->seq_pos < cur_len && t->seq_pos != 0x7FFFFFFFu + n)
            break;
        if (t->seq_pos >= 0x7FFFFFFFu) {
            t->seq_pos = 0;                         /* PLAY: step 0 (its remainder 0) */
        } else {
            t->seq_pos -= cur_len;
            t->seq_rem = (uint8_t)div_rem_next(div, t->seq_rem);
        }
        t->seq_cnt++;                               /* the step: steps since PLAY mod LEN, so a LEN change keeps
                                                     * the track on the shared clock (and LEN back = in sync) */
        t->seq_idx = (uint16_t)(t->seq_cnt % (len ? len : 1u));
        if (t->rskip && t->rskip_idx == t->seq_idx) {
            t->rskip = 0;
            t->rat_n = 0;
        } else {
            step_fire(t, step_samples(t, div_period(div, t->seq_rem), t->seq_cnt));
        }
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
    if (song.mute_q) {                              /* TRACKS' waiting mutes: before the bar's first steps */
        uint32_t src = comp_src(), gs = comp_ghost_src(), sb = gs < NTRK ? 1u << gs : 0u, l16 = grids_l16(gclk.cnt);
        int bar = song.playing && ((gclk.cnt + 1u) & 15u) == 0u;
        if (bar && (song.mute_q & sb) && gclk.pos + n + MUTE_LEAD >= l16) {   /* the COMP source plays on muted (ghost
                                                         * key): it changes ahead of the bar */
            if (!trk[src].p[P_MUTE]) {
                trk[src].p[P_MUTE] = 1;             /* mute: its 5 ms fade (fx.c) ends before the bar's first hit */
                song.mute_q &= (uint8_t)~sb;
            } else {
                pr |= sb;                           /* unmute: its silent voices end (declick) before the bar */
            }
        }
        if (!song.playing || (bar && gclk.pos + n >= l16)) {
            for (i = 0; i < NTRK; i++)
                if ((song.mute_q >> i) & 1u) {
                    trk[i].p[P_MUTE] = (int16_t)!trk[i].p[P_MUTE];
                    if (trk[i].p[P_MUTE])
                        pr |= 1u << i;
                    else if (i == src && song.playing)
                        trk[i].gfade = 0;           /* heard at once: the bar's first hit with its attack */
                }
            song.mute_q = 0;
        }
    }
    for (i = 0; i < NTRK; i++)
        if ((pr >> i) & 1u)
            drum_cut(&trk[i]);
    keyboard_block();
    midi_block();
    for (i = 0; i < NTRK; i++)
        seq_tick(&trk[i], n);
    grids_tick(n);
    if (song.playing)
        song.tick++;
}
