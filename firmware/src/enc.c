/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE
 * The decoder after upstream Felucca 1.1.5 (276f72a, issue #126: hal/fm1_input.h fm1__frame):
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The knobs as the drum firmware reads them. The frozen HAL (hal/fm1_input.h, upstream 1.0) decodes the encoders once
 * a scan frame (11 TIMER5 ticks, ~1.1 ms) but takes a contact state only when two frames in a row saw it: a click is
 * 4 transitions and a detented knob snaps through them, so a quick turn (from ~40 detents/s with the transitions
 * bunched in a quarter of the click, ~80/s in half, ~110/s even) lost whole clicks and a flick counted next to nothing.
 * Upstream fixed it in its HAL (1.1.5, #126); ours stays frozen, so the same decoder runs here on the same samples:
 * enc_frame runs from the scan's hook (led_dim.c led_dim_tick, main.c's fm1_timer5_irq right after fm1_input_tick)
 * on the tick that completed a frame (fm1__tick_col 0: fm1_in.raw holds every column of it). The HAL's decoder still
 * runs beside it; nothing reads its steps.
 * Upstream's decoder: the stock quadrature tables (0x4182 / 0x2814, + = clockwise on the hardware), every frame's state
 * counts (no 2-frame filter); an FM-1 detent is one full cycle (4 transitions) and the knob rests in one state, the one
 * seen at power-on (relearned after FM1_REST_FRAMES still elsewhere, #23). Steps are emitted on arriving back at it,
 * the net transitions rounded to whole cycles (>= 2 counts one: a lost transition or two forgiven). A two-state jump
 * (both contacts changed within a frame) counts two on in the direction of travel; from the detent, the direction of
 * a click in the last ENC_GO frames, else nothing. Counted up to ~300 detents/s (tests/enc_test.c).
 * The steps: enc_st.total[e] (only the ISR writes it, a word) minus enc_seen[e] (only the main loop): enc_take needs
 * no IRQ lock. Cost: 7 encoders once a frame, ~20 instructions each, nothing on the other 10 ticks. */
#define ENC_GO 20u                                 /* a 2-state jump from the detent: the way of a click this recent (~22 ms) */
static struct {
    uint8_t prev[FM1_NENC];                        /* the state counted last (valid once seeded) */
    uint8_t rest[FM1_NENC];                        /* the detent state (0..3) */
    int8_t sub[FM1_NENC];                          /* net transitions since the detent */
    int8_t dir[FM1_NENC];                          /* the last click's direction, and its frame */
    uint16_t still[FM1_NENC];                      /* frames since the state changed */
    uint8_t seeded;                                /* bit e: encoder e's first frame seen */
    uint32_t dfr[FM1_NENC];
    uint32_t frames;
    volatile uint32_t total[FM1_NENC];             /* steps, + = clockwise (wrapping; the ISR's) */
} enc_st;
static uint32_t enc_seen[FM1_NENC];               /* total as enc_take last read it (the main loop's) */

/* one scan frame (TIMER5 ISR, fm1_in.raw complete) */
static void enc_frame(void)
{
    uint32_t e, fr = enc_st.frames;
    for (e = 0; e < FM1_NENC; e++) {
        const uint8_t *m = FM1_ENC[e];
        uint32_t cur = ((fm1_in.raw[m[0]] >> m[1]) & 1u) << 1 | ((fm1_in.raw[m[2]] >> m[3]) & 1u), prev, idx;
        int32_t sub, n;
        if (!((enc_st.seeded >> e) & 1u)) {        /* first frame: the knob rests here */
            enc_st.seeded |= (uint8_t)(1u << e);
            enc_st.prev[e] = enc_st.rest[e] = (uint8_t)cur;
            enc_st.still[e] = 1;
            continue;
        }
        prev = enc_st.prev[e];
        if (cur == prev) {
            /* parked a long time off the detent state (held at power-on): that is the detent; never a second one
             * (#23: a knob held mid-click taught its complement as one more and every click counted twice) */
            if (enc_st.still[e] < 0xFFFFu && ++enc_st.still[e] == FM1_REST_FRAMES && cur != enc_st.rest[e]) {
                enc_st.rest[e] = (uint8_t)cur;
                enc_st.sub[e] = 0;
            }
            continue;
        }
        enc_st.still[e] = 1;
        enc_st.prev[e] = (uint8_t)cur;
        sub = enc_st.sub[e];
        idx = prev << 2 | cur;
        if ((0x4182u >> idx) & 1u)
            sub++;
        else if ((0x2814u >> idx) & 1u)
            sub--;
        else                                       /* two states in one frame: a fast turn, the way it was going */
            sub += 2 * (sub > 0 ? 1 : sub < 0 ? -1 : fr - enc_st.dfr[e] <= ENC_GO ? enc_st.dir[e] : 0);
        if (sub > 100 || sub < -100)
            sub = 0;                               /* (never off the detent that long) */
        if (cur == enc_st.rest[e]) {               /* back on the detent: whole cycles, a transition or two forgiven */
            n = sub < 0 ? -sub : sub;
            n = n >= 2 ? (n + 2) / 4 : 0;
            if (n) {
                enc_st.total[e] += (uint32_t)(sub < 0 ? -n : n);
                enc_st.dir[e] = (int8_t)(sub < 0 ? -1 : 1);
                enc_st.dfr[e] = fr;
            }
            sub = 0;
        }
        enc_st.sub[e] = (int8_t)sub;
    }
    enc_st.frames = fr + 1u;
}

/* detent steps of encoder e turned since the last call, + = clockwise (main loop) */
static int32_t enc_take(uint32_t e)
{
    uint32_t t = enc_st.total[e];
    int32_t s = (int32_t)(t - enc_seen[e]);
    enc_seen[e] = t;
    return s;
}
