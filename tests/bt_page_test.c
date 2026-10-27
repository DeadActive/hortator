/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* The Bluetooth page (firmware/src/bt_page.c: Hortator's look, felucca-ble's rows) on the host, built with
 * FELUCCA_BLE=1, the UI harness and the host flash (the settings record and the Bluetooth store through
 * settings_save / persist_boot / bt_st_poll): the menu's BLUETOOTH row, ON / OFF (the stack started once, only from
 * OFF; FAILED and safe start refuse), AUTOSTART (saved when the menu closes), ROLE, MODE (FM-LINK), RECONNECT (saved at
 * once in the store), the saved devices' lines (ON / NEAR / AWAY, FORGET? on the state line, OCT+ use now), + ADD A
 * DEVICE (an 8 s scan, then the list stays; MIDI first, saved ones left out; NOTHING FOUND: OCT+ again), the state line,
 * the redraw signature, AUTOSTART at 5 s after a clean boot, the remembered device of the first Bluetooth page carried
 * over, the top bar's icon. The BLE unit is the stub (ble_ui_stub.h). Screens: build/ui_shots/bt/. */
#define FELUCCA_FLASH 1
#include <stdint.h>
static uint8_t hflash[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
#include "ui_host.h"
#if !FELUCCA_BLE
#error "build with -DFELUCCA_BLE=1"
#endif
#include "ble_ui_stub.h"
int ble_midi_send(const uint8_t *m, uint32_t n) { (void)m; (void)n; return 0; }
uint32_t ble_midi_room(void) { return 0; }
#include "../firmware/src/bt_set.c"
#include "../firmware/src/smc.c"
#include "../firmware/src/bt_midi.c"
#include "../firmware/src/bt_fml.c"
#include "../firmware/src/bt_devs.c"
#include "../firmware/src/bt_page.c"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static const uint8_t A_SMC[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0xD6};   /* D6:05:04:03:02:01 */
static const uint8_t A_JBL[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
static const uint8_t A_ANON[6] = {0x9A, 0x78, 0x56, 0x34, 0x12, 0xC4};  /* C4:12:34:56:78:9A, no name */
static const uint8_t A_FM1[6] = {0x2A, 0x3F, 0x10, 0x20, 0x30, 0xC1};   /* another FM-1: label FM-1 3F2A */

static void dev_set(uint32_t i, const uint8_t a[6], uint32_t type, uint32_t midi, int32_t rssi, const char *name)
{
    memset(&bs.dev[i], 0, sizeof bs.dev[i]);
    memcpy(bs.dev[i].addr, a, 6);
    bs.dev[i].type = (uint8_t)type;
    bs.dev[i].midi = (uint8_t)midi;
    bs.dev[i].rssi = (int8_t)rssi;
    bs.dev[i].seen_ms = fm1_ms;
    str_cpy(bs.dev[i].name, name, sizeof bs.dev[i].name);
    if (bs.ndev <= i)
        bs.ndev = i + 1u;
}
static void four(void)                               /* the scan list by signal: a speaker, the SMC (MIDI), no name,
                                                      * an FM-1 */
{
    bs.ndev = 0;
    dev_set(0, A_JBL, 0, 0, -50, "JBL Flip 5");
    dev_set(1, A_SMC, 1, 1, -59, "SMC-Mixer");
    dev_set(2, A_ANON, 1, 0, -80, "");
    dev_set(3, A_FM1, 1, 1, -62, "Felucca FM-1");
}
static void tap(uint32_t b)
{
    press(b);
    ui_frame();
    release_all();
    ui_frame();
}
static void knob(int32_t s)
{
    turn(EN_K1, s);
    ui_frame();
}
static void row(uint32_t r)                          /* the page's cursor to row r (SELECT from where it is) */
{
    uint32_t k;
    for (k = 0; btp.sel != r && k < 30u; k++) {
        turn(EN_SELECT, 1);
        ui_frame();
    }
}
static void remember(const uint8_t a[6], uint32_t type)   /* the first Bluetooth page's remembered device (btset) */
{
    btset.bt_lo = a[0] | (uint32_t)a[1] << 8 | (uint32_t)a[2] << 16;
    btset.bt_hi = a[3] | (uint32_t)a[4] << 8 | (uint32_t)a[5] << 16;
    btset.bt_type = type;
}
static void power_cycle(void)                        /* power off: .bss cleared, the flash records loaded again */
{
    memset(&btset, 0, sizeof btset);
    memset(&bt_st, 0, sizeof bt_st);
    memset(&btp, 0, sizeof btp);
    memset(&bt_dv, 0, sizeof bt_dv);
    persist_boot();
}
static void fresh(void)                              /* erased flash, the stack OFF, nothing listed, the menu's list */
{
    memset(hflash, 0xFF, sizeof hflash);
    memset(&persist_saved, 0, sizeof persist_saved);  /* (power-on: .bss; else an equal record is not written again) */
    ui_host_init();
    memset(&bt_st, 0, sizeof bt_st);
    persist_boot();                                  /* (finds the flash: settings_save writes) */
    memset(&bs, 0, sizeof bs);
    memset(&bh, 0, sizeof bh);
    bs_role = -1;
    memset(&btp, 0, sizeof btp);
    memset(&bt_dv, 0, sizeof bt_dv);
    conn_up = 0;
    safe_start = 0;
    host_boot_clean = 1;
    bt_boot_clean = 1;
    fm1_ms = 100000;
    bt_auto_done = 0;
    ui.menu = 1;
    ui.menu_sel = 0;
    ui.home = 0;
    ui.force = 1;
}
static void open_page(void)
{
    uint32_t k;
    for (k = 0; ui.menu_sel != MI_BT && k < MI_COUNT; k++) {   /* (one row per frame) */
        turn(EN_SELECT, 1);
        ui_frame();
    }
    tap(B_OCTUP);
}
static void snap(const char *name)
{
    char path[96];
    uint32_t i;
    ui.force = 1;
    for (i = 0; i < 3; i++)
        ui_frame();
    snprintf(path, sizeof path, "build/ui_shots/bt/%s.ppm", name);
    shot(path);
}
static uint32_t ink_in(uint32_t y0, uint32_t y1)    /* ink pixels in rows y0..y1, inside the menu window (x 12..225) */
{
    uint32_t n = 0, x, y;
    for (y = y0; y < y1; y++)
        for (x = 12u; x < 226u; x++)
            n += fb[y * 240u + x] == C_INK;
    return n;
}
static uint32_t lit(uint32_t y0, uint32_t y1)        /* non-black pixels in rows y0..y1 */
{
    uint32_t n = 0, i;
    for (i = y0 * 240u; i < y1 * 240u; i++)
        n += fb[i] != 0;
    return n;
}
static uint32_t head_hash(void)                      /* the top bar's pixels */
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < 240u * 16u; i++)
        h = (h ^ fb[i]) * 16777619u;
    return h;
}
static uint32_t rec_palette(void)                    /* the flash record's palette word (bit 8: AUTOSTART) */
{
    persist_t p;
    if (st_load(OBJ_SETTINGS, &p, sizeof p) != (int)sizeof p)
        return 0xFFFFFFFFu;
    return p.palette;
}
static uint32_t rec_mode(void)                       /* the flash record's MODE bits (zoom 5..6) */
{
    persist_t p;
    if (st_load(OBJ_SETTINGS, &p, sizeof p) != (int)sizeof p)
        return 0xFFFFFFFFu;
    return (p.zoom >> 5) & 3u;
}
static void state(char *t) { bt_state_text(t); }
static void link_slot(uint32_t slot, const uint8_t *a)   /* the host slot up on a (0: down) */
{
    memset(&bh.slot[slot], 0, sizeof bh.slot[slot]);
    if (a) {
        bh.slot[slot].st = BLE_LS_UP;
        memcpy(bh.slot[slot].addr, a, 6);
    }
}
static int idx_of(const uint8_t a[6])
{
    bt_dev_t d;
    uint32_t i;
    for (i = 0; bt_devs_get(i, &d); i++)
        if (!memcmp(d.addr, a, 6))
            return (int)i;
    return -1;
}
static void add(const uint8_t a[6], uint32_t kind, const char *name)
{
    bt_dev_t d;
    memset(&d, 0, sizeof d);
    memcpy(d.addr, a, 6);
    d.kind = (uint8_t)kind;
    str_cpy(d.name, name, sizeof d.name);
    bt_devs_add(&d);
}

static void test_menu_row(void)
{
    fresh();
    check("menu: BLUETOOTH is the row before ABOUT; eleven rows (BACKLIGHT, spec 2026-10-11-fixes §12)",
          str_eq(MI_NAME[MI_BT], "BLUETOOTH") && MI_BT + 1 == MI_ABOUT && MI_COUNT == 11 &&
          str_eq(MI_NAME[MI_BACKLIGHT], "BACKLIGHT"));
    {   /* MENU > USB AUDIO is the standard build's only (owner 2026-10-26): no row, no 10 CH code, 2 CH always */
        uint32_t k, none = 1;
        for (k = 0; k < MI_COUNT; k++)
            none &= !str_eq(MI_NAME[k], "USB AUDIO");
        check("menu: no USB AUDIO row in the Bluetooth build (2 CH always; a stored 10 CH is not read)",
              none && !USB10_BUILD && !USB8_OK);
    }
    {
        uint32_t k;
        for (k = 0; k < MI_BT; k++) {
            turn(EN_SELECT, 1);
            ui_frame();
        }
        snap("00_menu");
        shot("build/ui_shots/fixes/menu_backlight_ble.ppm");   /* (the Bluetooth build's menu, with BACKLIGHT) */
    }
    check("menu: the eleven rows end above the hints", ink_in(MENU_Y0 + 10u * MENU_DY, MENU_Y0 + 10u * MENU_DY + 10u) > 50 &&
          ink_in(MENU_Y0 + 10u * MENU_DY + 12u, MENU_HY) == 0 && ink_in(MENU_HY, MENU_HY + 14u) > 50);
    tap(B_OCTUP);
    check("menu: OCT+ on BLUETOOTH opens the page", ui.menu == 3 && btp.sel == 0);
    check("the page opened: nothing started, no role asked", !bs.starts && !bs.wants && bs_role < 0);
    bt_devs_frame();
    check("the page open (HOST): the scan wanted continuous (ON / NEAR / AWAY)", bh.scan_want == 2);
    snap("01_off");
    check("the page draws (head, state, rows, hints)", lit(0, 20) > 50 && lit(21, 230) > 600);
    check("the rows: BLUETOOTH AUTOSTART ROLE MODE RECONNECT, then ADD (no device saved)",
          str_eq(BT_NAME[BTR_ON], "BLUETOOTH") && str_eq(BT_NAME[BTR_AUTO], "AUTOSTART") &&
          str_eq(BT_NAME[BTR_ROLE], "ROLE") && str_eq(BT_NAME[BTR_MODE], "MODE") &&
          str_eq(BT_NAME[BTR_RATE], "RECONNECT") && bt_page_rows() == 6u);
    tap(B_OCTDN);
    bt_devs_frame();
    check("OCT-: back to the menu's list on BLUETOOTH; the page's scan ends", ui.menu == 1 && ui.menu_sel == MI_BT &&
          bh.scan_want == 0);
    tap(B_OCTUP);
    menu_close();                                    /* (HOME held on the page) */
    bt_devs_frame();
    check("HOME held on the page: the menu closes, the page's scan ends", ui.menu == 0 && bh.scan_want == 0);
}

static void test_on_off(void)
{
    fresh();
    open_page();
    knob(1);
    check("BLUETOOTH ON (KNOB 1 right): the role handed over (HOST), want on, the stack started once",
          bs.starts == 1 && bs.wants == 1 && bs.want == 1 && bs_role == 0 && btp.on);
    knob(1);
    ui_frame();
    check("ON again / frames: no more calls", bs.starts == 1 && bs.wants == 1);
    knob(-1);
    check("BLUETOOTH OFF (KNOB 1 left): want 0", bs.wants == 2 && !bs.want && !btp.on);
    bs.state = BLE_ST_IDLE;
    tap(B_OCTUP);
    check("OCT+ toggles ON: the stack already started is not started again", btp.on && bs.starts == 1 && bs.want == 1);
    tap(B_OCTUP);
    check("OCT+ toggles OFF", !btp.on && !bs.want && bs.wants == 4);
}

static void test_refusals(void)
{
    fresh();
    bs.state = BLE_ST_FAILED;
    bs.err = "NO RF TRIM IN FLASH";
    open_page();
    knob(1);
    check("FAILED: BLUETOOTH ON says POWER OFF AND ON TO RETRY, nothing called",
          btp.msg && str_eq(btp.msg, "POWER OFF AND ON TO RETRY") && !bs.starts && !bs.wants && !btp.on);
    btp.msg_t = 0;
    snap("05_failed");
    fresh();
    safe_start = 1;
    btp.sel = 0;
    ui.menu = 3;
    bt_page_input(1u << panel.btn[B_OCTUP]);         /* (safe start: ui_input shows no menu; the page refuses too) */
    check("safe start: BLUETOOTH ON says NOT IN SAFE START, nothing called",
          btp.msg && str_eq(btp.msg, "NOT IN SAFE START") && !bs.starts && !bs.wants && !btp.on);
    safe_start = 0;
    fresh();
    open_page();
    row(BT_SET_ROWS);
    tap(B_OCTUP);
    check("+ ADD A DEVICE while BLUETOOTH is OFF: refused with a message", !btp.add && btp.msg &&
          str_eq(btp.msg, "BLUETOOTH IS OFF"));
}

/* AUTOSTART: the row after BLUETOOTH; KNOB 1 right ON, left OFF, OCT+ toggles; saved when the menu closes (the
 * settings record's palette word, bit 8, as before), not per change; nothing started */
static void test_autostart_row(void)
{
    uint32_t ok = 1, pal;
    fresh();
    check("AUTOSTART: OFF by default (no settings record)", btset.bt_auto == 0u);
    open_page();
    row(BTR_AUTO);
    knob(1);
    ok &= btset.bt_auto == 1u;
    knob(1);
    ok &= btset.bt_auto == 1u;
    knob(-1);
    ok &= btset.bt_auto == 0u;
    knob(-1);
    ok &= btset.bt_auto == 0u;
    check("AUTOSTART: KNOB 1 right ON, left OFF (stays at the ends)", ok);
    tap(B_OCTUP);
    ok = btset.bt_auto == 1u;
    tap(B_OCTUP);
    ok &= btset.bt_auto == 0u;
    tap(B_OCTUP);
    check("AUTOSTART: OCT+ toggles", ok && btset.bt_auto == 1u);
    check("AUTOSTART: nothing started; not written per change", !bs.starts && !bs.wants && !btp.on &&
          rec_palette() == 0xFFFFFFFFu);
    snap("07_autostart_on");
    settings.palette = THEME_TAG | 1u;
    theme_set(THEME_TAG | 1u);
    pal = settings.palette;
    menu_close();
    check("AUTOSTART: the menu closed: saved (palette word bit 8, as the first Bluetooth page)",
          rec_palette() == (pal | 1u << 8));
    settings.palette = 0;
    power_cycle();
    settings_init();
    check("AUTOSTART survives a power cycle; the palette loads without the bit", btset.bt_auto == 1u &&
          settings.palette == pal);
}

/* ROLE, MODE, RECONNECT: KNOB 1 steps (MODE / RECONNECT stop at the ends, OCT+ wraps); ROLE and RECONNECT and MODE
 * FM-LINK in the Bluetooth store at once (written when stopped); MODE AUTO / SMC DAW / MIDI in the settings record */
static void test_role_mode_rate(void)
{
    bt_set_t s;
    uint32_t ok = 1, i;
    char t[40];
    fresh();
    add(A_SMC, BT_KIND_CTRL, "SMC-Mixer");
    bt_st_poll();
    open_page();
    check("HOST with a saved device: 5 rows, the device, ADD", bt_page_rows() == 7u);
    row(BTR_ROLE);
    knob(1);
    bt_set_get(&s);
    check("ROLE: KNOB 1 right DEVICE, the BLE unit told, the store dirty", s.role == 1u && bs_role == 1 && bt_st.dirty);
    check("ROLE DEVICE: no device lines (5 rows)", bt_page_rows() == 5u);
    bt_devs_frame();
    check("ROLE DEVICE: the page asks no scan", bh.scan_want == 0);
    snap("09_role_device");
    song.playing = 1;
    bt_st_poll();
    check("the store is not written while playing", bt_st.dirty);
    song.playing = 0;
    bt_st_poll();
    check("stopped: written", !bt_st.dirty && !bt_st.fails);
    power_cycle();
    bt_set_get(&s);
    check("ROLE DEVICE survives a power cycle (the store)", s.role == 1u);
    ui.menu = 1;
    open_page();
    row(BTR_ROLE);
    tap(B_OCTUP);
    bt_set_get(&s);
    check("ROLE: OCT+ toggles back to HOST", s.role == 0u && bs_role == 0);
    row(BTR_MODE);
    for (i = 0; i < 5u; i++) {
        knob(1);
        bt_set_get(&s);
        ok &= s.mode == (i < 3u ? i + 1u : 3u);
    }
    check("MODE: KNOB 1 steps AUTO SMC DAW MIDI FM-LINK, stops at FM-LINK", ok);
    check("MODE FM-LINK: a flag in the store; the settings record keeps MIDI (an older firmware reads that)",
          (bt_st.r.set & 8u) && btset.btmode == 2u);
    snap("10_mode_fmlink");
    menu_close();
    bt_st_poll();
    check("MODE FM-LINK: the settings record says MIDI", rec_mode() == 2u);
    power_cycle();
    bt_set_get(&s);
    check("MODE FM-LINK survives a power cycle", s.mode == BT_MODE_LINK);
    ui.menu = 1;
    open_page();
    row(BTR_MODE);
    tap(B_OCTUP);
    bt_set_get(&s);
    check("MODE: OCT+ wraps FM-LINK -> AUTO, the flag cleared", s.mode == BT_MODE_AUTO && !(bt_st.r.set & 8u) &&
          btset.btmode == 0u);
    row(BTR_RATE);
    knob(1);
    bt_set_get(&s);
    ok = s.rate == BT_RATE_FAST && !bt_set_slow(1);
    knob(1);
    knob(1);
    bt_set_get(&s);
    ok &= s.rate == BT_RATE_SLOW && bt_set_slow(0);
    tap(B_OCTUP);
    bt_set_get(&s);
    ok &= s.rate == BT_RATE_AUTO && bt_set_slow(1) && !bt_set_slow(0);
    check("RECONNECT: AUTO FAST SLOW (stops at SLOW, OCT+ wraps); SLOW or AUTO while playing rests longer", ok);
    bt_st.r.set = 6u;                                /* (bits 1-2 = 3: a damaged value) */
    bt_set_get(&s);
    check("RECONNECT: a value out of range reads AUTO", s.rate == BT_RATE_AUTO);
    bt_st.r.set = 0;
    state(t);
    check("BLUETOOTH OFF: the state line says OFF", str_eq(t, "OFF"));
}

/* the saved devices: lines, ON / NEAR / AWAY, FORGET?, use now; + ADD A DEVICE */
static void test_devices(void)
{
    char t[40];
    bt_dev_t d;
    fresh();
    open_page();
    knob(1);
    bs.state = BLE_ST_IDLE;
    state(t);
    check("ON, nothing saved: ON, NO DEVICE SAVED", str_eq(t, "ON, NO DEVICE SAVED"));
    row(BT_SET_ROWS);
    tap(B_OCTUP);
    check("+ ADD A DEVICE, OCT+: the list opens", btp.add && btp.an == 0);
    state(t);
    check("the ADD list: SCANNING 8 S", str_eq(t, "SCANNING 8 S"));
    snap("11_add_looking");
    four();
    ui_frame();
    check("the ADD list: MIDI ones first (by signal), then the others", btp.an == 4u &&
          !memcmp(btp.adev[0].addr, A_SMC, 6) && !memcmp(btp.adev[1].addr, A_FM1, 6) &&
          !memcmp(btp.adev[2].addr, A_JBL, 6) && !memcmp(btp.adev[3].addr, A_ANON, 6));
    fm1_ms += 3500;
    state(t);
    check("the ADD list: seconds left go down (SCANNING 5 S)", str_eq(t, "SCANNING 5 S"));
    fm1_ms += 5000;
    ui_frame();
    state(t);
    check("8 s on: ADD: PICK A DEVICE", str_eq(t, "ADD: PICK A DEVICE"));
    bs.ndev = 1;                                     /* (the scan list changes: the ADD list stays as it is) */
    ui_frame();
    check("after its scan the ADD list stays put", btp.an == 4u);
    snap("12_add_list");
    tap(B_OCTUP);
    check("OCT+ on SMC-Mixer: saved (a controller, its name), the list closed, the cursor on its line",
          !btp.add && bt_devs_n() == 1u && bt_devs_get(0, &d) && str_eq(d.name, "SMC-Mixer") && d.kind == BT_KIND_CTRL &&
          d.type == 1u && btp.sel == BT_SET_ROWS);
    bt_devs_frame();
    check("the new device the CTRL slot's target", bh.targets[0] && !memcmp(bh.tgt[0], A_SMC, 6) && bh.tgt_type[0] == 1u);
    four();
    row(BT_SET_ROWS + 1u);
    tap(B_OCTUP);
    ui_frame();
    check("ADD again: the saved SMC-Mixer is not listed", btp.an == 3u && !memcmp(btp.adev[0].addr, A_FM1, 6));
    tap(B_OCTUP);
    check("an FM-1 picked: saved as FM-1 kind, first", bt_devs_n() == 2u && bt_devs_get(0, &d) && d.kind == BT_KIND_FM1);
    bt_dev_label(&d, t);
    check("an FM-1's line: FM-1 and its address' last four digits", str_eq(t, "FM-1 3F2A"));
    bt_devs_frame();
    check("the FM-1 the FM1 slot's target", !memcmp(bh.tgt[1], A_FM1, 6));
    link_slot(0, A_SMC);
    ui_frame();
    state(t);
    check("one link up: ON, 1 CONNECTED", str_eq(t, "ON, 1 CONNECTED"));
    check("the lines: SMC ON (linked), FM-1 NEAR (seen lately)", bt_devs_state((uint32_t)idx_of(A_SMC)) == 2 &&
          bt_devs_state((uint32_t)idx_of(A_FM1)) == 1);
    snap("13_devices");
    bs.ndev = 0;
    link_slot(0, 0);
    state(t);
    check("nothing linked: ON, SEARCHING; the lines AWAY", str_eq(t, "ON, SEARCHING") &&
          bt_devs_state(0) == 0 && bt_devs_state(1) == 0);
    row(BT_SET_ROWS + (uint32_t)idx_of(A_SMC));
    tap(B_OCTUP);
    check("OCT+ on an AWAY device: NOT AROUND, the order kept", btp.msg && str_eq(btp.msg, "NOT AROUND") &&
          idx_of(A_SMC) == 1);
    four();
    tap(B_OCTUP);
    check("OCT+ on a NEAR device: use it now (to the top)", idx_of(A_SMC) == 0);
    row(BT_SET_ROWS + (uint32_t)idx_of(A_FM1));
    knob(-1);
    state(t);
    check("KNOB 1 left on a line: FORGET <name>? on the state line", btp.confirm && str_eq(t, "FORGET FM-1 3F2A?"));
    snap("14_forget");
    tap(B_OCTDN);
    check("OCT-: no, nothing forgotten, still on the page", !btp.confirm && bt_devs_n() == 2u && ui.menu == 3);
    knob(-1);
    tap(B_OCTUP);
    check("OCT+: yes, forgotten", !btp.confirm && bt_devs_n() == 1u && idx_of(A_FM1) < 0);
    bt_st_poll();
    power_cycle();
    check("the saved devices survive a power cycle (the store)", bt_devs_n() == 1u && bt_devs_get(0, &d) &&
          str_eq(d.name, "SMC-Mixer"));
    ui.menu = 1;
    open_page();
    knob(1);
    bs.state = BLE_ST_IDLE;
    bs.ndev = 0;
    row(BT_SET_ROWS + 1u);
    tap(B_OCTUP);
    fm1_ms += BT_ADD_MS;
    ui_frame();
    state(t);
    check("nothing found in 8 s: NOTHING FOUND", str_eq(t, "NOTHING FOUND") && btp.an == 0);
    snap("15_nothing_found");
    tap(B_OCTUP);
    state(t);
    check("NOTHING FOUND, OCT+: scans again", btp.add && str_eq(t, "SCANNING 8 S"));
    tap(B_OCTDN);
    check("the ADD list, OCT-: back to the page", !btp.add && ui.menu == 3);
}

/* the first Bluetooth page's remembered device: the first saved device when there is no store yet */
static void test_carried_over(void)
{
    bt_dev_t d;
    char t[24];
    fresh();
    remember(A_SMC, 1);
    btset.btmode = 1;
    btset.bt_auto = 1;
    settings_save();
    power_cycle();
    check("carried over: the remembered device is the first saved device (a controller, its address type)",
          bt_devs_n() == 1u && bt_devs_get(0, &d) && !memcmp(d.addr, A_SMC, 6) && d.type == 1u && d.kind == BT_KIND_CTRL);
    {
        bt_set_t s;
        bt_set_get(&s);
        check("carried over: MODE and AUTOSTART as the settings record has them", s.mode == BT_MODE_SMC && s.autostart);
    }
    bt_dev_label(&d, t);
    check("no name yet: DEVICE and the address' last four digits", str_eq(t, "DEVICE 0201"));
    four();
    bt_devs_frame();
    bt_devs_get(0, &d);
    check("a scan lists it: its name kept, the store dirty", str_eq(d.name, "SMC-Mixer") && bt_st.dirty);
    bt_st_poll();
    power_cycle();
    check("with a store: the store's list (not carried over again)", bt_devs_n() == 1u && bt_devs_get(0, &d) &&
          str_eq(d.name, "SMC-Mixer"));
    bt_devs_forget(0);
    bt_st_poll();
    power_cycle();
    check("forgotten: stays forgotten (the settings record's device is not carried over a second time)",
          bt_devs_n() == 0u && (btset.bt_lo | btset.bt_hi));
}

static void frames_at(uint32_t ms, uint32_t n)       /* n UI frames at ms after power-on */
{
    fm1_ms = ms;
    while (n--)
        ui_frame();
}
static int nothing_asked(void) { return !bs.starts && !bs.wants && !btp.on; }

/* AUTOSTART ON: once, 5 s after power-on, from the UI (ui_input), as BLUETOOTH ON; not in safe start, nor with OCT-
 * + OCT+ held; skipped with a message after a boot that was not clean */
static void test_autostart(void)
{
    uint32_t k, ok;
    fresh();
    ui.menu = 0;
    btset.bt_auto = 1;
    frames_at(0, 3);
    frames_at(4999, 3);
    check("autostart: nothing before 5 s", nothing_asked());
    frames_at(5000, 1);
    check("autostart at 5 s: want on, the role, the stack started, BLUETOOTH ON", bs.starts == 1 && bs.wants == 1 &&
          bs.want == 1 && bs_role == 0 && btp.on);
    frames_at(60000, 3);
    btp.on = 0;
    frames_at(70000, 3);
    check("autostart: once", bs.starts == 1 && bs.wants == 1);

    fresh();
    ui.menu = 0;
    frames_at(5000, 3);
    check("AUTOSTART OFF: nothing at 5 s", nothing_asked());

    fresh();
    ui.menu = 0;
    btset.bt_auto = 1;
    safe_start = 1;
    frames_at(5000, 3);
    bt_autostart();
    check("autostart: not in safe start", nothing_asked());
    safe_start = 0;

    fresh();
    ui.menu = 0;
    btset.bt_auto = 1;
    fm1_ms = 4990;
    fm1_in.buttons |= 1u << panel.btn[B_OCTDN] | 1u << panel.btn[B_OCTUP];   /* (held, no edges) */
    bt_autostart();
    fm1_ms = 5000;
    bt_autostart();
    release_all();
    frames_at(6000, 2);
    check("autostart: not while OCT- + OCT+ are held (UPDATE MODE's count), nor later", nothing_asked());

    fresh();
    ui.menu = 0;
    btset.bt_auto = 1;
    bt_boot_clean = 0;
    frames_at(5000, 1);
    check("autostart skipped after a boot that was not clean: nothing called, BT AUTOSTART SKIPPED",
          nothing_asked() && ui.msg_t && str_eq(ui.msg, "BT AUTOSTART SKIPPED"));
    snap("08_autostart_skipped");
    for (k = 0, ok = 1; k < 100u; k++) {
        frames_at(5100 + k * 15u, 1);
        ok &= !str_eq(ui.msg, "BT AUTOSTART SKIPPED") || ui.msg_t < 40u;
    }
    check("autostart skipped: the message once", ok && nothing_asked());
    ui.menu = 1;
    open_page();
    knob(1);
    check("autostart skipped: BLUETOOTH ON by hand still starts it", bs.starts == 1 && btp.on);

    fresh();
    btset.bt_auto = 1;
    fm1_ms = 3000;
    open_page();
    knob(1);
    frames_at(5000, 3);
    check("autostart after a start by hand: nothing more", bs.starts == 1 && bs.wants == 1);

    fresh();
    ui.menu = 0;
    btset.bt_auto = 1;
    bs.state = BLE_ST_FAILED;
    bs.err = "NO RF TRIM IN FLASH";
    frames_at(5000, 3);
    check("autostart with the stack FAILED: nothing called", nothing_asked());
}

static void test_state_lines(void)
{
    char t[40];
    fresh();
    open_page();
    knob(1);
    bs.state = BLE_ST_STARTING;
    state(t);
    check("STARTING", str_eq(t, "STARTING"));
    bs.state = BLE_ST_ADVERTISING;
    state(t);
    check("the device role advertising: WAITING FOR A HOST", str_eq(t, "WAITING FOR A HOST"));
    bs.state = BLE_ST_SUBSCRIBED;
    conn_up = 1;
    str_cpy(conn_name, "MacBook", sizeof conn_name);
    state(t);
    check("the device role linked: CONNECTED and the host", str_eq(t, "CONNECTED MacBook"));
    snap("16_device_connected");
    conn_up = 0;
    bs.state = BLE_ST_IDLE;
    bs.err = "DISCONNECTED";
    bs.code = 8;
    snap("04_idle_error");
    bs.state = BLE_ST_FAILED;
    bs.err = "NO RF TRIM IN FLASH";
    state(t);
    check("FAILED and the reason", str_eq(t, "FAILED NO RF TRIM IN FLASH"));
}

static void test_head_icon(void)
{
    uint32_t a, b, c;
    fresh();
    ui.menu = 0;
    ui.home = 1;
    ui.force = 1;
    ui_frame();
    a = head_hash();
    check("the top bar's icon: none while Bluetooth is off", bt_head_icon() == 0u);
    btp.on = 1;
    bs.state = BLE_ST_IDLE;
    ui_frame();
    b = head_hash();
    check("on, nothing linked: struck (1), drawn (the head redrawn)", bt_head_icon() == 1u && b != a);
    shot("build/ui_shots/bt/17_head_struck.ppm");
    link_slot(1, A_FM1);
    ui_frame();
    c = head_hash();
    check("a link up: lit (2), the strike gone", bt_head_icon() == 2u && c != b && c != a);
    shot("build/ui_shots/bt/18_head_lit.ppm");
    link_slot(1, 0);
    bs.state = BLE_ST_RECEIVING;
    check("the device role linked: lit", bt_head_icon() == 2u);
    btp.on = 0;
    ui_frame();
    check("off again: the head as before", head_hash() == a);
}

static void test_signature(void)
{
    fresh();
    open_page();
    knob(1);
    bs.state = BLE_ST_IDLE;
    add(A_SMC, BT_KIND_CTRL, "SMC-Mixer");
    ui_frame();
    memset(fb, 0, sizeof fb);
    ui_frame();
    check("nothing changed: no redraw", lit(0, 240) == 0);
    link_slot(0, A_SMC);
    ui_frame();
    check("a link up: redrawn", lit(21, 230) > 600);
    memset(fb, 0, sizeof fb);
    btset.btmode = 2;
    ui_frame();
    check("MODE changed: redrawn", lit(21, 230) > 600);
}

static void test_scroll(void)
{
    uint8_t a[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0xE0};
    uint32_t i;
    char n[8] = "CTRL 0";
    fresh();
    for (i = 0; i < 8u; i++) {
        a[0] = (uint8_t)i;
        n[5] = (char)('1' + i);
        add(a, BT_KIND_CTRL, n);
    }
    open_page();
    knob(1);
    bs.state = BLE_ST_IDLE;
    check("eight saved: 14 rows", bt_page_rows() == 14u);
    row(13);
    ui_frame();
    check("the cursor on ADD: the rows scrolled", btp.top == 13u - BT_VIS + 1u);
    snap("06_scrolled");
}

int main(void)
{
    test_menu_row();
    test_on_off();
    test_refusals();
    test_autostart_row();
    test_role_mode_rate();
    test_devices();
    test_carried_over();
    test_state_lines();
    test_head_icon();
    test_signature();
    test_scroll();
    test_autostart();
    printf(fails ? "bt_page_test: %d FAILED\n" : "bt_page_test: all passed\n", fails);
    return fails ? 1 : 0;
}
