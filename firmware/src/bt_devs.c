/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* FELUCCA_BLE (felucca.c includes it after bt_fml.c): the saved Bluetooth devices (after felucca-ble's bt_devs.c;
 * docs/superpowers/specs/2026-10-22-bt-devices-design.md 5.2, 5.3 there). Up to BT_NDEV devices, controllers and FM-1s,
 * in the user's pick order (ADD A DEVICE, OCT+ "use now": to the top; connecting by itself never reorders), kept with
 * their names over power-off in the Bluetooth store (bt_set.c bt_st). Each frame the host's two links get their
 * targets: for each kind the first saved device that is around (seen by a scan in the last BT_AROUND_MS) or linked; a
 * device that dropped keeps its link's claim for BT_GRACE_MS, so no stand-in takes its place while it may still come
 * back; none around: the first saved device of the kind (its link scans for it, in windows, for as long as Bluetooth
 * is on). A saved device without a name (the one the first Bluetooth page remembered, carried over: bt_st_load) gets
 * the name, and its kind, the first time a scan lists it.
 *   And this FM-1's own Bluetooth address (bt_mac, core_ble_mac: ble_start), so other FM-1s can save it: made once (a
 * random static address) and kept in the store. And the radio calibration's copy (bt_rf_io, core_rf_keep_*: the BLE
 * unit hands it over whenever it finds it in the stock config store; Hortator never writes there, the copy is a
 * safeguard): 68 bytes and a mark in the store's 72. Main loop only. */
#define BT_AROUND_MS 20000u
#define BT_GRACE_MS 30000u
enum { BT_KIND_CTRL, BT_KIND_FM1 };
typedef struct { uint8_t addr[6], type, kind; char name[17]; } bt_dev_t;   /* name: up to 16 + NUL */
static struct {
    uint8_t up[2], up_addr[2][6];                   /* per kind: its link up last frame, on which address */
    uint8_t claim[2], claim_addr[2][6];             /* per kind: a dropped device's claim on its link, since claim_ms */
    uint32_t claim_ms[2];
    uint8_t page;                                   /* the page is open: scan (bt_devs_page) */
} bt_dv;

static int bt_dev_read(uint32_t i, bt_dev_t *d)    /* 1: a saved device in entry i */
{
    const bt_st_dev_t *r;
    bt_st_load();
    r = &bt_st.r.dev[i];
    if (!(r->flags & 0x80u))
        return 0;
    memcpy(d->addr, r->addr, 6);
    d->type = r->flags & 1u;
    d->kind = (r->flags >> 1) & 1u;
    memcpy(d->name, r->name, 16);
    d->name[16] = 0;
    return 1;
}
static uint32_t bt_devs_all(bt_dev_t *l)           /* the saved devices in order */
{
    uint32_t i, n = 0;
    for (i = 0; i < BT_NDEV; i++)
        n += (uint32_t)bt_dev_read(i, &l[n]);
    return n;
}
static void bt_devs_put(const bt_dev_t *l, uint32_t n)   /* the list written back (in order, the rest cleared), saved */
{
    uint32_t i;
    bt_st_load();
    for (i = 0; i < BT_NDEV; i++) {
        bt_st_dev_t *r = &bt_st.r.dev[i];
        memset(r, 0, sizeof *r);
        if (i < n) {
            memcpy(r->addr, l[i].addr, 6);
            r->flags = (uint8_t)(0x80u | (l[i].type & 1u) | (l[i].kind & 1u) << 1);
            memcpy(r->name, l[i].name, 16);
        }
    }
    bt_st_save();
}
static uint32_t bt_devs_n(void)
{
    bt_dev_t l[BT_NDEV];
    return bt_devs_all(l);
}
static int bt_devs_get(uint32_t i, bt_dev_t *d)
{
    bt_dev_t l[BT_NDEV];
    if (i >= bt_devs_all(l))
        return 0;
    *d = l[i];
    return 1;
}
static void bt_devs_add(const bt_dev_t *d)         /* to the top; the same address replaced; full: the least recently
                                                     * picked of its kind goes, else the last */
{
    bt_dev_t l[BT_NDEV + 1u];
    uint32_t n = bt_devs_all(l + 1), i, k;
    l[0] = *d;
    l[0].name[16] = 0;
    for (i = 1, k = 1; i <= n; i++)
        if (memcmp(l[i].addr, d->addr, 6))
            l[k++] = l[i];
    n = k;
    if (n > BT_NDEV) {
        for (i = n; --i > 0u && l[i].kind != d->kind;)
            ;
        k = i ? i : n - 1u;                        /* the oldest of its kind, else the oldest */
        for (i = k; i + 1u < n; i++)
            l[i] = l[i + 1u];
        n--;
    }
    bt_devs_put(l, n);
}
static void bt_devs_forget(uint32_t i)
{
    bt_dev_t l[BT_NDEV];
    uint32_t n = bt_devs_all(l), k;
    if (i >= n)
        return;
    for (k = i; k + 1u < n; k++)
        l[k] = l[k + 1u];
    bt_devs_put(l, n - 1u);
}
static void bt_devs_use(uint32_t i)                /* to the top (the user's pick) */
{
    bt_dev_t d;
    if (bt_devs_get(i, &d))
        bt_devs_add(&d);
}
static uint32_t bt_dev_kind(const char *name)      /* an FM-1 by its name (every FM-1 says so), else a controller */
{
    static const char N[] = "Felucca FM-1";
    uint32_t i;
    for (i = 0; N[i]; i++)
        if (name[i] != N[i])
            return BT_KIND_CTRL;
    return BT_KIND_FM1;
}
static void bt_devs_page(int on) { bt_dv.page = (uint8_t)(on != 0); }   /* the page open: scan for it */

/* the slot's link up on that address */
static int bt_slot_on(uint32_t kind, const uint8_t addr[6])
{
    ble_ui_slot_t x;
    return ble_ui_slot(kind, &x) && x.st == BLE_LS_UP && !memcmp(x.addr, addr, 6);
}
static int bt_dev_scanned(const uint8_t addr[6], ble_ui_dev_t *s)   /* in the scan list: 1 and its entry */
{
    uint32_t i;
    for (i = 0; ble_ui_dev(i, s); i++)
        if (!memcmp(s->addr, addr, 6))
            return 1;
    return 0;
}
static int bt_dev_around(const bt_dev_t *d)        /* seen by a scan in the last BT_AROUND_MS */
{
    ble_ui_dev_t s;
    return bt_dev_scanned(d->addr, &s) && fm1_ms - s.seen_ms <= BT_AROUND_MS;
}
static int bt_devs_state(uint32_t i)               /* 2 linked, 1 around, 0 away */
{
    bt_dev_t d;
    if (!bt_devs_get(i, &d))
        return 0;
    return bt_slot_on(d.kind, d.addr) ? 2 : bt_dev_around(&d);
}
/* the list index of kind's device: the first in order that is around, linked (index `linked`), or holds the claim
 * of a device that dropped less than BT_GRACE_MS ago; -1 none */
static int bt_devs_choose(uint32_t kind, const uint8_t *around, int linked, uint32_t now_ms)
{
    bt_dev_t l[BT_NDEV];
    uint32_t n = bt_devs_all(l), i;
    int claim = bt_dv.claim[kind] && now_ms - bt_dv.claim_ms[kind] < BT_GRACE_MS;
    for (i = 0; i < n; i++)
        if (l[i].kind == kind && (around[i] || (int)i == linked || (claim && !memcmp(l[i].addr, bt_dv.claim_addr[kind], 6))))
            return (int)i;
    return -1;
}
/* a saved device without a name (carried over from the first Bluetooth page) that a scan lists by name: the name and
 * its kind kept (saved) */
static void bt_devs_name(bt_dev_t *l, uint32_t n)
{
    ble_ui_dev_t s;
    uint32_t i, got = 0;
    for (i = 0; i < n; i++)
        if (!l[i].name[0] && bt_dev_scanned(l[i].addr, &s) && s.name[0]) {
            s.name[sizeof s.name - 1u] = 0;
            str_cpy(l[i].name, s.name, sizeof l[i].name);
            l[i].kind = (uint8_t)bt_dev_kind(s.name);
            got = 1;
        }
    if (got)
        bt_devs_put(l, n);
}
/* the main loop (core_ble_api.c bt_service): each link's target, the background scan wanted */
static void bt_devs_frame(void)
{
    static const uint8_t Z[6];
    bt_dev_t l[BT_NDEV];
    uint8_t around[BT_NDEV];
    uint32_t n = bt_devs_all(l), i, kind, scan = bt_dv.page ? 2u : 0u;   /* (2: the page's, continuous) */
    bt_devs_name(l, n);
    for (i = 0; i < n; i++)
        around[i] = (uint8_t)bt_dev_around(&l[i]);
    for (kind = 0; kind < 2u; kind++) {
        ble_ui_slot_t x;
        int linked = -1, c, first = -1;
        int up = ble_ui_slot(kind, &x) && x.st == BLE_LS_UP;
        if (up) {
            memcpy(bt_dv.up_addr[kind], x.addr, 6);
            bt_dv.claim[kind] = 0;
        } else if (bt_dv.up[kind]) {               /* it dropped: its claim, for the grace */
            bt_dv.claim[kind] = 1;
            bt_dv.claim_ms[kind] = fm1_ms;
            memcpy(bt_dv.claim_addr[kind], bt_dv.up_addr[kind], 6);
        }
        bt_dv.up[kind] = (uint8_t)up;
        for (i = 0; i < n; i++) {
            if (l[i].kind != kind)
                continue;
            if (first < 0)
                first = (int)i;
            if (up && !memcmp(l[i].addr, x.addr, 6))
                linked = (int)i;
        }
        c = bt_devs_choose(kind, around, linked, fm1_ms);
        if (c < 0)
            c = first;                             /* (none around: scan for the first) */
        if (c >= 0)
            ble_ui_slot_target(kind, l[c].addr, l[c].type);
        else
            ble_ui_slot_target(kind, Z, 0);
        if (up && linked != first && !scan)
            scan = 1;                              /* (on a lower one: look for the first, in windows) */
    }
    ble_ui_scan_want((int)scan);
}

/* the radio calibration's copy: put 1 keeps rec (saved, when it is another than the one kept), 0 reads it (1 = one is
 * kept) */
static int bt_rf_io(uint8_t *rec, int put)
{
    uint8_t *k;
    bt_st_load();
    k = bt_st.r.rf;
    if (!put) {
        if (k[68] != 0xA5u)
            return 0;
        memcpy(rec, k, 68);
        return 1;
    }
    if (k[68] != 0xA5u || memcmp(k, rec, 68)) {
        memcpy(k, rec, 68);
        k[68] = 0xA5u;
        bt_st_save();
    }
    return 1;
}

static void bt_mac(uint8_t mac[6])                 /* this FM-1's address: the stored one, else made now and stored */
{
    uint8_t *r;
    uint32_t i, x;
    bt_st_load();
    r = bt_st.r.mac;
    if (r[6] != 0xA5u) {
        x = fm1_ticks() ^ fm1_ms * 2654435761u;    /* (the moment Bluetooth was first turned on) */
        for (i = 0; i < 6u; i++) {
            x = x * 1103515245u + 12345u;
            r[i] = (uint8_t)(x >> 16);
        }
        r[0] |= 0xC0u;                             /* random static: the top two bits set, at whichever end */
        r[5] |= 0xC0u;                             /* the stack keeps the most significant byte */
        r[6] = 0xA5u;
        bt_st_save();
    }
    memcpy(mac, r, 6);
}
