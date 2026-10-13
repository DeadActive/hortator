/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE (GLO + G4, the track a parameter is learned on, CLEAR ALL's confirm) */
/* MIDI LEARN, the UI's side (upstream Felucca 1.5's midi_learn.c, adapted; spec
 * docs/superpowers/specs/2026-10-13-midi-learn-design.md). GLO held (the MUTE screen) + G4 turns it on and off. On: a
 * knob turned that edits a per-track parameter picks it on the track it edits, as it edits it (ml_knob); the next CC
 * that comes (midi_learn.c ml_heard: any channel, USB, TRS, Bluetooth in MIDI mode) is set to it, on that track,
 * whichever track is selected later. One CC sets one parameter, a parameter has one CC: learning one again replaces
 * both. OCT- clears the picked parameter's CC (nothing picked: CLEAR ALL LEARNED?), OCT+ is done. The footer's first
 * row says what is picked and its CC (ml_line). The menu, NAME or another confirm end it; never stored. The map is
 * saved after each change (project.c learn_save: deferred while playing). Included by ui_input.c. */
#define ML_CONFIRM 6u                              /* ui.confirm: CLEAR ALL LEARNED? */
#define ML_NOCC 0x80u                              /* ml_drop: no CC to match (a CC is 0..127) */

static uint32_t ml_count(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < ML_N; i++)
        n += ml_map[i].code != 0u;
    return n;
}
/* the CC of parameter id on track k, + 1; 0 none */
static uint32_t ml_cc_of(uint32_t k, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < ML_N; i++)
        if (ml_map[i].code == id + 1u && ml_map[i].trk == k)
            return ml_map[i].cc + 1u;
    return 0;
}
static void ml_put(uint32_t i, uint32_t cc, uint32_t k, uint32_t id)   /* (the ISR reads the entries: never half of one) */
{
    ml_ent_t e = {(uint8_t)cc, (uint8_t)k, (uint8_t)(id + 1u), 0};
    if (id >= P_COUNT)
        e.cc = e.trk = e.code = 0;
    fm1_irq_off();
    ml_map[i] = e;
    fm1_irq_on();
}
/* takes away cc's entry and parameter id of track k's (either: ML_NOCC / P_COUNT for none); 1 = one went */
static int ml_drop(uint32_t cc, uint32_t k, uint32_t id)
{
    uint32_t i, gone = 0;
    for (i = 0; i < ML_N; i++) {
        const ml_ent_t *e = &ml_map[i];
        if (e->code && (e->cc == cc || (e->code == id + 1u && e->trk == k))) {
            ml_put(i, 0, 0, P_COUNT);
            gone = 1;
        }
    }
    return (int)gone;
}
/* cc -> parameter id of track k, in place of what either had; 1 done, 0 no room (ML_N others), 2 a CC or a parameter
 * that is never learned */
static uint32_t ml_learn(uint32_t cc, uint32_t k, uint32_t id)
{
    uint32_t i;
    if (!ml_free_cc(cc) || k >= NTRK || !ml_param_ok(id))
        return 2;
    for (i = 0; i < ML_N; i++)                       /* room: a free entry, or one this learn replaces */
        if (!ml_map[i].code || ml_map[i].cc == cc || (ml_map[i].code == id + 1u && ml_map[i].trk == k))
            break;
    if (i == ML_N)
        return 0;
    (void)ml_drop(cc, k, id);
    for (i = 0; i < ML_N; i++)
        if (!ml_map[i].code) {
            ml_put(i, cc, k, id);
            break;
        }
    return 1;
}
static int ml_clear_all(void)                      /* every CC forgotten, saved; 1 = there were */
{
    uint32_t i, n = ml_count();
    for (i = 0; i < ML_N; i++)
        ml_put(i, 0, 0, P_COUNT);
    learn_save();
    return n != 0u;
}

static void ml_off(void)                          /* (its CLEAR ALL LEARNED? goes with it: never a YES after) */
{
    ui.ml.on = ui.ml.pick = 0;
    ml_arm = 0;
    ml_heard = 0;
    if (ui.confirm == ML_CONFIRM) {
        ui.confirm = 0;
        ui.force = 1;
    }
}
/* the picked knob gone (a MODEL without it): nothing picked */
static void ml_pick_check(void)
{
    if (ui.ml.pick && !ml_knob_real(track_desc(&trk[ui.ml.trk % NTRK], ui.ml.id))) {
        ui.ml.pick = 0;
        ml_arm = 0;
        ml_heard = 0;
    }
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
    ui.ml.pick = 1;
    ml_heard = 0;
    ml_arm = 1;
}
/* the picked parameter's name: "T3 DECAY", a section's knob with its section ("T3 L1.DEPTH", "T3 F.DECAY", "T3 R.TUNE",
 * "T3 S.RATE": the same labels are on other pages); b: 16 bytes */
static void ml_name(char *b)
{
    uint32_t id = ui.ml.id, k = ui.ml.trk % NTRK;
    const char *pre = id >= P_LFO1 && id < P_LFO2 ? "L1." : id >= P_LFO2 && id < P_LFO2 + LF_N ? "L2."
                    : id >= P_RMODEL && id <= P_RPOS ? "R." : id >= P_FTYPE && id <= P_FDEC ? "F."
                    : id >= P_SLCR && id <= P_SLDEPTH ? "S." : "";
    b[0] = 'T';
    b[1] = (char)('1' + k);
    b[2] = ' ';
    str_cpy(b + 3, pre, 4);
    str_cpy(b + str_len(b), track_desc(&trk[k], id)->label, 8);
}
/* every frame: a CC heard for the picked parameter is learned (the picked knob still there) */
static void ml_poll(void)
{
    uint32_t cc, r;
    char a[16], b[16];
    ml_pick_check();                                  /* (a MODEL changed otherwise: a load, a song's row) */
    cc = ml_heard;
    if (!cc)
        return;
    ml_heard = 0;
    if (!ui.ml.on || !ui.ml.pick)
        return;
    cc--;
    r = ml_learn(cc, ui.ml.trk, ui.ml.id);
    if (r != 1u) {
        ui_message(r ? "CC NOT LEARNABLE" : "LEARN FULL");
        return;
    }
    str_cpy(a, "CC", sizeof a);
    fmt_int(a + 2, (int32_t)cc);
    str_cpy(a + str_len(a), " -> ", 5);
    ml_name(b);
    ui_say(a, b);
    ui.ml.pick = 0;
    ml_arm = 0;
    learn_save();                                     /* (deferred while playing) */
}
/* OCT- / OCT+ while learning: OCT- clears the picked parameter's CC (nothing picked: CLEAR ALL LEARNED?), OCT+ done */
static void ml_oct(uint32_t up)
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
    if (!ml_drop(ML_NOCC, ui.ml.trk, ui.ml.id)) {
        ui_message("NO CC TO CLEAR");
        return;
    }
    ml_name(b);
    ui_say(b, " CC CLEARED");
    learn_save();
}
/* the footer's first row while learning: "LEARN: TURN A KNOB", "T3 DECAY: SEND A CC", "T3 DECAY = CC74"; b: 32 bytes */
static void ml_line(char *b)
{
    uint32_t cc;
    if (!ui.ml.pick) {
        str_cpy(b, "LEARN: TURN A KNOB", 32);
        return;
    }
    ml_name(b);
    cc = ml_cc_of(ui.ml.trk, ui.ml.id);
    if (!cc) {
        str_cpy(b + str_len(b), ": SEND A CC", 12);
        return;
    }
    str_cpy(b + str_len(b), " = CC", 6);
    fmt_int(b + str_len(b), (int32_t)cc - 1);
}
/* LEARN ends when the menu, NAME or a confirm other than its own opens */
static void ml_guard(void)
{
    if (ui.ml.on && (ui.menu || name_on() || (ui.confirm && ui.confirm != ML_CONFIRM)))
        ml_off();
}
