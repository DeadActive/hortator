/* SPDX-License-Identifier: GPL-3.0-only */
/* The GRIND style and WHEN in the device settings (persist_t.palette bits 18..23): the real settings_save and persist_boot
 * against a simulated flash, the way tests/boot_test.c runs them. */
#define GRIND_NATIVE 1
#define FELUCCA_FLASH 1
#undef NDEBUG
#include <assert.h>
#include <stdint.h>
static uint8_t hflash[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
#include "../ui_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static uint32_t rs = 7;
static void rfill(void *p, uint32_t n)
{
    uint8_t *b = p;
    while (n--) {
        rs = rs * 1664525u + 1013904223u;
        *b++ = (uint8_t)(rs >> 24);
    }
}

/* a power cycle: .noinit is anything, the GRIND screen state is its power-on value, the flash stays */
static void power_cycle(void)
{
    rfill(&settings, sizeof settings);
    gm_v[GU_STYLE] = 21;
    gm_v[GU_AGGR] = gm_v[GU_DENS] = gm_v[GU_CHAOS] = gm_v[GU_HUMAN] = 99;
    gm_v[GU_MUT] = 55;
    gm_v[GU_WHEN] = 0;
    persist_boot();
    settings_init();
}
static void fresh_flash(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    ui_host_init();
    persist_boot();
    settings.magic = SETTINGS_MAGIC;
    settings.palette = THEME_TAG | 1u;
    settings.lowcut = 0;
    settings.zoom = 0;
}

static void roundtrip(void)
{
    uint32_t style, when, ok = 1;
    for (style = 0; style < GM_ALGORITHM_COUNT; style++)
        for (when = 0; when < 2u; when++) {
            gm_config f;
            fresh_flash();
            gm_v[GU_STYLE] = (int16_t)style;
            gm_v[GU_WHEN] = (int16_t)when;
            settings_save();
            power_cycle();
            gm_factory_config((uint8_t)(style + 1u), &f);
            ok &= gm_v[GU_STYLE] == (int16_t)style && gm_v[GU_WHEN] == (int16_t)when;
            ok &= gm_v[GU_AGGR] == f.aggression && gm_v[GU_DENS] == f.density && gm_v[GU_CHAOS] == f.chaos &&
                  gm_v[GU_HUMAN] == f.humanize && gm_v[GU_MUT] == 0;       /* (the style's own voicing) */
        }
    check("persist: all 22 styles x both WHEN survive a power cycle; the controls come back as the style's factory values", ok);
}

static void older_record_reads_the_defaults(void)
{
    uint32_t ok = 1;
    fresh_flash();
    gm_v[GU_STYLE] = 0;
    gm_v[GU_WHEN] = 1;                               /* the defaults: the record then has bits 18..23 clear, as an older one */
    settings_save();
    ok &= (persist_saved.palette >> GM_SET_SHIFT) == 0u;
    power_cycle();
    ok &= gm_v[GU_STYLE] == 0 && gm_v[GU_WHEN] == 1;
    check("persist: a record without GRIND bits (every older firmware's) reads STYLE 1 and WHEN END", ok);
}

static void other_settings_untouched(void)
{
    uint32_t ok = 1;
    fresh_flash();
    midi_thru = 1;
    solo_mom = 1;
    rec_quant = 2;
    settings.mutebar = 1;
    settings.accel = 0;
    settings.perfpage = 1;
    gm_v[GU_STYLE] = 13;
    gm_v[GU_WHEN] = 0;
    settings_save();
    midi_thru = 0;
    solo_mom = 0;
    rec_quant = 0;
    power_cycle();
    ok &= midi_thru == 1 && solo_mom == 1 && rec_quant == 2 && settings.mutebar == 1 && settings.accel == 0 &&
          settings.perfpage == 1 && (settings.palette & 0xFFu) == ((THEME_TAG | 1u) & 0xFFu);
    ok &= gm_v[GU_STYLE] == 13 && gm_v[GU_WHEN] == 0;
    check("persist: saving the style leaves THRU, SOLO, QUANT, MUTE, ACCEL, PERFORM PAGE and the theme as they were", ok);
}

static void bits_do_not_overlap(void)
{
    uint32_t ok = 1, style, when, used = 0;
    /* the bits other settings use: palette 0..17 (theme 0..7, BT auto 8, backlight 9..11, USB audio 12..13, THRU 14,
     * SOLO 15, QUANT 16..17) */
    ok &= (GM_SET_SHIFT == 18u);
    for (style = 0; style < 32u; style++)
        for (when = 0; when < 2u; when++) {
            gm_v[GU_STYLE] = (int16_t)style;
            gm_v[GU_WHEN] = (int16_t)when;
            used |= gm_settings_pack();
        }
    ok &= (used & ~0x3Fu) == 0u && (used << GM_SET_SHIFT) >> 24 == 0u;   /* only bits 18..23 */
    gm_v[GU_STYLE] = 0;
    gm_v[GU_WHEN] = 1;
    check("persist: the packed word uses only palette bits 18..23, nothing another setting owns", ok);
}

static void damaged_style_falls_back(void)
{
    uint32_t ok = 1, v;
    for (v = GM_ALGORITHM_COUNT; v < 32u; v++) {     /* style bits 22..31: none is a style */
        gm_settings_unpack(v | (1u << 5));
        ok &= gm_v[GU_STYLE] == 0 && gm_v[GU_WHEN] == 0;
        ok &= gm_v[GU_STYLE] >= 0 && gm_v[GU_STYLE] < (int16_t)GM_ALGORITHM_COUNT;   /* (an index into N_GSTYLE) */
    }
    gm_settings_unpack(0xFFFFFFFFu);                 /* a record of all ones: still a valid screen state */
    ok &= gm_v[GU_STYLE] == 0 && gm_v[GU_WHEN] == 0 && gm_v[GU_AGGR] <= 100 && gm_v[GU_MUT] == 0;
    check("persist: an impossible style number loads as STYLE 1 and never indexes past the names", ok);
}

static void a_turn_through_the_list_saves_once(void)
{
    uint32_t ok = 1, i, before;
    fresh_flash();
    {
        uint32_t t;
        for (t = 0; t < 6u && !(cur_page()->scope == SC_GRIND); t++) {
            press(B_ARP);
            ui_frame();
            release_all();
            ui_frame();
        }
    }
    ok &= cur_page()->scope == SC_GRIND;
    before = persist_saved.palette;
    for (i = 0; i < 6u; i++) {                       /* six detents on STYLE, a few frames apart */
        turn(EN_K1, 1);
        ui_frame();
        ui_frame();
    }
    ok &= gm_v[GU_STYLE] > 1;
    ok &= persist_saved.palette == before;           /* nothing written while the knob is still moving */
    for (i = 0; i < 100u; i++)
        ui_frame();                                  /* about 1.5 s of quiet */
    ok &= (persist_saved.palette >> GM_SET_SHIFT & 31u) == (uint32_t)gm_v[GU_STYLE];
    check("persist: a turn through the styles writes once, after it stops (no flash write at every detent)", ok);
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    roundtrip();
    older_record_reads_the_defaults();
    other_settings_untouched();
    bits_do_not_overlap();
    damaged_style_falls_back();
    a_turn_through_the_list_saves_once();
    printf("test_persist: %s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
