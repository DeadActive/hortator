/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE (projects only, the fork's keys and screen) */
/* NAME (upstream Felucca 1.0's ui_name.c, adapted; included by ui_input.c): naming a project on the device.
 * SAVE (PROJECT, the second detent) opens it before anything is written, prefilled with the current name ("PROJECT A"
 * when there is none); the NAME knob renames the selected used slot. OCT+ writes (the transport stopped: else STOP
 * TO SAVE and the screen stays), OCT- or HOME cancels. The keys type and never sound, record, send MIDI or play
 * PERFORM (seq.c keyboard_block: song.seq_mode 1; fx_allowed). Upper case, at most 12:
 *   white keys (16, F3..G5)  ABC: AB CD EF GH IJK LM NO PQ RS TU VW XYZ 123 456 789 0-. ; a tap types the group's
 *                            first character, another tap of the same key within 0.8 s the next (cycling); another
 *                            key or 0.8 s keeps it. 123: 1 2 3 4 5 6 7 8 9 0 - . _ / # + , one tap each.
 *   black keys, by name      F# cursor left, G# SPACE, A# cursor right, C# DELETE (held: repeats, as the arrows),
 *                            D# ABC / 123.
 *   KNOB 1 the cursor; KNOB 2 the character at the cursor (NAME_SET; at the end: a new one).
 *   LEDs: every key that types or edits lit; the key being cycled blinks. Spaces at the ends are dropped. */
enum { NK_NONE, NK_SAVE, NK_RENAME };
#define NM_TAP_MS 800u
#define NM_REP_MS 450u
#define NM_RATE_MS 90u
#define NM_T(ms) ((ms) * 1000u * FM1_TICKS_PER_US)
enum { NB_LEFT, NB_SPACE, NB_RIGHT, NB_DEL, NB_MODE, NB_NONE };
static const char *const NM_ABC[16] = {"AB", "CD", "EF", "GH", "IJK", "LM", "NO", "PQ", "RS", "TU", "VW", "XYZ",
                                       "123", "456", "789", "0-."};
static const char NM_NUM[17] = "1234567890-._/#+";
static struct {
    uint8_t kind, slot;                /* NK_*; the slot written */
    uint8_t len, cur;                  /* the name's length; the cursor 0..len */
    uint8_t num;                       /* the white keys: 0 ABC, 1 123 */
    uint8_t key, tap;                  /* the white key (place + 1) being cycled, 0 none; its character's index */
    uint8_t rep;                       /* a held arrow / DELETE (key + 1), 0 none */
    uint32_t t, rep_t;                 /* ticks of the last tap; of the next repeat */
    uint32_t sig;                      /* what was drawn */
    char s[NAME_LEN + 1u];
} nm;

static int name_on(void) { return nm.kind != NK_NONE; }
static void name_close(void)
{
    nm.kind = NK_NONE;
    page_entered();                                /* the keys back to the page (seq_mode) */
}
/* the keys: key_black / key_place (perform.c): a white key's place 0..15 among the white keys from F3 */
static uint32_t nm_black(uint32_t k)
{
    switch ((k + 5u) % 12u) {
    case 6: return NB_LEFT;
    case 8: return NB_SPACE;
    case 10: return NB_RIGHT;
    case 1: return NB_DEL;
    case 3: return NB_MODE;
    default: return NB_NONE;
    }
}
static const char *nm_group(uint32_t p)
{
    static char one[2];
    if (!nm.num)
        return NM_ABC[p & 15u];
    one[0] = NM_NUM[p & 15u];
    one[1] = 0;
    return one;
}

static void name_open(uint32_t kind, uint32_t slot)
{
    char b[NAME_LEN + 1u];
    nm.kind = (uint8_t)kind;
    nm.slot = (uint8_t)(slot & 3u);
    nm.num = nm.key = nm.rep = 0;
    nm.sig = 0;
    if (kind == NK_RENAME)
        project_name(slot, b);
    else
        name_set(b, chain.name);
    if (!b[0]) {
        name_set(b, "PROJECT A");
        b[8] = (char)('A' + (slot & 3u));
    }
    name_set(nm.s, b);
    nm.len = nm.cur = (uint8_t)str_len(nm.s);
    song.seq_mode = 1;                             /* the keys' presses are NAME's (seq.c: no hits, no MIDI) */
    ui.msg_t = 0;                                  /* ("AGAIN: SAVE" done: the title shows) */
    ui.force = 1;
}
static void name_rename(void)                      /* PROJECT, the NAME knob */
{
    uint32_t k = (uint32_t)song.g[G_SLOT] - 1u;
    if (!project_used(k))
        ui_message("EMPTY SLOT");
    else if (transport_busy())
        ui_message("STOP TO SAVE");
    else
        name_open(NK_RENAME, k);
}

static void nm_commit(void)                        /* the letter being cycled is kept: the cursor past it */
{
    if (nm.key) {
        nm.key = 0;
        nm.cur++;
    }
}
static int nm_insert(char c)                       /* at the cursor (it stays on it); 0 = full */
{
    uint32_t i;
    if (nm.len >= NAME_LEN) {
        ui_message("NAME FULL");
        return 0;
    }
    for (i = nm.len; i > nm.cur; i--)
        nm.s[i] = nm.s[i - 1u];
    nm.s[nm.cur] = c;
    nm.s[++nm.len] = 0;
    return 1;
}
static void nm_white(uint32_t p, uint32_t now)
{
    const char *g = nm_group(p);
    uint32_t n = str_len(g);
    if (nm.key == p + 1u && now - nm.t < NM_T(NM_TAP_MS)) {   /* the same key again: its next character */
        nm.tap = (uint8_t)((nm.tap + 1u) % n);
        nm.s[nm.cur] = g[nm.tap];
        nm.t = now;
        return;
    }
    nm_commit();
    if (!nm_insert(g[0]))
        return;
    if (n > 1u) {
        nm.key = (uint8_t)(p + 1u);
        nm.tap = 0;
        nm.t = now;
    } else {
        nm.cur++;
    }
}
static void nm_do(uint32_t f)                      /* a black key's function */
{
    uint32_t i;
    nm_commit();
    switch (f) {
    case NB_LEFT:
        if (nm.cur)
            nm.cur--;
        break;
    case NB_RIGHT:
        if (nm.cur < nm.len)
            nm.cur++;
        break;
    case NB_SPACE:
        if (nm_insert(' '))
            nm.cur++;
        break;
    case NB_DEL:
        if (!nm.cur)
            break;
        for (i = --nm.cur; i < nm.len; i++)
            nm.s[i] = nm.s[i + 1u];
        nm.len--;
        break;
    case NB_MODE:
        nm.num ^= 1u;
        break;
    default:
        break;
    }
}
static void nm_knob(uint32_t k, int32_t s)         /* KNOB 1 the cursor, KNOB 2 the character there */
{
    int32_t i, n = (int32_t)sizeof NAME_SET - 1;
    nm_commit();
    if (k == 0u) {
        nm.cur = (uint8_t)clamp((int32_t)nm.cur + s, 0, nm.len);
        return;
    }
    if (nm.cur == nm.len && !nm_insert(' '))
        return;
    for (i = 0; i < n && NAME_SET[i] != nm.s[nm.cur]; i++)
        ;
    i = i < n ? i : 0;
    nm.s[nm.cur] = NAME_SET[((i + s) % n + n) % n];
}
static void name_ok(void)                          /* OCT+: written (ends trimmed); refused while playing */
{
    char b[NAME_LEN + 1u];
    uint32_t a = 0, z;
    nm_commit();
    for (z = nm.len; z && nm.s[z - 1u] == ' '; z--)
        ;
    while (a < z && nm.s[a] == ' ')
        a++;
    name_set(b, "");
    for (z -= a; z--;)
        b[z] = nm.s[a + z];
    if (transport_busy()) {
        ui_message("STOP TO SAVE");
        return;
    }
    if (nm.kind == NK_SAVE) {
        name_set(chain.name, b);
        project_save(nm.slot);
    } else {
        project_rename(nm.slot, b);
    }
    name_close();
}
/* one UI frame of NAME (ui_input.c): button edges, note edges, the HOME button's tap / hold */
static void name_input(uint32_t pressed, uint32_t notes, uint32_t now, uint32_t home)
{
    uint32_t k;
    int32_t s;
    song.seq_mode = 1;
    if (home) {                                    /* HOME: cancel */
        name_close();
        return;
    }
    if ((pressed >> panel.btn[B_PLAY]) & 1u)
        transport_req = song.playing || chain_busy() ? 2 : 1;
    if (nm.key && now - nm.t >= NM_T(NM_TAP_MS))
        nm_commit();
    for (k = 0; k < 27u; k++) {
        if (!((notes >> k) & 1u))
            continue;
        if (!key_black(k)) {
            if (key_place(k) < 16u)
                nm_white(key_place(k), now);
            continue;
        }
        nm_do(nm_black(k));
        if (nm_black(k) == NB_LEFT || nm_black(k) == NB_RIGHT || nm_black(k) == NB_DEL) {
            nm.rep = (uint8_t)(k + 1u);
            nm.rep_t = now + NM_T(NM_REP_MS);
        }
    }
    if (nm.rep && !((fm1_in.notes >> (nm.rep - 1u)) & 1u))
        nm.rep = 0;
    else if (nm.rep && (int32_t)(now - nm.rep_t) >= 0) {
        nm_do(nm_black(nm.rep - 1u));
        nm.rep_t = now + NM_T(NM_RATE_MS);
    }
    for (k = 0; k < 2u; k++)
        if ((s = panel_enc(EN_K1 + k)) != 0)
            nm_knob(k, s);
    enc_drop();
    if ((pressed >> panel.btn[B_OCTUP]) & 1u)
        name_ok();
    else if ((pressed >> panel.btn[B_OCTDN]) & 1u)
        name_close();
}
static uint32_t name_leds(uint32_t now)            /* the keys lit: every one that acts; the one cycling blinks */
{
    uint32_t k, m = 0, blink = ((now / NM_T(250u)) & 1u) == 0u;
    for (k = 0; k < 27u; k++) {
        uint32_t on = key_black(k) ? nm_black(k) != NB_NONE : key_place(k) < 16u && (nm.key != key_place(k) + 1u || blink);
        m |= on << k;
    }
    return m;
}

/* the screen: what is written, the name in large cells with the cursor, the cycling key's letters, the white keys'
 * groups, the black keys, OCT+ / OCT- */
static void draw_name(void)
{
    char b[40];
    uint32_t i, sig = 2166136261u;
    sig = str_hash(sig, nm.s) + nm.cur * 7u + nm.key * 131u + nm.tap * 977u + nm.num * 3u + nm.kind * 11u +
          (uint32_t)transport_busy() * 29u + (ui.msg_t ? str_hash(5u, ui.msg) : 0u);
    if (!ui.force && sig == nm.sig)
        return;
    nm.sig = sig;
    ui.force = 0;
    if (ui.msg_t)                                  /* (the title shows a message for a while) */
        ui.msg_t--;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    str_cpy(b, nm.kind == NK_SAVE ? "SAVE PROJECT A" : "RENAME PROJECT A", sizeof b);
    b[str_len(b) - 1u] = (char)('A' + nm.slot);
    draw_text_box(4, 6, 180, &FONT_S, ui.msg_t ? ui.msg : b, ui.msg_t ? C_HI : C_GRAY, 0);
    fmt_int(b, nm.len);
    str_cpy(b + str_len(b), "/12", 4);
    draw_text_box(184, 6, 52, &FONT_S, b, nm.len >= NAME_LEN ? C_WHITE : C_DIM, 0);
    cv_begin(240, 40, C_BLACK);
    for (i = 0; i <= NAME_LEN; i++) {
        int32_t x = 12 + 18 * (int32_t)i;
        char c[2] = {i < nm.len ? nm.s[i] : 0, 0};
        if (i < NAME_LEN)
            cv_rect(x + 2, 36, 14, 1, C_LINE);
        if (i == nm.cur)
            cv_rect(x, 37, 18, 3, nm.key ? C_AMB : C_WHITE);
        if (c[0] && c[0] != ' ')
            cv_text(x + 2, 4, &FONT_L, c, i == nm.cur && nm.key ? C_AMB : C_WHITE);
    }
    cv_blit(0, 34);
    if (nm.key) {
        const char *g = nm_group(nm.key - 1u);
        str_cpy(b, g, sizeof b);
        str_cpy(b + str_len(b), "  TAP AGAIN: NEXT", 20);
        draw_text_box(4, 84, 232, &FONT_S, b, C_AMB, 0);
    } else {
        draw_text_box(4, 84, 232, &FONT_S, nm.num ? "123: ONE TAP, ONE CHARACTER" : "ABC: TAP AGAIN, NEXT LETTER", C_GRAY, 0);
    }
    for (i = 0; i < 2u; i++) {                     /* the white keys: the left octave, then the right one */
        uint32_t p;
        b[0] = 0;
        for (p = i * 8u; p < i * 8u + 8u; p++) {
            str_cpy(b + str_len(b), nm_group(p), 6);
            if (p < i * 8u + 7u)
                str_cpy(b + str_len(b), " ", 2);
        }
        draw_text_box(0, 110 + 20 * (int32_t)i, 240, &FONT_S, b, C_HI, 1);
    }
    draw_text_box(0, 156, 240, &FONT_S, "F# LEFT   G# SPACE   A# RIGHT", C_GRAY, 1);
    draw_text_box(0, 174, 240, &FONT_S, nm.num ? "C# DELETE   D# ABC" : "C# DELETE   D# 123", C_GRAY, 1);
    draw_text_box(0, 200, 240, &FONT_S, "KNOB 1 MOVE   KNOB 2 CHAR", C_DIM, 1);
    draw_text_box(0, 218, 240, &FONT_S, nm.kind == NK_SAVE ? "OCT- CANCEL   OCT+ SAVE" : "OCT- CANCEL   OCT+ RENAME",
                  C_WHITE, 1);
}
