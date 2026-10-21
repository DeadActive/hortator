/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE (8 tracks, the LFOs' base, Hortator's parameter codes, the actions) */
/* MIDI LEARN, the audio interrupt's side (upstream Felucca 1.5's midi_control.c midi_learned / midi_cc_set, adapted;
 * spec docs/superpowers/specs/2026-10-13-midi-learn-design.md). Up to ML_N sources, each bound to one parameter of
 * one track or to one action, from any channel of the MIDI input ring (USB, TRS, Bluetooth in MIDI mode: seq.c
 * midi_block). The map: an entry {num, trk, code, src}: src ML_CC / ML_NOTE, num the CC / the note; code = the
 * parameter's P_* id + 1 (0 = empty): Hortator's P_* ids never move (projects store parameters by id, new ones are
 * appended), ml_ids_pinned holds them; or an action (ML_A_*, from 0x80: spec §7). A parameter or a PERFORM macro
 * takes a CC; a button action (PLAY REC NEXT / PREV TRACK, a PERFORM effect or mute) a CC (press >= 64, let go < 64:
 * edges only) or a note (on: press, off / velocity 0: let go). The UI (ui_learn.c) writes entries with the
 * interrupts off; ml_arm / ml_heard hand it a source heard while something is picked; ml_q hands it the presses of
 * PLAY REC NEXT / PREV (they need the UI: transport_req, track_rec_toggle, track_select). PERFORM's effects and
 * macros are set here, as the layer's keys are (seq.c keyboard_block). Saved in its own flash object (project.c
 * learn_save / learn_load). CC 0 / 32 (bank select) and 120..127 (channel mode) are never learned. */
#define ML_N 32u
#define ML_CC 0u                                   /* ml_ent_t src */
#define ML_NOTE 1u
#define ML_A_PLAY 0x80u                            /* the action codes (stable: they are stored) */
#define ML_A_REC 0x81u
#define ML_A_NEXT 0x82u                            /* NEXT TRACK / PREV TRACK */
#define ML_A_PREV 0x83u
#define ML_A_FX 0x90u                              /* + PF_*: a PERFORM effect (PF_R8 .. PF_ODN), a mute (PF_M1 + k) */
#define ML_A_MAC 0xB0u                             /* + 0..3: a PERFORM macro FILTER CRUSH THROW DEPTH */
typedef struct { uint8_t num, trk, code, src; } ml_ent_t;
typedef char ml_ent_size[sizeof(ml_ent_t) == 4u ? 1 : -1];
typedef char ml_ids_pinned[P_E0 == 1 && P_LEVEL == 9 && P_SLEN == 22 && P_LFO1 == 32 && P_RMODEL == 48 &&
                           P_FTYPE == 55 && P_PRIO == 60 && P_LSRC1 == 61 && P_LS3SRC == 63 && P_LS4DEPTH == 68 &&
                           P_COUNT < 0x80 ? 1 : -1];
typedef char ml_pf_pinned[PF_R8 == 0 && PF_REV == 3 && PF_LPF == 4 && PF_TAPE == 6 && PF_OUP == 8 && PF_ODN == 9 &&
                          PF_M1 == 10 && PF_N == 18 && ML_A_FX + PF_N <= ML_A_MAC && ML_N <= 32u ? 1 : -1];
static ml_ent_t ml_map[ML_N];
static volatile uint8_t ml_arm;                    /* the UI waits for a source: 1 a CC (a parameter or a macro
                                                    * picked), 2 a CC or a note (a button action picked) */
static volatile uint16_t ml_heard;                 /* .. the source that came meanwhile + 1 (num | src << 8; 0 none; a
                                                    * learnable one wins) */
static volatile uint32_t ml_down;                  /* ISR: the entries whose button is down */
#define ML_QN 8u
static volatile uint8_t ml_q[ML_QN], ml_qw, ml_qr; /* ISR -> UI: presses of PLAY REC NEXT / PREV (codes) */

static int ml_free_cc(uint32_t cc) { return cc && cc != 32u && cc < 120u; }
/* a parameter a CC may set: not MODEL (a model comes with its sound: model_step), not RESON's MODEL (a capped model is
 * refused: reson_refused), not MUTE / DUCK (keys, not knobs) */
static int ml_param_ok(uint32_t id)
{
    return id < P_COUNT && id != P_MODEL && id != P_RMODEL && id != P_MUTE && id != P_DUCK;
}
static int ml_fx(uint32_t code) { return code >= ML_A_FX && code < ML_A_FX + PF_N; }
/* a button action (pressed and let go: a CC or a note); a macro takes a CC's value */
static int ml_btn(uint32_t code) { return (code >= ML_A_PLAY && code <= ML_A_PREV) || ml_fx(code); }
static int ml_act_ok(uint32_t code) { return ml_btn(code) || (code >= ML_A_MAC && code < ML_A_MAC + 4u); }
/* a code the map may hold: a parameter on track k, or an action (k 0) */
static int ml_code_ok(uint32_t k, uint32_t code)
{
    return code > P_COUNT ? ml_act_ok(code) && !k : code && k < NTRK && ml_param_ok(code - 1u);
}
/* source s (num | src << 8) may be learned: a note any, a CC not 0 32 120..127 */
static int ml_src_ok(uint32_t s) { return (s >> 8) == ML_NOTE ? s < (ML_NOTE << 8 | 128u) : s < 128u && ml_free_cc(s); }

/* d is a knob the model has (not "-", no range: lfo_track's test) */
static int ml_knob_real(const param_desc_t *d) { return d->max > d->min && d->label && d->label[0] && d->label[0] != '-'; }

/* CC value v (0..127) -> lo..hi: a bipolar range (lo < 0 < hi) has its 0 at 64 */
static int32_t ml_lin(int32_t lo, int32_t hi, uint32_t v)
{
    if (lo < 0 && hi > 0)
        return v < 64u ? lo + ((int32_t)v * -lo + 32) / 64 : ((int32_t)(v - 64u) * hi + 31) / 63;
    return lo + ((int32_t)v * (hi - lo) + 63) / 127;
}
/* .. d's range; an enum steps over its shown order (DIV: enum_order) */
static int32_t ml_value(const param_desc_t *d, uint32_t v)
{
    const uint8_t *o = enum_order(d);
    return o ? o[((int32_t)v * (d->max - d->min) + 63) / 127] + d->min : ml_lin(d->min, d->max, v);
}

/* a learned CC sets parameter id of t (in the block: after lfo_apply, before the tracks render). The base gets it
 * (motion_slot: an LFO's held knob its saved value); the base as it is: nothing more (as upstream). An LFO on the
 * track, or the knob one of an LFO's own: the LFOs' copies put back first, applied again after (no advance), so
 * lfo_restore keeps the new value and an LFO on the knob modulates around it (or starts / stops at once); while
 * motion plays it is the knob's new base (as a turn) */
static __attribute__((noinline)) void ml_set(track_t *t, uint32_t id, uint32_t v)
{
    const param_desc_t *d = track_desc(t, id);
    uint32_t k = (uint32_t)(t - trk), lf;
    int32_t nv;
    if (!ml_knob_real(d))
        return;                                     /* the model has no such knob */
    if (id >= P_SLEN && id <= P_SSWING && chain_busy())
        return;                                     /* PATTERN while a song plays (or is armed): its rows' */
    if (id == P_SRC && chain_busy())
        return;
    nv = ml_value(d, v);
    if ((mo.base_ok >> k) & 1u)
        mo.base[k][id] = (int16_t)nv;
    if (*motion_slot(t, id) == nv)
        return;
    lf = song.lfo_in && (t->lon || lfo_own(id));    /* (lfo_track: lon, the LFOs on; an LFO's or a slot's knob) */
    if (lf)
        lfo_restore_track(t);
    t->p[id] = (int16_t)nv;
    if (PRE_SOUND(id))
        preset_touch(t);                            /* presets.c: the voice edited */
    if (lf)
        lfo_track(t, 0);
}

/* perf_mh: the PERFORM effects whose learned button is down (perform.c: perf_held = the keys' | these). The ISR, or
 * the UI with the interrupts off (an entry changed: its hold goes with it) */
static void ml_holds(void)
{
    uint32_t i, m = 0;
    for (i = 0; i < ML_N; i++)
        if (((ml_down >> i) & 1u) && ml_fx(ml_map[i].code))
            m |= PF_BIT(ml_map[i].code - ML_A_FX);
    perf_mh = m;
    perf_held = perf_kh | m;
}

/* entry i's button pressed / let go (a CC: edges only; a note: every note on is a press). A PERFORM effect is held
 * while it is down, as its key in the layer (the last pressed buffer effect plays: perf_ord); not while PERFORM's
 * effects are off (perf_kill: the menu, a dialog, NAME, the MUTE screen). PLAY REC NEXT / PREV: the press to the UI */
static __attribute__((noinline)) void ml_press(uint32_t i, uint32_t down, uint32_t note)
{
    uint32_t c = ml_map[i].code, bit = 1u << i;
    if (!note && down == ((ml_down >> i) & 1u))
        return;
    if (!ml_fx(c)) {
        ml_down = down ? ml_down | bit : ml_down & ~bit;
        if (down && (uint8_t)(ml_qw - ml_qr) < ML_QN) {
            ml_q[ml_qw % ML_QN] = (uint8_t)c;
            ml_qw++;
        }
        return;
    }
    if (down && perf_kill)
        return;
    ml_down = down ? ml_down | bit : ml_down & ~bit;
    if (down)
        perf_ord[c - ML_A_FX] = ++perf_seq;
    ml_holds();
}

/* seq.c midi_block, each block: PERFORM's effects off (perf_kill) end the effects held by MIDI: none comes back when
 * they are on again, until its button is pressed again */
static void learn_block(void)
{
    uint32_t i;
    if (!perf_kill || !perf_mh)
        return;
    for (i = 0; i < ML_N; i++)
        if (ml_fx(ml_map[i].code))
            ml_down &= ~(1u << i);
    ml_holds();
}

/* a source heard while something is picked: the UI's (ml_poll) */
static void ml_hear(uint32_t s)
{
    uint32_t h = ml_heard;
    if (!h || (!ml_src_ok(h - 1u) && ml_src_ok(s)))
        ml_heard = (uint16_t)(s + 1u);
}

/* seq.c midi_block: a CC (any channel) */
static __attribute__((noinline)) void learn_cc(uint32_t cc, uint32_t v)
{
    uint32_t i;
    if (ml_arm) {                                    /* something picked: the CC is the UI's (sets nothing) */
        ml_hear(cc);
        return;
    }
    if (!ml_free_cc(cc))
        return;
    for (i = 0; i < ML_N; i++) {
        const ml_ent_t *e = &ml_map[i];
        uint32_t c = e->code;
        if (!c || e->num != cc || e->src != ML_CC)
            continue;
        if (c <= P_COUNT) {
            if (e->trk < NTRK && ml_param_ok(c - 1u))
                ml_set(&trk[e->trk], c - 1u, v);
        } else if (c >= ML_A_MAC && c < ML_A_MAC + 4u) {
            perf_k[c - ML_A_MAC] = (int8_t)ml_lin(c == ML_A_MAC ? -100 : 0, 100, v);   /* FILTER: 0 at 64 */
        } else if (ml_btn(c)) {
            ml_press(i, v >= 64u, 0);
        }
    }
}

/* seq.c midi_block: a note on (on: velocity > 0) / off, any channel; 1 = MIDI LEARN's (no drum plays it) */
static __attribute__((noinline)) int learn_note(uint32_t n, uint32_t on)
{
    uint32_t i;
    if (ml_arm == 2u) {                              /* a button action picked: the note is the UI's */
        if (on)
            ml_hear(ML_NOTE << 8 | n);
        return 1;
    }
    for (i = 0; i < ML_N; i++) {
        const ml_ent_t *e = &ml_map[i];
        if (e->code && e->num == n && e->src == ML_NOTE) {
            if (ml_btn(e->code))
                ml_press(i, on, 1);
            return 1;
        }
    }
    return 0;
}
