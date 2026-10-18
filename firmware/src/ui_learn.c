/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE (GLO + G4, the track a parameter is learned on, CLEAR ALL's confirm, the
 * actions) */
/* MIDI LEARN, the UI's side (upstream Felucca 1.5's midi_learn.c, adapted; spec
 * docs/superpowers/specs/2026-10-13-midi-learn-design.md). GLO held (the MUTE screen) + G4 turns it on and off. On: a
 * knob turned that edits a per-track parameter picks it on the track it edits, as it edits it (ml_knob); the next CC
 * that comes (midi_learn.c ml_heard: any channel, USB, TRS, Bluetooth in MIDI mode) is set to it, on that track,
 * whichever track is selected later. The actions (spec §7): PLAY, REC, ALGORITHM right / left (NEXT / PREV TRACK), a
 * PERFORM layer key (FX held or PAGE: an effect, a track's mute) pick theirs instead of doing their job (ml_pick_act);
 * a PERFORM macro knob picks its macro as it edits it; the next CC or note (a macro: a CC) is bound to it. One source
 * sets one thing, a thing has one source: learning one again replaces both. OCT- clears the picked one's source
 * (nothing picked: CLEAR ALL LEARNED?), OCT+ is done. The footer's first row says what is picked and its source
 * (ml_line). The menu, NAME or another confirm end it; never stored. The map is
 * saved after each change (project.c learn_save: deferred while playing). Included by ui_input.c. */
#define ML_CONFIRM 6u                              /* ui.confirm: CLEAR ALL LEARNED? */
#define ML_NOSRC 0xFFFFu                           /* ml_drop: no source to match (a source is num | src << 8) */

static uint32_t ml_count(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < ML_N; i++)
        n += ml_map[i].code != 0u;
    return n;
}
/* the source of code on track k (an action: k 0), + 1 (num | src << 8); 0 none */
static uint32_t ml_src_of(uint32_t k, uint32_t code)
{
    uint32_t i;
    for (i = 0; i < ML_N; i++)
        if (ml_map[i].code == code && ml_map[i].trk == k)
            return (uint32_t)(ml_map[i].num | ml_map[i].src << 8) + 1u;
    return 0;
}
static uint32_t ml_cc_of(uint32_t k, uint32_t id) { return ml_src_of(k, id + 1u); }   /* parameter id's CC + 1 */
/* entry i := source s -> code of track k (code 0: empty). The ISR reads the entries: never half of one; a button down
 * on the old entry is up (a PERFORM effect it held ends) */
static void ml_put(uint32_t i, uint32_t s, uint32_t k, uint32_t code)
{
    ml_ent_t e = {(uint8_t)s, (uint8_t)k, (uint8_t)code, (uint8_t)(s >> 8)};
    if (!code)
        e.num = e.trk = e.src = 0;
    fm1_irq_off();
    ml_map[i] = e;
    ml_down &= ~(1u << i);
    ml_holds();
    fm1_irq_on();
}
/* takes away source s's entry and code of track k's (either: ML_NOSRC / 0 for none); 1 = one went */
static int ml_drop(uint32_t s, uint32_t k, uint32_t code)
{
    uint32_t i, gone = 0;
    for (i = 0; i < ML_N; i++) {
        const ml_ent_t *e = &ml_map[i];
        if (e->code && ((uint32_t)(e->num | e->src << 8) == s || (e->code == code && e->trk == k))) {
            ml_put(i, 0, 0, 0);
            gone = 1;
        }
    }
    return (int)gone;
}
/* source s -> code of track k, in place of what either had; 1 done, 0 no room (ML_N others), 2 a source or a code
 * that is never learned (a note: a button action only) */
static uint32_t ml_bind(uint32_t s, uint32_t k, uint32_t code)
{
    uint32_t i;
    if (!ml_src_ok(s) || !ml_code_ok(k, code) || ((s >> 8) == ML_NOTE && !ml_btn(code)))
        return 2;
    for (i = 0; i < ML_N; i++)                       /* room: a free entry, or one this learn replaces */
        if (!ml_map[i].code || (uint32_t)(ml_map[i].num | ml_map[i].src << 8) == s ||
            (ml_map[i].code == code && ml_map[i].trk == k))
            break;
    if (i == ML_N)
        return 0;
    (void)ml_drop(s, k, code);
    for (i = 0; i < ML_N; i++)
        if (!ml_map[i].code) {
            ml_put(i, s, k, code);
            break;
        }
    return 1;
}
static uint32_t ml_learn(uint32_t cc, uint32_t k, uint32_t id) { return ml_bind(cc, k, id + 1u); }   /* CC -> P_* id */
static int ml_clear_all(void)                      /* everything forgotten, saved; 1 = there was */
{
    uint32_t i, n = ml_count();
    for (i = 0; i < ML_N; i++)
        ml_put(i, 0, 0, 0);
    learn_save();
    return n != 0u;
}

static void ml_unpick(void)
{
    ui.ml.pick = ui.ml.act = 0;
    ml_arm = 0;
    ml_heard = 0;
}
static void ml_off(void)                          /* (its CLEAR ALL LEARNED? goes with it: never a YES after) */
{
    ui.ml.on = 0;
    ml_keys = 0;
    ml_unpick();
    if (ui.confirm == ML_CONFIRM) {
        ui.confirm = 0;
        ui.force = 1;
    }
}
/* the picked knob gone (a MODEL without it): nothing picked */
static void ml_pick_check(void)
{
    if (ui.ml.pick && !ui.ml.act && !ml_knob_real(track_desc(&trk[ui.ml.trk % NTRK], ui.ml.id)))
        ml_unpick();
}
static void ml_toggle(void)                        /* GLO + G4 */
{
    if (ui.ml.on) {
        ml_off();
        ui_message("LEARN DONE");
        return;
    }
    ml_off();
    ui.ml.on = 1;
    ml_keys = 1;
    ui_message("MIDI LEARN ON");
}
/* a knob turned parameter id of track k (ui_input.c, after its edit): picked while learning */
static void ml_knob(uint32_t k, uint32_t id)
{
    const param_desc_t *d;
    if (!ui.ml.on || k >= NTRK || id >= P_COUNT)
        return;
    if (!ml_param_ok(id)) {
        ml_pick_check();                             /* (MODEL: the picked knob may be gone) */
        ui_message("NOT LEARNABLE");                 /* (MODEL, RESON's MODEL) */
        return;
    }
    d = track_desc(&trk[k], id);
    if (!ml_knob_real(d))
        return;
    ui.ml.trk = (uint8_t)k;
    ui.ml.id = (uint8_t)id;
    ui.ml.act = 0;
    ui.ml.pick = 1;
    ml_heard = 0;
    ml_arm = 1;
}
/* an action's control used while learning (PLAY, REC, ALGORITHM, a PERFORM key or macro knob): it is picked; a button
 * action awaits a CC or a note, a macro a CC */
static void ml_pick_act(uint32_t code)
{
    if (!ui.ml.on || !ml_act_ok(code))
        return;
    ui.ml.trk = ui.ml.id = 0;
    ui.ml.act = (uint8_t)code;
    ui.ml.pick = 1;
    ml_heard = 0;
    ml_arm = ml_btn(code) ? 2u : 1u;
}
/* the picked one's name: "T3 DECAY", a section's knob with its section ("T3 L1.DEPTH", "T3 F.DECAY", "T3 R.TUNE",
 * "T3 S.RATE": the same labels are on other pages); an action "PLAY", "REC", "NEXT TRACK", "PREV TRACK", "FX R1/16",
 * "FX MUTE T3", "FX FILTER"; b: 16 bytes */
static const char *const ML_ACT_NAME[4] = {"PLAY", "REC", "NEXT TRACK", "PREV TRACK"};
static const char *const ML_MAC_NAME[4] = {"FX FILTER", "FX CRUSH", "FX THROW", "FX DEPTH"};
static void ml_name(char *b)
{
    uint32_t id = ui.ml.id, k = ui.ml.trk % NTRK, c = ui.ml.act;
    const char *pre = id >= P_LFO1 && id < P_LFO2 ? "L1." : id >= P_LFO2 && id < P_LFO2 + LF_N ? "L2."
                    : id >= P_RMODEL && id <= P_RPOS ? "R." : id >= P_FTYPE && id <= P_FDEC ? "F."
                    : id >= P_SLCR && id <= P_SLDEPTH ? "S." : "";
    if (c) {
        if (c <= ML_A_PREV) {
            str_cpy(b, ML_ACT_NAME[c - ML_A_PLAY], 16);
        } else if (c >= ML_A_MAC) {
            str_cpy(b, ML_MAC_NAME[(c - ML_A_MAC) & 3u], 16);
        } else if (c - ML_A_FX < PF_M1) {
            str_cpy(b, "FX ", 16);
            str_cpy(b + 3, PF_NAME[c - ML_A_FX], 8);
        } else {
            str_cpy(b, "FX MUTE T1", 16);
            b[9] = (char)('1' + (c - ML_A_FX - PF_M1) % NTRK);
        }
        return;
    }
    b[0] = 'T';
    b[1] = (char)('1' + k);
    b[2] = ' ';
    str_cpy(b + 3, pre, 4);
    str_cpy(b + str_len(b), track_desc(&trk[k], id)->label, 8);
}
/* source s (num | src << 8) as "CC74" / "NOTE36" at b (8 bytes) */
static void ml_src_name(char *b, uint32_t s)
{
    str_cpy(b, (s >> 8) == ML_NOTE ? "NOTE" : "CC", 8);
    fmt_int(b + str_len(b), (int32_t)(s & 127u));
}
/* the top bar: "CC74 -> T3 DECAY", "NOTE36 -> PLAY" (noinline: its buffers are not on the stack under learn_save) */
static __attribute__((noinline)) void ml_say_bound(uint32_t s)
{
    char a[24], b[16];
    ml_src_name(a, s);
    str_cpy(a + str_len(a), " -> ", 5);
    ml_name(b);
    ui_say(a, b);
}
/* every frame: a source heard for the picked one is learned (the picked knob still there); a PERFORM key pressed
 * while learning picks its effect (seq.c keyboard_block: ml_kb_hit) */
static void ml_poll(void)
{
    uint32_t s, r, hits, k, code;
    ml_keys = ui.ml.on;
    if (ml_kb_hit) {
        fm1_irq_off();
        hits = ml_kb_hit;
        ml_kb_hit = 0;
        fm1_irq_on();
        for (k = 0; ui.ml.on && k < 27u; k++)       /* (the lowest key with an effect) */
            if (((hits >> k) & 1u) && perf_key(k) < PF_N) {
                ml_pick_act(ML_A_FX + perf_key(k));
                break;
            }
    }
    ml_pick_check();                                  /* (a MODEL changed otherwise: a load, a song's row) */
    s = ml_heard;
    if (!s)
        return;
    ml_heard = 0;
    if (!ui.ml.on || !ui.ml.pick)
        return;
    s--;
    code = ui.ml.act ? ui.ml.act : ui.ml.id + 1u;
    r = ml_bind(s, ui.ml.trk, code);
    if (r != 1u) {
        ui_message(r ? "CC NOT LEARNABLE" : "LEARN FULL");
        return;
    }
    ml_say_bound(s);
    ml_unpick();
    learn_save();                                     /* (deferred while playing) */
}
/* every frame: the learned buttons pressed since (midi_learn.c ml_q): bit 0 PLAY, bit 1 REC; *dt NEXT - PREV */
static uint32_t ml_drain(int32_t *dt)
{
    uint32_t r = 0, c;
    *dt = 0;
    while (ml_qr != ml_qw) {
        c = ml_q[ml_qr % ML_QN];
        ml_qr++;
        if (c == ML_A_PLAY)
            r |= 1u;
        else if (c == ML_A_REC)
            r |= 2u;
        else
            *dt += c == ML_A_NEXT ? 1 : -1;
    }
    return r;
}
/* OCT- / OCT+ while learning: OCT- clears the picked one's source (nothing picked: CLEAR ALL LEARNED?), OCT+ done */
static __attribute__((noinline)) void ml_oct(uint32_t up)   /* (noinline: its buffer off ui_input's frame) */
{
    char b[16];
    if (up) {
        ml_off();
        ui_message("LEARN DONE");
        return;
    }
    if (!ui.ml.pick) {
        if (!ml_count()) {
            ui_message("NOTHING LEARNED");
            return;
        }
        ui.confirm = ML_CONFIRM;
        ui.force = 1;
        return;
    }
    if (!ml_drop(ML_NOSRC, ui.ml.trk, ui.ml.act ? ui.ml.act : ui.ml.id + 1u)) {
        ui_message(ui.ml.act ? "NOTHING TO CLEAR" : "NO CC TO CLEAR");
        return;
    }
    ml_name(b);
    ui_say(b, ui.ml.act ? " CLEARED" : " CC CLEARED");
    learn_save();
}
/* the footer's first row while learning: "LEARN: TURN OR PRESS", "T3 DECAY: SEND A CC", "T3 DECAY = CC74", "PLAY:
 * SEND CC/NOTE", "PLAY = NOTE36"; b: 32 bytes */
static void ml_line(char *b)
{
    uint32_t s;
    if (!ui.ml.pick) {
        str_cpy(b, "LEARN: TURN OR PRESS", 32);
        return;
    }
    ml_name(b);
    s = ml_src_of(ui.ml.trk, ui.ml.act ? ui.ml.act : ui.ml.id + 1u);
    if (!s) {
        str_cpy(b + str_len(b), ui.ml.act && ml_btn(ui.ml.act) ? ": SEND CC/NOTE" : ": SEND A CC", 16);
        return;
    }
    str_cpy(b + str_len(b), " = ", 4);
    ml_src_name(b + str_len(b), s - 1u);
}
/* LEARN ends when the menu, NAME, the PRESETS list or a confirm other than its own opens */
static void ml_guard(void)
{
    if (ui.ml.on && (ui.menu || name_on() || pl_on() || (ui.confirm && ui.confirm != ML_CONFIRM)))
        ml_off();
}
