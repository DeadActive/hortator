/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* Live cue (spec docs/superpowers/specs/2026-10-26-cue-variations-fill-copy-design.md): the next slot (A..D) or a
 * variation (1..8) of the playing slot queued while it plays, switched exactly where the pattern ends (the longest
 * track's: as SONG rows end), every track then from its step 0 on the grid a plain loop would be on (song.c
 * seq_restart); the same key again: at the next step. The behaviour follows mono2k by Iñigo Hernández (an FM-1
 * firmware, GPL-3.0-only: its queued pattern switch); no code of it is copied.
 * This file is the ISR's part (seq.c events_block): the boundary, the switch, a slot switch's 5 ms fade (fx.c
 * mix_part). The main loop stages a queued switch beforehand and files the outgoing state after it; the slots' data
 * are project.c's: proj_cue_apply, the ISR's swap of the live state with the staged one (host tests without project.c:
 * tests/drum_host.h's stub). */
#define CUE_SW_SLOT 1u                     /* cue.done: a slot switch / a variation switch happened */
#define CUE_SW_VAR 2u
enum { CUE_E_FULL = 1, CUE_E_SONG, CUE_E_NOSLOT, CUE_E_EMPTY, CUE_E_SAME, CUE_E_PARK = 8 };   /* + c: SAVE C FIRST */
#define CUE_FADE (FS * 5u / 1000u)         /* a slot switch's fade out: 5 ms */
#define CUE_FSTEP (32767u / CUE_FADE + 1u) /* .. its gain step a sample (Q15) */
#define CUE_LEAD (CUE_FADE + 2u * CTL)     /* own clock: it starts this far ahead of the boundary (done before the
                                            * boundary's block, whatever the phase of the blocks) */
static uint8_t var_cur;                    /* the variation playing: 0 = variation 1 (the slot's record's steps) */
static struct {
    volatile uint8_t q_slot, q_var;        /* queued: slot + 1 / variation + 1, 0 none (main writes, the ISR takes) */
    volatile uint8_t now;                  /* the same key again: switch at the next step */
    uint8_t last[4];                       /* each slot's last-played variation (RAM only; power-on 0) */
    uint16_t b24;                          /* 1/24 beats since PLAY at the last restart, mod B24_WRAP */
    volatile uint8_t done, err;            /* ISR -> main: CUE_SW_SLOT | CUE_SW_VAR; a refusal CUE_E_* (said once) */
    uint8_t stg, stg_slot, stg_var;        /* what the main loop staged (project.c STG_*), for slot / variation */
    uint8_t out, out_var, from;            /* the switch's outgoing state, in the staging area for cue_poll to file:
                                            * its kind, its variation, (a slot switch) the slot it left */
    uint16_t stg_pre[NTRK], out_pre[NTRK]; /* the tracks' presets: the staged slot's, the outgoing one's */
    uint8_t stg_pre_ed[NTRK], out_pre_ed[NTRK];
    uint8_t fade;                          /* 0 none, 1 fading out to the boundary, 2 ramping back up (a cancel) */
    uint16_t fg;                           /* the fade's gain at the next block's start (Q15) */
} cue;

static int proj_cue_apply(void);           /* project.c: the staged state in, the outgoing one out (0, or nothing
                                            * staged: CUE_E_SAME) */

static void cue_fade_set(uint32_t g, int32_t d, uint32_t at)   /* this block's mix gain (fx.c mix_part) */
{
    mix_fade = (int32_t)g;
    mix_fade_d = d;
    mix_fade_at = at;
}
static void cue_fade_up(void)              /* a fade under way, its switch gone (cancelled, refused, STOP): back up */
{
    if (cue.fade == 1u)
        cue.fade = 2;
}

/* the switch, x samples into this block (the ISR), or at once while stopped (the main loop, the IRQ off): checks
 * first, changes after. k: the track whose step boundary it is (the grid's time since PLAY moves on by its steps) */
static __attribute__((noinline)) int cue_switch(uint32_t x, uint32_t k)
{
    uint32_t den, b24 = song.playing ? (cue.b24 + (trk[k].seq_cnt + 1u) * step24((uint32_t)trk[k].p[P_SDIV], &den)) %
                                           B24_WRAP : cue.b24;   /* the grid's time: + the steps track k played */
    int r = proj_cue_apply();
    if (r) {
        cue.q_slot = cue.q_var = cue.now = 0;
        if (r != CUE_E_SAME)
            cue.err = (uint8_t)r;
        cue_fade_up();
        return r;
    }
    cue.b24 = (uint16_t)b24;
    if (song.playing) {
        seq_restart(x, cue.b24);
        chain.cut = 1;                             /* (seq_tick: the block's rest after x) */
        acid_gates_close();
    }
    if (cue.fade == 1u) {                          /* this block: the old voices were cut before it renders */
        if (cue.done & CUE_SW_SLOT && mix_fade == 0) {
            for (k = 0; k < NTRK; k++)
                trk[k].dtail = 0;                  /* (faded out: no declick tail of them at full gain) */
            cue_fade_set(32767u, 0, CTL);          /* the new steps from the block's start (as they fire) */
        } else {
            cue_fade_set((uint32_t)mix_fade, -(int32_t)CUE_FSTEP, x);   /* (not faded out: their declick ramps on) */
        }
        cue.fade = 0;
        cue.fg = 32767;
    }
    cue.q_slot = cue.q_var = cue.now = 0;
    return 0;
}

/* every block, first (events_block): the mix gain of this block (fx.c mix_part): a slot switch's fade on its way
 * down, or back up after a cancel; full otherwise. n: the block */
static void cue_fade_tick(uint32_t n)
{
    if (mix_fade != 32767 || mix_fade_d)
        cue_fade_set(32767u, 0, CTL);
    if (cue.fade == 1u && !cue.q_slot)
        cue.fade = 2;                              /* (its switch gone: cancelled, refused, STOP) */
    if (cue.fade == 1u) {                          /* fading out: this block from fg down */
        cue_fade_set(cue.fg, -(int32_t)CUE_FSTEP, CTL);
        cue.fg = (uint16_t)(cue.fg > n * CUE_FSTEP ? cue.fg - n * CUE_FSTEP : 0u);
    } else if (cue.fade == 2u) {                   /* back up */
        cue_fade_set(cue.fg, (int32_t)CUE_FSTEP, CTL);
        cue.fg = (uint16_t)(cue.fg + n * CUE_FSTEP < 32767u ? cue.fg + n * CUE_FSTEP : 32767u);
        if (cue.fg == 32767u)
            cue.fade = 0;
    }
}
/* before the block's seq_ticks (events_block), playing, no song: the queued switch's boundary in this block -> the
 * switch. adv: the sequencer's advance this block (following a clock: the clock's), lead: how far ahead a slot
 * switch's fade starts (own clock CUE_LEAD; following, a pulse at least: the boundary's last pulse), n: the block */
static __attribute__((noinline)) void cue_tick(uint32_t n, uint32_t adv, uint32_t lead)   /* (noinline: the
                                                   * audio ISR's loops stay as they were: target_budget.py) */
{
    uint32_t k, best = NTRK, dist = 0xFFFFFFFFu;
    if (!cue.q_slot && !cue.q_var)
        return;
    if (!song.playing || chain.running)
        return;
    if (cue.now) {                                 /* the soonest next step of any track */
        for (k = 0; k < NTRK; k++) {
            track_t *t = &trk[k];
            uint32_t cur;
            if (t->seq_pos >= 0x7FFFFFFFu)
                return;                            /* (PLAY's first block: step 0 is now anyway) */
            cur = step_samples(t, div_period((uint32_t)t->p[P_SDIV], t->seq_rem), t->seq_cnt);
            if (cur - t->seq_pos < dist) {
                dist = cur - t->seq_pos;
                best = k;
            }
        }
    } else {                                       /* the end of the longest track's pattern */
        uint32_t bn = 0, bd = 1, len, cur;
        track_t *t;
        for (k = 0; k < NTRK; k++) {
            uint32_t m, d;
            m = chain_beats(&trk[k], &d);
            if (m * bd > bn * d) {
                best = k;
                bn = m;
                bd = d;
            }
        }
        t = &trk[best];
        len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
        if (t->seq_pos >= 0x7FFFFFFFu || t->seq_idx + 1u != len)
            return;
        cur = step_samples(t, div_period((uint32_t)t->p[P_SDIV], t->seq_rem), t->seq_cnt);
        dist = cur > t->seq_pos ? cur - t->seq_pos : 0u;
        if (cue.q_slot && cue.fade != 1u && dist <= adv + lead) {
            cue.fade = 1;                          /* a slot switch: its fade starts (this block on) */
            cue.fg = (uint16_t)mix_fade;
            cue_fade_set(cue.fg, -(int32_t)CUE_FSTEP, CTL);
            cue.fg = (uint16_t)(cue.fg > n * CUE_FSTEP ? cue.fg - n * CUE_FSTEP : 0u);
        }
    }
    if (best < NTRK && dist <= adv)
        cue_switch(dist, best);
}

static void cue_start(void)                        /* seq_start: the grid's time from PLAY */
{
    cue.b24 = 0;
}
static void cue_stop(void)                         /* seq_stop: a queued cue dropped (a fade goes back up) */
{
    cue.q_slot = cue.q_var = cue.now = 0;
}
