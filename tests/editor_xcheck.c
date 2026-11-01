/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The whole web editor (editor.c with editor_backup.c and editor_pattern.c, as felucca.c builds it) on the host, with
 * the firmware's USB SysEx path and a host flash, driven by a script on stdin; tests/editor_wire.mjs runs the same
 * script through the page's mock device (web/editor/mock_device.mjs) and compares the replies byte for byte, so the
 * mock and web/EDITOR_PROTOCOL.md cannot drift from the firmware unnoticed. One command a line:
 *   flash upr|rec S|lk S|var S <hex>   a storage object written (storage.c st_save), as the mock's flash holds it
 *   boot                               a power cycle (persist_boot: the slots and user presets from flash)
 *   load S                             LOAD of slot S (the device's own)
 *   play | stop | song 0/1 | noflash 0/1 | park S|- | otarace | turn K ID V | advance MS
 *   frame <hex>                        a request through the SysEx path, one main-loop pass: prints its reply's hex
 *                                      ("-": none)
 *   pushes MS                          MS ms of main-loop passes (every 5 ms), then prints every push frame sent since
 *                                      the last "pushes" (frames, advance and these passes), hex, space-separated
 *                                      ("-": none). (Pushes need WATCH on; the order is compared sorted.) */
#define FELUCCA_FLASH 1
#define FELUCCA_OTA 1
#include <stdint.h>
static uint8_t hflash[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
#define VAR_XIP(off) ((const void *)(hflash + (off)))
#define ED_RESTORE_LOCK 1                             /* (ui.c asks editor_backup.c, as felucca.c builds it) */
#include "ui_host.h"
#undef FELUCCA_OTA
#define FELUCCA_OTA 1

static uint8_t wire[65536];
static uint32_t nwire, ota_race;
static void usb_host_take(void)
{
    while (so_r != so_w) {
        uint32_t pkt = sx_out_q[so_r % SXQ], cin = pkt & 15u, k = cin == 4u || cin == 7u ? 3u : cin - 4u, i;
        for (i = 0; i < k && nwire < sizeof wire; i++)
            wire[nwire++] = (uint8_t)(pkt >> (8u * (i + 1u)));
        so_r++;
    }
}
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { usb_host_take(); }
#define EDB_OTA_REQ() (ota_race != 0u)              /* (the upgrade command landing between ed_service's check and
                                                     * COMMIT's: the race editor_backup.c guards against) */
#include "../firmware/src/editor_proto.h"
#include "../firmware/src/editor_backup.c"
#include "../firmware/src/editor_pattern.c"
#define EDS_OTA_REQ() (ota_race != 0u)
#include "../firmware/src/editor_sample.c"
#include "../firmware/src/editor.c"

static uint32_t hex(const char *s, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (s[0] && s[1] && n < max) {
        unsigned v;
        if (sscanf(s, "%2x", &v) != 1)
            break;
        out[n++] = (uint8_t)v;
        s += 2;
    }
    return n;
}
static char pushed[1 << 16];                        /* the push frames since the last "pushes", hex */
static uint32_t npushed;
static void take(int reply)                         /* what went out: pushes kept; reply: its first reply, one line */
{
    uint32_t i, j, s = 0, any = 0;
    usb_host_take();
    for (i = 0; i < nwire; i++) {
        if (wire[i] == 0xF0)
            s = i;
        if (wire[i] != 0xF7 || i - s < 6u)
            continue;
        if (wire[s + 5u] == ED_SEQ_PUSH) {
            for (j = s; j <= i && npushed + 4u < sizeof pushed; j++)
                npushed += (uint32_t)snprintf(pushed + npushed, sizeof pushed - npushed, "%02x", wire[j]);
            if (npushed + 2u < sizeof pushed) {
                pushed[npushed++] = ' ';
                pushed[npushed] = 0;
            }
        } else if (reply && !any) {
            for (j = s; j <= i; j++)
                printf("%02x", wire[j]);
            any = 1;
        }
    }
    if (reply)
        printf(any ? "\n" : "-\n");
    nwire = 0;
}
static void power_on(void)
{
    host_init();
    memset(&ui, 0, sizeof ui);
    memset(&mo, 0, sizeof mo);
    memset(proj_slot, 0, sizeof proj_slot);
    memset(lk_slot, 0, sizeof lk_slot);
    memset(var_buf, 0, sizeof var_buf);
    memset(var_who, 0, sizeof var_who);
    memset(&cue, 0, sizeof cue);
    memset(&chain, 0, sizeof chain);
    var_play = 0;
    var_cur = 0;
    var_dirty = 0;
    persist_boot();
    settings_init();
    memset(&ed, 0, sizeof ed);
    memset(&edb, 0, sizeof edb);
    memset(&mout, 0, sizeof mout);
    usb.config = 1;
    usb.ota_req = 0;
    sx_ready = sx_collect = 0;
    so_r = so_w = 0;
    nwire = 0;
    fm1_ms = 100000u;
}

int main(void)
{
    static char line[8192];
    static uint8_t b[4096];
    memset(hflash, 0xFF, sizeof hflash);
    power_on();
    while (fgets(line, sizeof line, stdin)) {
        char w[16], x[16];
        int a = 0, c = 0, d = 0;
        uint32_t n;
        if (sscanf(line, "%15s", w) != 1)
            continue;
        if (!strcmp(w, "flash")) {
            char *h = strrchr(line, ' ') + 1;
            sscanf(line, "%*s %15s %d", x, &a);
            n = hex(h, b, sizeof b);
            if (!strcmp(x, "upr"))
                st_save(OBJ_UPRESET0 + 1u, b, n);
            else
                st_save((!strcmp(x, "rec") ? OBJ_PROJECT0 : !strcmp(x, "lk") ? OBJ_LOCKS0 : OBJ_VAR0) + (uint32_t)a, b, n);
        } else if (!strcmp(w, "boot")) {
            power_on();
        } else if (!strcmp(w, "load")) {
            sscanf(line, "%*s %d", &a);
            proj_load_from((uint32_t)a);
        } else if (!strcmp(w, "play")) {
            song.playing = 1;
        } else if (!strcmp(w, "stop")) {
            song.playing = 0;
            cue_cancel();
        } else if (!strcmp(w, "song")) {
            sscanf(line, "%*s %d", &a);
            chain.running = (uint8_t)a;
        } else if (!strcmp(w, "noflash")) {
            sscanf(line, "%*s %d", &a);
            flash_ok = !a;
        } else if (!strcmp(w, "park")) {
            if (sscanf(line, "%*s %d", &a) == 1) {  /* the park holds slot a's unsaved variations */
                var_who[var_play ^ 1u] = (uint8_t)(a + 1);
                var_dirty |= (uint8_t)(1u << a);
            } else {
                var_dirty &= (uint8_t)~(1u << ((var_who[var_play ^ 1u] - 1u) & 3u));
                var_who[var_play ^ 1u] = 0;
            }
        } else if (!strcmp(w, "otarace")) {
            ota_race = 1;
        } else if (!strcmp(w, "turn")) {
            sscanf(line, "%*s %d %d %d", &a, &c, &d);
            trk[a].p[c] = (int16_t)d;
        } else if (!strcmp(w, "advance")) {
            sscanf(line, "%*s %d", &a);
            for (; a > 0; a -= 5) {
                fm1_ms += 5u;
                ed_service();
                take(0);
            }
        } else if (!strcmp(w, "pushes")) {
            sscanf(line, "%*s %d", &a);
            for (; a > 0; a -= 5) {
                fm1_ms += 5u;
                ed_service();
                take(0);
            }
            if (npushed && pushed[npushed - 1u] == ' ')
                pushed[--npushed] = 0;
            printf("%s\n", npushed ? pushed : "-");
            npushed = 0;
            pushed[0] = 0;
        } else if (!strcmp(w, "frame")) {
            n = hex(line + 6, b, sizeof b);
            for (c = 0; c < (int)n; c++)
                sysex_byte(b[c]);
            ed_service();
            take(1);
            if (sx_ready)
                ota_frame_done();                   /* (main.c's ota_service: ota_take frees what the editor left) */
            ota_race = 0;
        }
    }
    return 0;
}
