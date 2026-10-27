/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* The Bluetooth store and the saved devices (firmware/src/bt_set.c bt_st, firmware/src/bt_devs.c; after felucca-ble's
 * tests/bt_devs_test.c) on the host, built with FELUCCA_BLE=1, the UI harness and the host flash: the store's two
 * sectors are free (no other object, the user samples, the update staging, the stock config store; inside the store's
 * flash window; the loader's record sweep leaves them alone), A / B writes, the newest valid copy, a torn write, a damaged
 * copy, the sequence's wrap, nothing written while the transport runs; the list (order, the same address once, the full
 * list, forgetting, a long name), the kind by name, each link's device by the user's pick order with the 30 s grace;
 * this FM-1's address (made once, kept); the radio calibration's copy (kept only when it changes). The BLE unit is the
 * stub (ble_ui_stub.h: the scan list in `bs`, the slots and targets in `bh`). */
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
/* the store's flash window (hal/fm1_flash.h FL_STORE_OK, read from the header: it cannot be included on the host) */
static uint32_t fl_lim[4];                           /* FL_DATA_LO / HI, FL_GLOB_LO / HI */
static int fl_limits(void)
{
    static const char *const K[4] = {"#define FL_DATA_LO", "#define FL_DATA_HI", "#define FL_GLOB_LO", "#define FL_GLOB_HI"};
    char ln[256];
    uint32_t got = 0, k;
    FILE *f = fopen("firmware/hal/fm1_flash.h", "r");
    if (!f)
        return 0;
    while (fgets(ln, sizeof ln, f))
        for (k = 0; k < 4u; k++)
            if (!strncmp(ln, K[k], strlen(K[k]))) {
                fl_lim[k] = (uint32_t)strtoul(ln + strlen(K[k]), 0, 16);
                got |= 1u << k;
            }
    fclose(f);
    return got == 15u;
}
static int store_ok(uint32_t off, uint32_t n)        /* FL_STORE_OK */
{
    return (off >= fl_lim[0] && off + n <= fl_lim[1]) || (off >= fl_lim[2] && off + n <= fl_lim[3]);
}

static int fails;
static void ok(const char *what, int good)
{
    printf("%s  %s\n", good ? "ok  " : "FAIL", what);
    fails += !good;
}
static bt_dev_t mk(uint8_t a0, uint32_t kind, const char *name)
{
    bt_dev_t d;
    memset(&d, 0, sizeof d);
    d.addr[0] = a0;
    d.addr[1] = 0x11;
    d.addr[5] = 0x42;
    d.kind = (uint8_t)kind;
    str_cpy(d.name, name, sizeof d.name);
    return d;
}
static void fresh(void)                              /* erased flash, nothing loaded */
{
    memset(hflash, 0xFF, sizeof hflash);
    memset(&persist_saved, 0, sizeof persist_saved);
    ui_host_init();
    memset(&bt_st, 0, sizeof bt_st);
    persist_boot();
    memset(&bs, 0, sizeof bs);
    memset(&bh, 0, sizeof bh);
    memset(&bt_dv, 0, sizeof bt_dv);
    song.playing = 0;
    fm1_ms = 100000u;
}
static void reload(void)                             /* a power cycle: the store read again */
{
    memset(&bt_st, 0, sizeof bt_st);
    bt_st_load();
}
static void seen(uint32_t i, const bt_dev_t *d, uint32_t at_ms)   /* the scan list's entry i: d, seen at at_ms */
{
    memset(&bs.dev[i], 0, sizeof bs.dev[i]);
    memcpy(bs.dev[i].addr, d->addr, 6);
    str_cpy(bs.dev[i].name, d->name, sizeof bs.dev[i].name);
    bs.dev[i].seen_ms = at_ms;
    if (bs.ndev <= i)
        bs.ndev = i + 1u;
}
static void link_slot(uint32_t slot, const bt_dev_t *d)   /* the slot up on d (0: down) */
{
    memset(&bh.slot[slot], 0, sizeof bh.slot[slot]);
    if (d) {
        bh.slot[slot].st = BLE_LS_UP;
        memcpy(bh.slot[slot].addr, d->addr, 6);
    }
}
static int idx_of(uint8_t a0)
{
    bt_dev_t d;
    uint32_t i;
    for (i = 0; bt_devs_get(i, &d); i++)
        if (d.addr[0] == a0)
            return (int)i;
    return -1;
}
static int overlaps(uint32_t a, uint32_t an, uint32_t b, uint32_t bn) { return a < b + bn && b < a + an; }
static int erased(uint32_t off, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (hflash[off + i] != 0xFFu)
            return 0;
    return 1;
}
static uint32_t seq_of(uint32_t base) { bt_st_rec_t r; memcpy(&r, hflash + base, sizeof r); return r.seq; }

static void test_sectors(void)
{
    uint32_t o, c, k, clean = fl_limits();
    ok("sectors: the flash window read from hal/fm1_flash.h", clean);
    for (k = 0; k < 2u; k++) {
        uint32_t s = bt_st_sector(k);
        clean &= !(s & 0xFFFu) && store_ok(s, 0x1000u);
        for (o = 0; o < OBJ_COUNT; o++)
            for (c = 0; c < 2u; c++)
                clean &= !overlaps(s, 0x1000u, st_sector(o, c), 0x1000u);
        clean &= !overlaps(s, 0x1000u, SMP_USER_BASE, SMP_USER_SLOTS * SMP_USER_SIZE);   /* the user samples */
        clean &= !overlaps(s, 0x1000u, 0xD4000u, 0x8000u);                             /* the lock banks (0.24.0) */
        clean &= !overlaps(s, 0x1000u, 0xE0000u, 0x5000u);                             /* the update staging */
        clean &= !overlaps(s, 0x1000u, 0xE5000u, 0xFC000u - 0xE5000u);                 /* the stock config store */
        clean &= !overlaps(s, 0x1000u, 0x4000u, 0x93000u - 0x4000u);                   /* the app */
        clean &= s + 0x1000u <= 0xFF000u;                                              /* (below key_mac's sector) */
    }
    ok("sectors: 0x9F000 and 0xFE000, 4 KiB aligned, inside the store's flash window, no other object / the user "
       "samples / the lock banks / the staging / the stock store / the app", clean && BT_ST_A == 0x9F000u && BT_ST_B == 0xFE000u);
    ok("sectors: the record fits a sector's first two pages", sizeof(bt_st_rec_t) <= 512u);
}

static void test_store(void)
{
    bt_dev_t a = mk(0xA1, BT_KIND_CTRL, "SMC-Mixer"), d;
    uint8_t rec[292];
    fresh();
    bt_st_load();
    ok("store: none yet -> empty, nothing saved, nothing written", bt_devs_n() == 0u && bt_st.cur == 2u &&
       erased(BT_ST_A, 0x1000u) && erased(BT_ST_B, 0x1000u));
    bt_devs_add(&a);
    song.playing = 1;
    bt_st_poll();
    ok("store: not written while the transport runs", bt_st.dirty && erased(BT_ST_A, 0x1000u));
    song.playing = 0;
    bt_st_poll();
    ok("store: stopped -> copy A written, B untouched", !bt_st.dirty && bt_st.cur == 0u && !erased(BT_ST_A, 4u) &&
       erased(BT_ST_B, 0x1000u));
    ok("store: the sector's last 256 bytes stay erased (the loader's update-record sweep ignores it)",
       erased(BT_ST_A + 0xF00u, 0x100u));
    reload();
    ok("store: read back after a power cycle", bt_devs_n() == 1u && bt_devs_get(0, &d) && str_eq(d.name, "SMC-Mixer"));
    bt_devs_forget(0);
    bt_st_poll();
    ok("store: the next write goes to B (A kept)", bt_st.cur == 1u && seq_of(BT_ST_B) == seq_of(BT_ST_A) + 1u);
    reload();
    ok("store: the newer copy (B) wins", bt_devs_n() == 0u && bt_st.cur == 1u);
    bt_devs_add(&a);
    bt_st_poll();
    ok("store: then A again", bt_st.cur == 0u && seq_of(BT_ST_A) == seq_of(BT_ST_B) + 1u);
    /* a torn write: B erased and its record written but its magic (the commit) not */
    memcpy(rec, hflash + BT_ST_A, sizeof rec);
    memset(hflash + BT_ST_B, 0xFF, 0x1000u);
    memcpy(hflash + BT_ST_B + 4u, rec + 4u, sizeof rec - 4u);
    hflash[BT_ST_B + 8u] = (uint8_t)(rec[8] + 1u);   /* (a newer sequence) */
    reload();
    ok("store: a write torn before its magic -> the other copy in charge", bt_st.cur == 0u && bt_devs_n() == 1u);
    memcpy(hflash + BT_ST_B, rec, sizeof rec);
    hflash[BT_ST_B + 8u] = (uint8_t)(rec[8] + 1u);   /* a newer sequence, but the check no longer matches */
    reload();
    ok("store: a damaged copy (its check) is ignored", bt_st.cur == 0u);
    {                                                /* the sequence wraps */
        bt_st_rec_t r;
        memcpy(&r, hflash + BT_ST_A, sizeof r);
        r.seq = 0xFFFFFFFFu;
        r.crc = st_crc32(&r, sizeof r - 4u);
        memcpy(hflash + BT_ST_A, &r, sizeof r);
        r.seq = 0;
        r.dev[0].name[0] = 'X';
        r.crc = st_crc32(&r, sizeof r - 4u);
        memcpy(hflash + BT_ST_B, &r, sizeof r);
        reload();
        ok("store: the sequence wraps (0 after 0xFFFFFFFF is newer)", bt_st.cur == 1u && bt_devs_get(0, &d) &&
           d.name[0] == 'X');
    }
    hflash[BT_ST_A + 4u] = 2;                        /* (a future version) */
    memset(hflash + BT_ST_B, 0xFF, 0x1000u);
    reload();
    ok("store: another version is not read (empty)", bt_st.cur == 2u && bt_devs_n() == 0u);
    fresh();
    flash_ok = 0;
    bt_devs_add(&a);
    bt_st_poll();
    ok("store: no flash -> kept in RAM, nothing written", bt_devs_n() == 1u && erased(BT_ST_A, 0x1000u) &&
       erased(BT_ST_B, 0x1000u));
    flash_ok = 1;
}

static void test_list(void)
{
    bt_dev_t a = mk(0xA1, BT_KIND_CTRL, "SMC-Mixer"), b = mk(0xB2, BT_KIND_CTRL, "Xkey Air 25"),
             c = mk(0xC3, BT_KIND_CTRL, "nanoKEY"), f = mk(0xF1, BT_KIND_FM1, "Felucca FM-1"), d;
    uint32_t i;
    fresh();
    bt_devs_add(&a);
    bt_devs_add(&b);
    bt_devs_add(&c);
    ok("add_order: the last added first", bt_devs_n() == 3u && idx_of(0xC3) == 0 && idx_of(0xB2) == 1 && idx_of(0xA1) == 2);
    bt_devs_add(&a);
    ok("add_order: the same address again moves to the top, once", bt_devs_n() == 3u && idx_of(0xA1) == 0 &&
       idx_of(0xC3) == 1 && idx_of(0xB2) == 2);
    ok("add_order: the name and kind kept", bt_devs_get(0, &d) && str_eq(d.name, "SMC-Mixer") && d.kind == BT_KIND_CTRL);
    fresh();
    for (i = 0; i < 8u; i++) {
        bt_dev_t x = mk((uint8_t)(0x10u + i), BT_KIND_CTRL, "CTRL");
        bt_devs_add(&x);
    }
    {
        bt_dev_t x = mk(0x20, BT_KIND_CTRL, "NINTH");
        bt_devs_add(&x);
    }
    ok("evict: a ninth controller -> the least recently picked controller goes", bt_devs_n() == 8u &&
       idx_of(0x20) == 0 && idx_of(0x10) < 0 && idx_of(0x11) == 7);
    fresh();
    bt_devs_add(&f);
    for (i = 0; i < 7u; i++) {
        bt_dev_t x = mk((uint8_t)(0x10u + i), BT_KIND_CTRL, "CTRL");
        bt_devs_add(&x);
    }
    {
        bt_dev_t g = mk(0xF2, BT_KIND_FM1, "Felucca FM-1");
        bt_devs_add(&g);
    }
    ok("evict: full, a new FM-1 -> the old FM-1 goes, the controllers stay", bt_devs_n() == 8u && idx_of(0xF2) == 0 &&
       idx_of(0xF1) < 0 && idx_of(0x10) >= 0);
    fresh();
    bt_devs_add(&a);
    bt_devs_add(&b);
    bt_devs_add(&c);
    bt_devs_forget(1);
    ok("forget: the rest close up", bt_devs_n() == 2u && idx_of(0xC3) == 0 && idx_of(0xA1) == 1 && idx_of(0xB2) < 0);
    ok("forget: the entry it left is cleared", !bt_st.r.dev[2].flags && !bt_st.r.dev[2].addr[0]);
    bt_devs_use(1);
    ok("use: to the top", idx_of(0xA1) == 0 && idx_of(0xC3) == 1);
    ok("kind: Felucca FM-1 is an FM-1", bt_dev_kind("Felucca FM-1") == BT_KIND_FM1);
    ok("kind: anything else is a controller", bt_dev_kind("SMC-Mixer") == BT_KIND_CTRL && bt_dev_kind("") == BT_KIND_CTRL &&
       bt_dev_kind("Felucca") == BT_KIND_CTRL);
    fresh();
    {
        bt_dev_t x = mk(0x77, BT_KIND_CTRL, "W-0123456789-ABCDEFGHIJ");
        bt_devs_add(&x);
        ok("name_cut: a long name kept to 16", bt_devs_get(0, &d) && str_len(d.name) == 16u &&
           str_eq(d.name, "W-0123456789-ABC"));
    }
}

static void test_choose(void)
{
    bt_dev_t a = mk(0xA1, BT_KIND_CTRL, "SMC-Mixer"), b = mk(0xB2, BT_KIND_CTRL, "Xkey Air 25"),
             f = mk(0xF1, BT_KIND_FM1, "Felucca FM-1");
    uint8_t around[BT_NDEV];
    int k;
    fresh();
    bt_devs_add(&b);
    bt_devs_add(&a);
    memset(around, 0, sizeof around);
    around[0] = around[1] = 1;
    ok("choose_order: both around -> the first picked", bt_devs_choose(BT_KIND_CTRL, around, -1, fm1_ms) == 0);
    around[0] = 0;
    ok("choose_order: the first away -> the next", bt_devs_choose(BT_KIND_CTRL, around, -1, fm1_ms) == 1);
    ok("choose_order: an FM-1 is not a controller", bt_devs_choose(BT_KIND_FM1, around, -1, fm1_ms) == -1);
    fresh();
    bt_devs_add(&b);
    bt_devs_add(&a);
    seen(0, &a, fm1_ms);
    seen(1, &b, fm1_ms);
    link_slot(BLE_SLOT_CTRL, &a);
    bt_devs_frame();
    link_slot(BLE_SLOT_CTRL, 0);
    {
        uint32_t t = fm1_ms;
        seen(0, &a, t - 60000u);
        seen(1, &b, t);
        bt_devs_frame();
        ok("choose_grace: a dropped -> it stays the target (no stand-in)", !memcmp(bh.tgt[BLE_SLOT_CTRL], a.addr, 6));
        fm1_ms = t + 29000u;
        seen(1, &b, fm1_ms);
        bt_devs_frame();
        ok("choose_grace: 29 s on -> still waiting for it", !memcmp(bh.tgt[BLE_SLOT_CTRL], a.addr, 6));
        fm1_ms = t + 31000u;
        seen(1, &b, fm1_ms);
        bt_devs_frame();
        ok("choose_grace: 31 s on -> the next one around", !memcmp(bh.tgt[BLE_SLOT_CTRL], b.addr, 6));
        link_slot(BLE_SLOT_CTRL, &b);
        fm1_ms = t + 40000u;
        seen(0, &a, fm1_ms);
        bt_devs_frame();
        ok("choose_grace: the first back -> it takes the slot again", !memcmp(bh.tgt[BLE_SLOT_CTRL], a.addr, 6));
        ok("choose_grace: the order is the user's: a still first", idx_of(0xA1) == 0 && idx_of(0xB2) == 1);
    }
    fresh();
    bt_devs_add(&b);
    bt_devs_add(&a);
    seen(0, &a, fm1_ms);
    seen(1, &b, fm1_ms);
    link_slot(BLE_SLOT_CTRL, &a);
    bt_devs_frame();
    bt_devs_forget(0);
    bt_devs_frame();
    ok("forget_connected: the next around at once (no grace)", !memcmp(bh.tgt[BLE_SLOT_CTRL], b.addr, 6));
    fresh();
    bt_devs_add(&a);
    bt_devs_add(&f);
    seen(0, &a, fm1_ms);
    seen(1, &f, fm1_ms - 5000u);
    bt_devs_frame();
    ok("targets: the controller's slot -> the controller", !memcmp(bh.tgt[BLE_SLOT_CTRL], a.addr, 6));
    ok("targets: the FM-1's slot -> the FM-1", !memcmp(bh.tgt[BLE_SLOT_FM1], f.addr, 6));
    bt_devs_forget((uint32_t)idx_of(0xF1));
    bt_devs_frame();
    k = !bh.tgt[BLE_SLOT_FM1][0] && !bh.tgt[BLE_SLOT_FM1][1] && !bh.tgt[BLE_SLOT_FM1][5];
    ok("targets: no FM-1 saved -> no target", k);
    fresh();
    bt_devs_add(&b);
    bt_devs_add(&a);
    seen(1, &b, fm1_ms);
    link_slot(BLE_SLOT_CTRL, &b);
    bt_devs_frame();
    ok("scan_want: linked to a lower one -> a background scan for the first (windowed: 1)", bh.scan_want == 1);
    link_slot(BLE_SLOT_CTRL, &a);
    bt_devs_frame();
    ok("scan_want: on the first, the page closed -> none", bh.scan_want == 0);
    bt_devs_page(1);
    bt_devs_frame();
    ok("scan_want: the page open -> a continuous scan (2)", bh.scan_want == 2);
    bt_devs_page(0);
    fresh();
    bt_devs_add(&a);
    bt_devs_add(&b);
    seen(0, &a, fm1_ms - 21000u);
    link_slot(BLE_SLOT_CTRL, &b);
    ok("state: linked -> 2, not seen for 21 s -> 0", bt_devs_state((uint32_t)idx_of(0xB2)) == 2 &&
       bt_devs_state((uint32_t)idx_of(0xA1)) == 0);
    seen(0, &a, fm1_ms - 1000u);
    ok("state: seen a second ago -> around", bt_devs_state((uint32_t)idx_of(0xA1)) == 1);
    fresh();
    bt_devs_add(&b);
    bt_devs_add(&a);
    bt_devs_add(&f);
    bt_devs_frame();
    ok("targets_when_none_around: the first controller and the first FM-1 (scanned for)",
       !memcmp(bh.tgt[BLE_SLOT_CTRL], a.addr, 6) && !memcmp(bh.tgt[BLE_SLOT_FM1], f.addr, 6));
    fm1_ms += 120000u;
    bt_devs_frame();
    ok("targets_when_none_around: minutes later still (reconnect indefinitely)", !memcmp(bh.tgt[BLE_SLOT_CTRL], a.addr, 6));
}

static void test_mac_rf(void)
{
    uint8_t m1[6], m2[6], rec[68], got[68];
    uint32_t i;
    fresh();
    bt_mac(m1);
    ok("mac: none stored -> made, marked, to be saved", bt_st.r.mac[6] == 0xA5u && !memcmp(bt_st.r.mac, m1, 6) &&
       bt_st.dirty);
    bt_st_poll();
    host_ticks += 123457u;
    fm1_ms += 999u;
    bt_mac(m2);
    ok("mac: asked again (another moment) -> the same, nothing to save", !memcmp(m1, m2, 6) && !bt_st.dirty);
    ok("mac: a random static address (the top two bits set at both ends)", (m1[0] & 0xC0u) == 0xC0u &&
       (m1[5] & 0xC0u) == 0xC0u);
    reload();
    bt_mac(m2);
    ok("mac: kept over a power cycle", !memcmp(m1, m2, 6) && !bt_st.dirty);
    fresh();
    ok("rf: none kept", !bt_rf_io(got, 0));
    for (i = 0; i < 68u; i++)
        rec[i] = (uint8_t)(i * 7u + 3u);
    bt_rf_io(rec, 1);
    ok("rf: put -> kept, to be saved", bt_st.dirty && bt_rf_io(got, 0) && !memcmp(got, rec, 68));
    bt_st_poll();
    bt_rf_io(rec, 1);
    ok("rf: the same again -> nothing to save (no flash write at each power-on)", !bt_st.dirty);
    reload();
    ok("rf: kept over a power cycle", bt_rf_io(got, 0) && !memcmp(got, rec, 68));
    rec[5] ^= 1u;
    bt_rf_io(rec, 1);
    ok("rf: another record -> kept, saved", bt_st.dirty && bt_rf_io(got, 0) && got[5] == rec[5]);
    ok("rf: 72 bytes of the store (68 and the mark)", sizeof bt_st.r.rf == 72u);
}

int main(void)
{
    test_sectors();
    test_store();
    test_list();
    test_choose();
    test_mac_rf();
    printf(fails ? "bt_devs: %d FAILED\n" : "bt_devs: all checks passed\n", fails);
    return fails != 0;
}
