/* SPDX-License-Identifier: GPL-3.0-only
 * FM-Link: 2026 DEADACTIVE */
/* FM-Link: two FM-1s in time over Bluetooth without MIDI clock (docs/superpowers/specs/2026-10-21-fm-link-design.md).
 * The engine, pure: one fml_t per unit, every time passed in (a unit's TIMER4 ticks, 24 MHz; a sample is FML_TPS
 * ticks), the messages in and out as bytes. tests/fmlink_test.c runs two of them against a simulated link; the
 * firmware's glue is bt_fml.c.
 *   The leader (CLK INT) keeps its sequencer's position in beats since PLAY (Q16) and tells the follower its timeline:
 *   STATE (playing, BPM, the tick t_ref at which it was at beats_ref). The follower (CLK LINK) learns the offset between
 *   the two tick counts from PING / PONG, timed one way at a time: Bluetooth sends at connection events, so a packet
 *   waits 0..15 ms for one, and a reply always waits most of one: a round trip's halves are not equal. So each way is
 *   timed on its own, by packets sent from a timer at random moments (so some are written just before an event): the
 *   follower's PINGs (the leader's PONG says when each came) and the leader's TIME beacons. Each way's fastest packet
 *   of the last FML_WIN has almost no wait; the mean of the two ways' fastest gives the offset, the small rest
 *   cancelling. It then computes where the leader is at each of
 *   its blocks, starts there (a fresh start: at the announced tick; joining: on the next bar), and keeps its
 *   own sequencer's beats on the leader's by advancing a block by one sample more or less.
 * fml_block runs in the audio interrupt, fml_poll / fml_rx in the main loop (the glue calls fml_rx with interrupts off:
 * the timeline's words change together). */
#define FML_TPS 544u                                /* TIMER4 ticks a sample (audio.c DAC_TICKS) */
#define FML_MS 24000u                               /* ticks a millisecond */
#define FML_START_DELAY (80u * FML_MS)              /* a leader's PLAY starts both this much later */
#define FML_PEER_TICKS (2000u * FML_MS)             /* a peer is linked while it was heard in the last 2 s */
#define FML_HELLO_TICKS (500u * FML_MS)
#define FML_EVERY (100u * FML_MS)                   /* PING / TIME every 100..150 ms (random: every phase of the */
#define FML_SPREAD (50u * FML_MS)                   /* link's connection events) */
#define FML_STATE_TICKS (1000u * FML_MS)
#ifndef FML_WIN
#define FML_WIN 48u                                 /* samples each way the offset is taken from (about 6 s) */
#endif
#ifndef FML_SMOOTH
#define FML_SMOOTH 3u                               /* the offset follows its estimate by 1/8 a sample (no jumps when the
                                                     * fastest packet leaves the window) */
#endif
#define FML_BAR (4u << 16)                          /* a bar: 4 beats (Q16) */
#define FML_RESYNC 2205                             /* samples (50 ms): further off than this the follower joins again */
enum { FML_NONE, FML_LEADER, FML_FOLLOWER };
typedef struct {
    uint8_t role;                                   /* this unit's: FML_LEADER (CLK INT), FML_FOLLOWER (CLK LINK) */
    uint8_t seen;                                   /* an FM-Link message came, the last at seen_at */
    uint32_t seen_at;
    uint8_t timers_on, ping_n, pong_due, pong_n;
    uint32_t next_hello, next_ping, next_state, pong_t0, pong_t, rnd;
    int32_t win_up[FML_WIN], win_down[FML_WIN];    /* the follower's samples: to the leader (offset + that way's delay),
                                                     * from it (offset - that way's delay) */
    int32_t off;                                    /* the follower: leader ticks - its ticks */
    uint32_t up_n, up_i, down_n, down_i;
    uint8_t off_ok;
    volatile uint8_t l_valid, l_playing, l_fresh, l_bpm, l_seq;   /* the leader's timeline: its own, or as received */
    volatile uint32_t l_tref, l_bref;
    volatile uint8_t announce;                      /* the leader: a STATE now (PLAY, STOP) */
    uint8_t bpm;                                    /* this unit's BPM (the last block's) */
    uint8_t playing, pending;                       /* its sequencer runs; a start is waiting (leader: start_at) */
    uint32_t start_at, target;                      /* the leader's start tick; the follower's start position */
    uint32_t beats, rem;                            /* its position since PLAY: beats Q16, the remainder of samples */
    volatile uint32_t last_t, last_beats;           /* the last block: heard at last_t, at last_beats */
    uint32_t joined_beats;                          /* the follower: where it last started */
    volatile uint8_t msg_follow;                    /* the follower: a PLAY / STOP of its own came while linked */
} fml_t;

static void fml_init(fml_t *f, uint32_t role)
{
    memset(f, 0, sizeof *f);
    f->role = (uint8_t)role;
}
static int fml_due(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }
static uint32_t fml_next(fml_t *f, uint32_t now)   /* the next PING / TIME: 100..150 ms on */
{
    f->rnd = f->rnd * 1103515245u + 12345u + now;
    return now + FML_EVERY + (f->rnd >> 8) % FML_SPREAD;
}
static void fml_offset(fml_t *f)                    /* the follower: the offset from each way's fastest packet */
{
    uint32_t i;
    int32_t up = f->win_up[0], down = f->win_down[0];
    for (i = 1; i < f->up_n; i++)
        up = f->win_up[i] < up ? f->win_up[i] : up;
    for (i = 1; i < f->down_n; i++)
        down = f->win_down[i] > down ? f->win_down[i] : down;
    up = (int32_t)(((int64_t)up + down) / 2);      /* (the estimate) */
    if (!f->off_ok)
        f->off = up;                                /* the first: at once */
    else
        f->off += (up - f->off) >> FML_SMOOTH;
    f->off_ok = f->up_n >= 8u && f->down_n >= 8u;
}
static int fml_linked(const fml_t *f, uint32_t now) { return f->seen && (int32_t)(now - f->seen_at) < (int32_t)FML_PEER_TICKS; }
static uint32_t fml_rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint8_t *fml_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
    return p + 4;
}

/* a message from the peer, t = when it came (this unit's ticks) */
static void fml_rx(fml_t *f, const uint8_t *p, uint32_t len, uint32_t t)
{
    if (!len)
        return;
    f->seen = 1;
    f->seen_at = t;
    if (p[0] == 'P' && len >= 6u && f->role == FML_LEADER) {         /* PING: answered by the next poll */
        f->pong_due = 1;
        f->pong_n = p[1];
        f->pong_t0 = fml_rd32(p + 2);
        f->pong_t = t;
    } else if (p[0] == 'Q' && len >= 10u && f->role == FML_FOLLOWER) {   /* PONG: when a PING came: a sample up */
        uint32_t t0 = fml_rd32(p + 2), tl = fml_rd32(p + 6);
        if (t - t0 > 500u * FML_MS)
            return;                                 /* (a stale answer) */
        f->win_up[f->up_i] = (int32_t)(tl - t0);    /* offset + the delay to the leader */
        f->up_i = (f->up_i + 1u) % FML_WIN;
        f->up_n += f->up_n < FML_WIN;
        fml_offset(f);
    } else if (p[0] == 'T' && len >= 5u && f->role == FML_FOLLOWER) {    /* TIME: the leader's tick as it sent it */
        f->win_down[f->down_i] = (int32_t)(fml_rd32(p + 1) - t);   /* offset - the delay from the leader */
        f->down_i = (f->down_i + 1u) % FML_WIN;
        f->down_n += f->down_n < FML_WIN;
        fml_offset(f);
    } else if (p[0] == 'S' && len >= 12u && f->role == FML_FOLLOWER) {   /* STATE: the leader's timeline */
        f->l_seq = p[1];
        f->l_playing = p[2] & 1u;
        f->l_fresh = (p[2] >> 1) & 1u;
        f->l_bpm = p[3];
        f->l_tref = fml_rd32(p + 4);
        f->l_bref = fml_rd32(p + 8);
        f->l_valid = 1;
    }
}

/* the main loop: the next message to send now (its length, 0 none); called until 0 */
static uint32_t fml_poll(fml_t *f, uint32_t now, uint8_t *out)
{
    int linked = fml_linked(f, now);
    if (!f->timers_on) {
        f->timers_on = 1;
        f->next_hello = f->next_ping = f->next_state = now;
    }
    if (f->pong_due) {                              /* PONG: the PING's tick and when it came */
        f->pong_due = 0;
        out[0] = 'Q';
        out[1] = f->pong_n;
        fml_wr32(fml_wr32(out + 2, f->pong_t0), f->pong_t);
        return 10;
    }
    if (f->role == FML_LEADER && linked &&
        (f->announce || f->bpm != f->l_bpm || fml_due(now, f->next_state))) {
        f->announce = 0;
        f->next_state = now + FML_STATE_TICKS;
        if (f->playing) {                           /* playing: where it is (the last block) */
            f->l_tref = f->last_t;
            f->l_bref = f->last_beats;
            f->l_fresh = 0;
        }
        f->l_playing = (uint8_t)(f->playing || f->pending);
        f->l_bpm = f->bpm;
        out[0] = 'S';
        out[1] = ++f->l_seq;
        out[2] = (uint8_t)(f->l_playing | f->l_fresh << 1);
        out[3] = f->l_bpm;
        fml_wr32(fml_wr32(out + 4, f->l_tref), f->l_bref);
        return 12;
    }
    if (fml_due(now, f->next_hello)) {
        f->next_hello = now + FML_HELLO_TICKS;
        out[0] = 'H';
        out[1] = 1;                                 /* (the version) */
        out[2] = f->role;
        return 3;
    }
    if (f->role == FML_FOLLOWER && linked && fml_due(now, f->next_ping)) {
        f->next_ping = fml_next(f, now);
        out[0] = 'P';
        out[1] = ++f->ping_n;
        fml_wr32(out + 2, now);
        return 6;
    }
    if (f->role == FML_LEADER && linked && fml_due(now, f->next_ping)) {
        f->next_ping = fml_next(f, now);            /* TIME: its tick, now */
        out[0] = 'T';
        fml_wr32(out + 1, now);
        return 5;
    }
    return 0;
}

static int32_t fml_samples(int64_t beats_q16, uint32_t spb) { return (int32_t)(beats_q16 * (int64_t)spb / 65536); }
/* dt ticks as beats (Q16) at spb samples a beat, to the nearest sample: no 64-bit division (fml_block runs in the audio
 * interrupt, whose code has no __divdi3); the fraction in two 8-bit steps of long division (r << 16 overflows) */
static int64_t fml_beats(int32_t dt, uint32_t spb)
{
    uint32_t a = dt < 0 ? 0u - (uint32_t)dt : (uint32_t)dt, s = (a + FML_TPS / 2u) / FML_TPS, r = s % spb;
    uint32_t hi = (r << 8) / spb, lo = (((r << 8) % spb) << 8) / spb;
    int64_t q = (int64_t)(s / spb) * 65536 + (int64_t)(hi << 8 | lo);
    return dt < 0 ? -q : q;
}

/* one block of n samples heard at tick t, at this unit's bpm (spb samples a beat), its sequencer playing or not, the
 * UI's transport request (0 none, 1 PLAY, 2 STOP, 3 PLAY from the top). Returns the transport request to act on (the
 * same values) and the sequencer's advance in *seq_n (n, a sample more or less; a start: up to half a block either
 * way, the rest of the block it starts in) */
static uint32_t fml_block(fml_t *f, uint32_t t, uint32_t n, uint32_t bpm, uint32_t spb, int playing, uint32_t ui_req,
                          uint32_t *seq_n)
{
    uint32_t req = 0, q;
    int32_t c = 0;
    int linked = fml_linked(f, t);
    f->bpm = (uint8_t)bpm;
    if (!playing && f->playing)
        f->playing = 0;                             /* (stopped by other means: a load, the clock changed) */
    if (f->role == FML_LEADER) {
        if (playing && !f->playing && !f->pending) {   /* started by other means (a count-in's end, SONG): from here */
            f->playing = 1;
            f->beats = f->rem = 0;
            f->announce = 1;
        }
        if (ui_req == 1u || ui_req == 3u) {
            if (linked) {                           /* both start at start_at */
                f->pending = 1;
                f->start_at = t + FML_START_DELAY;
                f->l_tref = f->start_at;
                f->l_bref = 0;
                f->l_fresh = 1;
                f->announce = 1;
                if (playing)
                    req = 2;                        /* (GLO + PLAY while playing: from the top with the follower) */
                f->playing = 0;
            } else {
                req = ui_req;
                f->playing = 1;
                f->beats = f->rem = 0;
            }
        } else if (ui_req == 2u) {
            req = 2;
            f->pending = f->playing = 0;
            f->announce = 1;
        }
        if (f->pending && (int32_t)(t - f->start_at) >= -(int32_t)(n * FML_TPS / 2u)) {
            f->pending = 0;
            f->playing = 1;
            f->beats = f->rem = 0;
            req = 1;
            c = (int32_t)(t - f->start_at) / (int32_t)FML_TPS;   /* late: on by that much in this block */
        }
    } else if (f->role == FML_FOLLOWER && linked && f->l_valid && f->off_ok) {
        int64_t exp = (int64_t)f->l_bref + fml_beats((int32_t)(t + (uint32_t)f->off - f->l_tref), spb);
        if (ui_req)
            f->msg_follow = 1;                      /* its own PLAY / STOP: the leader's are what count */
        if (f->l_playing && !f->playing) {
            if (!f->pending) {
                f->pending = 1;
                f->target = f->l_fresh && exp < (int64_t)FML_BAR ? 0u :
                            exp <= 0 ? 0u : (uint32_t)((exp + FML_BAR - 1) / FML_BAR * FML_BAR);   /* the next bar */
            }
            if (fml_samples(exp - (int64_t)f->target, spb) > (int32_t)n)
                f->target += FML_BAR;               /* (its moment passed: the bar after) */
            else if (fml_samples(exp - (int64_t)f->target, spb) >= -(int32_t)(n / 2u)) {
                f->pending = 0;
                f->playing = 1;
                f->beats = f->joined_beats = f->target;
                f->rem = 0;
                req = 1;
                c = fml_samples(exp - (int64_t)f->target, spb);
            }
        } else if (!f->l_playing && (f->playing || f->pending)) {
            req = f->playing ? 2u : 0u;
            f->playing = f->pending = 0;
        } else if (f->playing) {
            int32_t err = fml_samples(exp - (int64_t)f->beats, spb);
            if (err > FML_RESYNC || err < -FML_RESYNC) {
                req = 2;                            /* far off (it played on its own): stop, join on the next bar */
                f->playing = 0;
            } else {
                c = err >= 4 ? 1 : err <= -4 ? -1 : 0;
            }
        }
    } else {                                        /* no link, no timeline yet, or no role: as CLK INT */
        f->pending = 0;
        if (ui_req == 1u || ui_req == 3u) {
            f->playing = 1;
            f->beats = f->rem = 0;
        } else if (ui_req == 2u) {
            f->playing = 0;
        }
        req = ui_req;
    }
    c = c < -(int32_t)n ? -(int32_t)n : c > (int32_t)n ? (int32_t)n : c;
    *seq_n = n + (uint32_t)c;
    f->last_t = t;
    f->last_beats = f->beats;
    if (f->playing) {
        q = *seq_n * 65536u + f->rem;
        f->beats += q / spb;
        f->rem = q % spb;
    }
    return req;
}
