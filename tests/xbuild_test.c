/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The variations across the two builds (the standard build has them, the Bluetooth build has not: core.h VAR_ON), on
 * one host flash image handed from one build's binary to the other's through a file (tests/run_drum_tests.sh):
 *   std-make IMG  (standard) slots A and B saved with variations (A: 3, B: 2), a long USR2 shortened by the boot
 *   ble IMG       (Bluetooth) boots on it: LOAD A plays variation 1, an edit SAVEd onto A keeps A's variation link,
 *                 a slot cue (SAVE + white key) switches A -> B and back, the black keys queue nothing (HOME, the cue
 *                 map), the map's black keys dark, the top line CUE A, SAVE onto another slot and PASTE link no
 *                 variations; 0xC0000..0xC7FFF never erased or programmed and byte for byte as it was; USR2 as
 *                 the standard build cut it (48 KiB, two zones)
 *   std-check IMG (standard) boots on what the Bluetooth build left: no VARIATIONS LOST, A has the Bluetooth build's
 *                 edit on variation 1 and its variation 4 as saved, B its variation 3
 *   ble-alone     (Bluetooth) on its own: an 80 KiB USR2 (upstream Felucca's) is not cut and plays whole (3 zones), no
 *                 flash write at boot; saves, cues and pastes never touch 0xC0000..0xC7FFF, a saved record links none
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
#include "../firmware/src/smc.c"
#include "../firmware/src/bt_midi.c"
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

#define VA 0xC0000u                                   /* the variations' flash, OBJ_VAR0..3 */
#define VN 0x8000u
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
#if VAR_ON
    memset(var_buf, 0, sizeof var_buf);
#endif
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
static int var_ops(void)                              /* erases / programs logged in 0xC0000..0xC7FFF */
{
    uint32_t i, n = 0;
    for (i = 0; i < host_nflog && i < HOST_NFLOG; i++)
        n += host_flog[i].off < VA + VN && host_flog[i].off + host_flog[i].n > VA;
    return (int)n + (host_nflog > HOST_NFLOG);       /* (a log past its end: counted as a miss) */
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

#if VAR_ON
/* ---- the standard build */
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
    check("standard after Bluetooth: B has its variation 3", ok);
    check("standard after Bluetooth: USR2 still 48 KiB, two zones, nothing cut again", usr_nz[1] == 2 &&
          rd32(U2 + 16u) == 0xBE00u && !(persist_note & PN_USR2));
}
#else
/* ---- the Bluetooth build */
static void ble_round(const char *img)
{
    static uint8_t vkeep[VN];
    project_t a0;
    char b[16];
    int ok = img_load(img);
    check("Bluetooth: the standard build's image read", ok);
    memcpy(vkeep, hflash + VA, VN);
    boot();
    check("Bluetooth: power-on writes nothing (no USR2 cut, nothing else), no VARIATIONS LOST note",
          host_nflog == 0 && !(persist_note & (PN_USR2 | PN_USR2F | PN_VARS)));
    check("Bluetooth: USR2 as the standard build cut it (48 KiB, two zones: nothing read past 0xBFFFF)",
          usr_nz[1] == 2 && rd32(U2 + 16u) == 0xBE00u && usr_zone[1][1].off - usr_zone[1][0].off + 0x100u < 0xBE00u);
    a0 = proj_slot[0];
    project_load(0);
    ok = str_eq(ui.msg, "LOADED") && var_cur == 0 && trk[0].step[0].on && trk[0].step[15].on && !trk[0].step[1].on;
    check("Bluetooth: LOAD A plays variation 1 (the record's steps)", ok);
    trk[0].step[8].on = 1;                            /* an edit, saved onto A */
    project_save(0);
    ok = str_eq(ui.msg, "SAVED") && proj_slot[0].var_len == a0.var_len && proj_slot[0].var_link == a0.var_link &&
         a0.var_len;
    check("Bluetooth: SAVE onto A keeps A's variation link (its record still links its variations)", ok);
    ui.home = 1;                                      /* HOME: the black keys queue nothing */
    ui.hmode = 1;
    keys(1u << 3);                                    /* G#3: variation 2 in the standard build */
    ui_frame();
    keys(0);
    ui_frame();
    check("Bluetooth: a black key on HOME queues nothing", !cue.q_var && !cue.q_slot && var_cur == 0);
    transport_req = 1;                                /* play; SAVE held + white key 2: slot B */
    render_mix(0, 0, CTL);
    release_all();
    press(B_SAVE);
    keys(1u << STEP_KEY[1]);
    ui_frame();
    fm1_in.buttons |= 1u << panel.btn[B_SAVE];
    ui_frame();
    keys((1u << STEP_KEY[1]) | (1u << 5));            /* + a black key: nothing in this build */
    ui_frame();
    ok = ui.cue_map && cue.q_slot == 2u && !cue.q_var;
    ok &= cue_led(5) == CM_OFF && cue_led(8) == CM_OFF && cue_led(12) == CM_OFF;
    (void)head_mode(b);
    ok &= str_eq(b, "CUE>B");
    check("Bluetooth: SAVE held + white key 2 queues slot B (CUE>B); the black keys queue no variation, dark", ok);
    keys(0);
    release_all();
    ui_frame();
    run_until_slot(1);
    ok = proj_loaded() == 1 && var_cur == 0 && trk[0].step[1].on && !trk[0].step[0].on;
    check("Bluetooth: the cue switches to B at the pattern's end, its variation 1", ok);
    (void)cue_slot(0);
    run_until_slot(0);
    ok = proj_loaded() == 0 && trk[0].step[8].on;
    transport_req = 2;
    render_mix(0, 0, CTL);
    cue_poll();
    release_all();
    press(B_SAVE);
    ui_frame();
    fm1_in.buttons |= 1u << panel.btn[B_SAVE];
    { uint32_t i; for (i = 0; i < 500u && !ui.cue_map; i++) ui_frame(); }
    (void)head_mode(b);
    ok &= str_eq(b, "CUE A");
    release_all();
    ui_frame();
    check("Bluetooth: cued back to A (its edit), stopped; the map's top line CUE A (no variation number)", ok);
    project_save(2);                                  /* A's live state saved onto C */
    ok = str_eq(ui.msg, "SAVED") && !proj_slot[2].var_len && !proj_slot[2].var_link;
    project_load(0);
    project_copy(0);
    project_paste(3);                                 /* A pasted onto D (RAM) */
    ok &= !proj_slot[3].var_len && !proj_slot[3].var_link;
    check("Bluetooth: SAVE onto another slot and PASTE link no variations (the target's stay in flash, untouched)", ok);
    project_load(1);                                  /* (D's paste: DISCARD EDITS? asked only on D) */
    project_save(1);
    ok = proj_slot[1].var_len != 0;
    check("Bluetooth: B saved again keeps its link", ok);
    check("Bluetooth: no erase / program in 0xC0000..0xC7FFF, its bytes as the standard build left them",
          var_ops() == 0 && !memcmp(vkeep, hflash + VA, VN));
    check("Bluetooth: the image written", img_save(img));
}
static void ble_alone(void)
{
    static uint8_t vkeep[VN];
    int ok;
    memset(hflash, 0xFF, sizeof hflash);
    rs = 99;
    usr_image(U2, 0x13E00u);                          /* upstream's 80 KiB USR2 */
    memcpy(vkeep, hflash + VA, VN);
    boot();
    ok = host_nflog == 0 && rd32(U2 + 16u) == 0x13E00u && usr_nz[1] == 3 && !(persist_note & PN_USR2);
    check("Bluetooth alone: an 80 KiB USR2 is not cut and plays whole (3 zones), nothing written at power-on", ok);
    slot_steps(0);
    project_save(0);
    slot_steps(1);
    project_save(1);
    transport_req = 1;
    render_mix(0, 0, CTL);
    (void)cue_slot(0);
    run_until_slot(0);
    transport_req = 2;
    render_mix(0, 0, CTL);
    cue_poll();
    project_copy(0);
    project_paste(2);
    ok = !proj_slot[2].var_len && !proj_slot[2].var_link;
    project_save_go(2);                               /* (SAVE onto the pasted slot: as OVERWRITE C*? YES) */
    ok &= !proj_slot[0].var_len && !proj_slot[1].var_len && !proj_slot[2].var_len && str_eq(ui.msg, "SAVED");
    check("Bluetooth alone: saved records link no variations", ok);
    check("Bluetooth alone: saves, cues and pastes never write 0xC0000..0xC7FFF (the USR2 sample there kept)",
          var_ops() == 0 && !memcmp(vkeep, hflash + VA, VN) && host_fl_stray == 0);
}
#endif

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "", *img = argc > 2 ? argv[2] : "";
#if VAR_ON
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
