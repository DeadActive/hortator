/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca menu (HOME held): COLOR, SPEAKER EQ (FLAT LOWCUT BASS+), USB LEVEL (MASTER FIXED), USB AUDIO (2 CH 10 CH; not in the BLE build), PERFORM (HOLD PAGE), ZOOM, KNOB ACCEL, BACKLIGHT (OFF DIM LO DIM HI INV INV LO INV HI), HARDWARE CALIBRATION, (BLE build: BLUETOOTH,) ABOUT. */
/* ------------------------------------------------------------ menu --- */
#if FELUCCA_BLE                                     /* the Bluetooth page's row (bt_page.c) before ABOUT */
/* (no USB AUDIO row: 10 CH is the standard build's only, fx.c USB10_BUILD) */
enum { MI_COLOR, MI_SPKEQ, MI_USB, MI_PERF, MI_ZOOM, MI_ACCEL, MI_BACKLIGHT, MI_PANEL, MI_BT, MI_ABOUT, MI_BACK, MI_COUNT };
static const char *const MI_NAME[MI_COUNT] = {"COLOR", "SPEAKER EQ", "USB LEVEL", "PERFORM", "ZOOM", "ACCEL",
                                              "BACKLIGHT", "HARDWARE CALIBRATION", "BLUETOOTH", "ABOUT", "BACK"};
#define MENU_DY 14u                                 /* eleven rows above the hints (screen y) */
#define MENU_HY 196u
static void bt_page_open(void);                     /* bt_page.c (after ble/ble_core.h): ui.menu 3 */
static void bt_page_close(void);
static void bt_page_draw(void);
static void bt_page_input(uint32_t pressed);
static void bt_autostart(void);                     /* AUTOSTART: once, 5 s after power-on (ui_input only, gate 3) */
#else
enum { MI_COLOR, MI_SPKEQ, MI_USB, MI_USBAUD, MI_PERF, MI_ZOOM, MI_ACCEL, MI_BACKLIGHT, MI_PANEL, MI_ABOUT, MI_BACK,
       MI_COUNT };
static const char *const MI_NAME[MI_COUNT] = {"COLOR", "SPEAKER EQ", "USB LEVEL", "USB AUDIO", "PERFORM", "ZOOM",
                                              "ACCEL", "BACKLIGHT", "HARDWARE CALIBRATION", "ABOUT", "BACK"};
#define MENU_DY 14u                                 /* eleven rows above the hints (screen y) */
#define MENU_HY 196u
#endif
#define MENU_Y0 40u                                 /* the first row (screen y), inside the MENU window */
static const char *const SPK_EQ[3] = {"FLAT", "LOWCUT", "BASS+"};   /* settings.lowcut (fx_lowcut) */
static uint32_t *menu_onoff(uint32_t i)            /* an ON / OFF row's setting, 0 if none */
{
#if USB10_BUILD
    if (i == MI_USBAUD)
        return &usbaud;
#endif
    return i == MI_USB ? &settings.usbfix : i == MI_PERF ? &settings.perfpage : i == MI_ZOOM ? &settings.zoom :
           i == MI_ACCEL ? &settings.accel : 0;
}
#if USB8_OK

/* MENU > USB AUDIO: the change reaches the computer when the menu closes (not on each turn: one reconnect, however
 * often the row is stepped, none when it is stepped back; and no USB drop while the menu is still being read).
 * usb_mode_service (ui_input, the main loop) then: USB off (usb_detach: D+ pull-up off, the host sees the FM-1 leave;
 * nothing streams, MIDI out dropped: usb8_quiesce), USBM_WAIT_MS later on again (usb_start) with the other descriptor
 * set; the host enumerates it afresh (bcdDevice 3.11 / 3.81, "Hortator" / "Hortator 8ch"). The sequencer, the audio
 * and the UI run on throughout (the ISRs); MIDI in and out come back with the host's configuration. Following CLK USB,
 * the clock pauses with the cable: the steps wait for its pulses (the top bar says so), and the lost-clock stop (seq.c,
 * 0.5 s without a pulse) waits till USBM_GRACE_MS after the reconnect; a clock that has not come back by then stops
 * the sequencer as a pulled cable does (a DAW may see "Hortator 8ch" as another port and need it chosen again).
 * SAFE START, an unfinished update and the update loader never switch: they present 2 CH (usb_app.c's set). The
 * Bluetooth build has none of it (fx.c USB10_BUILD): no row, 2 CH always, a stored 10 CH ignored. */
#define USBM_WAIT_MS 1000u
#define USBM_GRACE_MS 2000u                            /* CLK USB: after the reconnect, this long for the clock to return */
static struct { uint8_t state; uint32_t t; } usbm;
#if defined(DRUM_HOST) || defined(SIM_HOST)
static uint32_t host_usb_offs, host_usb_ons;          /* the host tests: no USB hardware */
static void usbm_hw_off(void) { host_usb_offs++; usb.up = 0; }
static void usbm_hw_on(void) { host_usb_ons++; }
#else
static void usbm_hw_off(void) { usb_detach(); }
static void usbm_hw_on(void)
{
    usb.detached = 0;                                  /* (usb_retry tries again each second if no host answers) */
    usb_start();
}
#endif
static void usb_mode_service(void)
{
    if (!USB8_OK || safe_start)
        return;
    if (usbm.state == 0) {
        if (ui.menu || (usbaud & 1u) == usb10 || usb.detached)
            return;                                    /* (usb.detached: UBOOT is on its way) */
        usbm_hw_off();
        usb8_quiesce();
        usb_hold_until = fm1_ms + USBM_WAIT_MS + USBM_GRACE_MS;   /* (seq.c: no lost-clock stop till then) */
        ui_say(song.g[G_CLOCK] == 1 ? "CLK USB PAUSED: " : "USB RECONNECT: ", usbaud ? "10 CH" : "2 CH");
        usbm.state = 1;
        usbm.t = fm1_ms;
        return;
    }
    if (fm1_ms - usbm.t < USBM_WAIT_MS)
        return;
    usb10 = (uint8_t)(usbaud & 1u);                   /* the set the next enumeration gets (usb_app8.c usb8_desc) */
    usbm.state = 0;
    usbm_hw_on();
}
#endif

/* ABOUT (owner 2026-10-22): the credits under the fixed logo, one string a credit, wrapped at its spaces to the text's
 * width (a credit's further lines indented); SELECT scrolls them a row a detent (ui.about_top), clamped at both ends;
 * a thumb at the right shows where. LICENSING.md and README's Credits say the same in full. */
static const char *const AB[] = {
    "DRUM MACHINE ON FELUCCA", FELUCCA_VERSION, "FORK: DEADACTIVE",
    "FELUCCA: LEO KUROSHITA, H\xDCGELTON INSTRUMENTS", "GITHUB.COM/HUGELTON/FELUCCA",
    "MUTABLE INSTRUMENTS, EMILIE GILLET: PLAITS + STMLIB (DRUM ALGORITHMS, MIT), GRIDS (PATTERN ENGINE, GPL-3.0+), "
    "STREAMS (COMPRESSOR, MIT)",
    "DAISYSP, ELECTROSMITH + EMILIE GILLET: MODAL / MEMBRANE (SKIN, RESON; MIT)",
    "SAMPLES: VERSILIAN STUDIOS VSCO-2 CE / VCSL (CC0) + H\xDCGELTON SAMPLE PACK",
    "PIXEL FONT: DEADACTIVE",
    "MD VOICES MEASURED FROM JOE LANDERS' MACHINEDRUM EMULATOR, A FORK OF GEARMULATOR BY THE USUAL SUSPECTS (DSP56300 "
    "PROJECT): MEASUREMENTS ONLY, NO CODE OR DATA FROM IT",
    "MACHINEDRUM IS A TRADEMARK OF ELEKTRON. NOT AFFILIATED WITH ELEKTRON, JOE LANDERS OR THE USUAL SUSPECTS",
    "JIELI AC79 SDK (APACHE-2.0) IN THE PACKAGE",
    "RECOVERY TOOL: JL-UBOOT-TOOL BY KAGAIMIQ (ANDREY GRIGORYEV), MIT",
    "WEB FONT: TERMINUS (DIMITAR ZHEKOV, OFL)", "WEB ICONS: FUKIAI (H\xDCGELTON INSTRUMENTS, MIT)",
    "BOOT SCREEN STUDY: UNIFRAKTUR FONTS (J. 'MACH' WUST, PETER WIEGEL, OFL)",
    "GPL-3.0, NO WARRANTY"};
#define AB_Y0 78                                    /* the first row (under the logo), 14 px rows, eleven shown */
#define AB_DY 14
#define AB_ROWS 11u
#define AB_X 6
#define AB_IND 12                                   /* a credit's further lines */
#define AB_R 228                                    /* the text's right edge (exclusive) */
#define AB_BX 232                                   /* the thumb: x 232..234 in rows 78..227 */
#define AB_BH (int32_t)(AB_ROWS * AB_DY - 4u)
#define AB_MAXL 64u
static struct { uint8_t c, at, n; } ab_line[AB_MAXL];   /* the wrapped rows: credit, first character, length */
static uint32_t ab_lines;

static void ab_str(char *b, uint32_t r)              /* row r's text */
{
    memcpy(b, AB[ab_line[r].c] + ab_line[r].at, ab_line[r].n);
    b[ab_line[r].n] = 0;
}

static void about_wrap(void)                         /* once: the credits as rows */
{
    uint32_t c, len, at, end, cut, fit, n = 0;
    char b[64];
    if (ab_lines)
        return;
    for (c = 0; c < sizeof AB / sizeof AB[0]; c++) {
        const char *s = AB[c];
        for (len = 0; s[len]; len++)
            ;
        for (at = 0; at < len && n < AB_MAXL;) {
            int32_t x = at ? AB_X + AB_IND : AB_X;
            for (cut = fit = 0, end = at + 1u; end <= len && end - at < sizeof b; end++) {
                memcpy(b, s + at, end - at);
                b[end - at] = 0;
                if (x + md_text_w(&MD_F3, 2, b) > AB_R)
                    break;
                fit = end;
                if (end == len || s[end] == ' ')
                    cut = end;                      /* the last word end that fits */
            }
            if (!cut)                               /* (a word wider than the row: as much as fits) */
                cut = fit > at ? fit : at + 1u;
            ab_line[n].c = (uint8_t)c;
            ab_line[n].at = (uint8_t)at;
            ab_line[n].n = (uint8_t)(cut - at);
            n++;
            for (at = cut; at < len && s[at] == ' '; at++)
                ;
        }
    }
    ab_lines = n;
}

static uint32_t about_max(void)                      /* the last first row: the last credit on the last row */
{
    about_wrap();
    return ab_lines > AB_ROWS ? ab_lines - AB_ROWS : 0u;
}

static void draw_about(void)                         /* the credits' rows and the thumb (into the band's canvas) */
{
    uint32_t i, r;
    char b[64];
    int32_t th, ty, mx = (int32_t)about_max();
    for (i = 0; i < AB_ROWS && (r = ui.about_top + i) < ab_lines; i++) {
        int32_t y = AB_Y0 + (int32_t)i * AB_DY;
        ab_str(b, r);
        md_text(ab_line[r].at ? AB_X + AB_IND : AB_X, y, &MD_F3, 2, b, C_INK);
        if (ab_line[r].c == 1u && !ab_line[r].at)   /* the version's row: the build date at the right */
            md_text(AB_R - md_text_w(&MD_F3, 2, __DATE__), y, &MD_F3, 2, __DATE__, C_INK);
    }
    th = mx ? AB_BH * (int32_t)AB_ROWS / (int32_t)ab_lines : AB_BH;
    th = th < 10 ? 10 : th;
    ty = AB_Y0 + (mx ? (AB_BH - th) * (int32_t)ui.about_top / mx : 0);
    cv_rect(AB_BX, ty, 3, th, C_INK);
}

#if USB10_BUILD
#define USBAUD_SIG (usbaud * 2654435761u)
#else
#define USBAUD_SIG 0u
#endif
static void draw_menu(void)
{
    static int16_t pk[8];                           /* ABOUT: the logo's peak dots */
    static uint32_t pf;                             /* the logo's frame shown */
    uint32_t i, pass, fr = fm1_ms / 66u,
                      sig = ui.menu * 7u + ui.menu_sel * 131u + settings.palette * 1009u + settings.lowcut * 7919u +
                            settings.zoom * 104729u + settings.accel * 15485863u + settings.usbfix * 32452843u +
                            settings.perfpage * 49979687u + backlight * 67867967u + USBAUD_SIG +
                            ui.about_top * 86028121u;
#if FELUCCA_BLE
    if (ui.menu == 3u) {                            /* the Bluetooth page: its own drawing and signature */
        bt_page_draw();
        return;
    }
#endif
    if (!ui.force && sig == ui.menu_sig) {
        if (ui.menu == 2 && fr != pf) {             /* ABOUT: the logo moves (15 fps), alone: rows 17..77, x 82..157
                                                     * (an even x: the dither keeps its phase) */
            pf = fr;
            cv_begin_at(76, 61, 17, C_BG);
            cv_oy = -17;
            cv_vu(2, 18, fr, 1, pk);
            cv_blit(82, 17);
        }
        return;
    }
    ui.menu_sig = sig;
    ui.force = 1;
    draw_head();                                    /* the top bar as everywhere */
    for (pass = 0; pass < 2u; pass++) {             /* the canvas holds 124 rows: two bands, screen rows 16..239 */
        uint32_t y0 = H_HEAD + pass * 124u, h = pass ? 240u - H_HEAD - 124u : 124u;
        cv_begin_at(240, h, y0, C_BG);
        cv_oy = -(int32_t)y0;                       /* (drawn in screen rows) */
        cv_rect(0, (int32_t)y0, 1, (int32_t)h, C_INK);
        cv_rect(239, (int32_t)y0, 1, (int32_t)h, C_INK);
        cv_rect(0, 239, 240, 1, C_INK);
        if (ui.menu == 2) {                         /* ABOUT: the logo, the credits (scrolled) */
            if (pass == 0u && fr != pf) {           /* (the peaks step once a frame, not once a band) */
                pf = fr;
                cv_vu(84, 18, fr, 1, pk);
            } else {
                int16_t keep[8];
                memcpy(keep, pk, sizeof keep);
                cv_vu(84, 18, fr, 1, keep);
            }
            draw_about();
        } else {                                    /* MENU: a window, the cursor an inverted bar */
            cv_popup(6, 30, 228, 184, "MENU", C_INK, C_BG);
            for (i = 0; i < MI_COUNT; i++) {
                int32_t y = (int32_t)(MENU_Y0 + i * MENU_DY);
                int sel = i == ui.menu_sel;
                const char *v = 0;
                if (menu_onoff(i))
                    v = i == MI_USB ? (*menu_onoff(i) ? "FIXED" : "MASTER") : i == MI_PERF ? (*menu_onoff(i) ? "PAGE" : "HOLD") :
                        *menu_onoff(i) ? "ON" : "OFF";
#if USB10_BUILD
                if (i == MI_USBAUD)
                    v = usbaud ? "10 CH" : "2 CH";
#endif
                if (i == MI_SPKEQ)
                    v = SPK_EQ[settings.lowcut % 3u];
                if (i == MI_BACKLIGHT)              /* (item 9: upstream's LEDS levels) */
                    v = BL_NAME[backlight % BL_COUNT];
                if (i == MI_COLOR)                  /* the theme's name (owner 2026-10-23) */
                    v = THEMES[theme_index(settings.palette)].name;
                list_row(10, 225, y, MI_NAME[i], v, sel);
            }
            i = (uint32_t)cv_keycap(12, (int32_t)MENU_HY, "OCT+", "OK", C_INK);
            i = (uint32_t)cv_keycap((int32_t)i + 8, (int32_t)MENU_HY, "OCT-", "BACK", C_INK);
            cv_keycap((int32_t)i + 8, (int32_t)MENU_HY, "SEL", "MOVE", C_INK);
        }
        cv_oy = 0;
        cv_blit(0, y0);
    }
    ui.force = 0;
}

static void enc_drop(void)                             /* knob turns nobody takes */
{
    uint32_t k;
    for (k = 0; k < NE; k++)
        panel_enc(k);
}

static void menu_close(void)
{
#if FELUCCA_BLE
    if (ui.menu == 3u)                                 /* HOME held on the Bluetooth page: it closes too */
        bt_page_close();
#endif
    settings_save();                                   /* palette / panel table, if changed */
    ui.menu = 0;
    ui.force = 1;
    go_home();                                         /* HOME in the mode last shown */
}

/* the start screen (main.c fm1_main, right after lcd_init; spec 2026-10-19-md-ui §9a): the VU mark still on its
 * downbeat frame, HORTATOR under it, in the stored theme; two bands of 120 rows (the canvas holds 124) */
static void draw_boot_title(void)
{
    uint32_t pass;
    for (pass = 0; pass < 2u; pass++) {
        cv_begin_at(240, 120, pass * 120u, C_BG);
        cv_oy = -(int32_t)(pass * 120u);             /* (drawn in screen rows) */
        cv_vu(48, 34, 0, 0, 0);
        md_text(120 - md_text_w(&MD_F3, 2, "HORTATOR") / 2, 168, &MD_F3, 2, "HORTATOR", C_INK);
        cv_frame(0, 0, 240, 240, C_INK);
        cv_oy = 0;
        cv_blit(0, pass * 120u);
    }
}

/* menu: SELECT moves, OCT+ confirms, OCT- cancels (ABOUT -> list -> close) */
static void menu_input(uint32_t pressed)
{
    int32_t s;
    uint32_t ok = (pressed >> panel.btn[B_OCTUP]) & 1u, back = (pressed >> panel.btn[B_OCTDN]) & 1u;
#if FELUCCA_BLE
    if (ui.menu == 3u) {                               /* the Bluetooth page (OCT-: back to the list) */
        bt_page_input(pressed);
        enc_drop();
        return;
    }
#endif
    if (back) {
        if (ui.menu == 2)
            ui.menu = 1, ui.force = 1;
        else
            menu_close();
        return;
    }
    if ((s = panel_enc(EN_SELECT)) != 0 && ui.menu == 1)
        ui.menu_sel = (uint8_t)((ui.menu_sel + (s > 0 ? 1u : MI_COUNT - 1u)) % MI_COUNT);
    else if (s != 0 && ui.menu == 2) {                 /* ABOUT: the credits a row a detent, clamped */
        int32_t t = (int32_t)ui.about_top + s, mx = (int32_t)about_max();
        ui.about_top = (uint8_t)(t < 0 ? 0 : t > mx ? mx : t);
    }
    s = panel_enc(EN_K1);
    if (s != 0 && ui.menu == 1 && ui.menu_sel == MI_COLOR) {
        settings.palette = theme_step(settings.palette, s);
        theme_set(settings.palette);
        ui.force = 1;                               /* every region in the new colours */
    }
    if ((s != 0 || ok) && ui.menu == 1 && ui.menu_sel == MI_SPKEQ) {
        /* KNOB 1 steps FLAT LOWCUT BASS+ and stops at the ends; OCT+ steps and wraps (upstream 1.0.2) */
        settings.lowcut = s > 0 ? (settings.lowcut < 2u ? settings.lowcut + 1u : 2u)
                        : s < 0 ? (settings.lowcut ? settings.lowcut - 1u : 0u) : (settings.lowcut + 1u) % 3u;
        fx_lowcut = (uint8_t)settings.lowcut;
        ok = 0;
        s = 0;
    }
    if ((s != 0 || ok) && ui.menu == 1 && ui.menu_sel == MI_BACKLIGHT) {
        /* BACKLIGHT (item 9): KNOB 1 steps OFF DIM LO DIM HI INV INV LO INV HI and stops at the ends; OCT+ steps and
         * wraps (as SPEAKER EQ) */
        backlight = s > 0 ? (backlight + 1u < BL_COUNT ? backlight + 1u : BL_COUNT - 1u)
                  : s < 0 ? (backlight ? backlight - 1u : 0u) : (backlight + 1u) % BL_COUNT;
        ok = 0;
        s = 0;
    }
    if ((s != 0 || ok) && ui.menu == 1 && menu_onoff(ui.menu_sel)) {
        /* KNOB 1: right = ON, left = OFF; OCT+ toggles */
        uint32_t *v = menu_onoff(ui.menu_sel);
        *v = s > 0 ? 1u : s < 0 ? 0u : !*v;
        fx_usb_fixed = (uint8_t)settings.usbfix;
        ok = 0;
    }
    if (ok && ui.menu == 1) {
        switch (ui.menu_sel) {
        case MI_COLOR:                                 /* OCT+ steps through the themes too */
            settings.palette = theme_step(settings.palette, 1);
            theme_set(settings.palette);
            ui.force = 1;
            break;
        case MI_PANEL:
            panel_setup();
            ui.force = 1;
            break;
#if FELUCCA_BLE
        case MI_BT:
            bt_page_open();
            break;
#endif
        case MI_ABOUT:
            ui.menu = 2;
            ui.about_top = 0;                          /* the credits from the top */
            ui.force = 1;
            break;
        default:
            menu_close();
            break;
        }
    }
    enc_drop();                                        /* swallow the rest while the menu is up */
}

