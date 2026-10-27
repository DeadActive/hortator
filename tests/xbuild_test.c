/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The variations across the two builds (both have them, one flash format: the Bluetooth build keeps its two RAM banks
 * outside .bss, app_ble.ld .ram_bss, zeroed by persist_boot), on one host flash image handed from one build's binary to
 * the other's through a file (tests/run_drum_tests.sh):
 *   std-make IMG  (standard) slots A and B saved with variations (A: 4, B: 3), a long USR2 shortened by the boot
 *   ble IMG       (Bluetooth) boots on it: nothing written, no VARIATIONS LOST; LOAD A has its variation 4, the
 *                 black keys on HOME play variations, an edit SAVEd onto A keeps them; B has its variation 3 and gets
 *                 a variation 6, saved; SAVE held, white key 2 then a black key queues CUE>B3 and the switch plays it
 *   std-check IMG (standard) boots on what the Bluetooth build left: no VARIATIONS LOST, A has the Bluetooth build's
 *                 edit on variation 1 and its variation 4 as saved, B its variation 3 and the Bluetooth build's 6
 *   ble-alone     (Bluetooth) on its own: its RAM banks zeroed at power-on (whatever the RAM held), an 80 KiB USR2
 *                 shortened to 48 KiB once, as the standard build does; a variation saved survives a power cycle
 * Built with ASan + UBSan. */
#define FELUCCA_FLASH 1
#include <stdint.h>
static uint8_t hflash[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
#define VAR_XIP(off) ((const void *)(hflash + (off)))
#include "ui_host.h"
#if FELUCCA_BLE                                       /* the Bluetooth build's core (its BLE unit: the stubs) */
#include "ble_ui_stub.h"
uint32_t ble_midi_room(void) { return 64u; }
int ble_midi_send(const uint8_t *m, uint32_t n) { (void)m; (void)n; return 1; }
#include "../firmware/src/bt_set.c"
#include "../firmware/src/smc.c"
#include "../firmware/src/bt_midi.c"
#include "../firmware/src/bt_fml.c"
#include "../firmware/src/bt_devs.c"
#include "../firmware/src/bt_page.c"
#endif

static int fails;
static void check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}
static uint32_t rs = 1;
static uint32_t rnd(void) { return rs = rs * 1664525u + 1013904223u; }

#define U2 0xB4000u
static uint32_t rd32(uint32_t off) { uint32_t v; memcpy(&v, hflash + off, 4); return v; }
static int img_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(hflash, 1, sizeof hflash, f) : 0;
    if (f)
        fclose(f);
    return n == sizeof hflash;
}
static int img_save(const char *path)
{
    FILE *f = fopen(path, "wb");
    size_t n = f ? fwrite(hflash, 1, sizeof hflash, f) : 0;
    if (f)
        fclose(f);
    return n == sizeof hflash;
}
static void wipe_ram(void)                            /* a power cycle's RAM */
{
    memset(&mo, 0, sizeof mo);
    memset(proj_slot, 0, sizeof proj_slot);
    memset(lk_slot, 0, sizeof lk_slot);
    memset(var_buf, 0, sizeof var_buf);
    memset(var_who, 0, sizeof var_who);
    var_play = 0;
    var_dirty = 0;
    var_lost = 0;
    var_cur = 0;
    lk_lost = 0;
    persist_note = 0;
}
static void boot(void)                                /* power-on on the flash image as it is */
{
    host_init();
    memset(&ui, 0, sizeof ui);
    memset(&cue, 0, sizeof cue);
    wipe_ram();
    host_nflog = 0;
    host_fl_stray = 0;
    persist_boot();
    panel = PANEL_DEFAULT;                            /* (the buttons as ui_host_init has them) */
    ui.home = 1;
    ui.hmode = 1;
}
static void usr_image(uint32_t at, uint32_t data_len)   /* an upstream-style USR2: 3 zones, the last past 48 KiB */
{
    smp_user_hdr_t h;
    uint32_t i;
    memset(&h, 0, sizeof h);
    h.magic = SMP_USER_MAGIC;
    h.version = 1;
    h.nz = 3;
    memcpy(h.name, "BIGLOOP", 7);
    h.data_len = data_len;
    for (i = 0; i < 3u; i++) {
        smp_zone_t *z = &h.zone[i];
        z->off = i == 0 ? 0x100u : i == 1 ? 0xB000u : 0xC000u;
        z->n = i == 0 ? 0x2000u : i == 1 ? 0x4000u : 0x1000u;
        z->le = z->n - 1u;
        z->rate = 1u << 16;
        z->lo = (uint8_t)(i * 40u);
        z->hi = (uint8_t)(i * 40u + 39u);
    }
    for (i = 0; i < data_len; i++)
        hflash[at + 512u + i] = (uint8_t)(rnd() >> 24);
    h.crc = st_crc32(hflash + at + 512u, data_len);
    memcpy(hflash + at, &h, sizeof h);
}
static void slot_steps(uint32_t s)                    /* the live steps a slot is saved with: track 0 step s and 15 */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        memset(trk[k].step, 0, sizeof trk[k].step);
    trk[0].step[s].on = 1;
    trk[0].step[15].on = 1;
}
static void run_until_slot(uint32_t s)                /* playing: blocks until slot s is the loaded one (16 s at most) */
{
    uint32_t n;
    for (n = 0; n < 16u * FS / CTL && proj_loaded() != (int32_t)s; n++) {
        render_mix(0, 0, CTL);
        cue_poll();
    }
}

/* ---- the variations' content (both builds) */
static step_t VS[NTRK][NSTEP], VR[NTRK][NSTEP];
static int16_t VTM[NTRK][4], VTR[NTRK][4];
static void var_make(uint32_t seed)                   /* VS / VTM: a variation's content */
{
    uint32_t k, i;
    rs = seed;
    memset(VS, 0, sizeof VS);
    for (k = 0; k < NTRK; k++) {
        for (i = 0; i < NSTEP; i++)
            if ((rnd() >> 8) % 1000u < 200u) {
                VS[k][i].on = 1;
                VS[k][i].nt = (int8_t)((rnd() >> 8) % 24u);
            }
        VTM[k][0] = (int16_t)(1u + (seed + k) % 64u);
        VTM[k][1] = (int16_t)((seed + k) % 10u);
        VTM[k][2] = 0;
        VTM[k][3] = 0;
    }
}
static int var_put_made(uint32_t v)
{
    const step_t *sp[NTRK];
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        sp[k] = VS[k];
    return var_put(&var_buf[var_play], v, sp, VTM);
}
static int var_is_made(uint32_t v)
{
    step_t *rp[NTRK];
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        rp[k] = VR[k];
    return var_get(&var_buf[var_play], v, rp, VTR) == 1 && !memcmp(VTM, VTR, sizeof VTM) && !memcmp(VS, VR, sizeof VS);
}
static int live_is_made(void)                         /* the live steps are VS */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (memcmp(trk[k].step, VS[k], sizeof VS[k]))
            return 0;
    return 1;
}
static void bk_tap(uint32_t n)                        /* black key n (0..7: F#3 .. A#4) pressed and let go, a frame each */
{
    keys(1u << CUE_BK[n]);
    ui_frame();
    keys(0);
    ui_frame();
}

#if !FELUCCA_BLE
/* ---- the standard build */
static void std_make(const char *img)
{
    int ok;
    memset(hflash, 0xFF, sizeof hflash);
    rs = 77;
    usr_image(U2, 0x13E00u);                          /* upstream's 80 KiB USR2: this boot cuts it */
    boot();
    ok = (persist_note & PN_USR2) && rd32(U2 + 16u) == 0xBE00u && usr_nz[1] == 2;
    slot_steps(0);
    project_save(0);                                  /* A */
    var_make(41);
    ok &= var_put_made(3) == 0;                       /* A's variation 4 */
    project_save(0);
    slot_steps(1);
    project_save(1);                                  /* B (save as: A as flash has it) */
    var_make(42);
    ok &= var_put_made(2) == 0;                       /* B's variation 3 */
    project_save(1);
    ok &= proj_slot[0].var_len && proj_slot[1].var_len && str_eq(ui.msg, "SAVED");
    check("standard: A and B saved with variations (A: 4, B: 3), USR2 cut to 48 KiB", ok);
    check("standard: the image written", img_save(img));
}
static void std_check(const char *img)
{
    int ok = img_load(img);
    check("standard: the Bluetooth build's image read", ok);
    boot();
    ok = !(persist_note & PN_VARS) && !var_lost;
    check("standard after Bluetooth: no VARIATIONS LOST at power-on", ok);
    project_load(0);
    var_make(41);
    ok = str_eq(ui.msg, "LOADED") && trk[0].step[0].on && trk[0].step[8].on && trk[0].step[15].on && var_is_made(3);
    check("standard after Bluetooth: A has the Bluetooth build's edit (variation 1) and its variation 4 as saved", ok);
    project_load(1);
    var_make(42);
    ok = str_eq(ui.msg, "LOADED") && trk[0].step[1].on && var_is_made(2);
    var_make(43);
    ok &= var_is_made(5);
    check("standard after Bluetooth: B has its variation 3 and the Bluetooth build's variation 6", ok);
    check("standard after Bluetooth: USR2 still 48 KiB, two zones, nothing cut again", usr_nz[1] == 2 &&
          rd32(U2 + 16u) == 0xBE00u && !(persist_note & PN_USR2));
}
#else
/* ---- the Bluetooth build */
static void ble_round(const char *img)
{
    project_t a0;
    char b[16];
    uint32_t n;
    int ok = img_load(img);
    check("Bluetooth: the standard build's image read", ok);
    boot();
    check("Bluetooth: power-on writes nothing (USR2 already 48 KiB), no VARIATIONS LOST note",
          host_nflog == 0 && !(persist_note & (PN_USR2 | PN_USR2F | PN_VARS)) && !var_lost);
    check("Bluetooth: USR2 as the standard build cut it (48 KiB, two zones)", usr_nz[1] == 2 && rd32(U2 + 16u) == 0xBE00u);
    project_load(0);
    var_make(41);
    ok = str_eq(ui.msg, "LOADED") && var_cur == 0 && trk[0].step[0].on && trk[0].step[15].on && var_is_made(3);
    check("Bluetooth: LOAD A plays variation 1 and has its variation 4 as the standard build saved it", ok);
    trk[0].step[8].on = 1;                            /* an edit on variation 1, saved onto A */
    a0 = proj_slot[0];
    project_save(0);
    ok = str_eq(ui.msg, "SAVED") && proj_slot[0].var_len && proj_slot[0].var_link == a0.var_link && var_is_made(3);
    check("Bluetooth: SAVE onto A keeps its variations", ok);
    ui.home = 1;                                      /* HOME, stopped: a black key plays its variation at once */
    ui.hmode = 1;
    bk_tap(3);                                        /* C#4: variation 4 */
    ok = var_cur == 3 && live_is_made();
    bk_tap(0);                                        /* F#3: variation 1 (the edit) */
    ok &= var_cur == 0 && trk[0].step[8].on && trk[0].step[0].on;
    check("Bluetooth: the black keys on HOME play A's variation 4, then variation 1", ok);
    project_load(1);
    var_make(42);
    ok = str_eq(ui.msg, "LOADED") && var_is_made(2);
    var_make(43);
    ok &= var_put_made(5) == 0;                       /* B's variation 6, made here */
    project_save(1);
    ok &= str_eq(ui.msg, "SAVED") && proj_slot[1].var_len && var_is_made(5);
    check("Bluetooth: B has its variation 3; a variation 6 made here is saved", ok);
    project_load(0);
    transport_req = 1;                                /* play A; SAVE held, white key 2, then black key 3: B3 */
    render_mix(0, 0, CTL);
    release_all();
    press(B_SAVE);
    keys(1u << STEP_KEY[1]);
    ui_frame();
    fm1_in.buttons |= 1u << panel.btn[B_SAVE];
    ui_frame();
    keys(0);
    fm1_in.buttons |= 1u << panel.btn[B_SAVE];
    ui_frame();
    keys(1u << CUE_BK[2]);                            /* (then black key 3, SAVE still held) */
    fm1_in.buttons |= 1u << panel.btn[B_SAVE];
    ui_frame();
    keys(0);
    fm1_in.buttons |= 1u << panel.btn[B_SAVE];
    ui_frame();
    ok = ui.cue_map && cue.q_slot == 2u && cue.q_var == 3u;
    (void)head_mode(b);
    ok &= str_eq(b, "CUE>B3");
    check("Bluetooth: SAVE held, white key 2 then black key 3 queues B's variation 3 (CUE>B3)", ok);
    keys(0);
    release_all();
    ui_frame();
    run_until_slot(1);
    for (n = 0; n < 4u; n++)                          /* (the outgoing state filed) */
        cue_poll();
    var_make(42);
    ok = proj_loaded() == 1 && var_cur == 2 && live_is_made();
    check("Bluetooth: the cue switches to B at the pattern's end, playing its variation 3", ok);
    transport_req = 2;
    render_mix(0, 0, CTL);
    cue_poll();
    release_all();
    ui_frame();
    check("Bluetooth: the image written", img_save(img));
}
static void ble_alone(void)
{
    uint32_t i;
    int ok;
    memset(hflash, 0xFF, sizeof hflash);
    rs = 99;
    usr_image(U2, 0x13E00u);                          /* upstream's 80 KiB USR2 */
    host_init();
    memset(&ui, 0, sizeof ui);
    memset(&cue, 0, sizeof cue);
    wipe_ram();
    memset(var_buf, 0xA5, sizeof var_buf);            /* the banks' RAM as a reset leaves it (not .bss: not zeroed) */
    host_nflog = 0;
    host_fl_stray = 0;
    persist_boot();
    panel = PANEL_DEFAULT;
    ui.home = 1;
    ui.hmode = 1;
    for (ok = 1, i = 0; i < sizeof var_buf; i++)
        ok &= ((const uint8_t *)var_buf)[i] == 0;
    check("Bluetooth alone: the variation banks are zero after power-on, whatever the RAM held", ok);
    ok = (persist_note & PN_USR2) && rd32(U2 + 16u) == 0xBE00u && usr_nz[1] == 2;
    check("Bluetooth alone: an 80 KiB USR2 is shortened to 48 KiB once (USR2 SHORTENED TO 48K)", ok);
    slot_steps(0);
    project_save(0);
    var_make(44);
    ok = var_put_made(1) == 0;                        /* A's variation 2 */
    project_save(0);
    ok &= str_eq(ui.msg, "SAVED") && proj_slot[0].var_len;
    boot();                                           /* a power cycle */
    ok &= !(persist_note & (PN_VARS | PN_USR2)) && !var_lost;
    project_load(0);
    ok &= str_eq(ui.msg, "LOADED") && var_is_made(1);
    check("Bluetooth alone: a variation saved is there after a power cycle", ok);
    check("Bluetooth alone: no stray flash write", host_fl_stray == 0);
}
#endif

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "", *img = argc > 2 ? argv[2] : "";
#if !FELUCCA_BLE
    if (!strcmp(mode, "std-make"))
        std_make(img);
    else if (!strcmp(mode, "std-check"))
        std_check(img);
    else
        check("mode: std-make IMG | std-check IMG (the standard build)", 0);
#else
    if (!strcmp(mode, "ble"))
        ble_round(img);
    else if (!strcmp(mode, "ble-alone"))
        ble_alone();
    else
        check("mode: ble IMG | ble-alone (the Bluetooth build)", 0);
#endif
    printf(fails ? "xbuild_test %s: %d FAILED\n" : "xbuild_test %s: all passed\n", mode, fails);
    return fails ? 1 : 0;
}
