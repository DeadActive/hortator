/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The step tools (spec docs/superpowers/specs/2026-10-31-microtiming-quantize-copytools-design.md §3). SEQ held
 * TL_HOLD_MS, or SEQ + a key at once: the TOOLS screen while SEQ is held, as GLO's MUTE screen; a SEQ tap still turns
 * the SEQ pages, on its release (it opened them on the press before). Every key pressed with SEQ is the screen's until
 * let go (seq.c keyboard_block, mute_mask: no note, no MIDI, nothing recorded). Pressed in the menu, a confirm, NAME,
 * the PRESETS list, the MUTE screen or the cue map: dead until let go; with the PERFORM layer up or FX held: a plain
 * button (the release turns the pages). SEQ held at power-on is the safe start, as before (ui.c safe_start_check).
 * On the selected track (DUPLICATE: every track): the white keys the bank's 16 steps (OCT- / OCT+ the bank, as on
 * STEP), the black keys the tools (TL_K_*). A copied step is its whole step_t (on, accent, PROB, ratchet, note, slide /
 * tie, the micro in its spare byte) and its parameter locks (the lock bank's lock events at that step; motion events
 * stay where they are); a pasted step replaces the target's, its locks too; LOCKS FULL: as many as fit, and the
 * message. The buffers live in .bss and are never stored (a clipboard across projects); the edits are RAM edits as any
 * (SAVE keeps them, LOAD asks DISCARD EDITS?). A song playing: no edits (STOP TO EDIT), copies are fine */
#define TL_HOLD_MS 400u
#define TL_DOWN 1u
#define TL_OPEN 2u
#define TL_DEAD 4u
#define TL_PLAIN 8u
#define TL_LK 192u                  /* the clipboard's lock events (3 B each: spec §3) */
#define TL_SLK 48u                  /* the step buffer's: every recordable knob (motion.c motion_param: 44) */
/* the tools' black keys (key index from F3) */
#define TL_K_CPSTEP 1u              /* F#3 COPY STEP: held + a step */
#define TL_K_PSTEP 3u               /* G#3 PASTE STEP: held + steps */
#define TL_K_CPPAGE 5u              /* A#3 COPY PAGE */
#define TL_K_REPL 8u                /* C#4 PASTE REPLACE */
#define TL_K_MERGE 10u              /* D#4 PASTE MERGE */
#define TL_K_FILL 13u               /* F#4 PASTE FILL */
#define TL_K_CPSEL 15u              /* G#4 COPY SEL: the selection page; again: copy */
#define TL_K_PSEL 17u               /* A#4 PASTE SEL: then a step */
#define TL_K_DUP 20u                /* C#5 DUPLICATE */
#define TL_K_CLEAR 22u              /* D#5 CLEAR: "CLEAR TRACK n?", as REC held on a SEQ page */
typedef struct {
    step_t st;                      /* the step buffer (COPY STEP) */
    uint8_t st_ok, st_n, st_from;   /* .. holds a step; its locks; the step it came from */
    uint8_t st_id[TL_SLK];          /* .. their knobs and values */
    int8_t st_v[TL_SLK];
    uint8_t cn, c0;                 /* the clipboard: its steps (0: empty); the place its first came from */
    uint16_t lkn;                   /* .. its locks */
    step_t cs[NSTEP];
    uint16_t lk[TL_LK];             /* .. step (from its first) << 7 | knob, ascending */
    int8_t lv[TL_LK];
    uint8_t sel, s0, s1;            /* COPY SEL: the selection page up; START, END (steps 0..63) */
    uint8_t arm;                    /* PASTE SEL: waiting for a step */
    uint8_t full;                   /* a paste found the lock bank full */
} tl_t;
/* ~950 B: the standard build's .bss; the Bluetooth build has no .bss room to spare for it: the RAM-code region's
 * spare (app_ble.ld .ram_bss, after .ram_text's PC-guard window, below 0x01C06000, as the variations' banks and the
 * INSERTs' state), which crt0 does not zero: project.c persist_boot does (all zero: nothing copied, no page up) */
#if defined(FELUCCA_BLE) && FELUCCA_BLE
static tl_t tl __attribute__((section(".ram_bss"), aligned(4)));
#else
static tl_t tl;
#endif

static int32_t accel(uint32_t role, int32_t s, int32_t range);   /* (ui_input.c) */
static int cue_owns_keys(void);
static void play_press(void);

static uint32_t tl_len(const track_t *t) { return (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP); }
/* m <- "w a-b" (steps from 1; one step: "w a"), m 24 B */
static void tl_range(char *m, const char *w, uint32_t a, uint32_t b)
{
    uint32_t n;
    str_cpy(m, w, 24u);
    n = str_len(m);
    fmt_int(m + n, (int32_t)a + 1);
    if (b != a) {
        n = str_len(m);
        m[n] = '-';
        fmt_int(m + n + 1u, (int32_t)b + 1);
    }
}
static void tl_say(const char *w, uint32_t a, uint32_t b)
{
    char m[24];
    tl_range(m, w, a, b);
    ui_message(m);
}

/* ---- the edits (main loop; each lock-bank write with the audio IRQ off, as motion.c's) */
static int tl_has_locks(uint32_t k, uint32_t s)   /* track k's step s has a lock */
{
    uint32_t i = lk_seek(&mo.b, k, s);
    for (; i < mo.b.count && (uint32_t)(mo.b.key[i] >> 7) == (k << 6 | s); i++)
        if (lk_is_lock(&mo.b, i))
            return 1;
    return 0;
}
/* track k's locks at step s -> id / v (the first max); returns how many it has */
static uint32_t tl_locks_of(uint32_t k, uint32_t s, uint8_t *id, int8_t *v, uint32_t max)
{
    uint32_t i = lk_seek(&mo.b, k, s), n = 0;
    for (; i < mo.b.count && (uint32_t)(mo.b.key[i] >> 7) == (k << 6 | s); i++)
        if (lk_is_lock(&mo.b, i)) {
            if (n < max) {
                id[n] = (uint8_t)(mo.b.key[i] & 127u);
                v[n] = mo.b.val[i];
            }
            n++;
        }
    return n;
}
static void tl_step_in(uint32_t k, uint32_t s, const step_t *st)   /* the step whole, the target's locks gone */
{
    if (tl_has_locks(k, s))
        lock_clear_step(k, s, 0);
    trk[k].step[s] = *st;
}
static void tl_lock_in(uint32_t k, uint32_t s, uint32_t id, int32_t v)
{
    fm1_irq_off();
    if (lk_put(&mo.b, lk_key(k, s, id), v, 1) < 0)
        tl.full = 1;                                /* the bank full: this one refused (LOCKS FULL) */
    fm1_irq_on();
}
static void tl_clip_in(uint32_t k, uint32_t s, uint32_t j)   /* the clipboard's step j -> track k's step s */
{
    uint32_t i;
    tl_step_in(k, s, &tl.cs[j]);
    for (i = 0; i < tl.lkn; i++)
        if ((uint32_t)(tl.lk[i] >> 7) == j)
            tl_lock_in(k, s, tl.lk[i] & 127u, tl.lv[i]);
}
/* the clipboard <- n steps of track k from step a, their locks with them; 1: more than TL_LK locks (the rest cut) */
static int tl_copy(uint32_t k, uint32_t a, uint32_t n)
{
    uint32_t j, i, cut = 0;
    tl.cn = (uint8_t)n;
    tl.c0 = (uint8_t)a;
    tl.lkn = 0;
    for (j = 0; j < n; j++) {
        uint32_t s = a + j;
        tl.cs[j] = trk[k].step[s];
        for (i = lk_seek(&mo.b, k, s); i < mo.b.count && (uint32_t)(mo.b.key[i] >> 7) == (k << 6 | s); i++)
            if (lk_is_lock(&mo.b, i)) {
                if (tl.lkn < TL_LK) {
                    tl.lk[tl.lkn] = (uint16_t)(j << 7 | (mo.b.key[i] & 127u));
                    tl.lv[tl.lkn++] = mo.b.val[i];
                } else {
                    cut = 1;
                }
            }
    }
    return (int)cut;
}
static void tl_copied(uint32_t a, uint32_t n, int cut)
{
    if (cut)
        ui_message("COPIED, LOCKS CUT");
    else
        tl_say("COPIED ", a, a + n - 1u);
}
/* a paste's message: LOCKS FULL when locks were refused, else w a-b */
static void tl_pasted(const char *w, uint32_t a, uint32_t b)
{
    if (tl.full)
        ui_message("LOCKS FULL");
    else
        tl_say(w, a, b);
    tl.full = 0;
}
static int tl_clip_ok(void)
{
    if (tl.cn)
        return 1;
    ui_message("CLIPBOARD EMPTY");
    return 0;
}

static void tl_copy_step(uint32_t s)              /* COPY STEP: step s of the selected track into the step buffer */
{
    char m[24];
    tl.st = TSEL->step[s];
    tl.st_n = (uint8_t)tl_locks_of(song.sel, s, tl.st_id, tl.st_v, TL_SLK);   /* (never more: every knob once) */
    tl.st_ok = 1;
    tl.st_from = (uint8_t)s;
    tl_range(m, "STEP ", s, s);
    str_cpy(m + str_len(m), " COPIED", sizeof m - str_len(m));
    ui_message(m);
}
static void tl_paste_step(uint32_t s)             /* PASTE STEP: the step buffer into step s */
{
    uint32_t i;
    if (song_lock())
        return;
    tl_step_in(song.sel, s, &tl.st);
    for (i = 0; i < tl.st_n; i++)
        tl_lock_in(song.sel, s, tl.st_id[i], tl.st_v[i]);
    tl_pasted("PASTED ", s, s);
}
/* PASTE REPLACE / MERGE (merge: the clipboard's set steps only): its first 16 steps over the bank, inside LEN */
static void tl_paste_bank(int merge)
{
    uint32_t a = ui.bank * 16u, len = tl_len(TSEL), j, n = tl.cn < 16u ? tl.cn : 16u;
    if (!tl_clip_ok() || song_lock())
        return;
    for (j = 0; j < n && a + j < len; j++)
        if (!merge || tl.cs[j].on)
            tl_clip_in(song.sel, a + j, j);
    tl_pasted(merge ? "MERGED " : "PASTED ", a, (a + 16u < len ? a + 16u : len) - 1u);
}
static void tl_fill(void)                         /* PASTE FILL: the clipboard over the pattern, again and again, to LEN */
{
    uint32_t len = tl_len(TSEL), s, j = 0;
    if (!tl_clip_ok() || song_lock())
        return;
    for (s = 0; s < len; s++) {
        tl_clip_in(song.sel, s, j);
        if (++j >= tl.cn)
            j = 0;
    }
    tl_pasted("FILLED ", 0, len - 1u);
}
static void tl_paste_sel(uint32_t s)              /* PASTE SEL: the clipboard from step s on, clipped at 64 */
{
    uint32_t j;
    if (song_lock())
        return;
    for (j = 0; j < tl.cn && s + j < NSTEP; j++)
        tl_clip_in(song.sel, s + j, j);
    tl_pasted("PASTED ", s, s + j - 1u);
}
/* DUPLICATE: every track's pattern doubles, LEN x 2 (64 at most): steps LEN.. = steps 0.., their locks too */
static void tl_duplicate(void)
{
    uint8_t id[TL_SLK];
    int8_t v[TL_SLK];
    uint32_t k, s, i, n;
    if (song_lock())
        return;
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        uint32_t len = tl_len(t), to = 2u * len > NSTEP ? NSTEP : 2u * len;
        for (s = len; s < to; s++) {
            n = tl_locks_of(k, s - len, id, v, TL_SLK);
            tl_step_in(k, s, &t->step[s - len]);
            for (i = 0; i < n && i < TL_SLK; i++)
                tl_lock_in(k, s, id[i], v[i]);
        }
        t->p[P_SLEN] = (int16_t)to;                 /* (after the steps: the new ones play copied) */
    }
    if (tl.full)
        ui_message("LOCKS FULL");
    else
        ui_message("DUPLICATED");
    tl.full = 0;
}

/* the keys the ISR gave the screen: the tools first, then the white keys (a step: COPY STEP / PASTE STEP held, or
 * PASTE SEL armed) */
static void tl_close(void);
static void tl_keys(uint32_t hits)
{
    uint32_t k, held = fm1_in.notes, len = tl_len(TSEL);
    if ((hits >> TL_K_PSTEP) & 1u && !tl.st_ok)
        ui_message("NO STEP COPIED");
    if ((hits >> TL_K_CPPAGE) & 1u) {
        uint32_t a = ui.bank * 16u, n = a < len ? (len - a < 16u ? len - a : 16u) : 0u;
        if (n)
            tl_copied(a, n, tl_copy(song.sel, a, n));
    }
    if ((hits >> TL_K_REPL) & 1u)
        tl_paste_bank(0);
    if ((hits >> TL_K_MERGE) & 1u)
        tl_paste_bank(1);
    if ((hits >> TL_K_FILL) & 1u)
        tl_fill();
    if ((hits >> TL_K_CPSEL) & 1u) {
        if (!tl.sel) {                              /* the selection page: START / END from the bank shown */
            uint32_t a = ui.bank * 16u;
            tl.sel = 1;
            tl.s0 = (uint8_t)a;
            tl.s1 = (uint8_t)(a + 16u < len ? a + 15u : len > a ? len - 1u : a);   /* (a Grids track's
                                                         * banks follow its lane: past LEN, START alone) */
        } else {                                    /* again: START..END copied */
            tl.sel = 0;
            tl_copied(tl.s0, tl.s1 - tl.s0 + 1u, tl_copy(song.sel, tl.s0, tl.s1 - tl.s0 + 1u));
        }
    }
    if ((hits >> TL_K_PSEL) & 1u) {
        if (tl.arm)
            tl.arm = 0;
        else if (tl_clip_ok()) {
            tl.arm = 1;
            ui_message("PICK A STEP");
        }
    }
    if ((hits >> TL_K_DUP) & 1u)
        tl_duplicate();
    if (((hits >> TL_K_CLEAR) & 1u) && !song_lock()) {   /* the dialog in the screen's place (ui_input.c: REC held) */
        ui.confirm = 1;
        ui.confirm_trk = song.sel;
        tl_close();
        return;
    }
    for (k = 0; k < 16u; k++) {
        uint32_t s = ui.bank * 16u + k;
        if (!((hits >> STEP_KEY[k]) & 1u) || s >= len)
            continue;                               /* (past LEN: no step, as the STEP grid) */
        if ((held >> TL_K_CPSTEP) & 1u) {
            tl_copy_step(s);
        } else if ((held >> TL_K_PSTEP) & 1u) {
            if (tl.st_ok)
                tl_paste_step(s);
        } else if (tl.arm) {
            tl_paste_sel(s);
            if (!((held >> TL_K_PSEL) & 1u))
                tl.arm = 0;                         /* (A#4 held: a step more, and more) */
        }
    }
}

/* ---- SEQ: tap / hold */
static int tools_dead(void) { return ui.menu || ui.confirm || name_on() || pl_on() || ui.mute || ui.cue_map; }
static int tools_owns_keys(void)                       /* SEQ held (GLO not): the keys pressed now are the screen's */
{
    return (((fm1_in.buttons >> panel.btn[B_SEQ]) & 1u) || ui.seq_t0) &&
           !((fm1_in.buttons >> panel.btn[B_GLO]) & 1u) && !(ui.seq_t0 & (TL_DEAD | TL_PLAIN)) && !tools_dead() &&
           !ui.layer && !ui.fx_t0;
}
/* a tool key: it can act (a paste: something to paste); it is held or armed now */
static int tl_tool_ok(uint32_t key)
{
    if (key == TL_K_PSTEP)
        return tl.st_ok != 0;
    if (key == TL_K_REPL || key == TL_K_MERGE || key == TL_K_FILL || key == TL_K_PSEL)
        return tl.cn != 0;
    return 1;
}
static int tl_tool_act(uint32_t key)
{
    if (key == TL_K_CPSTEP || key == TL_K_PSTEP)
        return (int)((fm1_in.notes >> key) & 1u);
    return (key == TL_K_PSEL && tl.arm) || (key == TL_K_CPSEL && tl.sel);
}
static void tl_close(void)
{
    tl.sel = 0;
    tl.arm = 0;
    if (ui.tools) {
        ui.tools = 0;                               /* let go: the screen before */
        ui.force = 1;
    }
}
static void tools_input(uint32_t now, uint32_t notes)
{
    uint32_t bit = 1u << panel.btn[B_SEQ], *t0 = &ui.seq_t0, hits = 0;
    if (kb_mute_hit && tools_owns_keys() && !cue_owns_keys()) {
        fm1_irq_off();
        hits = kb_mute_hit;
        kb_mute_hit = 0;
        fm1_irq_on();
    }
    if (fm1_in.buttons & bit) {
        if (!*t0)
            *t0 = (now & ~15u) | TL_DOWN;
        if (!(*t0 & TL_OPEN) && tools_dead())
            *t0 |= TL_DEAD;
        else if (!(*t0 & TL_OPEN) && (ui.layer || ui.fx_t0))
            *t0 |= TL_PLAIN;
        if (!(*t0 & (TL_OPEN | TL_DEAD | TL_PLAIN)) &&
            (notes || hits || now - (*t0 & ~15u) >= TL_HOLD_MS * 1000u * FM1_TICKS_PER_US)) {
            *t0 |= TL_OPEN;                         /* the TOOLS screen */
            ui.tools = 1;
            ui.force = 1;
            tl.sel = 0;
            tl.arm = 0;
            grid_drop_holds();                      /* a grid key held edits nothing under it */
        }
        if (hits && (*t0 & TL_OPEN))
            tl_keys(hits);
    } else if (*t0) {
        if (hits && !(*t0 & (TL_DEAD | TL_PLAIN))) {
            *t0 |= TL_OPEN;                         /* (keys struck as SEQ went: theirs; no tap) */
            if (ui.tools)
                tl_keys(hits);
        }
        if (!(*t0 & (TL_OPEN | TL_DEAD)) && !tools_dead())
            open_family(FAM_SEQ);                   /* a tap: the SEQ pages, as the button always did (on the press) */
        *t0 = 0;
        tl_close();
    }
    mute_mask = (mute_mask & ~bit) |
                ((*t0 & TL_OPEN) || (!(*t0 & (TL_DEAD | TL_PLAIN)) && !tools_dead() && !ui.layer && !ui.fx_t0) ? bit
                                                                                                                : 0u);
}
/* the screen owns the panel (ui_input): PLAY as ever; OCT- / OCT+ the bank; the selection page's KNOB 1 START,
 * KNOB 2 END; nothing else (a HOME / REC press under it is no tap or hold) */
static void tools_panel(uint32_t pressed)
{
    int32_t s, v;
    if ((pressed >> panel.btn[B_PLAY]) & 1u)
        play_press();
    if (ui.home_t0)
        ui.home_t0 |= 2u;
    if (ui.rec_t0)
        ui.rec_t0 |= 2u;
    bank_fix();
    if ((pressed >> panel.btn[B_OCTUP]) & 1u)
        bank_set((int32_t)ui.bank + 1);
    if ((pressed >> panel.btn[B_OCTDN]) & 1u)
        bank_set((int32_t)ui.bank - 1);
    if (tl.sel) {
        if ((s = panel_enc(EN_K1)) != 0) {
            v = clamp((int32_t)tl.s0 + accel(EN_K1, s, NSTEP), 0, NSTEP - 1);
            tl.s0 = (uint8_t)v;
            if (tl.s1 < tl.s0)
                tl.s1 = tl.s0;                      /* (START past END: END with it) */
        }
        if ((s = panel_enc(EN_K1 + 1u)) != 0) {
            v = clamp((int32_t)tl.s1 + accel(EN_K1 + 1u, s, NSTEP), 0, NSTEP - 1);
            tl.s1 = (uint8_t)v;
            if (tl.s0 > tl.s1)
                tl.s0 = tl.s1;
        }
    }
    enc_drop();
}

/* ---- the LEDs (ui_leds): the bank's steps on the white keys (the playhead inverted; the selection page: the
 * selection), the tools lit where they can act (the pastes with something copied), the one held or armed blinking */
static void tools_leds(uint8_t *nl)
{
    static const uint8_t TK[10] = {TL_K_CPSTEP, TL_K_PSTEP, TL_K_CPPAGE, TL_K_REPL, TL_K_MERGE, TL_K_FILL, TL_K_CPSEL,
                                   TL_K_PSEL, TL_K_DUP, TL_K_CLEAR};
    const track_t *t = TSEL;
    uint32_t k, len = tl_len(t), blink = (fm1_ticks() / (250u * 1000u * FM1_TICKS_PER_US)) & 1u;
    for (k = 0; k < 16u; k++) {
        uint32_t si = ui.bank * 16u + k;
        int on = tl.sel ? (si >= tl.s0 && si <= tl.s1) : (si < len && t->step[si].on);
        if (!tl.sel && song.playing && si == t->seq_idx % len)
            on = !on;
        led_put(nl, 14u + STEP_KEY[k], on);
    }
    for (k = 0; k < 10u; k++) {
        uint32_t key = TK[k];
        led_put(nl, 14u + key, tl_tool_act(key) ? (int)blink : tl_tool_ok(key));
    }
    led_put(nl, panel.btn[B_SEQ], 1);
}

/* ---- the screen (ui_draw): the top bar as everywhere; the bank's 16 steps (filled on, a notch accented, a dot under
 * a step with locks, dithered past LEN, the playhead underlined), the whole pattern's 64 small (the bank shown marked;
 * the selection page: the selection underlined), then the tools on their keys (the one held or armed inverted, a
 * paste with nothing to paste dithered), the clipboard and the step buffer, or the last message */
#define TL_Y 22u                                    /* the upper canvas: 240 x 124 at y 22 */
#define TL_Y2 146u                                  /* the lower one: 240 x 93 */
#define TL_H2 93u
static void tl_text_r(int32_t xr, int32_t y, const char *s, uint16_t c) { md_text(xr - md_text_w(&MD_F3, 2, s), y, &MD_F3, 2, s, c); }
static void draw_tools(void)
{
    static const struct { uint8_t key; const char *note, *name; } TOOL[10] = {
        {TL_K_CPSTEP, "F#3", "COPY STEP"}, {TL_K_PSTEP, "G#3", "PASTE STEP"}, {TL_K_CPPAGE, "A#3", "COPY PAGE"},
        {TL_K_REPL, "C#4", "REPLACE"}, {TL_K_MERGE, "D#4", "MERGE"}, {TL_K_FILL, "F#4", "FILL"},
        {TL_K_CPSEL, "G#4", "COPY SEL"}, {TL_K_PSEL, "A#4", "PASTE SEL"}, {TL_K_DUP, "C#5", "DUPLICATE"},
        {TL_K_CLEAR, "D#5", "CLEAR"},
    };
    const track_t *t = TSEL;
    uint32_t k, len = tl_len(t), a = ui.bank * 16u, lkm = lk_step_mask(&mo.b, song.sel, a), sig, ph = NSTEP;
    char b[24];
    if (song.playing)
        ph = t->seq_idx % len;
    sig = ((ui.bank * 67u + len) * 131u + song.sel) * 16777619u ^ lkm;
    for (k = 0; k < NSTEP; k++)
        sig = (sig ^ ((uint32_t)t->step[k].on | (uint32_t)t->step[k].acc << 1)) * 16777619u;
    sig = (sig ^ (tl.sel | tl.s0 << 1 | tl.s1 << 8 | tl.arm << 15 | (uint32_t)tl.cn << 16 | (uint32_t)tl.st_ok << 23 |
                  (uint32_t)tl.c0 << 24)) * 16777619u;
    sig = (sig ^ (tl.lkn | (uint32_t)tl.st_n << 9 | (uint32_t)tl.st_from << 26 | ph << 16 | (fm1_in.notes & 0x2A52Au) << 7)) * 16777619u;
    sig = (sig ^ (ui.msg_t ? str_hash(7u, ui.msg) : 0u)) * 16777619u;
    if (ui.force) {                                 /* the ground over the two canvases (y 16..21), the frame's foot */
        lcd_fill(1, H_HEAD, 238, TL_Y - H_HEAD, C_BG);
        lcd_fill(0, H_HEAD, 1, TL_Y - H_HEAD, C_INK);
        lcd_fill(239, H_HEAD, 1, TL_Y - H_HEAD, C_INK);
        lcd_fill(0, 239, 240, 1, C_INK);
    }
    draw_head();
    if (ui.msg_t)
        ui.msg_t--;
    if (ui.bpm_t)
        ui.bpm_t--;
    if (!ui.force && sig == ui.graph_sig)
        return;
    ui.graph_sig = sig;
    ui.force = 0;
    cv_begin_at(240, 124, TL_Y, C_BG);
    cv_oy = 0;
    cv_rect(0, 0, 1, 124, C_INK);
    cv_rect(239, 0, 1, 124, C_INK);
    tl_range(b, "STEPS ", a, a + 15u);                /* (the track: the top bar's T<n>) */
    md_text(6, 3, &MD_F3, 2, b, C_INK);
    str_cpy(b, "LEN ", sizeof b);
    fmt_int(b + 4, (int32_t)len);
    tl_text_r(234, 3, b, C_INK);
    for (k = 0; k < 16u; k++) {                     /* the bank: 16 cells, 12 x 24, every 14 px from x 8 */
        uint32_t si = a + k;
        int32_t x = 8 + (int32_t)k * 14, y = 18;
        const step_t *st = &t->step[si];
        if (si >= len) {
            cv_dither(x, y, 12, 24, C_INK);
        } else if (st->on) {
            cv_rect(x, y, 12, 24, C_INK);
            if (st->acc)
                cv_rect(x + 3, y + 3, 6, 4, C_BG);
        } else {
            cv_frame(x, y, 12, 24, C_INK);
        }
        if ((lkm >> k) & 1u)
            cv_rect(x + 4, y + 27, 4, 3, C_INK);
        if (tl.sel ? si >= tl.s0 && si <= tl.s1 : si == ph)
            cv_rect(tl.sel ? x - 1 : x, y + 31, tl.sel ? 14 : 12, 3, C_INK);   /* the selection, or the playhead */
        if (!(k & 3u)) {
            fmt_int(b, (int32_t)si + 1);
            md_text(x, y + 36, &MD_F3, 2, b, C_INK);
        }
    }
    for (k = 0; k < NSTEP; k++) {                   /* the pattern: 4 rows of 16, 12 x 4 cells, every 8 px from y 70 */
        int32_t x = 8 + (int32_t)(k % 16u) * 14, y = 70 + (int32_t)(k / 16u) * 8;
        if (tl.sel && k >= tl.s0 && k <= tl.s1) {    /* the selection: a solid band, its steps on holes in it */
            cv_rect(x - 1, y - 1, 14, 6, C_INK);
            if (k < len && t->step[k].on)
                cv_rect(x + 2, y + 1, 8, 2, C_BG);
        } else if (k >= len) {
            cv_hdot(x, x + 11, y + 3, C_INK);
        } else if (t->step[k].on) {
            cv_rect(x, y, 12, 4, C_INK);
        } else {
            cv_rect(x, y + 3, 12, 1, C_INK);
        }
    }
    cv_rect(2, 70 + (int32_t)ui.bank * 8, 3, 4, C_INK);   /* the bank shown */
    if (tl.sel) {                                   /* the selection page: START / END on KNOB 1 / 2 */
        str_cpy(b, "K1 START ", sizeof b);
        fmt_int(b + str_len(b), (int32_t)tl.s0 + 1);
        md_text(6, 108, &MD_F3, 2, b, C_INK);
        str_cpy(b, "K2 END ", sizeof b);
        fmt_int(b + str_len(b), (int32_t)tl.s1 + 1);
        tl_text_r(234, 108, b, C_INK);
    } else if (tl.arm) {
        md_text(6, 108, &MD_F3, 2, "PASTE SEL: PICK A STEP", C_INK);
    }
    cv_blit(0, TL_Y);
    cv_begin_at(240, TL_H2, TL_Y2, C_BG);
    cv_oy = 0;
    cv_rect(0, 0, 1, (int32_t)TL_H2, C_INK);
    cv_rect(239, 0, 1, (int32_t)TL_H2, C_INK);
    cv_hdot(1, 238, 0, C_INK);
    for (k = 0; k < 10u; k++) {                     /* the tools: two columns, five rows */
        uint32_t key = TOOL[k].key;
        int32_t x = k < 5u ? 5 : 125, y = 4 + (int32_t)(k % 5u) * 12, xe;
        uint16_t pen = tl_tool_act(key) ? C_BG : C_INK;
        xe = md_text(x, y, &MD_F3, 2, TOOL[k].note, C_INK) + 5;   /* (the key, then its tool) */
        if (pen == C_BG)
            cv_rect(xe - 2, y - 1, md_text_w(&MD_F3, 2, TOOL[k].name) + 4, 12, C_INK);
        md_text(xe, y, &MD_F3, 2, TOOL[k].name, pen);
        if (!tl_tool_ok(key))
            cv_dither(x, y, xe + md_text_w(&MD_F3, 2, TOOL[k].name) - x, 10, C_BG);
    }
    md_text(md_text(125, 4 + 4 * 12, &MD_F3, 2, "OCT", C_INK) + 5, 4 + 4 * 12, &MD_F3, 2, "BANK", C_INK);
    cv_hdot(1, 238, 64, C_INK);
    if (ui.msg_t) {                                 /* the last message (over the clipboard's line) */
        str_cpy(b, ui.msg, sizeof b);
    } else if (tl.cn) {                             /* the clipboard: where it came from, its locks */
        tl_range(b, "CLIP ", tl.c0, tl.c0 + tl.cn - 1u);
    } else {
        str_cpy(b, "CLIP EMPTY", sizeof b);
    }
    if (!ui.msg_t && tl.lkn) {
        str_cpy(b + str_len(b), " +", sizeof b - str_len(b));
        fmt_int(b + str_len(b), (int32_t)tl.lkn);
        str_cpy(b + str_len(b), tl.lkn == 1u ? " LOCK" : " LOCKS", sizeof b - str_len(b));
    }
    md_text(5, 68, &MD_F3, 2, b, C_INK);
    if (tl.st_ok) {                                 /* the step buffer */
        tl_range(b, "STEP ", tl.st_from, tl.st_from);
        if (tl.st_n) {
            str_cpy(b + str_len(b), " +", sizeof b - str_len(b));
            fmt_int(b + str_len(b), (int32_t)tl.st_n);
            str_cpy(b + str_len(b), tl.st_n == 1u ? " LOCK" : " LOCKS", sizeof b - str_len(b));
        }
    } else {
        str_cpy(b, "STEP EMPTY", sizeof b);
    }
    md_text(5, 80, &MD_F3, 2, b, C_INK);
    cv_blit(0, TL_Y2);
}
