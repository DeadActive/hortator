/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* The Bluetooth page (firmware/src/bt_page.c, spec 2026-10-10 §2) on the host, built with FELUCCA_BLE=1, the UI
 * harness and the host flash (the remembered device and MODE through settings_save / persist_boot): the menu's
 * BLUETOOTH row, ON / OFF (the stack started once, only from OFF; FAILED and safe start refuse), MODE, a pick, FORGET,
 * SCAN, the pause ended on leaving, MIDI devices first, the redraw signature. Screens: build/ui_shots/bt/. */
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
#include "../firmware/src/smc.c"
#include "../firmware/src/bt_midi.c"
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

static void dev_set(uint32_t i, const uint8_t a[6], uint32_t type, uint32_t midi, int32_t rssi, const char *name)
{
    memset(&bs.dev[i], 0, sizeof bs.dev[i]);
    memcpy(bs.dev[i].addr, a, 6);
    bs.dev[i].type = (uint8_t)type;
    bs.dev[i].midi = (uint8_t)midi;
    bs.dev[i].rssi = (int8_t)rssi;
    str_cpy(bs.dev[i].name, name, sizeof bs.dev[i].name);
}
static void three(void)                              /* the scan list by signal: a speaker, the SMC (MIDI), no name */
{
    dev_set(0, A_JBL, 0, 0, -50, "JBL Flip 5");
    dev_set(1, A_SMC, 1, 1, -59, "SMC-Mixer");
    dev_set(2, A_ANON, 1, 0, -80, "");
    bs.ndev = 3;
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
static void row(uint32_t r)                          /* the page's cursor to row r (SELECT from the top) */
{
    uint32_t k;
    for (k = 0; btp.sel != r && k < 20u; k++) {
        turn(EN_SELECT, 1);
        ui_frame();
    }
}
static void saved(uint8_t a[6])
{
    a[0] = (uint8_t)btset.bt_lo, a[1] = (uint8_t)(btset.bt_lo >> 8), a[2] = (uint8_t)(btset.bt_lo >> 16);
    a[3] = (uint8_t)btset.bt_hi, a[4] = (uint8_t)(btset.bt_hi >> 8), a[5] = (uint8_t)(btset.bt_hi >> 16);
}
static void remember(const uint8_t a[6], uint32_t type)
{
    btset.bt_lo = a[0] | (uint32_t)a[1] << 8 | (uint32_t)a[2] << 16;
    btset.bt_hi = a[3] | (uint32_t)a[4] << 8 | (uint32_t)a[5] << 16;
    btset.bt_type = type;
}
static void power_cycle(void)                        /* power off: .bss cleared, the flash record loaded again */
{
    memset(&btset, 0, sizeof btset);
    persist_boot();
}
static void fresh(void)                              /* erased flash, the stack OFF, nothing listed, the menu's list */
{
    memset(hflash, 0xFF, sizeof hflash);
    memset(&persist_saved, 0, sizeof persist_saved);  /* (power-on: .bss; else an equal record is not written again) */
    ui_host_init();
    persist_boot();                                  /* (finds the flash: settings_save writes) */
    memset(&bs, 0, sizeof bs);
    memset(&btp, 0, sizeof btp);
    conn_up = 0;
    safe_start = 0;
    ui.menu = 1;
    ui.menu_sel = 0;
    ui.home = 0;
    ui.force = 1;
}
static void open_page(void)
{
    uint32_t k;
    for (k = 0; k < MI_BT; k++) {                   /* (one row per frame) */
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
static uint32_t lit(uint32_t y0, uint32_t y1)        /* non-black pixels in rows y0..y1 */
{
    uint32_t n = 0, i;
    for (i = y0 * 240u; i < y1 * 240u; i++)
        n += fb[i] != 0;
    return n;
}
static uint32_t rec_bt(uint32_t *lo, uint32_t *hi)   /* the flash record's MODE / type / address bits */
{
    persist_t p;
    if (st_load(OBJ_SETTINGS, &p, sizeof p) != (int)sizeof p)
        return 0xFFFFFFFFu;
    *lo = p.zoom >> 8;
    *hi = p.lowcut >> 8;
    return (p.zoom >> 5) & 7u;
}

static void test_menu_row(void)
{
    fresh();
    check("menu: BLUETOOTH is the row before ABOUT; ten rows", str_eq(MI_NAME[MI_BT], "BLUETOOTH") &&
          MI_BT + 1 == MI_ABOUT && MI_COUNT == 10);
    {
        uint32_t k;
        for (k = 0; k < MI_BT; k++) {
            turn(EN_SELECT, 1);
            ui_frame();
        }
        snap("00_menu");
    }
    check("menu: the ten rows end above the hints (BACK's row drawn, the hints apart)",
          lit(H_HEAD + 1 + 4 + 9 * 16, H_HEAD + 1 + 4 + 9 * 16 + 16) > 50 && lit(H_HEAD + 1 + 166, H_HEAD + 1 + 170) == 0);
    tap(B_OCTUP);
    check("menu: OCT+ on BLUETOOTH opens the page", ui.menu == 3 && btp.sel == 0);
    check("the page opened: nothing started, nothing asked of the stack",
          !bs.starts && !bs.targets && !bs.wants && !bs.scans && !bs.pauses);
    snap("01_off");
    check("the page draws (head, state, rows, hints)", lit(0, 20) > 50 && lit(21, 230) > 600);
    tap(B_OCTDN);
    check("OCT-: back to the menu's list on BLUETOOTH, the pause ended", ui.menu == 1 && ui.menu_sel == MI_BT &&
          bs.pauses == 1 && bs.pause == 0);
    tap(B_OCTUP);
    menu_close();                                    /* (HOME held on the page) */
    check("HOME held on the page: the menu closes, the pause ended", ui.menu == 0 && bs.pauses == 2 && bs.pause == 0);
}

static void test_on_off(void)
{
    uint8_t a[6];
    fresh();
    remember(A_SMC, 1);
    open_page();
    knob(1);
    check("BLUETOOTH ON (KNOB 1 right): the saved device the target, want on, the stack started once",
          bs.starts == 1 && bs.targets == 1 && !memcmp(bs.taddr, A_SMC, 6) && bs.ttype == 1 && bs.wants == 1 &&
          bs.want == 1 && btp.on);
    knob(1);
    ui_frame();
    ui_frame();
    check("ON again / frames: no more calls (target only on a real change)",
          bs.starts == 1 && bs.targets == 1 && bs.wants == 1);
    knob(-1);
    check("BLUETOOTH OFF (KNOB 1 left): want 0, no target", bs.wants == 2 && !bs.want && bs.targets == 1 && !btp.on);
    bs.state = BLE_ST_IDLE;
    tap(B_OCTUP);
    check("OCT+ toggles ON: the stack already started is not started again",
          btp.on && bs.starts == 1 && bs.targets == 2 && bs.want == 1);
    tap(B_OCTUP);
    check("OCT+ toggles OFF", !btp.on && !bs.want && bs.wants == 4);
    fresh();
    open_page();
    knob(1);
    saved(a);
    check("ON with no device remembered: the target is none (all zero)", bs.targets == 1 &&
          !memcmp(bs.taddr, a, 6) && !(a[0] | a[1] | a[2] | a[3] | a[4] | a[5]) && bs.starts == 1);
}

static void test_refusals(void)
{
    fresh();
    bs.state = BLE_ST_FAILED;
    bs.err = "NO RF TRIM IN FLASH";
    open_page();
    knob(1);
    check("FAILED: BLUETOOTH ON says POWER OFF AND ON TO RETRY, nothing called",
          btp.msg && str_eq(btp.msg, "POWER OFF AND ON TO RETRY") && !bs.starts && !bs.targets && !bs.wants && !btp.on);
    btp.msg_t = 0;
    snap("05_failed");
    fresh();
    safe_start = 1;
    btp.sel = 0;
    ui.menu = 3;
    bt_page_input(1u << panel.btn[B_OCTUP]);         /* (safe start: ui_input shows no menu; the page refuses too) */
    check("safe start: BLUETOOTH ON says NOT IN SAFE START, nothing called",
          btp.msg && str_eq(btp.msg, "NOT IN SAFE START") && !bs.starts && !bs.targets && !bs.wants && !btp.on);
    safe_start = 0;
    fresh();
    open_page();
    row(3);
    tap(B_OCTUP);
    check("SCAN while BLUETOOTH is OFF: refused with a message", !bs.scans && btp.msg && str_eq(btp.msg, "BLUETOOTH IS OFF"));
}

static void test_mode(void)
{
    static const int8_t TURN[5] = {1, 1, 1, -1, -1};
    static const uint8_t WANT[5] = {1, 2, 2, 1, 0};
    uint32_t i, lo, hi, ok = 1;
    fresh();                                         /* (erased flash: no settings record yet) */
    open_page();
    row(1);
    for (i = 0; i < 5u; i++) {
        knob(TURN[i]);
        ok &= btset.btmode == WANT[i] && rec_bt(&lo, &hi) == 0xFFFFFFFFu;
    }
    check("MODE: KNOB 1 steps AUTO SMC DAW MIDI, stops at the ends; no flash write per step", ok);
    tap(B_OCTUP);
    tap(B_OCTUP);
    check("MODE: OCT+ steps", btset.btmode == 2u && rec_bt(&lo, &hi) == 0xFFFFFFFFu);
    tap(B_OCTUP);
    check("MODE: OCT+ wraps to AUTO", btset.btmode == 0u && rec_bt(&lo, &hi) == 0xFFFFFFFFu);
    tap(B_OCTUP);
    tap(B_OCTDN);
    check("MODE: OCT- back to the menu's list: not written yet", ui.menu == 1 && rec_bt(&lo, &hi) == 0xFFFFFFFFu);
    menu_close();
    check("MODE: the menu closed: saved (the flash record)", ui.menu == 0 && rec_bt(&lo, &hi) == 1u);
    power_cycle();
    check("MODE survives a power cycle (the flash record)", btset.btmode == 1u);
    fresh();
    open_page();
    row(1);
    knob(1);
    knob(1);
    menu_close();                                    /* (HOME held on the page) */
    check("MODE: HOME held on the page closes the menu: saved", rec_bt(&lo, &hi) == 2u);
}

/* a pick takes the row on screen: the list changed after the last draw (the input runs before this frame's draw) */
static void test_pick_drawn(void)
{
    uint8_t a[6];
    fresh();
    bs.state = BLE_ST_SCANNING;
    open_page();
    knob(1);
    three();
    row(4);                                          /* (drawn: 4 = SMC-Mixer) */
    dev_set(0, A_JBL, 0, 1, -40, "JBL Flip 5");      /* the list changed: a MIDI device above the SMC */
    dev_set(1, A_SMC, 1, 1, -59, "SMC-Mixer");
    ui_input();                                      /* OCT+ before any draw of the new list */
    press(B_OCTUP);
    ui_input();
    release_all();
    saved(a);
    check("a pick takes the row last drawn, not the list as it is now", !memcmp(a, A_SMC, 6) && btset.bt_type == 1u &&
          !memcmp(bs.taddr, A_SMC, 6) && bs.ttype == 1);
}

static void test_list_pick_forget(void)
{
    uint8_t a[6];
    uint32_t lo, hi;
    fresh();
    bs.state = BLE_ST_IDLE;
    open_page();
    knob(1);                                         /* ON (the stack is up: not started again) */
    three();
    ui_frame();
    check("the list: MIDI devices first, then by signal (as the BLE unit sorts)", btp.n == 3 &&
          !memcmp(btp.dev[0].addr, A_SMC, 6) && !memcmp(btp.dev[1].addr, A_JBL, 6) && !memcmp(btp.dev[2].addr, A_ANON, 6));
    row(4);
    tap(B_OCTUP);
    saved(a);
    check("a device row, OCT+: remembered (address, type), the target, want on",
          !memcmp(a, A_SMC, 6) && btset.bt_type == 1u && bs.targets == 2 && !memcmp(bs.taddr, A_SMC, 6) &&
          bs.ttype == 1 && bs.want == 1);
    check("the pick is saved: the flash record holds it", rec_bt(&lo, &hi) != 0xFFFFFFFFu &&
          lo == btset.bt_lo && hi == btset.bt_hi && lo == (0x01u | 0x02u << 8 | 0x03u << 16));
    {
        char t[24];
        bt_dev_text(t);
        check("DEVICE: the remembered device's name once seen in a scan", str_eq(t, "SMC-Mixer"));
    }
    row(6);
    tap(B_OCTUP);
    saved(a);
    check("the third row (no name) picked: its address and type", !memcmp(a, A_ANON, 6) && btset.bt_type == 1u &&
          bs.targets == 3 && !memcmp(bs.taddr, A_ANON, 6));
    {
        char t[24];
        bt_dev_text(t);
        check("DEVICE: a device without a name shows its address, most significant byte first",
              str_eq(t, "C4:12:34:56:78:9A"));
    }
    power_cycle();
    saved(a);
    check("the remembered device survives a power cycle", !memcmp(a, A_ANON, 6) && btset.bt_type == 1u);
    row(2);
    knob(-1);
    saved(a);
    check("DEVICE, KNOB 1 left: FORGET (cleared, saved, the target none)",
          !btset.bt_lo && !btset.bt_hi && !btset.bt_type && bs.targets == 4 && !(bs.taddr[0] | bs.taddr[5]) &&
          rec_bt(&lo, &hi) == 0u && !lo && !hi);
    {
        char t[24];
        bt_dev_text(t);
        check("DEVICE: NONE", str_eq(t, "NONE"));
    }
    knob(-1);
    check("FORGET with none: nothing called", bs.targets == 4);
}

static void test_scan(void)
{
    fresh();
    bs.state = BLE_ST_IDLE;
    open_page();
    knob(1);
    row(3);
    tap(B_OCTUP);
    check("SCAN, OCT+: a scan (it pauses reconnecting)", bs.scans == 1);
    tap(B_OCTDN);
    check("OCT-: the pause ended", ui.menu == 1 && bs.pauses == 1 && bs.pause == 0);
}

static void test_addr_text(void)
{
    char t[24];
    bt_addr_txt(t, A_ANON, 17);
    check("address: AA:BB:CC:DD:EE:FF, most significant byte first", str_eq(t, "C4:12:34:56:78:9A"));
    bt_addr_txt(t, A_ANON, 16);
    check("address shortened when 17 characters do not fit", str_eq(t, "C4:12:..:9A"));
}

static void test_signature(void)
{
    fresh();
    bs.state = BLE_ST_SCANNING;
    bs.left = 9;
    open_page();
    knob(1);
    three();
    ui_frame();
    memset(fb, 0, sizeof fb);
    ui_frame();
    check("nothing changed: no redraw", lit(0, 240) == 0);
    bs.left = 8;
    ui_frame();
    check("the seconds left changed: redrawn", lit(21, 230) > 600);
    memset(fb, 0, sizeof fb);
    bs.dev[2].rssi = -70;
    ui_frame();
    check("an RSSI changed: redrawn", lit(21, 230) > 600);
    memset(fb, 0, sizeof fb);
    btset.btmode = 2;
    ui_frame();
    check("MODE changed: redrawn", lit(21, 230) > 600);
}

static void test_screens(void)
{
    fresh();
    bs.state = BLE_ST_SCANNING;
    bs.left = 7;
    remember(A_SMC, 1);
    open_page();
    knob(1);
    bs.state = BLE_ST_SCANNING;
    three();
    row(4);
    snap("02_scanning");
    fresh();
    bs.state = BLE_ST_SUBSCRIBED;
    remember(A_SMC, 1);
    btset.btmode = 0;
    three();
    conn_up = 1;
    memset(conn_name, 0, sizeof conn_name);
    str_cpy(conn_name, "SMC-Mixer", sizeof conn_name);
    open_page();
    knob(1);
    {
        char t[40];
        bt_state_text(t);
        check("connected: the state line says CONNECTED SMC-Mixer", str_eq(t, "CONNECTED SMC-Mixer"));
    }
    snap("03_connected");
    {                                                /* more than 8 found: 8 listed; the cursor scrolls */
        uint8_t a[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0xE0};
        uint32_t i;
        fresh();
        bs.state = BLE_ST_SCANNING;
        bs.left = 3;
        open_page();
        knob(1);
        for (i = 0; i < 12u; i++) {
            a[0] = (uint8_t)i;
            dev_set(i, a, 1, i == 5u || i == 9u, -40 - 6 * (int32_t)i, i == 1u ? "A VERY LONG DEVICE NAME 1" : i & 1u ? "" : "Speaker");
        }
        bs.ndev = 12;
        ui_frame();
        check("more than 8 found: 8 listed, the MIDI ones first", btp.n == 8 && btp.dev[0].midi && btp.dev[1].midi &&
              !btp.dev[2].midi && btp.dev[0].addr[0] == 5 && btp.dev[1].addr[0] == 9);
        row(11);
        check("the cursor on the last device: the rows scrolled", btp.top == 11u - BT_VIS + 1u);
        snap("06_scrolled");
    }
    fresh();
    bs.state = BLE_ST_IDLE;
    bs.err = "NO BLE-MIDI SERVICE";
    open_page();
    knob(1);
    btp.msg_t = 0;
    snap("04_idle_error");
    fresh();
    bs.state = BLE_ST_FAILED;
    bs.err = "NO RF TRIM IN FLASH";
    open_page();
    {
        char t[40];
        bt_state_text(t);
        check("failed: the state line says FAILED and the reason", str_eq(t, "FAILED NO RF TRIM IN FLASH"));
    }
}

int main(void)
{
    test_menu_row();
    test_on_off();
    test_refusals();
    test_mode();
    test_pick_drawn();
    test_list_pick_forget();
    test_scan();
    test_addr_text();
    test_signature();
    test_screens();
    printf(fails ? "bt_page_test: %d FAILED\n" : "bt_page_test: all passed\n", fails);
    return fails ? 1 : 0;
}
