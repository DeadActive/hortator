/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE (8 tracks, 128 places, the LFOs' copy, FDR8) */
/* MOTION (upstream Felucca 1.0's motion.c, adapted): knob moves recorded per step and played with the pattern.
 * Recording (ui_input.c motion_capture): the selected track armed (song.rec) and playing, a turn of one of its sound
 * knobs (motion_param) stores the knob's value at the nearest step of that track (the one playing, or the next past
 * its half); the same knob and step again: replaced. Not recording, while motion plays: the turn is the new base.
 * Playback (seq.c seq_tick, motion_step): a track with PLAY on entering step s sets the knobs its events at s name
 * (clamped to the knob's range); a value holds until that knob's next event; step 0, STOP and PLAY off put the base
 * back (every knob as set, taken at PLAY). In a block an LFO on a knob holds its modulated copy in t->p and the
 * value it returns to in t->lsave (lfo.c): motion reads and writes the latter (motion_slot).
 * Store: the lock bank (lockbank.c): 1024 events (motion and parameter locks) shared by the 8 tracks (upstream: 64
 * for 4; FDR8..FDRE: 128), sorted, saved as the project's lock bank (FDRF, project.c). The main loop writes with the
 * audio IRQ off; the ISR reads. */
/* (the store's types, MOTION_MAX and MO_W: lockbank.c) */

static struct {
    lk_bank_t b;                     /* the events and PLAY bits (saved as the project's lock bank) */
    int16_t base[NTRK][P_COUNT];     /* every knob as set, while motion plays */
    uint32_t act[NTRK][MO_W];        /* the knobs motion moved off the base (bit = id) */
    uint8_t base_ok, full;           /* tracks whose base is taken; a turn found no free place (the UI says so) */
    const lk_bank_t *src;            /* a song's row: its slot's bank (song.c), 0 = mo.b */
    const uint8_t *model;            /* .. its slot's models: a track's model now another one skips its P_E events */
} mo;

/* the knobs motion records: the sound (not the model, mute, choke, note, routing, timing, the LFOs' shapes); an LFO's
 * RATE and MORPH, a DEST slot's DEPTH (not its SRC / DEST: routing) */
static int motion_param(uint32_t id)
{
    if (id >= P_LFO1 && id < P_LFO1 + 2u * LF_N) {
        uint32_t f = (id - P_LFO1) % LF_N;
        return f == LF_RATE || f == LF_MORPH || f == LF_DEPTH;
    }
    if (id >= P_LSRC1 && id <= P_LS4DEPTH)
        return id == P_LS3DEPTH || id == P_LS4DEPTH;
    return (id >= P_E0 && id <= P_E7) || id == P_LEVEL || id == P_PAN || (id >= P_DIST && id <= P_REV) ||
           (id >= P_SLPAT && id <= P_SLDEPTH) || (id >= P_LLEVEL && id <= P_LDEC) || (id >= P_RTUNE && id <= P_RPOS) ||
           (id >= P_FCUT && id <= P_FDEC) || id == P_RKEY;   /* the FILTER's knobs, not its TYPE (as RESON's MODEL);
                                                                 * RESON's KEYF */
}
static int motion_on(uint32_t k) { return (int)((mo.b.on >> k) & 1u); }
static uint32_t motion_count(uint32_t k) { return lk_count(&mo.b, k, LK_MOTION); }   /* track k's motion events */
static int motion_moved(uint32_t k, uint32_t id) { return (int)((mo.act[k][id >> 5] >> (id & 31u)) & 1u); }
static void motion_mark(uint32_t k, uint32_t id) { mo.act[k][id >> 5] |= 1u << (id & 31u); }

/* knob id of t as it rests: an LFO on it this block keeps that in lsave (lfo.c), else t->p */
static int16_t *motion_slot(track_t *t, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < t->lnum; i++)
        if (t->lpid[i] == id)
            return &t->lsave[i];
    return &t->p[id];
}
static void motion_take_base(uint32_t k)
{
    uint32_t id;
    for (id = 0; id < P_COUNT; id++)
        mo.base[k][id] = *motion_slot(&trk[k], id);
    mo.act[k][0] = mo.act[k][1] = mo.act[k][2] = 0;
    mo.base_ok |= (uint8_t)(1u << k);
}
static void motion_restore(uint32_t k)            /* the moved knobs back to the base */
{
    uint32_t id;
    if (!((mo.base_ok >> k) & 1u))
        return;
    for (id = 0; id < P_COUNT; id++)
        if (motion_moved(k, id))
            *motion_slot(&trk[k], id) = mo.base[k][id];
    mo.act[k][0] = mo.act[k][1] = mo.act[k][2] = 0;
}

/* ---- the ISR (seq.c) */
static void motion_begin(void)                    /* seq_start: the base of every track */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        motion_restore(k);                         /* (a restart while playing) */
        motion_take_base(k);
    }
}
static void motion_end(void)                      /* seq_stop: the patch back */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        motion_restore(k);
    mo.base_ok = 0;
}
/* t enters step s: at step 0 the base back, then the knobs its events at s name */
static __attribute__((noinline)) void motion_step(track_t *t, uint32_t s)
{
    uint32_t k = (uint32_t)(t - trk), i, at = k << 6 | s, moved = 0;
    const lk_bank_t *m = mo.src ? mo.src : &mo.b;
    if (!((m->on >> k) & 1u))
        return;
    if (!((mo.base_ok >> k) & 1u))
        motion_take_base(k);
    if (!s) {
        moved = mo.act[k][0] | mo.act[k][1] | mo.act[k][2];
        motion_restore(k);
    }
    for (i = lk_seek(m, k, s); i < m->count && (uint32_t)(m->key[i] >> 7) == at; i++) {   /* the step's events */
        uint32_t id = m->key[i] & 127u;
        if (!lk_is_lock(m, i) && !(mo.model && mo.model[k] != t->p[P_MODEL] && id >= P_E0 && id <= P_E7)) {
            const param_desc_t *d = track_desc(t, id);
            *motion_slot(t, id) = (int16_t)clamp(m->val[i], d->min, d->max);
            motion_mark(k, id);
            moved = 1;
        }
    }
    if (moved && t->lnum && song.lfo_in) {         /* an LFO holds a knob this block: its copy from the new value, */
        lfo_restore_track(t);                       /* so this step's hit reads it (as lfo_hit does) */
        lfo_track(t, 0);
    }
}

/* ---- the IRQ off (callers) */
static void motion_add(uint32_t k, uint32_t s, uint32_t id, int32_t v)   /* replaces the same place; full: refused */
{
    if (lk_put(&mo.b, lk_key(k, s, id), v, 0) < 0) {   /* (the bank's 1024 shared with the locks) */
        mo.full = 1;
        return;
    }
    mo.b.on |= (uint8_t)(1u << k);
}
static void motion_forget(void)                   /* new knobs (a project load, INIT ALL): nothing to put back */
{
    memset(mo.act, 0, sizeof mo.act);
    mo.base_ok = 0;
    mo.full = 0;
}
static void motion_rebase(uint32_t k)             /* track k's sound replaced (a model, INIT SND): the new base */
{
    uint32_t id;
    if (!((mo.base_ok >> k) & 1u))
        return;
    for (id = 0; id < P_COUNT; id++)               /* the knobs the model left alone: back to the patch first, or */
        if (motion_moved(k, id) && (id < P_E0 || id > P_E7))   /* their recorded value would become the patch */
            *motion_slot(&trk[k], id) = mo.base[k][id];
    motion_take_base(k);
}

/* ---- the main loop (they take the IRQ guard) */
/* ui_input.c: knob id of t was just turned (t->p[id] is its new value) */
static void motion_capture(track_t *t, uint32_t id)
{
    uint32_t k = (uint32_t)(t - trk), s, len, cur;
    if (id >= P_COUNT || !motion_param(id))
        return;
    fm1_irq_off();
    if (song.playing && ((song.rec >> k) & 1u) && k == song.sel) {
        len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
        if (t->seq_pos >= 0x7FFFFFFFu) {
            s = 0;                                 /* PLAY: step 0 comes next */
        } else {
            s = t->seq_idx % len;
            cur = step_samples(t, div_period((uint32_t)t->p[P_SDIV], t->seq_rem), t->seq_cnt);
            if (t->seq_pos > cur / 2u)
                s = (s + 1u) % len;                /* past the half: the next step */
        }
        if ((mo.base_ok >> k) & 1u)
            motion_mark(k, id);                    /* a STOP before its step still puts the base back */
        motion_add(k, s, id, t->p[id]);
    } else if ((mo.base_ok >> k) & 1u) {
        mo.base[k][id] = t->p[id];                 /* a turn while motion plays: the new base */
    }
    fm1_irq_on();
}
static void motion_set_play(uint32_t k, int on)   /* the MOTION page's PLAY */
{
    fm1_irq_off();
    if (on) {
        mo.b.on |= (uint8_t)(1u << k);
    } else {
        mo.b.on &= (uint8_t)~(1u << k);
        motion_restore(k);
    }
    fm1_irq_on();
}
static void motion_clear(uint32_t k)              /* track k's motion events gone (its locks kept), its knobs back */
{
    fm1_irq_off();
    motion_restore(k);
    lk_clear_track(&mo.b, k, LK_MOTION);
    mo.b.on &= (uint8_t)~(1u << k);
    mo.full = 0;
    fm1_irq_on();
}
