/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* FELUCCA_BLE (felucca.c includes it first of the Bluetooth files, after ble/ble_core.h and storage.c): what the
 * Bluetooth page keeps over power-off (after felucca-ble's bt_set.c; docs/ble/PORTING.md rule 5).
 *   In the settings record, as before (both builds read and write them, project.c persist_boot / settings_save: the
 * btset bits, panel.c): MODE AUTO / SMC DAW / MIDI, AUTOSTART, and the one device the first Bluetooth page remembered.
 * Saved when the menu closes (menu_close), as the other menu settings. The record has no room for more (its spare
 * bits are taken) and is never grown: an older Hortator would refuse a longer one and lose every setting.
 *   In the Bluetooth store, a storage object of its own (bt_st): ROLE, RECONNECT, MODE FM-LINK (a flag over btset's
 * MODE, so an older firmware reads the MODE it last had), the saved devices (bt_devs.c), this FM-1's Bluetooth address
 * and the radio calibration's copy (bt_devs.c bt_mac, bt_rf_io). Two copies, flash 0x9F000 (the sector after the four
 * projects' 0x97000..0x9EFFF) and 0xFE000 (after the settings' 0xFC000..0xFDFFF), both inside the store's flash window
 * (fm1_flash.h FL_STORE_OK) and used by nothing else: user samples 0xA0000..0xDBFFF, MIDI LEARN and the user presets
 * 0xDC000..0xDFFFF, the update staging 0xE0000.., the stock config store 0xE5000..0xFBFFF (never written; the radio
 * calibration lives there), the loader's update-record sweep only erases sectors whose last 256 bytes hold a record
 * (an erased tail here). A write goes to the copy that is not the newest: erase, the record without its magic, then
 * the magic word last, then read back; a torn write leaves the other copy in charge (the newer valid sequence wins).
 * Its own magic and version; older firmware never reads it. No store yet (a first start of this build): the device the
 * settings record remembers becomes the first saved device (a controller; its name is filled in when a scan sees it,
 * bt_devs_frame), MODE and AUTOSTART are the record's. Written only from the main loop, never while the transport
 * runs (an erase holds the audio off): a change waits for the stop (bt_st_poll). */
#define BT_ST_MAGIC 0x31544248u                       /* "HBT1" */
#define BT_ST_VER 1u
#define BT_ST_A 0x9F000u
#define BT_ST_B 0xFE000u
#define BT_NDEV 8u
typedef struct { uint8_t addr[6], flags, rsv; char name[16]; } bt_st_dev_t;   /* flags: bit 0 a random address, bit
                                                                               * 1 an FM-1, bit 7 used */
typedef struct {
    uint32_t magic;
    uint16_t ver, len;                               /* BT_ST_VER, sizeof the record */
    uint32_t seq;                                    /* the newer valid copy wins (wrapping) */
    uint8_t set;                                     /* bit 0 ROLE DEVICE, bits 1-2 RECONNECT (0 AUTO 1 FAST 2 SLOW),
                                                      * bit 3 MODE FM-LINK */
    uint8_t rsv[3];
    bt_st_dev_t dev[BT_NDEV];                        /* the saved devices in the user's pick order (bt_devs.c) */
    uint8_t mac[8];                                  /* this FM-1's address, [6] 0xA5 when made (bt_mac) */
    uint8_t rf[72];                                  /* the radio calibration's 68 bytes, [68] 0xA5 when kept */
    uint32_t crc;                                    /* st_crc32 of everything before it */
} bt_st_rec_t;
_Static_assert(sizeof(bt_st_rec_t) == 292u, "the Bluetooth store's record");
static struct {
    uint8_t loaded, dirty, cur;                      /* cur: the copy now in charge (0 A, 1 B, 2 none) */
    uint8_t fails;                                   /* writes that did not read back (`ble` has the console) */
    bt_st_rec_t r;
} bt_st;

#if FELUCCA_FLASH
static uint32_t bt_st_sector(uint32_t copy) { return copy ? BT_ST_B : BT_ST_A; }
static int bt_st_valid(const bt_st_rec_t *r)
{
    return r->magic == BT_ST_MAGIC && r->ver == BT_ST_VER && r->len == sizeof *r &&
           r->crc == st_crc32(r, sizeof *r - 4u);
}
#endif

/* the store into RAM, once (the main loop: the page, the BLE unit's hooks); none valid: the first-generation
 * settings carried over */
static void bt_st_load(void)
{
    uint32_t i;
    if (bt_st.loaded)
        return;
    bt_st.loaded = 1;
    bt_st.cur = 2;
#if FELUCCA_FLASH
    if (flash_ok) {
        bt_st_rec_t c;
        int va, vb;
        va = !st_read(bt_st_sector(0), &bt_st.r, sizeof bt_st.r) && bt_st_valid(&bt_st.r);
        vb = !st_read(bt_st_sector(1), &c, sizeof c) && bt_st_valid(&c);
        if (vb && (!va || (c.seq != bt_st.r.seq && c.seq - bt_st.r.seq < 0x80000000u))) {
            bt_st.r = c;
            bt_st.cur = 1;
            return;
        }
        if (va) {
            bt_st.cur = 0;
            return;
        }
    }
#endif
    memset(&bt_st.r, 0, sizeof bt_st.r);
    if (btset.bt_lo | btset.bt_hi) {                 /* the remembered device: the first saved one (a controller) */
        bt_st_dev_t *d = &bt_st.r.dev[0];
        for (i = 0; i < 3u; i++) {
            d->addr[i] = (uint8_t)(btset.bt_lo >> (8u * i));
            d->addr[3u + i] = (uint8_t)(btset.bt_hi >> (8u * i));
        }
        d->flags = (uint8_t)(0x80u | (btset.bt_type & 1u));
    }
}

static void bt_st_save(void) { bt_st.dirty = 1; }   /* written by bt_st_poll (main loop, stopped) */

/* the main loop (core_ble_api.c bt_service): a change written once the transport is stopped */
static void bt_st_poll(void)
{
#if FELUCCA_FLASH
    uint32_t base, copy;
    bt_st_rec_t chk;
    if (!bt_st.dirty || !flash_ok || transport_busy())
        return;
    bt_st.dirty = 0;
    copy = bt_st.cur == 0u ? 1u : 0u;                /* the other copy (none yet: A) */
    base = bt_st_sector(copy);
    bt_st.r.magic = BT_ST_MAGIC;
    bt_st.r.ver = BT_ST_VER;
    bt_st.r.len = sizeof bt_st.r;
    bt_st.r.seq++;
    bt_st.r.crc = st_crc32(&bt_st.r, sizeof bt_st.r - 4u);
    if (st_erase(base) || st_prog(base + 4u, (const uint8_t *)&bt_st.r + 4u, 256u - 4u) ||
        st_prog(base + 256u, (const uint8_t *)&bt_st.r + 256u, sizeof bt_st.r - 256u) ||
        st_prog(base, &bt_st.r, 4u) ||               /* the magic last: the commit */
        st_read(base, &chk, sizeof chk) || memcmp(&chk, &bt_st.r, sizeof chk)) {
        bt_st.fails++;
        return;                                      /* (the other copy stays in charge) */
    }
    bt_st.cur = (uint8_t)copy;
#endif
}

enum { BT_MODE_AUTO, BT_MODE_SMC, BT_MODE_MIDI, BT_MODE_LINK };   /* LINK: FM-Link only, no MIDI either way */
enum { BT_RATE_AUTO, BT_RATE_FAST, BT_RATE_SLOW };   /* RECONNECT: a host link's rest after a drop (bt_set_slow) */
typedef struct { uint8_t mode, autostart, role, rate; } bt_set_t;   /* role: 1 DEVICE */

static void bt_set_get(bt_set_t *s)
{
    uint32_t set;
    bt_st_load();
    set = bt_st.r.set;
    s->mode = (uint8_t)(set & 8u ? BT_MODE_LINK : btset.btmode <= BT_MODE_MIDI ? btset.btmode : BT_MODE_AUTO);
    s->autostart = (uint8_t)(btset.bt_auto & 1u);
    s->role = (uint8_t)(set & 1u);
    s->rate = (uint8_t)(((set >> 1) & 3u) <= BT_RATE_SLOW ? (set >> 1) & 3u : BT_RATE_AUTO);
}

/* MODE (but FM-LINK) and AUTOSTART into btset (saved when the menu closes), the rest into the store (saved at once) */
static void bt_set_put(const bt_set_t *s)
{
    uint32_t set;
    bt_st_load();
    if (s->mode <= BT_MODE_MIDI)
        btset.btmode = s->mode;
    btset.bt_auto = s->autostart & 1u;
    set = (s->role & 1u) | (uint32_t)(s->rate <= BT_RATE_SLOW ? s->rate : 0u) << 1 | (s->mode == BT_MODE_LINK ? 8u : 0u);
    if (set != bt_st.r.set) {
        bt_st.r.set = (uint8_t)set;
        bt_st_save();
    }
}

/* RECONNECT: a host link rests longer after a drop or a failed connect (ble_fail_rest: 8 s, then 16 s): SLOW, or AUTO
 * while the transport plays (a mixer that dropped refuses for ~8 s, each refused try holds the audio off ~12 ms);
 * FAST, or AUTO stopped: 2 s doubling to 16 s */
static int bt_set_slow(int playing)
{
    bt_set_t s;
    bt_set_get(&s);
    return s.rate == BT_RATE_SLOW || (s.rate == BT_RATE_AUTO && playing);
}
