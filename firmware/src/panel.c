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
    return fm1_enc_take(panel.enc[role]) * panel.dir[role];
}

/* user settings that survive a reset */
#define SETTINGS_MAGIC 0x53455433u              /* "SET3" */
struct { uint32_t magic, palette, lowcut, zoom, mutebar, accel, usbfix, perfpage; } settings __attribute__((section(".noinit")));   /* mutebar:
                                                                                   * TRACKS mutes wait for the next bar */

static void settings_save(void);
static void settings_poll(void);              /* project.c */              /* project.c: flash copy (FELUCCA_FLASH) */

static void settings_init(void)
{
    if (settings.magic != SETTINGS_MAGIC || settings.palette >= NPALETTES) {
        settings.magic = SETTINGS_MAGIC;
        settings.palette = 4;                  /* MONO (default) */
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
    palette_set(settings.palette);
    fx_lowcut = (uint8_t)settings.lowcut;
    fx_usb_fixed = (uint8_t)settings.usbfix;
}
