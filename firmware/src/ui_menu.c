/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca menu (HOME held): COLOR, SPEAKER EQ (FLAT LOWCUT BASS+), ZOOM, KNOB ACCEL, HARDWARE CALIBRATION, ABOUT. */
/* ------------------------------------------------------------ menu --- */
enum { MI_COLOR, MI_SPKEQ, MI_ZOOM, MI_ACCEL, MI_PANEL, MI_ABOUT, MI_BACK, MI_COUNT };
static const char *const MI_NAME[MI_COUNT] = {"COLOR", "SPEAKER EQ", "ZOOM", "ACCEL", "HARDWARE CALIBRATION", "ABOUT", "BACK"};
static const char *const SPK_EQ[3] = {"FLAT", "LOWCUT", "BASS+"};   /* settings.lowcut (fx_lowcut) */
static uint32_t *menu_onoff(uint32_t i)            /* an ON / OFF row's setting, 0 if none */
{
    return i == MI_ZOOM ? &settings.zoom : i == MI_ACCEL ? &settings.accel : 0;
}

static void draw_menu(void)
{
    uint32_t i, pass, sig = ui.menu * 7u + ui.menu_sel * 131u + settings.palette * 1009u + settings.lowcut * 7919u +
                            settings.zoom * 104729u + settings.accel * 15485863u;
    if (!ui.force && sig == ui.menu_sig)
        return;
    ui.menu_sig = sig;
    if (ui.force)                                   /* head + rule + two bands cover rows 0..229 */
        lcd_fill(0, H_HEAD + 1 + 124 + 85, 240, 240 - (H_HEAD + 1 + 124 + 85), C_BLACK);
    cv_begin(240, H_HEAD, C_BLACK);
    cv_text(4, 1, &FONT_S, ui.menu == 2 ? "ABOUT" : "MENU", C_HI);
    cv_blit(0, Y_HEAD);
    lcd_fill(0, H_HEAD, 240, 1, C_LINE);
    for (pass = 0; pass < 2u; pass++) {             /* the canvas holds 124 rows: draw in two bands */
        cv_begin(240, pass ? 85u : 124u, C_BLACK);
        cv_oy = pass ? -124 : 0;
        if (ui.menu == 2) {
            cv_text(4, 4, &FONT_L, "FM-1 DRUMS", C_HI);
            cv_text(4, 36, &FONT_S, "DRUM MACHINE ON FELUCCA", C_AMB);
            cv_text(4, 54, &FONT_S, FELUCCA_VERSION, C_HI);
            cv_text(236 - text_w(&FONT_S, __DATE__), 54, &FONT_S, __DATE__, C_GRAY);   /* build date */
            cv_text(4, 72, &FONT_S, "FORK: DEADACTIVE", C_HI);
            cv_text(4, 88, &FONT_S, "FELUCCA: LEO KUROSHITA", C_HI);
            cv_text(4, 104, &FONT_S, "H\xDCGELTON INSTRUMENTS", C_AMB);
            cv_text(4, 119, &FONT_S, "GPL-3.0, NO WARRANTY", C_HI);
            cv_text(4, 132, &FONT_S, "GITHUB.COM/HUGELTON/FELUCCA", C_AMB);
            cv_text(4, 146, &FONT_S, "FONT: TERMINUS (OFL)", C_DIM);
            cv_text(4, 159, &FONT_S, "SAMPLES: VERSILIAN (CC0)", C_DIM);
            cv_text(4, 172, &FONT_S, "+ H\xDCGELTON SAMPLE PACK", C_DIM);
            cv_text(4, 185, &FONT_S, "UNTESTED ON HARDWARE", C_DIM);
        } else {
            for (i = 0; i < MI_COUNT; i++) {
                int32_t y = 4 + (int32_t)i * 24;
                int sel = i == ui.menu_sel;
                if (sel)
                    cv_rect(4, y + 6, 3, 3, C_WHITE);
                cv_text(14, y, &FONT_S, MI_NAME[i], sel ? C_WHITE : C_GRAY);
                if (menu_onoff(i))
                    cv_text(102, y, &FONT_S, *menu_onoff(i) ? "ON" : "OFF", C_HI);
                if (i == MI_SPKEQ)
                    cv_text(102, y, &FONT_S, SPK_EQ[settings.lowcut % 3u], C_HI);
                if (i == MI_COLOR) {
                    uint32_t k;
                    cv_text(102, y, &FONT_S, PALETTES[settings.palette].name, C_HI);
                    for (k = 0; k < 5u; k++)
                        cv_rect(160 + (int32_t)k * 14, y + 3, 10, 10, pal[k]);
                }
            }
            cv_text(4, 170, &FONT_S, "PRESETS MOVE", C_DIM);
            cv_text(4, 188, &FONT_S, "OCT+ OK   OCT- BACK", C_DIM);
        }
        cv_oy = 0;
        cv_blit(0, H_HEAD + 1 + pass * 124u);
    }
}

static void enc_drop(void)                             /* knob turns nobody takes */
{
    uint32_t k;
    for (k = 0; k < NE; k++)
        panel_enc(k);
}

static void menu_close(void)
{
    settings_save();                                   /* palette / panel table, if changed */
    ui.menu = 0;
    ui.force = 1;
    go_home();
}

/* menu: PRESETS moves, OCT+ confirms, OCT- cancels (ABOUT -> list -> close) */
static void menu_input(uint32_t pressed)
{
    int32_t s;
    uint32_t ok = (pressed >> panel.btn[B_OCTUP]) & 1u, back = (pressed >> panel.btn[B_OCTDN]) & 1u;
    if (back) {
        if (ui.menu == 2)
            ui.menu = 1, ui.force = 1;
        else
            menu_close();
        return;
    }
    if ((s = panel_enc(EN_PRESET)) != 0 && ui.menu == 1)
        ui.menu_sel = (uint8_t)((ui.menu_sel + (s > 0 ? 1u : MI_COUNT - 1u)) % MI_COUNT);
    s = panel_enc(EN_K1);
    if (s != 0 && ui.menu == 1 && ui.menu_sel == MI_COLOR) {
        settings.palette = (settings.palette + (s > 0 ? 1u : NPALETTES - 1u)) % NPALETTES;
        palette_set(settings.palette);              /* (the menu signature redraws) */
    }
    if ((s != 0 || ok) && ui.menu == 1 && ui.menu_sel == MI_SPKEQ) {
        /* KNOB 1 steps FLAT LOWCUT BASS+ and stops at the ends; OCT+ steps and wraps (upstream 1.0.2) */
        settings.lowcut = s > 0 ? (settings.lowcut < 2u ? settings.lowcut + 1u : 2u)
                        : s < 0 ? (settings.lowcut ? settings.lowcut - 1u : 0u) : (settings.lowcut + 1u) % 3u;
        fx_lowcut = (uint8_t)settings.lowcut;
        ok = 0;
        s = 0;
    }
    if ((s != 0 || ok) && ui.menu == 1 && menu_onoff(ui.menu_sel)) {
        /* KNOB 1: right = ON, left = OFF; OCT+ toggles */
        uint32_t *v = menu_onoff(ui.menu_sel);
        *v = s > 0 ? 1u : s < 0 ? 0u : !*v;
        ok = 0;
    }
    if (ok && ui.menu == 1) {
        switch (ui.menu_sel) {
        case MI_COLOR:                                 /* OCT+ steps through the palettes too */
            settings.palette = (settings.palette + 1u) % NPALETTES;
            palette_set(settings.palette);
            break;
        case MI_PANEL:
            panel_setup();
            ui.force = 1;
            break;
        case MI_ABOUT:
            ui.menu = 2;
            ui.force = 1;
            break;
        default:
            menu_close();
            break;
        }
    }
    enc_drop();                                        /* swallow the rest while the menu is up */
}

