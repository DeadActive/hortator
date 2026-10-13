/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE (8 tracks, the LFOs' base, Hortator's parameter codes) */
/* MIDI LEARN, the audio interrupt's side (upstream Felucca 1.5's midi_control.c midi_learned / midi_cc_set, adapted;
 * spec docs/superpowers/specs/2026-10-13-midi-learn-design.md). Up to ML_N CCs, each set to one parameter of one
 * track, from any channel of the MIDI input ring (USB, TRS, Bluetooth in MIDI mode: seq.c midi_block). The map: an
 * entry {cc, trk, code}, code = the parameter's P_* id + 1 (0 = empty): Hortator's P_* ids never move (projects
 * store parameters by id, new ones are appended), ml_ids_pinned holds them. The UI (ui_learn.c) writes entries with
 * the interrupts off; ml_arm / ml_heard hand it a CC heard while a parameter is picked. Saved in its own flash object
 * (project.c learn_save / learn_load). CC 0 / 32 (bank select) and 120..127 (channel mode) are never learned. */
#define ML_N 16u
typedef struct { uint8_t cc, trk, code, rsv; } ml_ent_t;
typedef char ml_ent_size[sizeof(ml_ent_t) == 4u ? 1 : -1];
typedef char ml_ids_pinned[P_E0 == 1 && P_LEVEL == 9 && P_SLEN == 22 && P_LFO1 == 32 && P_RMODEL == 48 &&
                           P_FTYPE == 55 && P_PRIO == 60 && P_COUNT < 255 ? 1 : -1];
static ml_ent_t ml_map[ML_N];
static volatile uint8_t ml_arm;                    /* the UI waits for a CC (LEARN on, a parameter picked) */
static volatile uint8_t ml_heard;                  /* .. the CC that came meanwhile + 1 (0 none; a learnable one wins) */

static int ml_free_cc(uint32_t cc) { return cc && cc != 32u && cc < 120u; }
/* a parameter a CC may set: not MODEL (a model comes with its sound: model_step), not RESON's MODEL (a capped model is
 * refused: reson_refused), not MUTE / DUCK (keys, not knobs) */
static int ml_param_ok(uint32_t id)
{
    return id < P_COUNT && id != P_MODEL && id != P_RMODEL && id != P_MUTE && id != P_DUCK;
}

/* CC value v (0..127) -> d's range: a bipolar knob (min < 0 < max) has its 0 at 64; an enum steps over its shown
 * order (DIV: enum_order) */
static int32_t ml_value(const param_desc_t *d, uint32_t v)
{
    int32_t span = d->max - d->min, pos;
    const uint8_t *o = enum_order(d);
    if (d->min < 0 && d->max > 0 && !o)
        return v < 64u ? d->min + ((int32_t)v * -d->min + 32) / 64 : ((int32_t)(v - 64u) * d->max + 31) / 63;
    pos = ((int32_t)v * span + 63) / 127;
    return o ? o[pos] + d->min : d->min + pos;
}

/* a learned CC sets parameter id of t (in the block: after lfo_apply, before the tracks render). The base gets it: the
 * LFOs' copies put back first, applied again after (no advance), so lfo_restore keeps the new value and an LFO on the
 * knob modulates around it; while motion plays it is the knob's new base (as a turn) */
static __attribute__((noinline)) void ml_set(track_t *t, uint32_t id, uint32_t v)
{
    const param_desc_t *d = track_desc(t, id);
    uint32_t k = (uint32_t)(t - trk);
    int32_t nv;
    if (d->max <= d->min || !d->label || d->label[0] == '-')
        return;                                     /* the model has no such knob */
    if (id >= P_SLEN && id <= P_SSWING && chain_busy())
        return;                                     /* PATTERN while a song plays (or is armed): its rows' */
    if (id == P_SRC && chain_busy())
        return;
    nv = ml_value(d, v);
    if (song.lfo_in)
        lfo_restore_track(t);
    t->p[id] = (int16_t)nv;
    if ((mo.base_ok >> k) & 1u)
        mo.base[k][id] = (int16_t)nv;
    if (song.lfo_in)
        lfo_track(t, 0);
}

/* seq.c midi_block: a CC (any channel) */
static __attribute__((noinline)) void learn_cc(uint32_t cc, uint32_t v)
{
    uint32_t i;
    if (ml_arm) {                                    /* a parameter picked: the CC is the UI's (sets nothing) */
        if (!ml_heard || (!ml_free_cc(ml_heard - 1u) && ml_free_cc(cc)))
            ml_heard = (uint8_t)(cc + 1u);
        return;
    }
    if (!ml_free_cc(cc))
        return;
    for (i = 0; i < ML_N; i++) {
        const ml_ent_t *e = &ml_map[i];
        if (e->code && e->cc == cc && e->trk < NTRK && ml_param_ok(e->code - 1u))
            ml_set(&trk[e->trk], e->code - 1u, v);
    }
}
