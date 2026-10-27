/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The web simulator's core, headless (tools/build_sim.sh): the same C the browser runs as wasm, through its API. */
#include "sim_core.c"
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static uint32_t fb_lit(void)                         /* pixels that are not black */
{
    const uint16_t *f = sim_fb();
    uint32_t i, n = 0;
    for (i = 0; i < 240u * 240u; i++)
        n += f[i] != 0;
    return n;
}

static uint32_t fb_hash(void)                        /* FNV-1a over the framebuffer */
{
    const uint8_t *p = (const uint8_t *)sim_fb();
    uint32_t i, h = 2166136261u;
    for (i = 0; i < 240u * 240u * 2u; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

static void run(uint32_t frames)                     /* frames of audio (the UI frames due run inside) */
{
    while (frames) {
        uint32_t n = frames < SIM_MAX_FRAMES ? frames : SIM_MAX_FRAMES;
        sim_render(n);
        frames -= n;
    }
}

static void test_boot_and_input(void)
{
    uint32_t h;
    sim_init(1);
    run(4096);
    check("boot: the screen draws", fb_lit() > 500u);
    h = fb_hash();
    sim_btn(B_EDIT, 1);
    run(1024);
    sim_btn(B_EDIT, 0);
    run(1024);
    check("a button (EDIT) changes the screen", fb_hash() != h);
    h = fb_hash();
    sim_enc(EN_K1, 3);
    run(2048);
    check("an encoder turn (KNOB 1) changes the screen", fb_hash() != h);
    h = fb_hash();
    sim_key(2, 1);                                   /* the second white key: track 2 */
    run(2048);
    sim_key(2, 0);
    run(2048);
    check("a note key (track 2's white key) selects track 2 and changes the screen", song.sel == 1u && fb_hash() != h);
}

static float peak(uint32_t frames)                  /* the loudest sample over the next frames */
{
    float m = 0;
    while (frames) {
        uint32_t n = frames < SIM_MAX_FRAMES ? frames : SIM_MAX_FRAMES, i;
        const float *a;
        sim_render(n);
        a = sim_audio();
        for (i = 0; i < 2u * n; i++)
            m = fabsf(a[i]) > m ? fabsf(a[i]) : m;
        frames -= n;
    }
    return m;
}

static void tap(uint32_t label)
{
    sim_btn(label, 1);
    run(1024);
    sim_btn(label, 0);
    run(1024);
}

static void four_on_the_floor(void)                  /* the current project's track 1 (until the demos) */
{
    uint32_t k;
    for (k = 0; k < 16u; k += 4u)
        trk[0].step[k].on = 1;
}

static void test_audio(void)
{
    float loud, quiet;
    uint32_t on = 0, off = 0, i;
    sim_init(1);
    four_on_the_floor();
    run(FS * 3u);                                    /* the earlier tests' FX tails (one process, not a RAM wipe) */
    check("before PLAY: silent", peak(FS / 2u) < 4.0f / 32768.0f);
    tap(B_PLAY);
    check("PLAY: the transport runs", sim_playing());
    loud = peak(FS);
    check("PLAY: audio within 1 s", loud > 0.01f);
    for (i = 0; i < 40u; i++) {                      /* PLAY's LED blinks while playing */
        run(FS / 40u / CTL * CTL);
        if ((sim_leds() >> B_PLAY) & 1u)
            on++;
        else
            off++;
    }
    check("PLAY's LED blinks while playing", on && off);
    sim_master(0);
    run(FS / 2u);
    quiet = peak(FS);
    check("MASTER at 0: much quieter", quiet < loud * 0.1f);
    sim_master(800);
    tap(B_PLAY);
    check("PLAY again: stopped", !sim_playing());
    run(FS * 3u);
    check("stopped: silent once the tails decay", peak(FS / 2u) < 4.0f / 32768.0f);
}

static uint32_t steps_lit(const track_t *t)          /* the white keys of t's first 16 steps that are on (sim keys) */
{
    uint32_t k, m = 0;
    for (k = 0; k < 16u; k++)
        if (t->step[k].on)
            m |= 1u << STEP_KEY[k];
    return m;
}

static uint32_t var_lit(void)                        /* the playing variation's black key (HOME, TRACKS, the SEQ pages:
                                                      * ui_input.c CUE_BK), a slot loaded */
{
    return proj_loaded() >= 0 ? 1u << CUE_BK[var_cur & 7u] : 0u;
}
static void test_leds(void)
{
    sim_init(1);
    run(4096);
    check("power-on: HOME's TRACKS mode, its button LED lit, the key LEDs track 1's steps (demo 1: the kick)",
          mix_mode() && ((sim_leds() >> B_HOME) & 1u) && sim_key_leds() == (steps_lit(&trk[0]) | var_lit()));
    tap(B_HOME);                                     /* HOME's second mode: the keys play */
    run(4096);
    check("HOME (mode 2): its button LED lit, only the selected track's key lit (track 1) and the playing variation's",
          ui.home && ((sim_leds() >> B_HOME) & 1u) && sim_key_leds() == (1u << KEY_TRK_KEY[song.sel] | var_lit()));
    sim_key(20, 1);                                  /* (C#5: the first 8 black keys are the variations) */
    run(2048);
    check("a held note key lights its LED", (sim_key_leds() >> 20) & 1u);
    sim_key(20, 0);
    sim_key(24, 1);
    run(2048);
    check("a held high key (F5) lights its own LED, besides the selected track's",
          sim_key_leds() == (1u << 24 | 1u << KEY_TRK_KEY[song.sel] | var_lit()));
    sim_key(24, 0);
    run(2048);
    check("released: only the selected track's key stays lit (and the playing variation's)",
          sim_key_leds() == (1u << KEY_TRK_KEY[song.sel] | var_lit()));
}

/* owner 2026-10-23: through the page's key calls too, a step key toggles its step on a short press's release; a
 * hold (STEP_HOLD_MS or more) turns an off step on, leaves an on one as it was */
static void test_step_tap_hold(void)
{
    uint32_t on;
    sim_init(1);
    run(4096);                                       /* power-on: TRACKS, the white keys track 1's steps */
    on = trk[0].step[0].on;
    sim_key(0, 1);                                   /* F3: step 1 */
    run(2048);
    check("TRACKS: a step key just pressed changes nothing yet", trk[0].step[0].on == on);
    sim_key(0, 0);
    run(2048);
    check("TRACKS: let go after ~0.05 s (a tap): step 1 toggled", trk[0].step[0].on == !on);
    memset(&trk[0].step[0], 0, sizeof trk[0].step[0]);
    sim_key(0, 1);
    run(44100u * (STEP_HOLD_MS + 200u) / 1000u);
    check("TRACKS: an off step held past STEP_HOLD_MS: on while still held", trk[0].step[0].on);
    sim_key(0, 0);
    run(2048);
    check("TRACKS: .. let go: stays on", trk[0].step[0].on);
    sim_key(0, 1);
    run(44100u * (STEP_HOLD_MS + 200u) / 1000u);
    sim_key(0, 0);
    run(2048);
    check("TRACKS: an on step held and let go: still on", trk[0].step[0].on);
}

static uint32_t dirty_count(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < SIM_FLASH_SIZE / 4096u; i++)
        n += (sim_flash_dirty()[i / 32u] >> (i % 32u)) & 1u;
    return n;
}

static void power_cycle(const uint8_t *image)        /* RAM lost (.noinit too), the flash image kept */
{
    memset(proj_slot, 0, sizeof proj_slot);
    settings.magic = 0;
    panel.magic = 0;
    sim_flash_reset();
    memcpy(sim_flash(), image, SIM_FLASH_SIZE);
    sim_init(0);
}

static uint8_t image[SIM_FLASH_SIZE];

static void test_demos_and_flash(void)
{
    uint32_t k, on = 0, i;
    sim_init(1);
    for (k = 0; k < 4u; k++)
        on += (uint32_t)project_used(k);
    check("demos: four used project slots", on == 4u);
    for (k = 0, on = 0; k < 16u; k++)
        on += trk[0].step[k].on;
    check("demos: the current project is demo 1 (track 1 has steps)", on > 0u);
    tap(B_PLAY);
    check("demos: PLAY grooves at once", peak(FS) > 0.01f);
    tap(B_PLAY);
    run(FS);
    check("demos: the fresh boot left dirty flash sectors", dirty_count() > 0u);
    check("demos: slot 1 is the remembered project", lastp.set && lastp.slot == 0u);
    sim_flash_clean();
    check("sim_flash_clean clears them", dirty_count() == 0u);
    trk[0].step[1].on = (uint8_t)!trk[0].step[1].on;
    on = trk[0].step[1].on;
    project_save(0);
    check("a save dirties sectors", dirty_count() > 0u);
    memcpy(image, sim_flash(), SIM_FLASH_SIZE);
    power_cycle(image);
    check("round trip: a boot from flash makes slot 1 the current project, with the saved change",
          trk[0].step[1].on == on && project_used(3));
    check("round trip: slot 1 remembered and loaded at power-on (LOADED), no LOAD FAILED question",
          lastp.set && lastp.slot == 0u && song.g[G_SLOT] == 1 && ui.confirm == 0u);
    trk[0].step[2].on = (uint8_t)!trk[0].step[2].on;
    on = trk[0].step[2].on;
    project_save(1);
    memcpy(image, sim_flash(), SIM_FLASH_SIZE);
    power_cycle(image);
    check("returning visit: the last project saved (slot 2) loads, as on the device",
          trk[0].step[2].on == on && song.g[G_SLOT] == 2 && lastp.slot == 1u);
    project_save(0);                                 /* (slot 1 remembered again for the cases below) */
    memcpy(image, sim_flash(), SIM_FLASH_SIZE);
    for (i = 0x97000u; i < 0xE0000u; i++)            /* hostile store contents */
        image[i] = (uint8_t)(i * 2654435761u >> 24);
    power_cycle(image);
    run(FS);
    check("hostile flash: boots and runs", fb_lit() > 500u);
}

/* the firmware's source list (felucca.c's #include "X.c") against the simulator's (sim_host.h): a source the
 * firmware gains and the simulator lacks fails here, by name. Run from the repo root (tools/build_sim.sh). */
static uint32_t c_includes(const char *path, char names[][32], uint32_t max)
{
    FILE *f = fopen(path, "r");
    char line[256];
    uint32_t n = 0;
    if (!f)
        return 0;
    while (fgets(line, sizeof line, f) && n < max) {
        char *q = strstr(line, "#include \""), *e, *b;
        if (!q || q != line)
            continue;
        q += 10;
        if (!(e = strchr(q, '"')) || e - q < 3 || strncmp(e - 2, ".c", 2))
            continue;
        *e = 0;
        b = strrchr(q, '/') ? strrchr(q, '/') + 1 : q;
        snprintf(names[n++], 32, "%s", b);
    }
    fclose(f);
    return n;
}

static void test_drift(void)
{
    static const char *const LEFT_OUT[] = {"lcd.c", "main.c", "ota.c", "console.c",   /* see sim_host.h */
                                           "enc.c",   /* the scan's knob decoder: the page feeds detents (enc_take) */
                                           "smc.c", "bt_midi.c", "bt_page.c", "core_ble_api.c"};   /* FELUCCA_BLE only */
    static char fw[64][32], sim[64][32];
    uint32_t nf = c_includes("firmware/src/felucca.c", fw, 64), ns = c_includes("tests/sim_host.h", sim, 64), i, j, ok = 1;
    check("drift: felucca.c and sim_host.h read", nf > 10u && ns > 10u);
    for (i = 0; i < nf; i++) {
        int found = 0;
        for (j = 0; j < ns; j++)
            found |= !strcmp(fw[i], sim[j]);
        for (j = 0; j < sizeof LEFT_OUT / sizeof LEFT_OUT[0]; j++)
            found |= !strcmp(fw[i], LEFT_OUT[j]);
        if (!found) {
            printf("FAIL  drift: felucca.c includes %s, the simulator does not (tests/sim_host.h)\n", fw[i]);
            ok = 0;
        }
    }
    check("drift: every firmware source is in the simulator or left out on purpose", ok);
}

static void hang(int sig)
{
    static const char msg[] = "FAIL  the simulator hung (no return within 10 s)\n";
    (void)sig;
    fflush(stdout);                                  /* a test binary: the lines before this one matter */
    write(1, msg, sizeof msg - 1u);
    _exit(1);
}

/* menu -> HARDWARE CALIBRATION: panel_setup waits on fm1_ms and input; in the browser no input arrives while it
 * spins (the worklet takes messages between quanta), so it must time out and come back, keeping the panel table */
static void test_calibration_menu(void)
{
    uint32_t i, ms0;
    panel_t before;
    sim_init(1);
    run(FS / 2u);
    ms0 = fm1_ms;
    run(FS);
    check("fm1_ms follows the audio clock", fm1_ms - ms0 >= 990u && fm1_ms - ms0 <= 1010u);
    before = panel;
    sim_btn(B_HOME, 1);                              /* HOME held: the menu */
    run(FS * 8u / 10u);
    sim_btn(B_HOME, 0);
    run(4096);
    check("HOME held opens the menu", ui.menu == 1u);
    for (i = 0; i < 20u && ui.menu_sel != MI_PANEL; i++) {
        sim_enc(EN_SELECT, 1);
        run(2048);
    }
    check("SELECT reaches HARDWARE CALIBRATION", ui.menu_sel == MI_PANEL);
    signal(SIGALRM, hang);
    alarm(10);
    sim_btn(B_OCTUP, 1);                             /* OK */
    run(4096);
    sim_btn(B_OCTUP, 0);
    run(FS);
    alarm(0);
    check("calibration times out and comes back, the panel table kept", !memcmp(&panel, &before, sizeof panel));
    sim_btn(B_HOME, 1);                              /* HOME held again: leave the menu */
    run(FS * 8u / 10u);
    sim_btn(B_HOME, 0);
    run(4096);
    tap(B_PLAY);
    check("out of the menu, the machine still plays", !ui.menu && peak(FS) > 0.01f);
}

static void test_frame_cost(void)                    /* informational: a UI frame runs in the audio thread */
{
    clock_t t0;
    uint32_t i;
    sim_init(1);
    tap(B_EDIT);
    t0 = clock();
    for (i = 0; i < 200u; i++) {
        ui.force = 1;
        sim_frame();
    }
    printf("info  UI frame (full redraw): %.0f us\n", (double)(clock() - t0) * 1e6 / CLOCKS_PER_SEC / 200.0);
}

int main(void)
{
    test_boot_and_input();
    test_audio();
    test_leds();
    test_step_tap_hold();
    test_demos_and_flash();
    test_drift();
    test_calibration_menu();
    test_frame_cost();
    printf(fails ? "sim_test: %d FAILED\n" : "sim_test: all ok\n", fails);
    return fails != 0;
}
