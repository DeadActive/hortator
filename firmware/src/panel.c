/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Physical panel: which matrix button / encoder carries which printed label.
 * The default table can be overridden by HARDWARE CALIBRATION (hold OCT- and
 * OCT+ while powering on), which asks for each label in turn. The learned
 * table lives in .noinit and, with FELUCCA_FLASH, in flash with the settings
 * (project.c). */
enum { B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC,
       B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };
static const char *const B_NAME[NB] = {"FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE",
                                        "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
static const char *const E_NAME[NE] = {"SELECT", "ALGORITHM", "PRESETS", "KNOB 1", "KNOB 2",
                                        "KNOB 3", "KNOB 4"};
#define PANEL_MAGIC 0x50414E35u          /* "PAN5": bump when PANEL_DEFAULT changes */

typedef struct {
    uint32_t magic;
    uint8_t btn[NB];             /* matrix button id (0..13) per label */
    uint8_t enc[NE];             /* matrix encoder (0..6) per role */
    int8_t dir[NE];              /* +1 / -1 so that clockwise is + */
} panel_t;
panel_t panel __attribute__((section(".noinit")));

static const panel_t PANEL_DEFAULT = {
    PANEL_MAGIC,
    {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1},   /* PLAY = 12, REC = 13 */
    {0, 1, 6, 2, 3, 4, 5},                           /* SELECT = enc 0, ALGORITHM = enc 1, PRESETS = enc 6 */
    {1, 1, 1, 1, 1, 1, 1},
};

static void panel_init(void)                     /* also after a flash load: ids are used as array indexes and shifts */
{
    uint32_t i, buttons = 0, encoders = 0, ok = panel.magic == PANEL_MAGIC;
    for (i = 0; ok && i < NB; i++) {                    /* (upstream 1.0) and no id learned twice */
        ok = panel.btn[i] < 14u && !(buttons & (1u << panel.btn[i]));
        buttons |= ok ? 1u << panel.btn[i] : 0u;
    }
    for (i = 0; ok && i < NE; i++) {
        ok = panel.enc[i] < 7u && !(encoders & (1u << panel.enc[i])) && (panel.dir[i] == 1 || panel.dir[i] == -1);
        encoders |= ok ? 1u << panel.enc[i] : 0u;
    }
    if (!ok)
        panel = PANEL_DEFAULT;
}

static uint32_t panel_btn_of(uint32_t matrix_id)        /* label of a matrix button, NB if none */
{
    uint32_t b;
    for (b = 0; b < NB; b++)
        if (panel.btn[b] == matrix_id)
            return b;
    return NB;
}

static void panel_led(uint32_t label, int on) { fm1_led_key(panel.btn[label], on); }

/* steps of a role, + = clockwise */
static int32_t panel_enc(uint32_t role)
{
    return enc_take(panel.enc[role]) * panel.dir[role];   /* (enc.c: not the HAL's fm1_enc_take, #126) */
}

/* user settings that survive a reset (mutebar: TRACKS mutes wait for the next bar). 32 B as in drum-v0.15.0: .noinit's
 * layout stays, so an update finds the RAM project slots after it */
#define SETTINGS_MAGIC 0x53455433u              /* "SET3" */
struct { uint32_t magic, palette, lowcut, zoom, mutebar, accel, usbfix, perfpage; } settings __attribute__((section(".noinit")));
_Static_assert(sizeof settings == 32u, ".noinit settings: the layout an update finds");
/* the Bluetooth settings (both builds): MODE (0 AUTO, 1 SMC DAW, 2 MIDI), the remembered device (address type,
 * address bytes 0..2 and 3..5; all 0 none), AUTOSTART (1 on: bt_page.c bt_autostart). Only from the flash record
 * (persist_boot: zoom's and lowcut's spare bits, the palette word's bit 8), else 0 */
static struct { uint32_t btmode, bt_type, bt_lo, bt_hi, bt_auto; } btset;
/* the last project saved to flash (spec 2026-10-11 §6): set, its slot 0..3 (lowcut's bits 2, 3..4). As btset: only
 * from the flash record, else 0 */
static struct { uint8_t set, slot; } lastp;
/* MENU > BACKLIGHT (spec 2026-10-11 §12; fixes 2026-10-21 item 9: upstream Felucca's LEDS levels): BL_* (ui_input.c
 * ui_leds, led_dim.c). OFF: lit or dark (as 1.0); DIM LO (1, the old ON): the unlit keys and the idle buttons glow at
 * 5 %, the STEP keys' unlit beats at 25 %; DIM HI: a brighter glow, 10 % (beats 30 %); INV: turned around as the stock
 * firmware, the idle ones lit and the active ones dark; INV LO / INV HI: so, the idle ones at 10 % / 25 %. The OCT
 * breath in every mode. As btset: only from the flash record (the palette word's bits 9..11, else lowcut's bit 7: an
 * older Hortator's ON is DIM LO), else OFF */
enum { BL_OFF, BL_DIM_LO, BL_DIM_HI, BL_INV, BL_INV_LO, BL_INV_HI, BL_COUNT };
static const char *const BL_NAME[BL_COUNT] = {"OFF", "DIM LO", "DIM HI", "INV", "INV LO", "INV HI"};
static uint32_t backlight;
/* MENU > USB AUDIO: 0 2 CH (the master, as before), 1 10 CH (the master + T1..T8; usb_app8.c). As btset: only from the
 * flash record (the palette word's bit 12; older records: 2 CH), else 2 CH. What the host sees is usb10 (fx.c): set
 * at power-on (ui.c drum_boot_init; SAFE START: 2 CH) and by the reconnect when the menu closes (ui_menu.c) */
static uint32_t usbaud;

static void settings_save(void);
static void settings_poll(void);              /* project.c */              /* project.c: flash copy (FELUCCA_FLASH) */

static void settings_init(void)
{
    if (settings.magic != SETTINGS_MAGIC) {
        settings.magic = SETTINGS_MAGIC;
        settings.palette = THEME_TAG | 0u;     /* MD RED (default; spec 2026-10-19-md-ui §2) */
        settings.lowcut = 0;
        settings.zoom = 0;                     /* large readout of the touched value: off */
        settings.accel = 1;                    /* knob acceleration (calm, upstream 1.0): on */
        settings.usbfix = 0;                   /* USB LEVEL MASTER */
        settings.perfpage = 0;                 /* PERFORM HOLD */
    }
    settings.mutebar &= 1u;
    settings.usbfix &= 1u;                    /* .noinit: fields added after "SET3" */
    settings.perfpage &= 1u;
    if (settings.accel > 1u)
        settings.accel = 1u;
    if (settings.lowcut > 2u)                  /* SPEAKER EQ: FLAT LOWCUT BASS+ */
        settings.lowcut = 0;
    settings.palette = THEME_TAG | theme_index(settings.palette);   /* an old palette id becomes its theme */
    theme_set(settings.palette);
    fx_lowcut = (uint8_t)settings.lowcut;
    fx_usb_fixed = (uint8_t)settings.usbfix;
}
