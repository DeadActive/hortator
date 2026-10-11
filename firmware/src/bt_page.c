/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* FELUCCA_BLE (felucca.c includes it after ble/ble_core.h; ui_menu.c declares its entry points): the Bluetooth page,
 * MENU > BLUETOOTH, ui.menu 3 (spec 2026-10-10 §2). A state line, then the rows BLUETOOTH, MODE, DEVICE, SCAN and up
 * to 8 devices found (BLE-MIDI ones first). SELECT moves, KNOB 1 changes the row, OCT+ acts, OCT- back to the menu.
 * BLUETOOTH ON is the only call of ble_user_start (gate 3: tools/check_ble_boot.py TRIGGERS has bt_page_on); it is
 * off at every power-on. The remembered device is saved with the settings (btset, project.c) at once, MODE when the
 * menu closes. */
#define BT_ROWS 4u                                  /* BLUETOOTH MODE DEVICE SCAN, then the devices */
#define BT_SHOW 8u                                  /* devices listed (the scan keeps 16) */
#define BT_VIS 9u                                   /* rows in view (the cursor scrolls them) */
#define BT_Y0 40                                    /* band y of the first row; the state line at 2, the error at 18 */
#define BT_DY 16
#define BT_VX 94                                    /* the value column: 17 characters */
static const char *const BT_MODE[3] = {"AUTO", "SMC DAW", "MIDI"};   /* btset.btmode */
static struct {
    uint8_t on;                                     /* BLUETOOTH ON (not saved) */
    uint8_t sel, top, n;                            /* the cursor, the first row in view, the devices listed */
    uint8_t msg_t;                                  /* frames the page's message stays in the head */
    const char *msg;
    char dname[24];                                 /* the remembered device's name, once seen this power-on */
    ble_ui_dev_t dev[BT_SHOW];                      /* the devices as drawn: a pick takes these */
    uint32_t sig;
} btp;

static void bt_say(const char *s)
{
    btp.msg = s;
    btp.msg_t = 40;
}

static int bt_saved(uint8_t a[6])                   /* the remembered device's address (btset); 0 = none */
{
    a[0] = (uint8_t)btset.bt_lo, a[1] = (uint8_t)(btset.bt_lo >> 8), a[2] = (uint8_t)(btset.bt_lo >> 16);
    a[3] = (uint8_t)btset.bt_hi, a[4] = (uint8_t)(btset.bt_hi >> 8), a[5] = (uint8_t)(btset.bt_hi >> 16);
    return (btset.bt_lo | btset.bt_hi) != 0;
}

/* AA:BB:CC:DD:EE:FF, the most significant byte first (ble_central.c put_dev); AA:BB:..:FF when max < 17 */
static void bt_addr_txt(char *o, const uint8_t a[6], uint32_t max)
{
    static const char H[] = "0123456789ABCDEF";
    uint32_t i;
    for (i = 0; i < 6u; i++) {
        o[i * 3u] = H[a[5u - i] >> 4];
        o[i * 3u + 1u] = H[a[5u - i] & 15u];
        o[i * 3u + 2u] = ':';
    }
    o[17] = 0;
    if (max < 17u)                                  /* AA:BB: .. :FF */
        o[6] = '.', o[7] = '.', o[8] = ':', o[9] = o[15], o[10] = o[16], o[11] = 0;
}

static void bt_dev_text(char *o)                    /* DEVICE: the name once seen, else the address; NONE (24 B) */
{
    uint8_t a[6];
    if (!bt_saved(a))
        str_cpy(o, "NONE", 24);
    else if (btp.dname[0])
        str_cpy(o, btp.dname, 18);
    else
        bt_addr_txt(o, a, 17);
}

static void bt_state_text(char *o)                  /* the state line (40 B; 29 characters shown) */
{
    uint32_t st = ble_ui_state(), code, n;
    const char *e = ble_ui_err(&code);
    char t[24];
    static const char *const ST[] = {"OFF", "STARTING", "IDLE", "SCANNING ", "CONNECTING", "CONNECTING",
                                     "CONNECTED ", "CONNECTED ", "FAILED "};
    str_cpy(o, st <= BLE_ST_FAILED ? ST[st] : "?", 40);
    if (!btp.on && (st == BLE_ST_IDLE || st == BLE_ST_OFF))
        str_cpy(o, "OFF", 40);
    n = str_len(o);
    if (st == BLE_ST_SCANNING) {
        fmt_int(o + n, (int32_t)ble_ui_scan_left());
        str_cpy(o + str_len(o), " S", 4);
    } else if (st == BLE_ST_FAILED) {
        str_cpy(o + n, e, 40 - n);
    } else if (st == BLE_ST_SUBSCRIBED || st == BLE_ST_RECEIVING) {
        if (!ble_ui_conn_name(t) || !t[0])
            bt_dev_text(t);
        t[23] = 0;
        str_cpy(o + n, t, 40 - n);
    }
    o[29] = 0;
}

/* the scan list, BLE-MIDI devices first, each part in the BLE unit's order (by signal), the first 8 kept; the
 * remembered device's name noted when it is listed */
static void bt_list(void)
{
    ble_ui_dev_t d;
    uint8_t a[6];
    uint32_t i, pass, n = ble_ui_devs(), have = bt_saved(a);
    char t[24];
    btp.n = 0;
    for (pass = 0; pass < 2u; pass++)
        for (i = 0; i < n && i < 16u; i++) {
            if (!ble_ui_dev(i, &d) || (d.midi != 0) != (pass == 0u))
                continue;
            if (have && d.name[0] && d.type == btset.bt_type && !memcmp(d.addr, a, 6))
                str_cpy(btp.dname, d.name, sizeof btp.dname);
            if (btp.n < BT_SHOW)
                btp.dev[btp.n++] = d;
        }
    if (have && !btp.dname[0] && ble_ui_conn_name(t) && t[0])   /* (linked: the target) */
        str_cpy(btp.dname, t, sizeof btp.dname);
}

/* BLUETOOTH ON: the remembered device the target, reconnecting wanted, the stack started if it is not (the user's
 * trigger: the only call of ble_user_start in the core, gate 3) */
__attribute__((noinline)) static void bt_page_on(void)
{
    uint8_t a[6];
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
    bt_saved(a);
    ble_ui_target(a, btset.bt_type);
    ble_ui_want(1);
    if (ble_ui_state() == BLE_ST_OFF)
        ble_user_start();
}

static void bt_pick(const ble_ui_dev_t *d)          /* a device row, OCT+: remembered (saved) and connected */
{
    btset.bt_lo = d->addr[0] | (uint32_t)d->addr[1] << 8 | (uint32_t)d->addr[2] << 16;
    btset.bt_hi = d->addr[3] | (uint32_t)d->addr[4] << 8 | (uint32_t)d->addr[5] << 16;
    btset.bt_type = d->type & 1u;
    str_cpy(btp.dname, d->name, sizeof btp.dname);
    settings_save();
    btp.on = 1;
    ble_ui_target(d->addr, btset.bt_type);
    ble_ui_want(1);
}

static void bt_forget(void)                         /* DEVICE, KNOB 1 left: none (saved); a link drops */
{
    static const uint8_t NONE[6];
    btset.bt_lo = btset.bt_hi = btset.bt_type = 0;
    btp.dname[0] = 0;
    settings_save();
    ble_ui_target(NONE, 0);
}

static void bt_page_open(void)                      /* MENU > BLUETOOTH, OCT+ */
{
    ui.menu = 3;
    btp.sel = btp.top = 0;
    ui.force = 1;
}

static void bt_page_close(void)                     /* OCT- or HOME held: reconnecting goes on */
{
    ble_ui_pause(0);
    btp.msg_t = 0;
}

static void bt_page_input(uint32_t pressed)
{
    uint32_t ok = (pressed >> panel.btn[B_OCTUP]) & 1u, rows = BT_ROWS + btp.n, st = ble_ui_state();
    int32_t s;
    if ((pressed >> panel.btn[B_OCTDN]) & 1u) {
        bt_page_close();
        ui.menu = 1;
        ui.force = 1;
        return;
    }
    if ((s = panel_enc(EN_SELECT)) != 0)            /* one row per detent, wrapping */
        btp.sel = (uint8_t)((btp.sel + (s > 0 ? 1u : rows - 1u)) % rows);
    s = panel_enc(EN_K1);
    switch (btp.sel) {
    case 0:                                         /* BLUETOOTH: KNOB 1 right ON, left OFF; OCT+ toggles */
        if (s > 0 || (ok && !btp.on)) {
            bt_page_on();
        } else if ((s < 0 || ok) && btp.on) {
            btp.on = 0;
            ble_ui_want(0);
        }
        break;
    case 1:                                         /* MODE: KNOB 1 steps, stops at the ends; OCT+ wraps. Saved when
                                                     * the menu closes (menu_close), not per step */
        btset.btmode = s > 0 ? (btset.btmode < 2u ? btset.btmode + 1u : 2u)
                     : s < 0 ? (btset.btmode ? btset.btmode - 1u : 0u) : ok ? (btset.btmode + 1u) % 3u : btset.btmode;
        break;
    case 2:                                         /* DEVICE: KNOB 1 left forgets it */
        if (s < 0 && (btset.bt_lo | btset.bt_hi))
            bt_forget();
        break;
    case 3:                                         /* SCAN (BLUETOOTH ON only) */
        if (ok && st == BLE_ST_FAILED)
            bt_say("POWER OFF AND ON TO RETRY");
        else if (ok && !btp.on)
            bt_say("BLUETOOTH IS OFF");
        else if (ok)
            ble_ui_scan();
        break;
    default:
        if (ok && btp.sel - BT_ROWS < btp.n && st != BLE_ST_FAILED)
            bt_pick(&btp.dev[btp.sel - BT_ROWS]);
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

/* the page; drawn again when its signature changes: the state line (state, seconds left, the connected name, the
 * reason), the last error, the list (count, addresses, RSSIs, names), the cursor, BLUETOOTH, MODE, the remembered
 * device, the head's message */
static void bt_page_draw(void)
{
    char st[40], dev[24], err[24], t[24];
    const char *title = "BLUETOOTH", *e;
    uint32_t code, sbt = ble_ui_state(), i, pass, h, v[5];
    int scan_ok;
    bt_list();
    if (btp.sel >= BT_ROWS + btp.n)                 /* the list shrank (a new scan) */
        btp.sel = (uint8_t)(BT_ROWS + btp.n - 1u);
    if (btp.sel < btp.top)
        btp.top = btp.sel;
    else if (btp.sel >= btp.top + BT_VIS)
        btp.top = (uint8_t)(btp.sel - BT_VIS + 1u);
    bt_state_text(st);
    bt_dev_text(dev);
    err[0] = 0;                                     /* the last error, under the state line (FAILED: on it) */
    e = ble_ui_err(&code);
    if (btp.on && sbt != BLE_ST_FAILED && e[0]) {
        str_cpy(err, e, 20);
        if (code && str_len(err) < 15u) {            /* DISCONNECTED 0x08 */
            static const char H[] = "0123456789ABCDEF";
            uint32_t k = str_len(err);
            err[k] = ' ', err[k + 1] = '0', err[k + 2] = 'x', err[k + 3] = H[(code >> 4) & 15u];
            err[k + 4] = H[code & 15u], err[k + 5] = 0;
        }
    }
    if (btp.msg_t)                                  /* messages: the page's, else the top bar's (BT CONNECTED ...) */
        btp.msg_t--, title = btp.msg;
    else if (ui.msg_t)
        ui.msg_t--, title = ui.msg;
    scan_ok = btp.on && sbt != BLE_ST_FAILED;
    v[0] = btp.on | (uint32_t)btp.sel << 8 | (uint32_t)btp.top << 16 | (uint32_t)btp.n << 24;
    v[1] = btset.btmode | btset.bt_type << 8 | settings.palette << 16;
    v[2] = btset.bt_lo, v[3] = btset.bt_hi, v[4] = (uint32_t)scan_ok;
    h = bt_hash(2166136261u, v, sizeof v);
    h = str_hash(str_hash(str_hash(str_hash(h, st), dev), title), err);
    h = bt_hash(h, btp.dev, btp.n * (uint32_t)sizeof btp.dev[0]);
    if (!ui.force && h == btp.sig)
        return;
    btp.sig = h;
    cv_begin(240, H_HEAD, C_BLACK);
    cv_text(4, 1, &FONT_S, title, C_HI);
    cv_blit(0, Y_HEAD);
    lcd_fill(0, H_HEAD, 240, 1, C_LINE);
    for (pass = 0; pass < 2u; pass++) {             /* the canvas holds 124 rows: two bands, rows 21..239 */
        cv_begin(240, pass ? 240u - H_HEAD - 1u - 124u : 124u, C_BLACK);
        cv_oy = pass ? -124 : 0;
        cv_text(4, 2, &FONT_S, st, sbt == BLE_ST_FAILED ? C_WHITE : C_AMB);
        cv_text(4, 18, &FONT_S, err, C_GRAY);
        cv_rect(0, 36, 240, 1, C_LINE);
        for (i = btp.top; i < BT_ROWS + btp.n && i < btp.top + BT_VIS; i++) {
            int32_t y = BT_Y0 + (int32_t)(i - btp.top) * BT_DY;
            int sel = i == btp.sel;
            uint16_t c = sel ? C_WHITE : C_GRAY;
            if (sel)
                cv_rect(4, y + 6, 3, 3, C_WHITE);
            if (i < BT_ROWS) {
                static const char *const NAME[BT_ROWS] = {"BLUETOOTH", "MODE", "DEVICE", "SCAN"};
                cv_text(14, y, &FONT_S, NAME[i], i == 3u && !scan_ok ? C_DIM : c);
                cv_text(BT_VX, y, &FONT_S, i == 0u ? (btp.on ? "ON" : "OFF") : i == 1u ? BT_MODE[btset.btmode % 3u]
                                           : i == 2u ? dev : "", C_HI);
            } else {
                const ble_ui_dev_t *d = &btp.dev[i - BT_ROWS];
                t[0] = (char)('1' + i - BT_ROWS), t[1] = 0;
                cv_text(14, y, &FONT_S, t, c);
                if (d->name[0])
                    str_cpy(t, d->name, 18);
                else
                    bt_addr_txt(t, d->addr, 17);
                cv_text(26, y, &FONT_S, t, sel ? C_WHITE : C_HI);
                if (d->midi)
                    cv_text(170, y, &FONT_S, "MIDI", C_AMB);
                fmt_int(t, d->rssi);
                cv_text(236 - text_w(&FONT_S, t), y, &FONT_S, t, C_GRAY);
            }
        }
        cv_text(4, 198, &FONT_S, "SELECT  OCT+ OK  OCT- BACK", C_DIM);
        cv_oy = 0;
        cv_blit(0, H_HEAD + 1 + pass * 124u);
    }
}
