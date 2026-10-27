/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* FELUCCA_BLE (felucca.c includes it after ble/ble_core.h, bt_set.c, bt_midi.c, bt_fml.c and bt_devs.c; ui_menu.c
 * declares its entry points): the Bluetooth page, MENU > BLUETOOTH, ui.menu 3, in Hortator's own look (spec 2026-10-10
 * §2) with felucca-ble's rows and behaviour (its docs/superpowers/specs/2026-10-22-bt-devices-design.md 5.1). A state
 * line (and the last error), the rows BLUETOOTH, AUTOSTART, ROLE (HOST, DEVICE), MODE (AUTO, SMC DAW, MIDI, FM-LINK)
 * and RECONNECT (AUTO, FAST, SLOW: bt_set.c bt_set_slow), then (HOST) the saved devices, a line each (a dot when
 * connected, a ring when not, its name, CTRL or FM-1, ON / NEAR / AWAY), and + ADD A DEVICE. SELECT moves, KNOB 1
 * changes the row, OCT+ acts, OCT- back to the menu. On a device line KNOB 1 left asks FORGET <name>? on the state line
 * (OCT+ yes, OCT- no) and OCT+ uses it now (the first of its kind); + ADD A DEVICE opens the scan list of nearby
 * devices not saved (BLE-MIDI ones first), found in BT_ADD_MS, then kept as they are: OCT+ saves and connects one,
 * OCT- goes back (NOTHING FOUND: OCT+ scans again). ROLE DEVICE: the FM-1 is the BLE-MIDI device a host connects to;
 * no device lines then.
 * BLUETOOTH ON is the only call of ble_user_start in the core (gate 3: tools/check_ble_boot.py TRIGGERS has
 * bt_page_on); it is off at every power-on unless AUTOSTART is on: then bt_autostart turns it on once, 5 s after
 * power-on, from ui_input (gate 3 CALLERS), after a clean boot only. AUTOSTART and MODE (but FM-LINK) are saved with
 * the settings when the menu closes (btset); ROLE, RECONNECT, FM-LINK and the saved devices at once (bt_set.c's
 * store). Main loop only. */
enum { BTR_ON, BTR_AUTO, BTR_ROLE, BTR_MODE, BTR_RATE };
#define BT_SET_ROWS 5u                              /* BLUETOOTH AUTOSTART ROLE MODE RECONNECT, then (HOST) the
                                                     * devices, ADD */
static const char *const BT_NAME[BT_SET_ROWS] = {"BLUETOOTH", "AUTOSTART", "ROLE", "MODE", "RECONNECT"};
#define BT_SHOW 8u                                  /* the ADD list's lines (the scan keeps 12) */
#define BT_ADD_MS 8000u                             /* the ADD list's scan, then the list stays */
#define BT_VIS 9u                                   /* rows in view (the cursor scrolls them) */
#define BT_Y0 40                                    /* band y of the first row; the state line at 2, the error at 18 */
#define BT_DY 16
#define BT_VX 94                                    /* the value column */
#define BT_LABEL 15u                                /* a device line's name: 15 characters (x 26..146) */
static const char *const BT_MODE_N[4] = {"AUTO", "SMC DAW", "MIDI", "FM-LINK"};
static const char *const BT_RATE_N[3] = {"AUTO", "FAST", "SLOW"};
static struct {
    uint8_t on;                                     /* BLUETOOTH ON (not saved) */
    uint8_t sel, top;                               /* the cursor (a row), the first row in view */
    uint8_t confirm;                                /* FORGET asked for saved device confirm - 1 (0: none) */
    uint8_t add, asel, atop, an;                    /* the ADD list open, its cursor, its first in view, its devices */
    uint8_t msg_t;                                  /* frames the page's message stays in the head */
    const char *msg;
    uint32_t add_t0;                                /* the ADD list's scan start (BT_ADD_MS: then it stays) */
    ble_ui_dev_t adev[BT_SHOW];                     /* the ADD list as drawn: a pick takes these */
    uint32_t sig;
} btp;

static void bt_say(const char *s)
{
    btp.msg = s;
    btp.msg_t = 40;
}
static uint32_t bt_role(void) { bt_set_t s; bt_set_get(&s); return s.role; }   /* 1: ROLE DEVICE */
static uint32_t bt_page_rows(void) { return bt_role() ? BT_SET_ROWS : BT_SET_ROWS + bt_devs_n() + 1u; }

/* the top bar's Bluetooth icon (ui_draw.c draw_head): 0 none (Bluetooth off), 1 struck (on, nothing linked:
 * searching, reconnecting, the device role waiting), 2 lit (a link up) */
static uint32_t bt_head_icon(void)
{
    ble_ui_slot_t x;
    uint32_t i, st = ble_ui_state();
    if (!btp.on)
        return 0;
    if (st == BLE_ST_SUBSCRIBED || st == BLE_ST_RECEIVING)
        return 2;
    for (i = 0; i < 2u; i++)
        if (ble_ui_slot(i, &x) && x.st == BLE_LS_UP)
            return 2;
    return 1;
}

/* a name as the screen can show it: the font's characters only (a device's name is any bytes), n - 1 at most */
static void bt_name(char *o, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n && s[i]; i++)
        o[i] = s[i] >= 0x20 && s[i] < 0x7F ? s[i] : '?';
    o[i] = 0;
}
static char *bt_hex2(char *o, uint8_t v)
{
    static const char H[] = "0123456789ABCDEF";
    *o++ = H[v >> 4];
    *o++ = H[v & 15u];
    return o;
}
/* a saved device's line text (24 B): its name; an FM-1 (they all say Felucca FM-1) FM-1 and its address' last four
 * hex digits, most significant first, as `ble list` shows the address; no name yet (carried over from the first
 * Bluetooth page, bt_st_load): DEVICE and the same four digits */
static void bt_dev_label(const bt_dev_t *d, char *o)
{
    if (d->kind == BT_KIND_FM1 || !d->name[0]) {
        str_cpy(o, d->kind == BT_KIND_FM1 ? "FM-1 " : "DEVICE ", 24);
        *bt_hex2(bt_hex2(o + str_len(o), d->addr[1]), d->addr[0]) = 0;
    } else {
        bt_name(o, d->name, BT_LABEL + 1u);
    }
}
static const char *bt_dev_value(int st) { return st == 2 ? "ON" : st ? "NEAR" : "AWAY"; }

static void bt_state_text(char *o)                  /* the state line (40 B; 29 characters shown) */
{
    uint32_t st = ble_ui_state(), code, n, i;
    const char *e = ble_ui_err(&code);
    char t[24];
    bt_dev_t d;
    if (btp.confirm && bt_devs_get(btp.confirm - 1u, &d)) {
        bt_dev_label(&d, t);
        str_cpy(o, "FORGET ", 40);
        str_cpy(o + 7, t, 32);
        str_cpy(o + str_len(o), "?", 40 - str_len(o));
    } else if (st == BLE_ST_FAILED) {
        str_cpy(o, "FAILED ", 40);
        str_cpy(o + 7, e, 33);
    } else if (st == BLE_ST_STARTING) {
        str_cpy(o, "STARTING", 40);
    } else if (!btp.on || st == BLE_ST_OFF) {
        str_cpy(o, "OFF", 40);
    } else if (st == BLE_ST_ADVERTISING) {
        str_cpy(o, "WAITING FOR A HOST", 40);
    } else if (st == BLE_ST_SUBSCRIBED || st == BLE_ST_RECEIVING) {   /* the device role: its host */
        str_cpy(o, "CONNECTED ", 40);
        if (ble_ui_conn_name(t))
            t[23] = 0, bt_name(o + 10, t, 30);
    } else if (btp.add) {                           /* its scan's seconds left, then what it found */
        n = BT_ADD_MS - (fm1_ms - btp.add_t0);
        if ((int32_t)n > 0) {
            str_cpy(o, "SCANNING ", 40);
            fmt_int(o + 9, (int32_t)((n + 999u) / 1000u));
            str_cpy(o + str_len(o), " S", 40 - str_len(o));
        } else {
            str_cpy(o, btp.an ? "ADD: PICK A DEVICE" : "NOTHING FOUND", 40);
        }
    } else if (!bt_devs_n()) {
        str_cpy(o, "ON, NO DEVICE SAVED", 40);
    } else {
        for (i = 0, n = 0; i < 2u; i++) {
            ble_ui_slot_t x;
            n += ble_ui_slot(i, &x) && x.st == BLE_LS_UP;
        }
        if (n) {
            str_cpy(o, "ON, ", 40);
            fmt_int(o + 4, (int32_t)n);
            str_cpy(o + str_len(o), " CONNECTED", 40 - str_len(o));
        } else {
            str_cpy(o, "ON, SEARCHING", 40);
        }
    }
    o[29] = 0;
}

/* the ADD list: the BLE unit's scan list without the saved devices, BLE-MIDI devices first, each part by signal, the
 * first BT_SHOW; the cursor kept on the device it was on (the list re-sorts as signals change); after BT_ADD_MS it
 * stays as it is */
static int bt_saved(const uint8_t a[6])
{
    bt_dev_t d;
    uint32_t i;
    for (i = 0; bt_devs_get(i, &d); i++)
        if (!memcmp(d.addr, a, 6))
            return 1;
    return 0;
}
static void bt_add_list(void)
{
    ble_ui_dev_t d;
    uint8_t was[6];
    uint32_t i, k, pass, n = ble_ui_devs(), had = btp.asel < btp.an;
    if (fm1_ms - btp.add_t0 >= BT_ADD_MS)
        return;                                     /* (its scan is over: the list stays as it is) */
    if (had)
        memcpy(was, btp.adev[btp.asel].addr, 6);
    btp.an = 0;
    for (pass = 0; pass < 2u; pass++)
        for (i = 0; i < n; i++) {
            if (!ble_ui_dev(i, &d) || (d.midi != 0) != (pass == 0u) || bt_saved(d.addr) || btp.an >= BT_SHOW)
                continue;
            d.name[sizeof d.name - 1u] = 0;
            btp.adev[btp.an++] = d;
        }
    for (k = 0; had && k < btp.an; k++)
        if (!memcmp(btp.adev[k].addr, was, 6))
            btp.asel = (uint8_t)k;
    if (btp.asel >= btp.an)
        btp.asel = btp.an ? (uint8_t)(btp.an - 1u) : 0u;
}
static void bt_add_close(void) { btp.add = 0; }

/* BLUETOOTH ON: links wanted, the role handed over, the stack started if it is not (the user's trigger: the only
 * call of ble_user_start in the core, gate 3; from the page's input and bt_autostart only); the saved devices are the
 * targets (bt_devs_frame) */
__attribute__((noinline)) static void bt_page_on(void)
{
    if (safe_start) {
        bt_say("NOT IN SAFE START");
        return;
    }
    if (ble_ui_state() == BLE_ST_FAILED) {          /* the stack cannot stop: only a power cycle starts it again */
        bt_say("POWER OFF AND ON TO RETRY");
        return;
    }
    if (btp.on)
        return;
    btp.on = 1;
    ble_ui_role((int)bt_role());                    /* (applied once the stack is up and idle) */
    ble_ui_want(1);
    if (ble_ui_state() == BLE_ST_OFF)
        ble_user_start();
}

/* AUTOSTART ON: BLUETOOTH ON once, 5 s after power-on (fm1_ms: the UI has run by then), as if chosen on the page.
 * Called every UI frame from ui_input only, never from the boot (felucca_init, drum_boot_init), the service hook or an
 * ISR (gate 3: CALLERS). Not in safe start, nor while OCT- + OCT+ are held (UPDATE MODE's count); nothing when
 * Bluetooth was started by hand before (or the stack is not OFF). Skipped, BT AUTOSTART SKIPPED in the top bar once,
 * after a boot that was not clean (bt_boot_clean: the last one failed in its first 30 s, where AUTOSTART's start
 * runs, or a hang reset the FM-1), so a Bluetooth start that crashes or hangs is not tried again by itself and the way
 * to UBOOT and the updater stays clear; BLUETOOTH ON by hand still starts it */
#ifndef BT_BOOT_CLEAN
#define BT_BOOT_CLEAN() (!bootguard.failed && !(fm1_boot.p3_rst & 4u))   /* (main.c bootguard; WDT: fm1_sys.h) */
#endif
static uint8_t bt_auto_done;                        /* the 5 s point passed (.bss: 0 at power-on) */
static uint8_t bt_boot_clean;                       /* bt_service's first pass: BT_BOOT_CLEAN (bootguard is cleared at
                                                     * 30 s) */
__attribute__((noinline)) static void bt_autostart(void)
{
    bt_set_t s;
    uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
    if (bt_auto_done || fm1_ms < 5000u)
        return;
    bt_auto_done = 1;
    bt_set_get(&s);
    if (!s.autostart || safe_start || btp.on || ble_ui_state() != BLE_ST_OFF || (fm1_in.buttons & both) == both)
        return;
    if (!bt_boot_clean) {
        ui_message("BT AUTOSTART SKIPPED");
        return;
    }
    bt_page_on();
}

static void bt_page_open(void)                      /* MENU > BLUETOOTH, OCT+ */
{
    ui.menu = 3;
    btp.sel = btp.top = btp.confirm = 0;
    bt_add_close();
    bt_devs_page(!bt_role());                       /* (HOST: scan while the page is open: its ON / NEAR / AWAY) */
    ui.force = 1;
}

static void bt_page_close(void)                     /* OCT- or HOME held: the page's scan and list end */
{
    btp.confirm = 0;
    btp.msg_t = 0;
    bt_add_close();
    bt_devs_page(0);
}

static uint32_t bt_step(uint32_t v, int32_t s, uint32_t ok, uint32_t max)   /* a knob steps, stops at the ends; OCT+
                                                                             * wraps */
{
    return s > 0 ? (v < max ? v + 1u : max) : s < 0 ? (v ? v - 1u : 0u) : ok ? (v < max ? v + 1u : 0u) : v;
}

static void bt_page_input(uint32_t pressed)
{
    uint32_t ok = (pressed >> panel.btn[B_OCTUP]) & 1u, back = (pressed >> panel.btn[B_OCTDN]) & 1u;
    uint32_t rows = bt_page_rows(), st = ble_ui_state(), k, n = bt_devs_n();
    int32_t s, sel;
    bt_set_t set;
    sel = panel_enc(EN_SELECT);
    s = panel_enc(EN_K1);
    if (btp.add) {                                  /* the ADD list: SELECT moves, OCT+ adds, OCT- back */
        if (sel && btp.an)
            btp.asel = (uint8_t)((btp.asel + (sel > 0 ? 1u : btp.an - 1u)) % btp.an);
        if (back) {
            bt_add_close();
        } else if (ok && !btp.an) {
            btp.add_t0 = fm1_ms;                    /* (nothing found: OCT+ scans again) */
        } else if (ok && btp.asel < btp.an) {
            const ble_ui_dev_t *a = &btp.adev[btp.asel];
            bt_dev_t d;
            memset(&d, 0, sizeof d);
            memcpy(d.addr, a->addr, 6);
            d.type = a->type & 1u;
            d.kind = (uint8_t)bt_dev_kind(a->name);
            bt_name(d.name, a->name, sizeof d.name);
            bt_devs_add(&d);                        /* (first of its kind: bt_devs_frame connects it) */
            bt_add_close();
            btp.sel = BT_SET_ROWS;                  /* on its line */
        }
        return;
    }
    if (btp.confirm) {                              /* FORGET <name>?: OCT+ yes, OCT- no; a knob or SELECT no */
        if (!ok && !back && !s && !sel)
            return;                                 /* (called every frame: waits for an answer) */
        k = btp.confirm - 1u;
        btp.confirm = 0;
        if (ok)
            bt_devs_forget(k);
        if (btp.sel >= bt_page_rows())
            btp.sel = (uint8_t)(bt_page_rows() - 1u);
        return;
    }
    if (back) {
        bt_page_close();
        ui.menu = 1;
        ui.force = 1;
        return;
    }
    if (sel)                                        /* one row per detent, wrapping */
        btp.sel = (uint8_t)((btp.sel + (sel > 0 ? 1u : rows - 1u)) % rows);
    switch (btp.sel) {
    case BTR_ON:                                    /* BLUETOOTH: KNOB 1 right ON, left OFF; OCT+ toggles */
        if (s > 0 || (ok && !btp.on)) {
            bt_page_on();
        } else if ((s < 0 || ok) && btp.on) {
            btp.on = 0;
            ble_ui_want(0);
        }
        break;
    case BTR_AUTO:                                  /* AUTOSTART: KNOB 1 right ON, left OFF; OCT+ toggles. Saved
                                                     * when the menu closes (menu_close), as MODE; acts at power-on */
        bt_set_get(&set);
        set.autostart = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : ok ? !set.autostart : set.autostart);
        bt_set_put(&set);
        break;
    case BTR_ROLE:                                  /* ROLE: KNOB 1 right DEVICE, left HOST; OCT+ toggles; saved */
        bt_set_get(&set);
        k = s > 0 ? 1u : s < 0 ? 0u : ok ? !set.role : set.role;
        if (k != set.role) {
            set.role = (uint8_t)k;
            bt_set_put(&set);
            ble_ui_role((int)k);                    /* (the BLE unit ends its links, a scan or advertising, then
                                                     * switches) */
            bt_devs_page(!k);
            btp.top = 0;
        }
        break;
    case BTR_MODE:                                  /* MODE, RECONNECT: KNOB 1 steps, stops at the ends; OCT+ wraps */
    case BTR_RATE:
        bt_set_get(&set);
        if (btp.sel == BTR_MODE)
            set.mode = (uint8_t)bt_step(set.mode, s, ok, BT_MODE_LINK);
        else
            set.rate = (uint8_t)bt_step(set.rate, s, ok, BT_RATE_SLOW);
        bt_set_put(&set);
        break;
    default:
        k = btp.sel - BT_SET_ROWS;
        if (k == n) {                               /* + ADD A DEVICE */
            if (ok && st == BLE_ST_FAILED)
                bt_say("POWER OFF AND ON TO RETRY");
            else if (ok && !btp.on)
                bt_say("BLUETOOTH IS OFF");
            else if (ok) {
                btp.add = 1;
                btp.asel = btp.atop = btp.an = 0;
                btp.add_t0 = fm1_ms;
            }
        } else if (s < 0) {                         /* a device: KNOB 1 left asks FORGET */
            btp.confirm = (uint8_t)(k + 1u);
        } else if (ok) {                            /* OCT+: use it now (around, not linked) */
            int at = bt_devs_state(k);
            if (at == 1)
                bt_devs_use(k);
            else
                bt_say(at ? "CONNECTED" : "NOT AROUND");
        }
        break;
    }
}

static uint32_t bt_hash(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *b = p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

/* one line at band y: its label at x (a dot or a ring before it: dot 2 connected, 1 saved, 0 none), a tag, its value
 * at the right; the cursor's line inverted */
static void bt_line(int32_t y, int sel, const char *label, const char *tag, const char *value, int dot, int dim)
{
    uint16_t pen = sel ? C_BG : C_INK;
    int32_t x = 14;
    if (sel)
        cv_rect(2, y - 2, 236, 14, C_INK);
    if (dot) {
        if (dot > 1)
            cv_rect(14, y + 1, 8, 8, pen);
        else
            cv_frame(14, y + 1, 8, 8, pen);
        x = 26;
    }
    md_text(x, y, &MD_F3, 2, label, pen);
    if (dim)                                        /* (unavailable: dithered, as the old SCAN) */
        cv_dither(x, y, md_text_w(&MD_F3, 2, label), 10, sel ? C_INK : C_BG);
    if (tag)
        md_text(150, y, &MD_F3, 2, tag, pen);
    if (value[0])
        md_text(234 - md_text_w(&MD_F3, 2, value), y, &MD_F3, 2, value, pen);
}

/* the key hints at y: (key, action) pairs */
static void bt_keys(const char *const *k, uint32_t n)
{
    uint32_t i;
    int32_t x = 4;
    for (i = 0; i < n; i++)
        x = cv_keycap(x, 196, k[2u * i], k[2u * i + 1u], C_INK) + 8;
}

/* the page; drawn again when its signature changes: the state line, the last error, the rows, the saved devices'
 * lines and states, the ADD list, the cursor, the head's message, the palette */
static void bt_page_draw(void)
{
    static const char *const KEYS[] = {"SEL", "", "OCT+", "OK", "OCT-", "BACK"};
    static const char *const DKEYS[] = {"K1", "FORGET", "OCT+", "USE", "OCT-", "BACK"};
    static const char *const AKEYS[] = {"SEL", "", "OCT+", "ADD", "OCT-", "BACK"};
    static const char *const CKEYS[] = {"OCT+", "YES", "OCT-", "NO"};
    char st[40], err[24], lb[24], t[8];
    const char *title = "BLUETOOTH", *e, *const *keys = KEYS;
    uint32_t code, sbt = ble_ui_state(), i, pass, h, n, rows, nk = 3;
    uint8_t ds[BT_NDEV];
    bt_set_t set;
    bt_dev_t d;
    int scan_ok;
    if (btp.add)
        bt_add_list();
    bt_set_get(&set);
    n = set.role ? 0u : bt_devs_n();
    rows = bt_page_rows();
    if (btp.sel >= rows)                            /* (a device forgotten, the role changed) */
        btp.sel = (uint8_t)(rows - 1u);
    if (btp.sel < btp.top)
        btp.top = btp.sel;
    else if (btp.sel >= btp.top + BT_VIS)
        btp.top = (uint8_t)(btp.sel - BT_VIS + 1u);
    if (btp.asel < btp.atop)
        btp.atop = btp.asel;
    else if (btp.asel >= btp.atop + BT_VIS)
        btp.atop = (uint8_t)(btp.asel - BT_VIS + 1u);
    for (i = 0; i < n; i++)
        ds[i] = (uint8_t)bt_devs_state(i);
    bt_state_text(st);
    err[0] = 0;                                     /* the last error, under the state line (FAILED: on it) */
    e = ble_ui_err(&code);
    if (btp.on && sbt != BLE_ST_FAILED && e[0] && !btp.confirm) {
        str_cpy(err, e, 20);
        if (code && str_len(err) < 15u) {            /* DISCONNECTED 0x08 */
            uint32_t k = str_len(err);
            err[k] = ' ', err[k + 1] = '0', err[k + 2] = 'x';
            *bt_hex2(err + k + 3, (uint8_t)code) = 0;
        }
    }
    if (btp.msg_t)                                  /* messages: the page's, else the top bar's (BT CONNECTED ...) */
        btp.msg_t--, title = btp.msg;
    else if (ui.msg_t)
        ui.msg_t--, title = ui.msg;
    scan_ok = btp.on && sbt != BLE_ST_FAILED;
    if (btp.confirm)
        keys = CKEYS, nk = 2;
    else if (btp.add)
        keys = AKEYS;
    else if (btp.sel >= BT_SET_ROWS && btp.sel - BT_SET_ROWS < n)
        keys = DKEYS;
    h = bt_hash(2166136261u, st, str_len(st));
    h = bt_hash(h, err, str_len(err));
    h = bt_hash(h, title, str_len(title));
    h = bt_hash(h, &set, sizeof set);
    h = bt_hash(h, ds, n);
    h = bt_hash(h, bt_st.r.dev, sizeof bt_st.r.dev);
    h = bt_hash(h, btp.adev, btp.an * (uint32_t)sizeof btp.adev[0]);
    {
        uint32_t v[3] = {btp.on | (uint32_t)btp.sel << 8 | (uint32_t)btp.top << 16 | (uint32_t)btp.confirm << 24,
                         btp.add | (uint32_t)btp.asel << 8 | (uint32_t)btp.atop << 16 | (uint32_t)btp.an << 24,
                         settings.palette | (uint32_t)scan_ok << 16};
        h = bt_hash(h, v, sizeof v);
    }
    if (!ui.force && h == btp.sig)
        return;
    btp.sig = h;
    cv_begin(240, H_HEAD, C_INK);                    /* the title bar: an ink bar, the title (or a message) in BG */
    md_text(6, 3, &MD_F3, 2, title, C_BG);
    cv_blit(0, Y_HEAD);
    for (pass = 0; pass < 2u; pass++) {             /* the canvas holds 124 rows: two bands, screen rows 16..239 */
        uint32_t y0 = H_HEAD + pass * 124u, hh = pass ? 240u - H_HEAD - 124u : 124u;
        cv_begin_at(240, hh, y0, C_BG);
        cv_oy = -(int32_t)(y0 - H_HEAD - 1u);       /* (rows from the old origin, H_HEAD + 1) */
        cv_rect(0, -1, 1, 240, C_INK);
        cv_rect(239, -1, 1, 240, C_INK);
        cv_rect(0, 238 - H_HEAD, 240, 1, C_INK);
        md_text(4, 2, &MD_F3, 2, st, C_INK);
        if (sbt != BLE_ST_FAILED && !btp.confirm)   /* (the state not FAILED, no question: dithered) */
            cv_dither(4, 2, md_text_w(&MD_F3, 2, st), 10, C_BG);
        md_text(4, 18, &MD_F3, 2, err, C_INK);
        cv_hdot(1, 238, 36, C_INK);
        if (btp.add) {                              /* the ADD list over the rows: a device a line, BLE-MIDI tagged */
            for (i = btp.atop; i < btp.an && i < btp.atop + BT_VIS; i++) {
                const ble_ui_dev_t *a = &btp.adev[i];
                int32_t y = BT_Y0 + (int32_t)(i - btp.atop) * BT_DY;
                if (a->name[0])
                    bt_name(lb, a->name, BT_LABEL + 1u);
                else
                    *bt_hex2(bt_hex2(bt_hex2(lb, a->addr[5]), a->addr[4]), a->addr[3]) = 0;
                fmt_int(t, a->rssi);
                bt_line(y, i == btp.asel, lb, a->midi ? "MIDI" : 0, t, 0, 0);
            }
            if (!btp.an)
                md_text(14, BT_Y0 + 2 * BT_DY, &MD_F3, 2, fm1_ms - btp.add_t0 < BT_ADD_MS ? "LOOKING FOR DEVICES..." :
                        "OCT+: SCAN AGAIN", C_INK);
        } else {
            for (i = btp.top; i < rows && i < btp.top + BT_VIS; i++) {
                int32_t y = BT_Y0 + (int32_t)(i - btp.top) * BT_DY;
                int sel = i == btp.sel;
                if (i < BT_SET_ROWS) {
                    const char *v = i == BTR_ON ? (btp.on ? "ON" : "OFF") : i == BTR_AUTO ? (set.autostart ? "ON" : "OFF")
                                  : i == BTR_ROLE ? (set.role ? "DEVICE" : "HOST") : i == BTR_MODE ? BT_MODE_N[set.mode & 3u]
                                  : BT_RATE_N[set.rate % 3u];
                    uint16_t pen = sel ? C_BG : C_INK;
                    if (sel)
                        cv_rect(2, y - 2, 236, 14, C_INK);
                    md_text(14, y, &MD_F3, 2, BT_NAME[i], pen);
                    md_text(BT_VX, y, &MD_F3, 2, v, pen);
                } else if (i - BT_SET_ROWS < n && bt_devs_get(i - BT_SET_ROWS, &d)) {
                    uint32_t k = i - BT_SET_ROWS;
                    bt_dev_label(&d, lb);
                    bt_line(y, sel, lb, d.kind == BT_KIND_FM1 ? "FM-1" : "CTRL", bt_dev_value(ds[k]), ds[k] == 2 ? 2 : 1, 0);
                } else {
                    bt_line(y, sel, "+ ADD A DEVICE", 0, "SCAN", 0, !scan_ok);
                }
            }
        }
        bt_keys(keys, nk);
        cv_oy = 0;
        cv_blit(0, y0);
    }
}

/* the main loop's hook (felucca.c ed_service: every pass and in its 15 ms wait): the BT stack's slice (at most 5 ms,
 * ble/ble.c), then the core's own Bluetooth work, cheap while nothing changed */
static void bt_service(void)
{
    static uint8_t first = 1;
    if (first) {                                    /* (bootguard is cleared 30 s on: read it now) */
        first = 0;
        bt_boot_clean = (uint8_t)BT_BOOT_CLEAN();
    }
    ble_service();
    bt_devs_frame();                                /* the saved devices: each link's target, the scan wanted */
    bt_midi_frame();                                /* MIDI mode: what the keys and the sequencer sent, to the device */
    bt_fml_frame();                                 /* FM-Link: the role from CLK, its messages to the other FM-1 */
    smc_frame();                                    /* the SMC-Mixer's LEDs and fader positions */
    bt_st_poll();                                   /* the Bluetooth store written, once stopped */
}
