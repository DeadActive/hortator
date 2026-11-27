/* SPDX-License-Identifier: GPL-3.0-only */
/* The menu's HARDWARE CALIBRATION and RESET TO DEFAULT through the real firmware code. panel_setup is a busy-wait on
 * the millisecond count and the keys: the host's watchdog hook (ui_host.h) is called at every pass, so a script of
 * presses, releases and knob turns at given milliseconds drives it. */
#define GRIND_NATIVE 1
#undef NDEBUG
#include <assert.h>
#include "../ui_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

enum { PRESS, RELEASE, TURN, ANYKEY };
typedef struct { uint32_t at; int kind; uint32_t arg; } ev_t;
static ev_t script[128];
static uint32_t nscript, next_ev, now_ms;
static void hook(void)                              /* one pass of the firmware's wait loop = 1 ms */
{
    fm1_ms++;
    now_ms++;
    while (next_ev < nscript && script[next_ev].at <= now_ms) {
        const ev_t *e = &script[next_ev++];
        if (e->kind == PRESS || e->kind == ANYKEY) {
            fm1_in.buttons |= 1u << e->arg;
            host_btn_edges |= 1u << e->arg;
        } else if (e->kind == RELEASE) {
            fm1_in.buttons &= ~(1u << e->arg);
        } else {
            host_enc[e->arg] += 1;
        }
    }
}
static void at(uint32_t t, int kind, uint32_t arg)
{
    script[nscript].at = t;
    script[nscript].kind = kind;
    script[nscript++].arg = arg;
}
static uint32_t run_setup(void)                     /* panel_setup under the script; the ms it took */
{
    next_ev = 0;
    now_ms = 0;
    fm1_in.buttons = 0;
    host_btn_edges = 0;
    memset(host_enc, 0, sizeof host_enc);
    host_wdt_hook = hook;
    panel_setup();
    host_wdt_hook = 0;
    return now_ms;
}
static uint32_t btn_id(uint32_t i) { return (i * 5u + 3u) % 14u; }   /* a permutation of 0..13 */
static uint32_t enc_id(uint32_t e) { return (e * 3u + 1u) % 7u; }    /* a permutation of 0..6 */

/* the first `n` keys tapped (key 0 held `hold0` ms), starting at 100 ms and 100 ms apart */
static uint32_t tap_keys(uint32_t n, uint32_t hold0)
{
    uint32_t i, t = 100;
    for (i = 0; i < n; i++) {
        at(t, PRESS, btn_id(i));
        at(t + (i == 0 ? hold0 : 50u), RELEASE, btn_id(i));
        t += (i == 0 ? hold0 : 50u) + 50u;
    }
    return t;
}

static void fresh(void)
{
    ui_host_init();
    nscript = 0;
}

static void teaches_everything(void)
{
    uint32_t ok = 1, i, t;
    fresh();
    t = tap_keys(14, 1400);                         /* the first key is held 1.4 s: a slow tap, not a cancel */
    for (i = 0; i < 7u; i++)
        at(t + 100u + i * 100u, TURN, enc_id(i));
    run_setup();
    for (i = 0; i < NB; i++)
        ok &= panel.btn[i] == btn_id(i);
    for (i = 0; i < NE; i++)
        ok &= panel.enc[i] == enc_id(i) && panel.dir[i] == 1;
    ok &= panel.magic == PANEL_MAGIC && !str_eq(ui.msg, "SETUP CANCELLED");
    check("calibration: every key and knob is taught; a key held 1.4 s is a tap", ok);
}

static void hold_cancels(void)
{
    uint32_t ok = 1, took, t;
    panel_t before;
    fresh();
    before = panel;
    t = tap_keys(3, 50);
    at(t, PRESS, btn_id(3));                        /* the fourth key stays down */
    took = run_setup();
    ok &= !memcmp(&panel, &before, sizeof panel) && str_eq(ui.msg, "SETUP CANCELLED");
    ok &= took >= t + 1500u && took < t + 1600u;    /* 1.5 s after the press, not the 30 s */
    check("calibration: holding a key 1.5 s cancels at once and keeps the old table", ok);
    if (!ok)
        printf("     took %u ms, key pressed at %u\n", took, t);
}

static void any_key_cancels_the_knob_step(void)
{
    uint32_t ok = 1, took, t;
    panel_t before;
    fresh();
    before = panel;
    t = tap_keys(14, 50);
    at(t + 100u, TURN, enc_id(0));                  /* two knobs taught ... */
    at(t + 200u, TURN, enc_id(1));
    at(t + 300u, ANYKEY, 4);                        /* ... then a key */
    took = run_setup();
    ok &= !memcmp(&panel, &before, sizeof panel) && str_eq(ui.msg, "SETUP CANCELLED") && took < t + 400u;
    check("calibration: a key pressed while a knob is asked for cancels and keeps the old table", ok);
}

static void idle_still_gives_up(void)
{
    uint32_t ok = 1, took;
    panel_t before;
    fresh();
    before = panel;
    took = run_setup();
    ok &= !memcmp(&panel, &before, sizeof panel) && took > 30000u && took < 30100u && str_eq(ui.msg, "SETUP CANCELLED");
    check("calibration: 30 s without input still gives up and keeps the old table", ok);
}

static void tap_menu_ok(void)
{
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
}
static void frames(uint32_t n)
{
    while (n--)
        ui_frame();
}

static void set_up_a_changed_device(void)
{
    uint32_t i;
    settings.palette = THEME_TAG | 3u;
    settings.lowcut = 2;
    settings.zoom = 1;
    settings.accel = 0;
    settings.usbfix = 1;
    settings.perfpage = 1;
    settings.mutebar = 1;
    backlight = BL_INV_HI;
    midi_thru = 1;
    solo_mom = 1;
    rec_quant = 2;
    gm_v[GU_STYLE] = 13;
    gm_v[GU_WHEN] = 0;
    gm_v[GU_AGGR] = 11;
    for (i = 0; i < NB; i++)
        panel.btn[i] = (uint8_t)btn_id(i);
    theme_set(settings.palette);
}
static int is_default(void)
{
    uint32_t i;
    int ok = (settings.palette & 0xFFu) == (THEME_TAG | 0u) && settings.lowcut == 0 && settings.zoom == 0 &&
             settings.accel == 1 && settings.usbfix == 0 && settings.perfpage == 0 && settings.mutebar == 0 &&
             backlight == BL_OFF && midi_thru == 0 && solo_mom == 0 && rec_quant == 0 && gm_v[GU_STYLE] == 0 &&
             gm_v[GU_WHEN] == 1 && fx_lowcut == 0 && fx_usb_fixed == 0;
    for (i = 0; i < NB; i++)
        ok &= panel.btn[i] == PANEL_DEFAULT.btn[i];
    for (i = 0; i < NE; i++)
        ok &= panel.enc[i] == PANEL_DEFAULT.enc[i] && panel.dir[i] == PANEL_DEFAULT.dir[i];
    return ok;
}

static void reset_to_default(void)
{
    uint32_t ok = 1, sig;
    gm_config f;
    fresh();
    ok &= str_eq(MI_NAME[MI_DEFAULTS], "RESET TO DEFAULT") && MI_DEFAULTS == MI_PANEL + 1;
    set_up_a_changed_device();
    ui.home = 0;
    ui.menu = 1;
    ui.menu_sel = MI_DEFAULTS;
    sig = trk[0].step[0].on + 16u * trk[0].p[P_MODEL];
    ok &= !is_default();
    tap_menu_ok();                                  /* the first OCT+ only asks */
    ok &= !is_default() && menu_def_t != 0;
    frames(120);                                    /* ~1.5 s: the question lapses */
    ok &= menu_def_t == 0;
    tap_menu_ok();
    ok &= !is_default();                            /* asking again after the window: still only asking */
    tap_menu_ok();                                  /* the second one acts */
    ok &= is_default() && str_eq(ui.msg, "DEFAULTS SET");
    gm_factory_config(1, &f);
    ok &= gm_v[GU_AGGR] == f.aggression && gm_v[GU_DENS] == f.density && gm_v[GU_MUT] == 0;   /* the style's own voicing */
    ok &= sig == (uint32_t)(trk[0].step[0].on + 16u * trk[0].p[P_MODEL]);                     /* projects and sounds stay */
    check("menu: RESET TO DEFAULT asks twice, then settings, GRIND style and panel table are the first start's", ok);
}

static void moving_away_cancels_the_question(void)
{
    uint32_t ok = 1;
    fresh();
    set_up_a_changed_device();
    ui.home = 0;
    ui.menu = 1;
    ui.menu_sel = MI_DEFAULTS;
    tap_menu_ok();
    ok &= menu_def_t != 0;
    turn(EN_SELECT, 1);
    frames(2);
    ok &= menu_def_t == 0 && ui.menu_sel != MI_DEFAULTS;
    turn(EN_SELECT, -1);
    frames(2);
    tap_menu_ok();                                  /* back on the row: asks again, does not act */
    ok &= !is_default() && menu_def_t != 0;
    menu_close();
    ok &= menu_def_t == 0;
    check("menu: SELECT away (or closing the menu) takes the question back; nothing was reset", ok);
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    teaches_everything();
    hold_cancels();
    any_key_cancels_the_knob_step();
    idle_still_gives_up();
    reset_to_default();
    moving_away_cancels_the_question();
    printf("test_menu: %s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
