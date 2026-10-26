/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Drum sequencer and input. 8 tracks x 64 steps (hit / accent / PROB / RATCH), per-track length, division and swing.
 * The 8 white keys F3..F4 hit tracks 1..8; MIDI notes on the drum channel hit every track whose NOTE
 * matches; an armed track records hits into the nearest step while playing. Runs in the audio ISR
 * before each block (events_block). Grids (grids.c) runs on its own 1/32 clock and plays the tracks whose SRC
 * is one of its channels. KEYS (seq_mode 3): the keys play the selected track chromatically (keys_on). */
static volatile uint8_t transport_req;   /* 1 start, 2 stop (from the UI) */
static volatile uint8_t fill_on;         /* REC held off SEQ (ui_input.c rec_fill): FILL steps play, !FILL ones not */
static volatile uint8_t panic_req;       /* bit per track: cut its voices */
static uint32_t kb_prev;
static volatile uint32_t mute_mask;   /* main: GLO's bit while the MUTE screen may take keys (ui_input.c glo_input), SAVE's
                                       * while the cue map may (cue_input), 0 = none; armed before the button is pressed
                                       * (a key struck with it is the screen's) */
static volatile uint32_t kb_mute;     /* ISR: the keys held that are the MUTE screen's (pressed with GLO) */
static volatile uint32_t kb_mute_hit; /* ISR -> main: those presses, until the UI takes them */
static volatile uint8_t ml_keys;      /* main: MIDI LEARN is on (ui_learn.c): a PERFORM layer key picks its effect */
static volatile uint32_t ml_kb_hit;   /* ISR -> main: those presses (no effect played), until the UI takes them */
/* key index (0 = F3, the lowest key) of the white keys F3 G3 A3 B3 C4 D4 E4 F4 -> tracks 1..8 */
static const uint8_t KEY_TRK_KEY[NTRK] = {0, 2, 4, 6, 7, 9, 11, 12};
#define KEY_LEARN 14u                    /* G4, the first white key above F4: MIDI LEARN on the MUTE screen (ui_learn.c) */
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
    return period + (uint32_t)(((cnt ^ t->seq_par) & 1u) ? -sw : sw);
}

/* Grids' clock: one Grids step per 1/32, counted on the 1/16 grid exactly as a 1/16 step track (no track swing)
 * counts its steps: the global swing makes every other 1/16 long; the even 1/32 plays at the 1/16, the odd one at
 * its half. So a tempo change keeps Grids on the step tracks' 1/16s, and no Grids step is ever skipped (the chaos
 * sequence stays the original's). */
#define MUTE_LEAD 256u   /* samples ahead of the bar: the COMP source mute fade (5 ms) and a declick end there */
static struct { uint32_t pos, cnt; uint8_t half, rem, at; } gclk;   /* samples into the 1/16, 1/16s since PLAY, odd 1/32
                                                                      * done; at: a 1/16 started this block (perform.c) */
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
        gclk.pos = start ? (song.g[G_CLOCK] ? gclk.pos - 0x7FFFFFFFu : 0) : gclk.pos - l16;   /* (as seq_tick) */
        gclk.cnt++;
        gclk.half = 0;
        gclk.at = 1;
        if (!(gclk.cnt & 3u)) {                     /* METRO (fx.c metro_block): a quarter, the bar's first accented */
            metro.due = gclk.pos + 1u;
            metro.adv = n;
            metro.bar = !(gclk.cnt & 15u);
        }
        grids_fire();                               /* the even 1/32, on the 1/16 */
    }
}

/* perform.c: 1 when a 1/16 of the Grids clock started in this block (read once a block, then cleared) */
static uint32_t seq_16th(void)
{
    uint32_t a = gclk.at;
    gclk.at = 0;
    return a;
}

/* PROB of step s coming up on track t (LEN len): 100 % plays; a percentage plays when the track's next random
 * number is below it; A/B when loop mod B = A - 1 and 1-SHOT on loop 0, loop = steps since PLAY / LEN; FILL while a
 * fill is held, !FILL while none is */
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
    if (c == COND_FILL || c == COND_NFILL)            /* (spec 2026-10-26) */
        return (c == COND_FILL) == (fill_on != 0u);
    cond_ab(c, &a, &b);
    return loop % b == a - 1u;
}

static const step_t *seq_steps(const track_t *t);   /* song.c: a song's row, else the track's own */

/* ACID's GATE knob: 10..100 % of len (samples) */
static uint32_t acid_gate_len(const track_t *t, uint32_t len)
{
    return len / 100u * (10u + (uint32_t)clamp(t->p[P_E0 + 7], 0, 127) * 90u / 127u);
}

/* drum_core.c drum_hit: a hit no step drives (MIDI, the keys, Grids) of a gated model closes its gate after GATE of
 * the track's step at the tempo now, as a step's note does (ACID_LIFE stays the backstop) */
static uint32_t hit_gate_len(const track_t *t)
{
    return acid_gate_len(t, div_period((uint32_t)t->p[P_SDIV], 0));
}

/* the gate of the voice the track's model started closes now (its release); gate_n 0 */
static void acid_gate_off(track_t *t)
{
    const dmodel_t *m = &DMODELS[t->model % NMODELS];
    t->gate_n = 0;
    if (m->gate && t->v[0].active)
        m->gate(t, &t->v[0], 0);
}

/* STOP, PLAY again, a song's row change: every gated track's note ends, nothing slides into the next step */
static __attribute__((noinline)) void acid_gates_close(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        acid_gate_off(&trk[i]);
        trk[i].slide_in = 0;
    }
}

/* the step after the playing one (LEN wraps) */
static const step_t *seq_next(const track_t *t)
{
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u;
    return &seq_steps(t)[(t->seq_idx + 1u) % len];
}

/* the gate of the ACID note just started (hit rat_k of the step's rat_n): GATE of a hit's length, held into the next
 * step (GATE_HELD) by the step's last hit when the step slides or the next is a TIE */
static __attribute__((noinline)) void acid_gate_set(track_t *t, const step_t *next)
{
    if (t->rat_k >= t->rat_n && (t->slide_in || next->tie))
        t->gate_n = GATE_HELD;
    else
        t->gate_n = acid_gate_len(t, t->rat_len / t->rat_n);
}

/* a gated model's step (spec §3): TIE holds the sounding note, its gate open through the whole step (the next step
 * decides: a rest closes it, an ON step restarts or glides, a TIE goes on); a slid-into ON step glides, any other ON
 * step restarts; the gate stays open into the next step when this one slides or the next is a TIE. Following Grids,
 * the steps play nothing (its hits close their own gates, drum_hit): a gate a step held closes. */
static __attribute__((noinline)) void acid_step(track_t *t, const step_t *s, const step_t *next, uint32_t len_s)
{
    const dmodel_t *m = trk_model(t);
    dvoice_t *v = &t->v[0];
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, held;
    int plays;
    model_follow(t);                                   /* a model changed this block: not its old voice's gate */
    held = v->active && (v->x[2] & 1);
    t->rat_n = 0;
    if (t->p[P_SRC]) {
        if (t->gate_n == GATE_HELD)
            acid_gate_off(t);
        t->slide_in = 0;
        return;
    }
    plays = s->on && !s->tie && step_plays(t, s, len);
    if (s->tie) {                                      /* PROB, RATCH, ACC ignored; after a rest: nothing; a live
                                                        * hit's gate (drum_hit) is not the sequencer's: not extended */
        if (!held || t->gate_n != GATE_HELD)
            return;
        t->slide_in = s->slide;                        /* a TIE with SLIDE glides into the next ON step */
        t->gate_n = GATE_HELD;
        return;
    }
    if (!plays) {                                      /* a rest, or PROB skipped it: a gate the sequencer holds
                                                        * closes; a counting one (a live hit's) ends by itself */
        if (t->gate_n == GATE_HELD)
            acid_gate_off(t);
        t->slide_in = 0;
        return;
    }
    t->rat_vel = (uint8_t)(s->acc ? 127u : 96u);
    if (held && t->slide_in) {
        t->nt = (int8_t)clamp(s->nt, NT_MIN, NT_MAX);
        v->vel = t->rat_vel;                           /* glide reads it: the slid-to step's accent */
        m->glide(t, v);
    } else {
        drum_play(t, t->rat_vel, s->nt);
    }
    t->rat_nt = t->nt;                                 /* the roll's note (rat_due) */
    t->slide_in = s->slide;
    t->rat_n = (uint8_t)(s->rat < 3u ? s->rat + 1u : 4u);
    t->rat_k = 1;
    t->rat_len = len_s;
    acid_gate_set(t, next);
}

/* the step that came up: decided once (step_plays), then RATCH hits spread over its length len_s, the first
 * now (rat_due plays the others). A track following Grids plays nothing from its steps; a drum's TIE step nothing. */
static void step_fire(track_t *t, uint32_t len_s)
{
    const step_t *s = &seq_steps(t)[t->seq_idx];
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u;
    if (trk_model(t)->gate) {
        acid_step(t, s, seq_next(t), len_s);
        return;
    }
    t->rat_n = 0;
    if (t->p[P_SRC] || !s->on || s->tie || !step_plays(t, s, len))
        return;
    t->rat_vel = (uint8_t)(s->acc ? 127u : 96u);
    drum_play(t, t->rat_vel, s->nt);
    t->rat_nt = t->nt;
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
        drum_play(t, t->rat_vel, t->rat_nt);
        t->rat_k++;
        if (trk_model(t)->gate)
            acid_gate_set(t, seq_next(t));          /* ACID: each hit its gate, the last one held when it slides */
    }
}

/* live recording: into the nearest step, as swung (the playing one, or the next one past its middle); returns the step */
static uint32_t rec_hit(track_t *t, uint32_t vel)
{
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, idx = t->seq_idx % len;
    uint32_t period = div_period((uint32_t)t->p[P_SDIV], t->seq_rem);
    if (t->seq_pos > step_samples(t, period, t->seq_cnt) / 2u) {
        idx = (idx + 1u) % len;
        t->rskip = 1;                               /* it sounds now: that step must not hit again */
        t->rskip_idx = (uint8_t)idx;
    }
    t->step[idx].on = 1;
    t->step[idx].tie = 0;                           /* (a TIE is read first: the hit would never play) */
    t->step[idx].cond = 0;                          /* a recorded step always plays, once */
    t->step[idx].rat = 0;
    if (!trk_model(t)->gate)
        t->step[idx].nt = 0;                        /* a drum's hit sounds at C4 (drum_hit): no NOTE left on the step
                                                     * (KEYS record writes its own after, keys_rec) */
    if (vel > 110u)
        t->step[idx].acc = 1;
    return idx;
}

static void input_hit(track_t *t, uint32_t vel)
{
    if (((song.rec >> trk_index(t)) & 1u) && song.playing && !t->p[P_SRC])
        rec_hit(t, vel);
    drum_hit(t, vel);
}

/* KEYS (spec §5; ui.c keys_mode, song.seq_mode 3): the 27 keys a chromatic keyboard of the selected track, F3 = NOTE 53
 * at octave 0 (song.octave -2..+2). kz: the key sounding + 1 (0 none) and its track; live record (Task 2): rec 1 while
 * the key's note is being written, last the step it wrote last, first the step its phrase started on (the TIEs stop
 * there: a note held longer than LEN does not erase itself) */
static struct { uint8_t key, trk, rec, last, first; } kz;

static int32_t keys_nt(uint32_t key) { return (int32_t)key - 7 + 12 * song.octave; }   /* NOTE - 60 */

/* the sounding key let go (or a key on another track took over): the note it holds on a gated model ends; a gate the
 * sequencer took over since (counting, or held) is the sequencer's: left alone */
static __attribute__((noinline)) void keys_off(void)
{
    track_t *t = &trk[kz.trk % NTRK];
    if (DMODELS[t->model % NMODELS].gate && t->gate_n == 0u)
        acid_gate_off(t);
    kz.key = 0;
    kz.rec = 0;
}

/* KEYS live record (spec §6): the key's note into the nearest step (rec_hit): ON + NOTE; a gated model's key goes on
 * recording while held (TIE, keys_tie), and a key pressed over it (legato) puts SLIDE on its last step. A drum: ON +
 * NOTE only */
static __attribute__((noinline)) void keys_rec(track_t *t, int32_t nt, int legato)
{
    uint32_t idx = rec_hit(t, 100u);
    step_t *s = &t->step[idx];
    int gated = DMODELS[(uint32_t)t->p[P_MODEL] % NMODELS].gate != 0;
    s->nt = (int8_t)clamp(nt, NT_MIN, NT_MAX);
    s->tie = 0;
    s->slide = 0;
    if (gated && legato && kz.rec && kz.last != idx)
        t->step[kz.last % NSTEP].slide = 1;
    if (!legato || !kz.rec)
        kz.first = (uint8_t)idx;                       /* a phrase starts here (a legato note goes on with it) */
    kz.rec = (uint8_t)gated;
    kz.last = (uint8_t)idx;
}

/* KEYS live record, every block while a gated model's key records: held past the middle of the step after the one it
 * wrote last: TIE there (the note goes on through it), a plain one (no PROB, RATCH, ACC, SLIDE). Disarmed, stopped,
 * round the loop to the phrase's start (LEN 1 too), or LEN cut under that start (its note out of the loop: a TIE would
 * hold another note): the recording ends */
static __attribute__((noinline)) void keys_tie(void)
{
    track_t *t = &trk[kz.trk % NTRK];
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, nx = (kz.last + 1u) % len;
    step_t *s;
    if (!song.playing || !((song.rec >> kz.trk) & 1u) || nx == kz.first || kz.first >= len) {
        kz.rec = 0;
        return;
    }
    if (t->seq_idx % len != nx || t->seq_pos >= 0x7FFFFFFFu ||
        t->seq_pos <= step_samples(t, div_period((uint32_t)t->p[P_SDIV], t->seq_rem), t->seq_cnt) / 2u)
        return;
    s = &t->step[nx];
    s->on = 0;
    s->tie = 1;
    s->slide = 0;
    s->acc = 0;
    s->cond = 0;
    s->rat = 0;
    s->nt = t->step[kz.last % NSTEP].nt;
    kz.last = (uint8_t)nx;
}

/* key pressed: the selected track plays its note at 100 (as the drum keys), no MIDI out. A gated model (ACID) is mono:
 * a key pressed while the last one still sounds slides to it (no restart), and the key holds the gate (gate_n 0: no
 * countdown; the sequencer's rests and TIEs leave it; keys_off closes it; ACID_LIFE stays the backstop). A drum: a hit
 * transposed by the note (drum_play, as a step's) */
static __attribute__((noinline)) void keys_on(uint32_t key)
{
    track_t *t = TSEL;
    const dmodel_t *m = trk_model(t);
    int32_t nt = clamp(keys_nt(key), NT_MIN, NT_MAX);
    int legato = kz.key && kz.trk == song.sel;
    if (kz.key && !legato)
        keys_off();                                    /* mono over the keys: another track's held note ends */
    if (((song.rec >> song.sel) & 1u) && song.playing && !t->p[P_SRC])
        keys_rec(t, nt, legato);                       /* live record (spec §6) */
    else
        kz.rec = 0;
    if (m->gate) {
        dvoice_t *v = &t->v[0];
        model_follow(t);
        if (legato && v->active && (v->x[2] & 1) && t->gate_n == 0u) {
            t->nt = (int8_t)nt;
            v->vel = 100;                              /* glide reads it: no accent */
            m->glide(t, v);
        } else {
            drum_play(t, 100u, nt);
        }
        t->gate_n = 0;
        t->slide_in = 0;
    } else {
        drum_play(t, 100u, nt);
    }
    t->key_nt = (int8_t)nt;
    kz.key = (uint8_t)(key + 1u);
    kz.trk = song.sel;
}

static void keyboard_block(void)
{
    uint32_t cur = fm1_in.notes, ch = cur ^ kb_prev, i, key;
    kb_prev = cur;
    for (key = 0; key < 27u; key++) {                     /* PERFORM (perform.c): a key pressed with FX held is the layer's
                                                     * until let go: its effect, no note, no MIDI, nothing recorded */
        uint32_t bit = 1u << key;
        if (!(ch & bit))
            continue;
        if (cur & bit) {
            if ((fm1_in.buttons & perf_mask) || (perf_mask & PERF_PAGE)) {
                kb_layer |= bit;
                if (ml_keys)
                    ml_kb_hit |= bit;                   /* MIDI LEARN: the key picks its effect (ui_learn.c), plays
                                                         * none (its release: perf_press of an effect not held) */
                else
                    perf_press(perf_key(key), 1);
                ch &= ~bit;
            } else if (fm1_in.buttons & mute_mask) {   /* GLO held: the MUTE screen's until let go (ui_input.c
                                                         * glo_input takes kb_mute_hit): no note, no MIDI */
                kb_mute |= bit;
                kb_mute_hit |= bit;
                ch &= ~bit;
            }
        } else if (kb_layer & bit) {
            kb_layer &= ~bit;
            perf_press(perf_key(key), 0);
            ch &= ~bit;
        } else if (kb_mute & bit) {
            kb_mute &= ~bit;
            ch &= ~bit;
        }
    }
    if (kz.key && ((ch >> (kz.key - 1u)) & 1u) && !((cur >> (kz.key - 1u)) & 1u))
        keys_off();                                 /* KEYS: the sounding key let go, whoever has the keys now */
    if (kz.rec)
        keys_tie();                                 /* KEYS live record: the held note's TIEs (before this block's
                                                     * presses: a legato key in the block that passes a middle finds
                                                     * its TIE, the SLIDE goes there) */
    if (song.seq_mode == 3u) {                      /* KEYS: the presses play the selected track */
        for (key = 0; key < 27u; key++)
            if (((ch & cur) >> key) & 1u)
                keys_on(key);
        ch &= ~cur;                                 /* (releases still send their note-off, as on the grid) */
    } else if (song.seq_mode == 1u) {
        ch &= ~cur;                                 /* the STEP grid / TRACKS / COMP ducks (ui_input.c) own presses; releases
                                                     * still send their note-off (no hung notes). TRACKS while armed
                                                     * (seq_mode 2): the keys play and record */
    }
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

static void midi_clock_pulse(uint32_t ms);                   /* midi_clock.c */
static void midi_clock_transport(uint32_t status, uint32_t ms);
static void learn_cc(uint32_t cc, uint32_t v);                /* midi_learn.c: MIDI LEARN */
static int learn_note(uint32_t n, uint32_t on);
static void learn_block(void);

static void midi_block(void)
{
    learn_block();                                  /* (MIDI LEARN: PERFORM off ends the effects MIDI holds) */
    if (midi_in_overflow) {                         /* (upstream 1.0, usb_app.c) the ring overflowed: the stream is
                                                     * broken, drop the backlog */
        midi_drop();
        return;
    }
    while (mi_r != mi_w) {
        uint32_t at = mi_r % MQ, pkt = midi_in_q[at], st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 0x0Fu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, i, status = (pkt >> 8) & 0xFFu;
        mi_r++;
        if (status >= 0xF8u) {                      /* realtime: the chosen clock source's (CLK USB / TRS) */
            uint32_t src = midi_in_source[at] ? midi_in_source[at] : 1u;
            if (song.g[G_CLOCK] && (uint32_t)song.g[G_CLOCK] == src) {
                if (status == 0xF8u)
                    midi_clock_pulse(midi_in_ms[at]);
                else
                    midi_clock_transport(status, midi_in_ms[at]);
            }
            continue;
        }
        if (st == 0xB0u) {                          /* a CC, any channel: MIDI LEARN's (midi_learn.c) */
            learn_cc(d1, d2);
            continue;
        }
        if ((st == 0x90u || st == 0x80u) && learn_note(d1, st == 0x90u && d2))
            continue;                               /* a learned note (any channel): its action, no drum */
        if (st != 0x90u || !d2 || ch != drum_ch())
            continue;                               /* note-offs: one-shots ignore them */
        for (i = 0; i < NTRK; i++)
            if ((uint32_t)trk[i].p[P_NOTE] == d1)
                input_hit(&trk[i], d2);
    }
}

#include "lockbank.c"               /* the lock bank: motion events and parameter locks, sorted (spec 2026-10-24) */
#include "varpack.c"                /* the variations 2..8 of a slot, packed (spec 2026-10-26 cue / variations) */
#include "motion.c"                 /* knob moves per step (upstream 1.0's motion recording) */
static void seq_stop(void);
#include "song.c"                   /* SONG: rows of the four slots (upstream 1.0's song chain) */
#include "cue.c"                    /* the live cue of slots and variations (spec 2026-10-26) */
#include "midi_learn.c"             /* MIDI LEARN: a learned CC sets its parameter, a CC / note its action (upstream
                                     * 1.5's, adapted) */

static void seq_start(void)
{
    uint32_t i;
    acid_gates_close();                             /* (PLAY while playing: nothing slides into step 0) */
    for (i = 0; i < NTRK; i++) {                    /* every track from its step 0, together */
        track_t *t = &trk[i];
        t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
        t->seq_pos = 0x7FFFFFFF;                    /* step 0 fires on the first block */
        t->seq_cnt = 0xFFFFFFFFu;                   /* step 0 is count 0 */
        t->seq_rem = 0;
        t->seq_par = 0;
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
    motion_begin();
    cue_start();                                    /* cue.c: the grid's time from PLAY */
    chain_start();                                  /* song.c: an armed song from row 1 */
}

static void seq_stop(void)
{
    song.playing = 0;
    acid_gates_close();                             /* ACID: no note drones on */
    motion_end();                                   /* motion.c: the patch back */
    chain_stop();                                   /* song.c: the timing and REC back */
    cue_stop();                                     /* cue.c: a queued cue dropped */
}

static void seq_tick(track_t *t, uint32_t n)
{
    uint32_t div = (uint32_t)t->p[P_SDIV], len = (uint32_t)t->p[P_SLEN];
    if (t->gate_n && t->gate_n != GATE_HELD) {        /* ACID: the gate closes this block (stopped too: a hit's) */
        if (t->gate_n <= n)
            acid_gate_off(t);
        else
            t->gate_n -= n;
    }
    if (!song.playing)
        return;
    t->seq_pos += n;
    for (;;) {
        uint32_t cur_len = step_samples(t, div_period(div, t->seq_rem), t->seq_cnt);
        rat_due(t, cur_len);
        if (t->seq_pos < cur_len && t->seq_pos != 0x7FFFFFFFu + n)
            break;
        if (t->seq_pos >= 0x7FFFFFFFu) {            /* PLAY: step 0 (its remainder 0); following a clock, the advance
                                                     * into it counts (it began on the pulse), else the steps sit that
                                                     * far behind the clock's grid for good */
            t->seq_pos = song.g[G_CLOCK] || chain.cut ? t->seq_pos - 0x7FFFFFFFu : 0;   /* (a song's row change: the
                                                     * block's rest after it, as the clock's) */
        } else {
            t->seq_pos -= cur_len;
            t->seq_rem = (uint8_t)div_rem_next(div, t->seq_rem);
        }
        t->seq_cnt++;                               /* the step: steps since PLAY mod LEN, so a LEN change keeps
                                                     * the track on the shared clock (and LEN back = in sync) */
        t->seq_idx = (uint16_t)(t->seq_cnt % (len ? len : 1u));
        motion_step(t, t->seq_idx);                 /* motion.c: its knobs at this step (before the hit) */
        if (t->rskip && t->rskip_idx == t->seq_idx) {
            t->rskip = 0;
            t->rat_n = 0;
        } else {
            step_fire(t, step_samples(t, div_period(div, t->seq_rem), t->seq_cnt));
        }
    }
}

#include "midi_clock.c"

/* TRACKS' waiting mutes (song.mute_q), before the bar's first steps: flipped in the block whose sequencer advance adv
 * crosses the bar line (the 1/16 grids_tick counts as the bar's first), at once when stopped; returns the tracks to
 * cut. The COMP source plays on muted (ghost key): it changes lead samples ahead of the bar (its 5 ms fade ends before
 * the bar's first hit). Own clock: adv = the block (known before the block's input), lead MUTE_LEAD. Following a clock
 * the advance is midi_clock_advance's, known only after the block's Clock packets are read: it stalls 1/8 pulse short
 * of each pulse and jumps over the bar line when the bar's pulse arrives, so the block's length predicts nothing (the
 * bar was never seen: the mutes waited forever); lead = a pulse: the source mutes on the bar's last pulse before it */
static __attribute__((noinline)) uint32_t mute_bar(uint32_t adv, uint32_t lead)
{
    uint32_t i, pr = 0, src = comp_src(), gs = comp_ghost_src(), sb = gs < NTRK ? 1u << gs : 0u, l16;
    int bar;
    if (!song.mute_q)
        return 0;
    l16 = grids_l16(gclk.cnt);
    bar = song.playing && ((gclk.cnt + 1u) & 15u) == 0u;
    if (bar && (song.mute_q & sb) && gclk.pos + adv + lead >= l16) {
        if (!trk[src].p[P_MUTE]) {
            trk[src].p[P_MUTE] = 1;                 /* mute: its 5 ms fade (fx.c) ends before the bar's first hit */
            song.mute_q &= (uint8_t)~sb;
        } else {
            pr |= sb;                               /* unmute: its silent voices end (declick) before the bar */
        }
    }
    if (!song.playing || (bar && gclk.pos + adv >= l16)) {
        for (i = 0; i < NTRK; i++)
            if ((song.mute_q >> i) & 1u) {
                trk[i].p[P_MUTE] = (int16_t)!trk[i].p[P_MUTE];
                if (trk[i].p[P_MUTE])
                    pr |= 1u << i;
                else if (i == src && song.playing && (!song.solo || ((song.solo >> i) & 1u)))
                    trk[i].gfade = 0;               /* heard at once: the bar's first hit with its attack (unless
                                                     * another track's SOLO silences it: trk_silent, inline here) */
            }
        song.mute_q = 0;
    }
    return pr;
}

/* cue.c (spec 2026-10-26), every block before the steps: the mix gain of a slot switch's fade; playing (run), no
 * song: a queued switch's boundary in this block -> the switch (its fade's lead: CUE_LEAD, following a clock a pulse at
 * least). noinline: the audio ISR's loops stay as they were (target_budget.py) */
static __attribute__((noinline)) void cue_block(uint32_t n, uint32_t adv, uint32_t run)
{
    cue_fade_tick(n);
    if (run && !chain.running)
        cue_tick(n, adv, song.g[G_CLOCK] && midi_clock.pulse_samples > CUE_LEAD ? midi_clock.pulse_samples : CUE_LEAD);
}

/* everything that happens between two rendered blocks */
static void events_block(uint32_t n)
{
    uint32_t i, pr, seq_n = n, run, clock_mode = (uint32_t)song.g[G_CLOCK];
    if (midi_clock.mode != clock_mode) {             /* CLK changed (knob or project): stop, a clean clock state */
        seq_stop();
        memset(&midi_clock, 0, sizeof midi_clock);
        midi_clock.mode = (uint8_t)clock_mode;
        midi_beat_samples = 0;
    }
    if (transport_req == 1u) {
        if (clock_mode)
            midi_clock_transport(0xFAu, fm1_ms);     /* PLAY while following: a Start, the steps wait for pulses */
        else
            seq_start();
        transport_req = 0;
    } else if (transport_req == 2u) {
        seq_stop();
        transport_req = 0;
    }
    pr = panic_req;
    panic_req = 0;
    if (!clock_mode)                                /* TRACKS' waiting mutes: the own clock's advance is the block */
        pr |= mute_bar(n, MUTE_LEAD);
    for (i = 0; i < NTRK; i++)
        if ((pr >> i) & 1u)
            drum_cut(&trk[i]);
    keyboard_block();
    midi_block();
    if (clock_mode) {                                /* following: the steps move with the clock (after its packets */
        seq_n = song.playing ? 0u : n;               /* of this block are read, as upstream: a tempo change rescales */
        if (song.playing) {                          /* first, then this block's advance is in the new units) */
            uint32_t last = midi_clock.have_pulse ? midi_clock.last_ms : midi_clock.start_ms;
#if USB8_OK
            if (fm1_ms - last > 500u && (clock_mode != 1u || (int32_t)(fm1_ms - usb_hold_until) >= 0)) {
#else
            if (fm1_ms - last > 500u) {
#endif
                seq_stop();                          /* the clock lost (a cable out): stop, nothing left running (CLK
                                                      * USB: not while MENU > USB AUDIO reconnects, ui_menu.c) */
                midi_clock.tempo_valid = 0;
            } else {
                seq_n = midi_clock_advance(fm1_ms);
            }
        }
        pr = mute_bar(seq_n, midi_clock.pulse_samples > MUTE_LEAD ? midi_clock.pulse_samples : MUTE_LEAD);
        for (i = 0; pr && i < NTRK; i++)            /* (after the block's keys and MIDI: a live hit in the bar's block
                                                     * counts as one just before the bar) */
            if ((pr >> i) & 1u)
                drum_cut(&trk[i]);
    }
    run = !clock_mode || midi_clock.have_pulse || !song.playing;   /* following: step 0 / the resumed step on the first
                                                     * pulse; stopped, seq_tick counts ACID's gates in real time */
    cue_block(n, seq_n, run);                        /* cue.c: this block's fade; a queued switch's boundary */
    if (run) {
        if (chain.running)
            chain_tick(seq_n);                       /* song.c: a row ends in this block -> the next one */
        for (i = 0; i < NTRK; i++)
            seq_tick(&trk[i], seq_n);
        grids_tick(seq_n);
        chain.cut = 0;
    }
    if (song.playing)
        song.tick++;
}
