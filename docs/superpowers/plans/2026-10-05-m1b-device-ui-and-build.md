# M1-B Device UI and Firmware Build Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the M1-A drum core its FM-1 user interface (pages, step grid, mixer, model swap, projects), make the real drum firmware build and fit, and prove the UI in a host simulator that renders PNG screenshots — still without installing anything on the FM-1.

**Architecture:** Felucca's UI layer is rewritten around drum pages (`pages.c` new; `ui.c`, `ui_input.c`, `ui_draw.c` rewritten; `ui_menu.c`, `icons.c`, `console.c`, `audio.c`, `project.c` adapted) while the frozen files stay untouched. `main.c` changes only in `felucca_init()` and the boot titles. A host UI harness (`tests/ui_host.h`) replaces the LCD with a framebuffer and the input HAL with injectable state, so page logic and drawing run on the Mac.

**Tech Stack:** C (JieLi clang 4 target / Apple clang host), Python 3 + Pillow (PPM→PNG), shell, Docker (JieLi toolchain).

**Spec:** `docs/superpowers/specs/2026-10-05-drum-core-m1-design.md` (§5 device controls, §6.3 real build). Previous plan: `docs/superpowers/plans/2026-10-05-m1a-host-drum-core.md` (merged).

## Global Constraints

- No device writes: never run `tools/fm1_install.py` or the web installer; never install `mido`/`python-rtmidi`. Packages only with `DRUM_PACKAGE=1`, named `felucca-UNTESTED.fwsc`.
- Frozen (checked by `tools/check_untouched.py`): `hal/`, `loader/`, `ota.c`, `usb.c`, `crt0.S`, `app.ld`, `storage.c` (ruling below: fully unchanged), `main.c` except `felucca_init()` and the two boot-title lines, the last 5 lines of `core.h`.
- **Ruling (this plan): `ST_MAGIC` stays unchanged.** The settings record (palette, low-cut, zoom, the learned panel calibration) has the same format in the drum firmware, so reading it is correct; changing the magic would throw away the user's panel calibration. Old Felucca projects are rejected by the new project magic `"FDR1"`. Spec §2.2/§2.3 intent ("old records ignored, not misread") is met.
- Package identity stays `FM-1_900`; version string `DRUM-0.1` (`FELUCCA_VERSION`).
- The update path must keep working: `ota_service()`/`ota_session()` untouched; `ed_service()` becomes an empty stub (safe: `ota_take()` frees every SysEx frame itself).
- All UI strings shown as values are at most 5 characters; labels at most 5.
- New-file licence header: `/* SPDX-License-Identifier: GPL-3.0-only` + Felucca copyright + `Drum machine fork: 2026 Eugene Vech */`.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Controls (spec §5, user decisions): HOME = macros of the selected track; EDIT = SOUND / SOUND 2; ENV = TRACK / MIDI; LFO = LAYER / LAYER 2; FX = FX / SLICER / DLY / REV-CHO; SEQ = STEP (16-key grid) / PATTERN; GLO = GLOBAL / SYSTEM; SAVE = PROJECT / TOOLS; REC = TRACKS mixer (tap there arms recording); SCL and ARP unused. STEP page: lowest 16 keys toggle off → on → accent → off on the current bank; OCT- / OCT+ change bank; key LEDs show steps and the playhead. PRESET = swap the selected track's model on HOME / TRACK pages; ALGORITHM = track select; SELECT = BPM.

## Review Focus

1. **Recovery path while the UI runs** (OCT-+OCT+ held 5 s → UBOOT; USB SysEx → UBOOT; boot-loop guard) — all live in frozen `main.c`; the plan must not block them (e.g. a UI loop that never returns, OCT buttons consumed). Pinned by `check_untouched.py` and `test_oct_both_reaches_main` (Task 2).
2. **STEP page key handling** — keys must edit steps and must not also trigger drums or MIDI notes; leaving the page restores drum keys. Pinned by `test_grid_keys` (Task 2) and `test_step_mode_keys` (Task 1).
3. **Model change from the UI while audio runs** — params and voices switched atomically (IRQ off), E-params get the new model's defaults (never out of range). Pinned by `test_model_swap` (Task 2).
4. **Project load of corrupt / old data** — wrong magic, bad checksum, out-of-range params or step bytes. Pinned by `test_project_roundtrip` / `test_project_rejects` (Task 4).
5. **Shorter LEN while viewing a later bank** — the bank / grid must follow, never index past LEN. Pinned by `test_bank_follows_len` (Task 2).

---

## File Structure

| File | Responsibility |
|---|---|
| `firmware/src/drum_core.c` | (+) `drum_shed()` for the audio overload guard |
| `firmware/src/seq.c` | (+) keys do not hit drums while the STEP grid is open |
| `firmware/src/pages.c` | (new) page families, page table, `page_desc` |
| `firmware/src/ui.c` | (rewrite) UI state, navigation, track/model/bank/step helpers |
| `firmware/src/ui_input.c` | (rewrite) LEDs, buttons, encoders, STEP grid entry; `panel_setup` kept verbatim |
| `firmware/src/ui_draw.c` | (rewrite) header, columns, graphs (model, grid, steps, FX, slicer, slots, scope), 8-track mixer, footer |
| `firmware/src/ui_menu.c` | (edit) ABOUT text |
| `firmware/src/icons.c` | (edit) drop synth-only references; drum labels |
| `firmware/src/project.c` | (rewrite) drum project format `"FDR1"`; persistence without user presets |
| `firmware/src/audio.c` | (edit) overload sheds a drum voice |
| `firmware/src/console.c` | (edit) status prints the model |
| `firmware/src/felucca.c` | (edit) include list; `ed_service` stub; no `upreset.c`/`editor.c` |
| `firmware/src/main.c` | (edit, allowed region) `felucca_init()`, boot titles |
| `firmware/src/upreset.c`, `firmware/src/editor.c`, `tests/upreset_test.c` | deleted (user presets and web editor return in M5) |
| `tests/ui_host.h` | (new) host UI harness: LCD framebuffer, input/IRQ stubs, PNG dump |
| `tests/ui_test.c` | (new) UI logic, project and screenshot tests |
| `tests/run_tests.sh` | (rewrite) the whole host suite for the drum firmware |
| `tests/target_budget.py`, `tests/target_budget.txt` | (edit) drum render functions |

---

### Task 1: Overload shedding and STEP-mode key gating (drum core)

**Files:**
- Modify: `firmware/src/drum_core.c`, `firmware/src/seq.c`, `tests/drum_test.c`

**Interfaces:**
- Produces: `static void drum_shed(void)` — stops the oldest sounding voice of any track (model or layer) with the declick tail; no-op when nothing sounds. `seq.c` `keyboard_block()` ignores keys while `song.seq_mode` is set (the STEP grid owns them).

- [ ] **Step 1: Write the failing tests** (append to `tests/drum_test.c` before `int main`, register in `main`)

```c
static void test_shed(void)
{
    uint32_t before;
    host_init();
    drum_set_model(&trk[0], DM_CYMB);
    drum_set_model(&trk[1], DM_CYMB);
    drum_hit(&trk[0], 127);                          /* the oldest */
    render_mix(0, 0, CTL);
    drum_hit(&trk[1], 127);
    drum_hit(&trk[1], 127);
    render_mix(0, 0, CTL);
    before = voices_sounding();
    drum_shed();
    check("shed: one voice fewer, the oldest (track 1) with its declick tail",
          before == 3 && voices_sounding() == 2 && !trk[0].v[0].active && trk[0].dtail != 0);
    host_init();
    drum_shed();
    check("shed: nothing sounding is a no-op", voices_sounding() == 0);
}

static void test_step_mode_keys(void)
{
    uint32_t a;
    host_init();
    song.seq_mode = 1;                               /* the STEP grid owns the keys */
    a = dvage;
    fm1_in.notes = 1u << KEY_TRK_KEY[0];
    render_mix(0, 0, CTL);
    check("STEP grid open: keys do not hit drums", dvage == a);
    song.seq_mode = 0;
    fm1_in.notes = 0;
    render_mix(0, 0, CTL);
    fm1_in.notes = 1u << KEY_TRK_KEY[0];
    render_mix(0, 0, CTL);
    check("STEP grid closed: keys hit drums again", dvage != a);
}
```
Register: `test_shed(); test_step_mode_keys();`

- [ ] **Step 2: Run to verify they fail**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: FAIL — `drum_shed` undeclared (compile error).

- [ ] **Step 3: Implement `drum_shed` (refactor `dv_make_room` onto one helper)**

In `firmware/src/drum_core.c` replace `dv_make_room` with:

```c
/* the oldest sounding voice of any track (model or layer): *ot its track; returns how many sound */
static uint32_t dv_oldest(track_t **ot, dvoice_t **ov)
{
    uint32_t i, k, n = 0;
    *ot = 0;
    *ov = 0;
    for (i = 0; i < NTRK; i++)
        for (k = 0; k < 2u * NDV; k++) {
            dvoice_t *v = k < NDV ? &trk[i].v[k] : &trk[i].lv[k - NDV];
            if (!v->active)
                continue;
            n++;
            if (!*ov || v->age < (*ov)->age) {
                *ov = v;
                *ot = &trk[i];
            }
        }
    return n;
}

/* over the cap: the oldest sounding voice of any track stops (declick tail) */
static void dv_make_room(void)
{
    track_t *ot;
    dvoice_t *ov;
    if (dv_oldest(&ot, &ov) >= DRUM_MAXV && ov)
        dv_cut(ot, ov);
}

/* audio overload (audio.c, > 85 % of a half): the oldest sounding voice stops */
static void drum_shed(void)
{
    track_t *ot;
    dvoice_t *ov;
    if (dv_oldest(&ot, &ov) && ov)
        dv_cut(ot, ov);
}
```

- [ ] **Step 4: Gate the keys in `firmware/src/seq.c`**

In `keyboard_block()` replace
```c
    uint32_t cur = fm1_in.notes, ch = cur ^ kb_prev, i;
    kb_prev = cur;
```
with
```c
    uint32_t cur = fm1_in.notes, ch = cur ^ kb_prev, i;
    kb_prev = cur;
    if (song.seq_mode)
        return;                                     /* the STEP grid (ui_input.c) owns the keys */
```

- [ ] **Step 5: Run the tests**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: all `ok`, `ALL DRUM HOST TESTS PASSED`.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/drum_core.c firmware/src/seq.c tests/drum_test.c
git commit -m "drum core: drum_shed for the audio overload guard; keys idle while the STEP grid is open

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: UI host harness, pages, UI logic and input

**Files:**
- Create: `firmware/src/pages.c`, `tests/ui_host.h`, `tests/ui_test.c`
- Rewrite: `firmware/src/ui.c`, `firmware/src/ui_input.c` (keep `panel_setup` and its `SETUP_IDLE_MS` define byte-for-byte)
- Modify: `firmware/src/icons.c`, `firmware/src/ui_menu.c` (ABOUT text only), `tests/run_drum_tests.sh`
- Create (temporary, replaced in Task 3): none — `ui_draw.c` is rewritten in Task 3; until then `tests/ui_host.h` defines `ui_draw()` only under `UI_NO_DRAW`.

**Interfaces:**
- Consumes: `drum_set_model`, `drum_cut`, `track_desc`, `TP`, `GP`, `N_MODEL`, `NMODELS`, `DMODELS`, `panel_*`, `B_*`, `EN_*`, `transport_req`, `song`, `trk`.
- Produces (used by Tasks 3–5):
  - `pages.c`: `FAM_*`, `SC_*`, `GR_*`, `page_t`, `PAGES[]`, `NPAGES`, `FAM_BTN[]`, `page_desc(pg, slot, &vp)`.
  - `ui.c`: `ui` state; `cur_page()`, `page_first(fam)`, `ui_say(a, b)`, `ui_message(s)`, `open_family(fam)`, `go_home()`, `grid_mode()`, `bank_set(b)`, `bank_fix()`, `track_select(i)`, `model_step(dir)`, `home_param(k, &vp)`, `step_tap(k)`, `track_clear(t)`.
  - `ui_input.c`: `ui_leds()`, `ui_input()`, `panel_setup()`, `enc_drop()` (moved here from ui_menu? no — stays in ui_menu.c).
  - `tests/ui_host.h`: `ui_host_init()`, `press(btn)`, `release_all()`, `turn(enc, steps)`, `keys(mask)`, `ui_frame()`, `fb[240*240]`, `shot(path)`.

- [ ] **Step 1: Write the harness and the failing tests**

`tests/ui_host.h`:
```c
/* Host build of the drum firmware's UI: the DSP harness, a 240x240 framebuffer for the LCD, and the
 * input / IRQ HAL replaced by state the tests set. Screens are written as PPM (tests convert to PNG). */
#include "drum_host.h"
#define FELUCCA_FLASH 0
#define FELUCCA_OTA 0
#define FELUCCA_CDC 0
/* ---- LCD: framebuffer (RGB565, native order) */
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_init(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fb[(y + j) * 240u + x + i] = (uint16_t)((px[j * w + i] >> 8) | (px[j * w + i] << 8));   /* canvas is byte-swapped */
}
/* ---- input HAL (hal/fm1_input.h API, host state) */
#define FM1_NCOL 11u
/* FM1_KEYMAP: copy the table byte for byte from hal/fm1_input.h (the LED positions depend on it) */
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = { /* <- verbatim rows from hal/fm1_input.h */ };
static uint8_t fm1_led[FM1_NCOL];
static uint32_t host_btn_edges, host_note_edges, host_ticks;
static int32_t host_enc[7];
#define FM1_TICKS_PER_US 24u
static uint32_t fm1_ticks(void) { return host_ticks += 24u * 1000u; }   /* 1 ms per call */
static int32_t fm1_enc_take(uint32_t e) { int32_t s = host_enc[e % 7u]; host_enc[e % 7u] = 0; return s; }
static uint32_t fm1_input_edges(uint32_t *released) { uint32_t p = host_btn_edges; (void)released; host_btn_edges = 0; return p; }
static uint32_t fm1_input_note_edges(void) { uint32_t p = host_note_edges; host_note_edges = 0; return p; }
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void fm1_wdt_feed(void) {}
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/pages.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/icons.c"
#ifndef UI_NO_DRAW
#include "../firmware/src/ui_draw.c"
#else
static void ui_draw(void) {}
#endif
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"
#ifdef UI_NO_PROJECT                                  /* until Task 4 rewrites project.c */
static void project_save(uint32_t s) { (void)s; }
static void project_load(uint32_t s) { (void)s; }
static int project_used(uint32_t s) { (void)s; return 0; }
static void settings_save(void) {}
#else
#include "../firmware/src/project.c"
#endif

static void ui_host_init(void)
{
    host_init();
    memset(&ui, 0, sizeof ui);
    memset(fb, 0, sizeof fb);
    panel = PANEL_DEFAULT;
    settings.magic = 0;
    settings_init();
    ui.home = 1;
    ui.force = 1;
}

static void press(uint32_t label)                   /* one press edge + held for this frame */
{
    host_btn_edges |= 1u << panel.btn[label];
    fm1_in.buttons |= 1u << panel.btn[label];
}
static void release_all(void) { fm1_in.buttons = 0; }
static void turn(uint32_t role, int32_t steps) { host_enc[panel.enc[role]] += steps * panel.dir[role]; }
static void keys(uint32_t mask) { host_note_edges |= mask & ~fm1_in.notes; fm1_in.notes = mask; }
static void ui_frame(void) { ui_input(); ui_leds(); ui_draw(); render_mix(0, 0, CTL); }

static void shot(const char *path)                  /* the framebuffer as binary PPM */
{
    FILE *f = fopen(path, "wb");
    uint32_t i;
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = fb[i];
        uint8_t px[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63u) << 2), (uint8_t)((c & 31u) << 3)};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}
```
(If the compiler reports a further HAL symbol used by the UI files — e.g. a `usb.*` field read by `draw_head` — add a host stub with the HAL header's exact signature here and note it in the ledger. `fm1_in` comes from `drum_host.h`; add the fields the UI reads (`buttons`, `notes`) there if missing.)

`tests/ui_test.c` (Task 2 tests; Tasks 3 and 4 append):
```c
/* Drum firmware UI checks on the host. */
#include "ui_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static void test_families(void)
{
    static const struct { uint32_t btn; uint32_t fam; } MAP[] = {
        {B_EDIT, FAM_SND}, {B_ENV, FAM_TRK}, {B_LFO, FAM_LAY}, {B_FX, FAM_FX},
        {B_SEQ, FAM_SEQ}, {B_GLO, FAM_GLO}, {B_SAVE, FAM_SAVE},
    };
    uint32_t i, ok = 1;
    ui_host_init();
    for (i = 0; i < sizeof MAP / sizeof MAP[0]; i++) {
        press(MAP[i].btn);
        ui_frame();
        release_all();
        ok &= !ui.home && cur_page()->fam == MAP[i].fam;
    }
    check("buttons open their page families (EDIT SOUND, ENV TRACK, LFO LAYER, FX, SEQ, GLO, SAVE)", ok);
    press(B_SCL);
    ui_frame();
    release_all();
    check("SCL does nothing", !ui.home && cur_page()->fam == FAM_SAVE);
    press(B_EDIT);
    ui_frame();
    release_all();
    press(B_EDIT);
    ui_frame();
    release_all();
    check("EDIT again: SOUND 2", str_eq(cur_page()->title, "SOUND 2"));
}

static void test_track_select(void)
{
    ui_host_init();
    turn(EN_ALGO, 3);
    ui_frame();
    check("ALGORITHM selects tracks (1 -> 2 per detent)", song.sel == 1);
    for (int i = 0; i < 20; i++) {
        turn(EN_ALGO, 1);
        ui_frame();
    }
    check("ALGORITHM stops at track 8", song.sel == NTRK - 1);
}

static void test_model_swap(void)
{
    uint32_t m0;
    ui_host_init();
    m0 = (uint32_t)trk[0].p[P_MODEL];
    trk[0].p[P_E1] = 3;                              /* an edited DECAY */
    drum_hit(&trk[0], 127);
    turn(EN_PRESET, 1);
    ui_frame();
    check("PRESET on HOME: next model with its default sound, voices stopped",
          (uint32_t)trk[0].p[P_MODEL] == (m0 + 1u) % NMODELS && trk[0].model == trk[0].p[P_MODEL] &&
              trk[0].p[P_E1] == DMODELS[(m0 + 1u) % NMODELS].edit[1].def && !trk[0].v[0].active);
    press(B_ENV);
    ui_frame();
    release_all();
    turn(EN_K1, -1);
    ui_frame();
    check("MODEL knob on TRACK: back to the first model, defaults loaded",
          (uint32_t)trk[0].p[P_MODEL] == m0 && trk[0].p[P_E1] == DMODELS[m0].edit[1].def);
    press(B_FX);
    ui_frame();
    release_all();
    turn(EN_PRESET, 1);
    ui_frame();
    check("PRESET elsewhere (FX page) leaves the model alone", (uint32_t)trk[0].p[P_MODEL] == m0);
}

static void test_home_macros(void)
{
    ui_host_init();
    trk[0].p[P_E1] = 10;
    turn(EN_K2, 5);
    ui_frame();
    check("HOME: KNOB 2 edits DECAY (P_E1) of the selected track", trk[0].p[P_E1] == 15);
}

static void open_step_page(void)
{
    press(B_SEQ);
    ui_frame();
    release_all();
}

static void test_grid_keys(void)
{
    uint32_t a;
    ui_host_init();
    open_step_page();
    check("SEQ opens the STEP grid (keys belong to it)", str_eq(cur_page()->title, "STEP") && song.seq_mode);
    a = dvage;
    keys(1u << 2);
    ui_frame();
    keys(0);
    ui_frame();
    check("grid: key 3 sets step 3 on, no drum hit", trk[0].step[2].on && !trk[0].step[2].acc && dvage == a);
    keys(1u << 2);
    ui_frame();
    keys(0);
    ui_frame();
    check("grid: second tap = accent", trk[0].step[2].on && trk[0].step[2].acc);
    keys(1u << 2);
    ui_frame();
    keys(0);
    ui_frame();
    check("grid: third tap = off", !trk[0].step[2].on && !trk[0].step[2].acc);
    trk[0].p[P_SLEN] = 64;
    press(B_OCTUP);
    ui_frame();
    release_all();
    keys(1u << 0);
    ui_frame();
    keys(0);
    ui_frame();
    check("grid: OCT+ moves to bank 2 (key 1 = step 17)", ui.bank == 1 && trk[0].step[16].on);
    {
        uint32_t i, on0 = 0, on1 = 0;
        for (i = 0; i < NSTEP; i++)
            on0 += trk[0].step[i].on + trk[0].step[i].acc;
        keys(1u << 20);
        ui_frame();
        keys(0);
        ui_frame();
        for (i = 0; i < NSTEP; i++)
            on1 += trk[0].step[i].on + trk[0].step[i].acc;
        check("grid: keys above the lowest 16 change no step", on0 == on1);
    }
    press(B_SEQ);                                    /* PATTERN page: keys play drums again */
    ui_frame();
    release_all();
    check("PATTERN page: the grid lets go of the keys", !song.seq_mode);
}

static void test_bank_follows_len(void)
{
    ui_host_init();
    trk[0].p[P_SLEN] = 64;
    open_step_page();
    press(B_OCTUP);
    ui_frame();
    release_all();
    press(B_OCTUP);
    ui_frame();
    release_all();
    press(B_OCTUP);
    ui_frame();
    release_all();
    check("bank 4 of 64 steps", ui.bank == 3);
    trk[0].p[P_SLEN] = 20;                           /* LEN shortened elsewhere (knob, project) */
    ui_frame();
    check("shorter LEN: the bank follows (20 steps = banks 1..2)", ui.bank == 1);
    keys(1u << 10);                                  /* step 27 > LEN: ignored */
    ui_frame();
    keys(0);
    ui_frame();
    check("grid: a key past LEN changes nothing", !trk[0].step[26].on);
}

static void test_mixer_and_rec(void)
{
    ui_host_init();
    press(B_REC);
    ui_frame();
    release_all();
    ui_frame();                                      /* REC acts on release (tap) */
    check("REC tap elsewhere opens the TRACKS mixer", !ui.home && cur_page()->fam == FAM_MIX);
    turn(EN_K1, 1);
    ui_frame();
    check("TRACKS: KNOB 1 selects the track", song.sel == 1);
    press(B_REC);
    ui_frame();
    release_all();
    ui_frame();
    check("TRACKS: REC tap arms the selected track and starts play", ((song.rec >> 1) & 1u) && song.playing);
}

static void test_clear_confirm(void)
{
    uint32_t i;
    ui_host_init();
    trk[0].step[0].on = trk[0].step[5].on = 1;
    open_step_page();
    fm1_in.buttons |= 1u << panel.btn[B_REC];
    for (i = 0; i < 800; i++)                        /* REC held 0.8 s (1 ms per tick) */
        ui_input();
    release_all();
    check("REC held on SEQ: the clear dialog", ui.confirm != 0);
    press(B_OCTUP);
    ui_frame();
    release_all();
    check("OCT+ clears the track's steps", !trk[0].step[0].on && !trk[0].step[5].on && !ui.confirm);
}

static void test_oct_both_reaches_main(void)
{
    ui_host_init();
    open_step_page();
    fm1_in.buttons = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
    ui_input();
    check("OCT- + OCT+ held: buttons stay visible to main.c (UBOOT countdown)",
          (fm1_in.buttons & ((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]))) ==
              ((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP])));
    release_all();
}

int main(void)
{
    test_families();
    test_track_select();
    test_model_swap();
    test_home_macros();
    test_grid_keys();
    test_bank_follows_len();
    test_mixer_and_rec();
    test_clear_confirm();
    test_oct_both_reaches_main();
    printf(fails ? "ui_test: %d FAILED\n" : "ui_test: all passed\n", fails);
    return fails ? 1 : 0;
}
```

In `tests/run_drum_tests.sh`, after the `drumsim` lines add:
```sh
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -DUI_NO_DRAW -DUI_NO_PROJECT -Ibuild/gen -Ifirmware/src -o "$OUT/ui_test" tests/ui_test.c -lm
"$OUT/ui_test"
```
(Task 3 drops `-DUI_NO_DRAW`, Task 4 drops `-DUI_NO_PROJECT`.)

- [ ] **Step 2: Run to verify it fails**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: FAIL — `pages.c` not found (compile error).

- [ ] **Step 3: `firmware/src/pages.c`**

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 Eugene Vech */
/* Drum UI pages: a family per page button (pressing it again steps through its pages), and what
 * the four knobs edit on each page. */
enum { FAM_HOME, FAM_SND, FAM_TRK, FAM_LAY, FAM_FX, FAM_SEQ, FAM_GLO, FAM_SAVE, FAM_MIX, FAM_COUNT };
enum { SC_TRACK, SC_GLOBAL, SC_GRID, SC_MIX };   /* knobs edit: the selected track, song.g, the STEP grid, the mixer */
enum { GR_NONE, GR_MODEL, GR_FX, GR_SLCR, GR_GRID, GR_STEPS, GR_SLOTS, GR_MIX };

typedef struct {
    const char *title;
    uint8_t fam, scope, graph;
    uint8_t id[4];               /* parameter ids; 0xFF = empty column */
} page_t;

static const page_t PAGES[] = {
    {"SOUND", FAM_SND, SC_TRACK, GR_MODEL, {P_E0, P_E1, P_E2, P_E3}},
    {"SOUND 2", FAM_SND, SC_TRACK, GR_MODEL, {P_E4, P_E5, P_E6, P_E7}},
    {"TRACK", FAM_TRK, SC_TRACK, GR_MODEL, {P_MODEL, P_LEVEL, P_PAN, P_CHOKE}},
    {"MIDI", FAM_TRK, SC_TRACK, GR_NONE, {P_NOTE, P_MUTE, 0xFF, 0xFF}},
    {"LAYER", FAM_LAY, SC_TRACK, GR_NONE, {P_LSET, P_LKEY, P_LLEVEL, P_LTUNE}},
    {"LAYER 2", FAM_LAY, SC_TRACK, GR_NONE, {P_LDEC, 0xFF, 0xFF, 0xFF}},
    {"FX", FAM_FX, SC_TRACK, GR_FX, {P_DIST, P_CHOR, P_DLY, P_REV}},
    {"SLICER", FAM_FX, SC_TRACK, GR_SLCR, {P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH}},
    {"DLY", FAM_FX, SC_GLOBAL, GR_NONE, {G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX}},
    {"REV/CHO", FAM_FX, SC_GLOBAL, GR_NONE, {G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH}},
    {"STEP", FAM_SEQ, SC_GRID, GR_GRID, {0xFF, 0xFF, 0xFF, 0xFF}},   /* KNOB 1: bank */
    {"PATTERN", FAM_SEQ, SC_TRACK, GR_STEPS, {P_SLEN, P_SDIV, P_SSWING, 0xFF}},
    {"GLOBAL", FAM_GLO, SC_GLOBAL, GR_NONE, {G_BPM, G_SWING, G_CLOCK, G_DRCH}},
    {"SYSTEM", FAM_GLO, SC_GLOBAL, GR_NONE, {G_MIDI, G_SYNC, G_ROUTE, G_INFO}},
    {"PROJECT", FAM_SAVE, SC_GLOBAL, GR_SLOTS, {G_SLOT, 0xFF, G_LOAD, G_SAVE}},
    {"TOOLS", FAM_SAVE, SC_GLOBAL, GR_NONE, {G_CLRSEQ, G_INITSND, 0xFF, 0xFF}},
    {"TRACKS", FAM_MIX, SC_MIX, GR_MIX, {0xFF, 0xFF, 0xFF, 0xFF}},   /* TRACK LEVEL LEN PAN */
};
#define NPAGES (sizeof(PAGES) / sizeof(PAGES[0]))

/* the button of each family (SCL and ARP have none) */
static const uint8_t FAM_BTN[FAM_COUNT] = {B_HOME, B_EDIT, B_ENV, B_LFO, B_FX, B_SEQ, B_GLO, B_SAVE, B_REC};

static const param_desc_t *page_desc(const page_t *pg, uint32_t slot, int16_t **valp)
{
    uint32_t id = pg->id[slot & 3u];
    *valp = 0;
    if (id == 0xFFu || pg->scope == SC_GRID || pg->scope == SC_MIX)
        return 0;
    if (pg->scope == SC_GLOBAL) {
        *valp = &song.g[id];
        return &GP[id];
    }
    *valp = &TSEL->p[id];
    return track_desc(TSEL, id);
}
```

- [ ] **Step 4: Rewrite `firmware/src/ui.c`**

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 Eugene Vech */
/* Drum machine user interface: state, page navigation, track / model selection, the STEP grid.
 * Four columns map to KNOB 1..4; rendering (ui_draw.c) is lazy: every element remembers what it
 * last drew and is redrawn only on change. */
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "DRUM-0.1"
#endif
static void project_save(uint32_t slot);
static void project_load(uint32_t slot);
static int project_used(uint32_t slot);
static void panel_setup(void);

#define ACC C_HI
#define VAL(c) ((c) == ui.hot_col && ui.hot_t ? C_WHITE : C_HI)
#define RATIO(d, v) ((d)->max > (d)->min ? ((int32_t)(v) - (d)->min) * 1000 / ((d)->max - (d)->min) : -1)
#define Y_HEAD 0
#define H_HEAD 20
#define Y_LABEL 26
#define Y_VALUE 44
#define Y_GAUGE 64
#define Y_SEP_END 70
#define Y_GRAPH 74
#define H_GRAPH 124
#define G_OY 24
#define Y_FOOT 202
#define H_FOOT 38

static struct {
    uint8_t home;
    uint8_t page;                /* index into PAGES */
    uint8_t fam_last[FAM_COUNT]; /* last page used per family */
    uint8_t bank;                /* STEP grid: 16-step bank shown on the keys */
    uint8_t hot_col, hot_t;      /* column whose knob was just turned (drawn white) */
    uint8_t menu;                /* 0 off, 1 list, 2 about (HOME held) */
    uint8_t menu_sel;
    uint32_t menu_sig, home_t0;
    uint8_t force;               /* full redraw pending */
    uint8_t msg_t;               /* transient message frames */
    uint8_t bpm_t;
    uint8_t arm, arm_t;          /* destructive action armed: param id, frames left to confirm */
    uint32_t rec_t0;             /* REC press time (btn_hold) */
    uint8_t confirm;             /* 1 = "clear track n?" */
    uint8_t confirm_trk;
    char msg[24];
    uint32_t enc_t[NE];
    char col[4][32];
    char focus_l[8], focus_v[8], focus_u[8];
    uint32_t graph_sig, head_sig, foot_sig, frame;
    uint8_t graph_top;
} ui;

static const page_t *cur_page(void) { return &PAGES[ui.page]; }

static uint32_t page_first(uint32_t fam)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == fam)
            return i;
    return 0;
}

static void ui_say(const char *a, const char *b)     /* transient message in the top bar: a + b */
{
    uint32_t n;
    str_cpy(ui.msg, a, sizeof ui.msg);
    n = str_len(ui.msg);
    str_cpy(ui.msg + n, b, sizeof ui.msg - n);
    ui.msg_t = 40;
}

static void ui_message(const char *s) { ui_say(s, ""); }

static int grid_mode(void) { return !ui.home && !ui.menu && cur_page()->scope == SC_GRID; }

static void page_entered(void)
{
    song.seq_mode = (uint8_t)grid_mode();            /* seq.c: the keys belong to the grid */
    ui.hot_t = 0;
    ui.force = 1;
}

static void open_family(uint32_t fam)
{
    if (!ui.home && cur_page()->fam == fam) {          /* same button again: next page */
        uint32_t i = ui.page + 1u;
        if (i >= NPAGES || PAGES[i].fam != fam)
            i = page_first(fam);
        ui.page = (uint8_t)i;
    } else {
        ui.page = ui.fam_last[fam] && PAGES[ui.fam_last[fam]].fam == fam ? ui.fam_last[fam]
                                                                          : (uint8_t)page_first(fam);
    }
    ui.fam_last[fam] = ui.page;
    ui.home = 0;
    page_entered();
}

static void go_home(void)
{
    ui.home = 1;
    ui.hot_t = 0;
    song.seq_mode = 0;
    ui.force = 1;
}

/* ------------------------------------------------------ STEP grid --- */
static uint32_t bank_count(void) { return ((uint32_t)clamp(TSEL->p[P_SLEN], 1, NSTEP) + 15u) / 16u; }
static void bank_set(int32_t b) { ui.bank = (uint8_t)clamp(b, 0, (int32_t)bank_count() - 1); }
static void bank_fix(void)                             /* LEN shortened (knob, project): onto the last bank */
{
    if (ui.bank >= bank_count())
        bank_set((int32_t)bank_count() - 1);
}

/* key k of the grid: step bank * 16 + k cycles off -> on -> accent -> off (inside LEN only) */
static void step_tap(uint32_t k)
{
    uint32_t si = ui.bank * 16u + k;
    step_t *s;
    if (k >= 16u || si >= (uint32_t)TSEL->p[P_SLEN])
        return;
    s = &TSEL->step[si];
    if (!s->on)
        s->on = 1;
    else if (!s->acc)
        s->acc = 1;
    else
        s->on = s->acc = 0;
}

static void track_clear(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        t->step[i].on = t->step[i].acc = 0;
}

/* --------------------------------------------------- track, model --- */
static void track_select(uint32_t i)
{
    if (i >= NTRK || i == song.sel)
        return;
    song.sel = (uint8_t)i;
    ui.bank = 0;
    ui.force = 1;
}

/* the next / previous model on the selected track, with its default sound; with the audio IRQ off,
 * so the ISR never renders a model with another model's parameters */
static void model_step(int32_t dir)
{
    uint32_t m = ((uint32_t)TSEL->p[P_MODEL] + (dir > 0 ? 1u : NMODELS - 1u)) % NMODELS;
    fm1_irq_off();
    drum_set_model(TSEL, m);
    fm1_irq_on();
    ui_say("MODEL ", N_MODEL[m]);
    ui.force = 1;
}

/* HOME: KNOB k edits the selected model's macro k (TUNE DECAY TONE CHAR) */
static const param_desc_t *home_param(uint32_t k, int16_t **vp)
{
    *vp = &TSEL->p[P_E0 + (k & 3u)];
    return track_desc(TSEL, P_E0 + (k & 3u));
}
```

- [ ] **Step 5: Rewrite `firmware/src/ui_input.c`**

Replace everything above `/* ---------------------------------------------------- panel setup --- */` with the code below; keep the panel-setup section (the `SETUP_IDLE_MS` define and `panel_setup()`) unchanged.

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 Eugene Vech */
/* Drum UI input: LEDs, knobs and buttons, the STEP grid on the keys, panel setup. */
/* ----------------------------------------------------------- LEDs --- */
/* The LED picture is built off-line and copied one byte per column: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker. */
static uint8_t led_pos[41];                        /* (col << 3) | row bit, 0xFF = none */

static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}

static void led_put(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}

static uint32_t cur_fam(void) { return ui.home ? FAM_HOME : cur_page()->fam; }

static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0};
    uint32_t k, c, fam = cur_fam();
    static uint8_t ready;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    led_put(nl, panel.btn[FAM_BTN[fam]], 1);
    led_put(nl, panel.btn[B_PLAY], song.playing && ((song.tick / 64u) & 1u) == 0u);   /* blinks: intended */
    led_put(nl, panel.btn[B_REC], song.rec != 0u);
    if (grid_mode()) {                                /* the bank's steps; the playhead inverted */
        const track_t *t = TSEL;
        led_put(nl, panel.btn[B_OCTDN], ui.bank > 0u);
        led_put(nl, panel.btn[B_OCTUP], ui.bank + 1u < bank_count());
        for (k = 0; k < 16u; k++) {
            uint32_t si = ui.bank * 16u + k;
            int on = si < (uint32_t)t->p[P_SLEN] && t->step[si].on;
            if (song.playing && si == t->seq_idx)
                on = !on;
            led_put(nl, 14u + k, on);
        }
    } else {
        for (k = 0; k < 27u; k++)
            led_put(nl, 14u + k, (int)((fm1_in.notes >> k) & 1u));
    }
    for (c = 0; c < FM1_NCOL; c++)
        fm1_led[c] = nl[c];
}

/* ---------------------------------------------------------- input --- */
static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    uint32_t now = fm1_ticks(), dt = now - ui.enc_t[role];
    ui.enc_t[role] = now;
    if (range > 40 && dt < 60u * 1000u * FM1_TICKS_PER_US)
        return s * (range > 150 ? 6 : 3);
    return s;
}

/* TRACKS mixer: KNOB 1 TRACK, 2 LEVEL (a muted track: the first turn unmutes), 3 LEN, 4 PAN */
static void tracks_edit(uint32_t slot, int32_t steps)
{
    track_t *t = TSEL;
    uint32_t id;
    if (slot == 0u) {
        track_select((uint32_t)clamp((int32_t)song.sel + (steps > 0 ? 1 : -1), 0, NTRK - 1));
        return;
    }
    if (slot == 1u && t->p[P_MUTE]) {
        t->p[P_MUTE] = 0;
        return;
    }
    id = slot == 1u ? P_LEVEL : slot == 2u ? P_SLEN : P_PAN;
    t->p[id] = (int16_t)clamp(t->p[id] + accel(EN_K1 + slot, steps, TP[id].max - TP[id].min), TP[id].min, TP[id].max);
}

static void tracks_rec_tap(void)                       /* arm / disarm; arming while stopped starts play */
{
    uint8_t bit = (uint8_t)(1u << song.sel);
    song.rec ^= bit;
    if ((song.rec & bit) && !song.playing)
        transport_req = 1;
}

static void edit_param(uint32_t slot, int32_t steps)
{
    int16_t *vp;
    const page_t *pg = cur_page();
    const param_desc_t *d;
    uint32_t id = pg->id[slot];
    int32_t v;
    if (pg->scope == SC_GRID) {
        if (slot == 0u)
            bank_set((int32_t)ui.bank + (steps > 0 ? 1 : -1));
        return;
    }
    if (pg->scope == SC_MIX) {
        tracks_edit(slot, steps);
        return;
    }
    if (pg->scope == SC_TRACK && id == P_MODEL) {     /* a model comes with its default sound */
        model_step(steps);
        return;
    }
    d = page_desc(pg, slot, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    v = clamp(*vp + accel(EN_K1 + slot, steps, d->max - d->min), d->min, d->max);
    *vp = (int16_t)v;
    if (!v || pg->scope != SC_GLOBAL)
        return;
    if ((id == G_LOAD || id == G_SAVE || id == G_CLRSEQ || id == G_INITSND) && ui.arm != id) {
        *vp = 0;                                      /* one detent arms, a second one within ~1.5 s acts */
        ui.arm = (uint8_t)id;
        ui.arm_t = 90;
        ui_say("AGAIN: ", d->label);
        return;
    }
    ui.arm = 0;
    switch (id) {                                     /* GO buttons: act, then back to 0 */
    case G_LOAD:
        *vp = 0;
        project_load((uint32_t)song.g[G_SLOT] - 1u);
        break;
    case G_SAVE:
        *vp = 0;
        project_save((uint32_t)song.g[G_SLOT] - 1u);
        break;
    case G_CLRSEQ:
        *vp = 0;
        track_clear(TSEL);
        ui_message("PATTERN CLEARED");
        break;
    case G_INITSND:
        *vp = 0;
        fm1_irq_off();
        drum_set_model(TSEL, (uint32_t)TSEL->p[P_MODEL]);
        fm1_irq_on();
        ui_message("SOUND INIT");
        ui.force = 1;
        break;
    default:
        break;
    }
}

/* HOME / REC: tap on release, hold 0.7 s fires once. t0 = press time | 1,
 * bit 1 = fired (or swallowed: then the release is no tap either) */
enum { BT_NONE, BT_TAP, BT_HOLD };
static uint32_t btn_hold(uint32_t *t0, uint32_t label, uint32_t now, int hold_ok)
{
    uint32_t tap;
    if ((fm1_in.buttons >> panel.btn[label]) & 1u) {
        if (!*t0)
            *t0 = (now | 1u) & ~2u;
        else if (hold_ok && !(*t0 & 2u) && now - (*t0 & ~3u) > 700u * 1000u * FM1_TICKS_PER_US) {
            *t0 |= 2u;
            return BT_HOLD;
        }
        return BT_NONE;
    }
    tap = *t0 && !(*t0 & 2u);
    *t0 = 0;
    return tap ? BT_TAP : BT_NONE;
}

static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), now = fm1_ticks(), id, b, k, fam = cur_fam();
    uint32_t home = btn_hold(&ui.home_t0, B_HOME, now, 1);
    uint32_t rec = btn_hold(&ui.rec_t0, B_REC, now, !ui.menu && (fam == FAM_SEQ || fam == FAM_MIX));
    int32_t s;
    if (home == BT_HOLD) {                              /* HOME held: open the menu, or leave it */
        if (ui.menu) {
            menu_close();
        } else {
            ui.menu = 1;
            ui.menu_sel = 0;
            ui.confirm = 0;
            ui.force = 1;
            song.seq_mode = 0;
        }
    }
    if (ui.menu) {
        if (ui.rec_t0)
            ui.rec_t0 |= 2u;
        if (!ui.home_t0)
            menu_input(pressed);
        return;
    }
    if (rec == BT_HOLD) {                               /* REC held on SEQ / TRACKS: "clear track n?" */
        ui.confirm = 1;
        ui.confirm_trk = song.sel;
        ui.force = 1;
    } else if (rec == BT_TAP && !ui.confirm) {
        if (fam == FAM_MIX)
            tracks_rec_tap();
        else if (fam == FAM_SEQ)
            song.rec ^= (uint8_t)(1u << song.sel);
        else
            open_family(FAM_MIX);
    }
    if (ui.confirm) {                                   /* OCT- cancels, OCT+ clears; nothing else reacts */
        if ((pressed >> panel.btn[B_OCTUP]) & 1u) {
            char m[12] = "1 CLEARED";
            m[0] = (char)('1' + ui.confirm_trk);
            track_clear(&trk[ui.confirm_trk % NTRK]);
            ui_say("TRACK ", m);
            ui.confirm = 0;
            ui.force = 1;
        } else if ((pressed >> panel.btn[B_OCTDN]) & 1u) {
            ui.confirm = 0;
            ui.force = 1;
        }
        enc_drop();
        return;
    }
    if (home == BT_TAP)
        go_home();
    bank_fix();                                         /* LEN may have changed (knob, load) */
    for (id = 0; id < 14u; id++) {
        if (!((pressed >> id) & 1u))
            continue;
        b = panel_btn_of(id);
        switch (b) {
        case B_PLAY:
            transport_req = song.playing ? 2 : 1;
            break;
        case B_REC:
        case B_HOME:                                    /* tap / hold: above */
            break;
        case B_OCTDN:
        case B_OCTUP:                                   /* the STEP grid's bank; elsewhere nothing */
            if (grid_mode())
                bank_set((int32_t)ui.bank + (b == B_OCTUP ? 1 : -1));
            break;
        default: {
            uint32_t f;
            for (f = FAM_HOME + 1u; f < FAM_COUNT; f++)
                if (FAM_BTN[f] == b && f != FAM_MIX)
                    open_family(f);
            break;
        }
        }
    }
    if (grid_mode())
        for (k = 0; k < 16u; k++)
            if ((notes >> k) & 1u)
                step_tap(k);
    if ((s = panel_enc(EN_PRESET)) != 0 && (ui.home || cur_fam() == FAM_TRK))
        model_step(s);
    if ((s = panel_enc(EN_ALGO)) != 0)
        track_select((uint32_t)clamp((int32_t)song.sel + (s > 0 ? 1 : -1), 0, NTRK - 1));
    if ((s = panel_enc(EN_SELECT)) != 0) {
        song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
        ui.bpm_t = 40;
    }
    for (k = 0; k < 4u; k++) {
        const page_t *pg = cur_page();
        int16_t *hv;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        if (ui.home || pg->scope == SC_GRID || pg->scope == SC_MIX || page_desc(pg, k, &hv)) {
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
        if (ui.home) {
            int16_t *vp;
            const param_desc_t *d = home_param(k, &vp);
            if (d->max > d->min)
                *vp = (int16_t)clamp(*vp + accel(EN_K1 + k, s, d->max - d->min), d->min, d->max);
        } else {
            edit_param(k, s);
        }
    }
}
```

- [ ] **Step 6: `icons.c` and `ui_menu.c`**

`firmware/src/icons.c`: in `param_icon` delete the three blocks that reference `TP[P_LWAVE]`, `TP[P_ARATE]` and `N_TRIO_MODE` and the `#if FELUCCA_SLICE` block, and add instead
```c
    if (d == &TP[P_SLRATE])
        return ICON_DIVISION;                 /* SLICER RATE is a note division, not Hz */
```
Append to `ICON_MAP[]` (drum labels):
```c
    /* drum models and pages */
    {"TUNE", ICON_PITCH}, {"DECAY", ICON_DECAY}, {"TONE", ICON_CUTOFF}, {"MODEL", ICON_DRUM},
    {"CHOKE", ICON_GATE}, {"LSET", ICON_SAMPLE}, {"LKEY", ICON_PITCH}, {"LLVL", ICON_LEVEL},
    {"LTUNE", ICON_PITCH}, {"LDEC", ICON_DECAY}, {"BANK", ICON_STEPS}, {"SET", ICON_SAMPLE},
    {"KEY", ICON_PITCH}, {"DRIVE", ICON_MOD}, {"SNAP", ICON_NOISE}, {"CLICK", ICON_ACCENT},
    {"LEN", ICON_STEPS}, {"MUTE", ICON_MIX},
```
`firmware/src/ui_menu.c` ABOUT block: replace the lines from `cv_text(4, 4, &FONT_L, "FELUCCA", C_HI);` to the `"VOICE: REF. KLATTSCH (MIT)"` line with:
```c
            cv_text(4, 4, &FONT_L, "FM-1 DRUMS", C_HI);
            cv_text(4, 36, &FONT_S, "DRUM MACHINE ON FELUCCA", C_AMB);
            cv_text(4, 54, &FONT_S, FELUCCA_VERSION, C_HI);
            cv_text(236 - text_w(&FONT_S, __DATE__), 54, &FONT_S, __DATE__, C_GRAY);   /* build date */
            cv_text(4, 72, &FONT_S, "FORK: EUGENE VECH", C_HI);
            cv_text(cv_text(4, 88, &FONT_S, "FELUCCA: LEO KUROSHITA", C_HI) + 8, 88, &FONT_S, "", C_AMB);
            cv_text(4, 104, &FONT_S, "H\xDCGELTON INSTRUMENTS", C_AMB);
            cv_text(4, 119, &FONT_S, "GPL-3.0, NO WARRANTY", C_HI);
            cv_text(4, 132, &FONT_S, "GITHUB.COM/HUGELTON/FELUCCA", C_AMB);
            cv_text(4, 146, &FONT_S, "FONT: TERMINUS (OFL)", C_DIM);
            cv_text(4, 159, &FONT_S, "SAMPLES: VERSILIAN (CC0)", C_DIM);
            cv_text(4, 172, &FONT_S, "+ H\xDCGELTON SAMPLE PACK", C_DIM);
            cv_text(4, 185, &FONT_S, "UNTESTED ON HARDWARE", C_DIM);
```

- [ ] **Step 7: Run the tests**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: `ui_test: all passed` and `ALL DRUM HOST TESTS PASSED`. Missing HAL symbols: add stubs to `tests/ui_host.h` as described there (ledger them); never change a frozen file.

- [ ] **Step 8: Commit**

```bash
git add firmware/src/pages.c firmware/src/ui.c firmware/src/ui_input.c firmware/src/icons.c firmware/src/ui_menu.c tests/ui_host.h tests/ui_test.c tests/run_drum_tests.sh
git commit -m "ui: drum pages, model swap, STEP grid on the keys, 8-track mixer input; host UI harness

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Drawing and screenshots

**Files:**
- Rewrite: `firmware/src/ui_draw.c`
- Modify: `tests/ui_test.c`, `tests/run_drum_tests.sh` (drop `-DUI_NO_DRAW`; convert screenshots)

**Interfaces:**
- Consumes: Task 2's `ui`, `PAGES`, `page_desc`, `home_param`, `grid_mode`, `bank_count`; `gfx.c` canvas; `icons.c`; `scope_buf`/`scope_w` (declared in `tests/ui_host.h` for the host, `audio.c` on target).
- Produces: `ui_draw()`; screenshots `build/ui_shots/*.png`.

- [ ] **Step 1: Write the failing test** (append to `tests/ui_test.c`, register `test_screens();` in `main`)

```c
static uint32_t fb_lit(uint32_t y0, uint32_t y1)    /* non-black pixels in rows y0..y1 */
{
    uint32_t n = 0, i;
    for (i = y0 * 240u; i < y1 * 240u; i++)
        n += fb[i] != 0;
    return n;
}

static void snap_page(const char *name)
{
    char path[96];
    uint32_t i;
    for (i = 0; i < 3; i++)
        ui_frame();
    snprintf(path, sizeof path, "build/ui_shots/%s.ppm", name);
    shot(path);
}

static void test_screens(void)
{
    static const struct { uint32_t btn; uint32_t presses; const char *name; } P[] = {
        {B_EDIT, 1, "01_sound"}, {B_EDIT, 2, "02_sound2"}, {B_ENV, 1, "03_track"}, {B_ENV, 2, "04_midi"},
        {B_LFO, 1, "05_layer"}, {B_FX, 1, "06_fx"}, {B_FX, 2, "07_slicer"}, {B_SEQ, 1, "08_step"},
        {B_SEQ, 2, "09_pattern"}, {B_GLO, 1, "10_global"}, {B_SAVE, 1, "11_project"},
    };
    uint32_t i, k, ok = 1;
    ui_host_init();
    for (i = 0; i < NTRK; i++)                       /* a pattern to show */
        for (k = 0; k < 16; k += 1 + i % 4)
            trk[i].step[k].on = 1;
    trk[0].step[4].acc = 1;
    transport_req = 1;
    snap_page("00_home");
    ok &= fb_lit(0, 20) > 50 && fb_lit(26, 70) > 200 && fb_lit(202, 240) > 100;
    for (i = 0; i < sizeof P / sizeof P[0]; i++) {
        press(FAM_BTN[FAM_HOME]);                    /* from HOME, n presses of the button */
        ui_frame();
        release_all();
        for (k = 0; k < 800; k++)
            ui_input();                              /* (HOME acts on release) */
        for (k = 0; k < P[i].presses; k++) {
            press(P[i].btn);
            ui_frame();
            release_all();
        }
        snap_page(P[i].name);
        ok &= fb_lit(26, 70) > 100 && fb_lit(202, 240) > 100;
    }
    press(B_REC);
    ui_frame();
    release_all();
    ui_frame();
    snap_page("12_tracks");
    ok &= fb_lit(74, 198) > 300;                     /* 8 strips */
    check("every page draws header, columns and footer (screens in build/ui_shots)", ok);
}
```

In `tests/run_drum_tests.sh` change the `ui_test` lines to:
```sh
mkdir -p build/ui_shots
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -DUI_NO_PROJECT -Ibuild/gen -Ifirmware/src -o "$OUT/ui_test" tests/ui_test.c -lm
"$OUT/ui_test"
"$PY" -c "import glob,PIL.Image as I; [I.open(p).resize((480,480),I.NEAREST).save(p[:-4]+'.png') for p in glob.glob('build/ui_shots/*.ppm')]" && rm -f build/ui_shots/*.ppm && echo "screens: build/ui_shots"
```

- [ ] **Step 2: Run to verify it fails**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: FAIL — compile errors in the old `ui_draw.c` (engines, `is_drum`, `step_on`, ...).

- [ ] **Step 3: Rewrite `firmware/src/ui_draw.c`**

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 Eugene Vech */
/* Drum UI drawing: status bar (top), columns + gauges, graphs, focus readout, footer
 * (the bank's steps + model / track / page). */
static void draw_menu(void);

static uint32_t str_hash(uint32_t h, const char *s)
{
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

/* at most 5 characters, and no wider than maxw */
static void fit(char *d, const char *src, const felucca_font_t *f, int32_t maxw)
{
    str_cpy(d, src, 6);
    while (d[0] && text_w(f, d) > maxw)
        d[str_len(d) - 1u] = 0;
}

static int32_t batt_level(void)
{
    return song.batt_raw >= 591 ? 3 : song.batt_raw >= 561 ? 2 : song.batt_raw >= 531 ? 1 : 0;
}
static int32_t batt_shown(void)
{
    if (usb.config && !usb.suspended)
        return 1 + (int32_t)((fm1_ms / 600u) % 3u);
    return batt_level();
}

/* top bar: transport, BPM | track, USB, battery; messages replace it */
static void draw_head(void)
{
    char b[16];
    uint32_t i;
    int32_t x;
    uint32_t rec = (song.rec >> song.sel) & 1u ? 2u : song.rec != 0u;
    uint32_t sig = (uint32_t)song.playing * 3u + rec * 5u + song.sel * 13131u +
                   (ui.msg_t ? str_hash(7u, ui.msg) : 0u) + (uint32_t)song.g[G_BPM] * 101u + (ui.bpm_t != 0) * 31u +
                   (uint32_t)batt_shown() * 7777u + (usb.config && !usb.suspended) * 99991u;
    if (!ui.force && sig == ui.head_sig)
        return;
    ui.head_sig = sig;
    cv_begin(240, H_HEAD, C_BLACK);
    if (ui.msg_t) {
        cv_text(4, 1, &FONT_S, ui.msg, C_HI);
        cv_blit(0, Y_HEAD);
        return;
    }
    if (song.playing) {
        for (i = 0; i < 5u; i++)
            cv_rect(4 + (int32_t)i * 2, 4 + (int32_t)i, 2, 10 - 2 * (int32_t)i, C_WHITE);
    } else {
        cv_rect(4, 5, 8, 8, C_HI);
    }
    if (rec)
        cv_rect(18, 6, 6, 6, rec == 2u ? C_WHITE : C_GRAY);
    fmt_int(b, song.g[G_BPM]);
    x = 32;
    if (FELUCCA_ICONS) {
        cv_icon(x, 2, ICON_TEMPO, C_GRAY);
        x += 14;
    }
    cv_text(x, 1, &FONT_S, b, ui.bpm_t ? C_WHITE : C_HI);
    b[0] = 'T';
    b[1] = (char)('1' + song.sel);
    b[2] = 0;
    cv_text(140, 1, &FONT_S, b, C_HI);
    {
        int32_t lvl = batt_shown(), k, bx = 236 - 19;
        cv_rect(bx, 4, 17, 1, C_GRAY);
        cv_rect(bx, 12, 17, 1, C_GRAY);
        cv_rect(bx, 4, 1, 9, C_GRAY);
        cv_rect(bx + 16, 4, 1, 9, C_GRAY);
        cv_rect(bx + 17, 6, 2, 5, C_GRAY);
        for (k = 0; k < lvl; k++)
            cv_rect(bx + 2 + k * 5, 6, 3, 5, lvl == 1 && batt_level() <= 1 ? C_WHITE : C_HI);
        if (usb.config && !usb.suspended)
            cv_text(bx - 28, 1, &FONT_S, "USB", C_DIM);
    }
    cv_blit(0, Y_HEAD);
}

static void draw_frame(void)
{
    uint32_t i;
    lcd_fill(0, H_HEAD, 240, Y_LABEL - H_HEAD, C_BLACK);
    lcd_fill(0, Y_SEP_END, 240, Y_GRAPH - Y_SEP_END, C_BLACK);
    lcd_fill(0, Y_GRAPH + H_GRAPH, 240, Y_FOOT - Y_GRAPH - H_GRAPH, C_BLACK);
    for (i = 0; i < 4u; i++)
        lcd_fill(i * 60u, Y_LABEL, 4, Y_SEP_END - Y_LABEL, C_BLACK);
    lcd_fill(0, H_HEAD, 240, 1, C_LINE);
    lcd_fill(0, Y_FOOT - 2, 240, 1, C_LINE);
    for (i = 1; i < 4u; i++)
        lcd_fill(i * 60u - 1u, H_HEAD + 4, 1, Y_SEP_END - H_HEAD - 4, C_LINE);
}

#define LABEL_X (FELUCCA_ICONS ? ICON_CELL + ICON_GAP : 0)
static void draw_column(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc,
                        int32_t ratio, uint32_t icon)
{
    char l[8], v[8], u[8], key[32];
    int32_t x, gw = 52, fx;
    if (icon == ICON_AUTO)
        icon = icon_for_label(label);
    fit(l, label, &FONT_S, 54 - LABEL_X);
    fit(v, val, &FONT_S, 48);
    fit(u, unit, &FONT_S, 54 - text_w(&FONT_S, v) - 3);
    str_cpy(key, l, 8);
    str_cpy(key + str_len(key), "|", 2);
    str_cpy(key + str_len(key), v, 8);
    str_cpy(key + str_len(key), "|", 2);
    str_cpy(key + str_len(key), u, 8);
    {
        uint32_t n = str_len(key);
        key[n] = (char)('A' + (vc == C_WHITE) + (vc == C_DIM) * 2);
        key[n + 1] = (char)(' ' + (ratio < 0 ? 0 : 1 + ratio / 20));
        key[n + 2] = (char)(icon == ICON_NONE ? '~' : '!' + icon % 90u);
        key[n + 3] = 0;
    }
    if (c == ui.hot_col) {
        str_cpy(ui.focus_l, l, 8);
        str_cpy(ui.focus_v, v, 8);
        str_cpy(ui.focus_u, u, 8);
    }
    if (!ui.force && str_eq(key, ui.col[c]))
        return;
    str_cpy(ui.col[c], key, sizeof ui.col[c]);
    cv_begin(55, Y_SEP_END - Y_LABEL, C_BLACK);
    if (FELUCCA_ICONS && icon != ICON_NONE && l[0])
        cv_icon(0, 1, icon, C_GRAY);
    cv_text(l[0] ? LABEL_X : 0, 0, &FONT_S, l, C_GRAY);
    x = cv_text(0, Y_VALUE - Y_LABEL, &FONT_S, v, vc);
    cv_text(x + 3, Y_VALUE - Y_LABEL, &FONT_S, u, C_DIM);
    if (ratio >= 0) {
        int32_t gy = Y_GAUGE - Y_LABEL;
        fx = ratio * gw / 1000;
        cv_rect(0, gy + 1, gw, 1, C_LINE);
        cv_rect(0, gy, fx, 3, C_DIM);
        cv_rect(fx, gy - 1, 1, 5, vc == C_WHITE ? C_WHITE : C_HI);
    }
    cv_blit(c * 60u + 4u, Y_LABEL);
}

/* ---------------------------------------------------------- graphs --- */
static uint32_t steps_hash(const track_t *t)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < NSTEP; i++)
        h = (h ^ (t->step[i].on + t->step[i].acc * 2u)) * 16777619u;
    return h ^ (uint32_t)t->p[P_SLEN] * 7919u;
}

/* the selected track's model: name large, voices and choke group */
static void graph_model(const track_t *t, uint16_t c)
{
    const dmodel_t *m = &DMODELS[(uint32_t)t->p[P_MODEL] % NMODELS];
    char b[16];
    cv_text(6, 10, &FONT_L, m->name, c);
    str_cpy(b, m->voices > 1 ? "2 VOICES" : "1 VOICE", sizeof b);
    cv_text(6, 52, &FONT_S, b, C_GRAY);
    if (t->p[P_CHOKE]) {
        str_cpy(b, "CHOKE 1", sizeof b);
        b[6] = (char)('0' + t->p[P_CHOKE]);
        cv_text(100, 52, &FONT_S, b, C_AMB);
    }
    if (t->p[P_LLEVEL])
        cv_text(6, 70, &FONT_S, "+ SAMPLE LAYER", C_AMB);
}

/* PATTERN page: 4 rows of 16 steps over LEN; accent tall, playhead white */
static void graph_steps(const track_t *t, uint16_t c)
{
    uint32_t i, len = (uint32_t)t->p[P_SLEN];
    for (i = 0; i < NSTEP && i < len; i++) {
        int32_t x = 6 + (int32_t)(i % 16u) * 14 + (int32_t)(i % 16u) / 4 * 4, y = 6 + (int32_t)(i / 16u) * 24;
        const step_t *st = &t->step[i];
        if (st->on)
            cv_rect(x, st->acc ? y : y + 4, 2, st->acc ? 14 : 10, st->acc ? C_WHITE : c);
        else
            cv_rect(x, y + 13, 2, 1, C_DIM);
        if (song.playing && i == t->seq_idx)
            cv_rect(x - 1, y + 16, 4, 3, C_WHITE);
    }
}

/* STEP page: the bank's 16 steps as the keys show them; bank n/m on the left */
static void graph_grid(const track_t *t, uint16_t c)
{
    uint32_t i, len = (uint32_t)t->p[P_SLEN];
    char b[8];
    for (i = 0; i < 16u; i++) {
        uint32_t si = ui.bank * 16u + i;
        int32_t x = 4 + (int32_t)i * 14 + (int32_t)(i / 4u) * 2, y = 30;
        const step_t *st = &t->step[si];
        if (si >= len) {
            cv_rect(x, y + 20, 11, 1, C_LINE);
            continue;
        }
        if (st->on)
            cv_rect(x, st->acc ? y : y + 10, 11, st->acc ? 40 : 30, st->acc ? C_WHITE : c);
        else
            cv_rect(x, y + 36, 11, 4, C_DIM);
        if (song.playing && si == t->seq_idx)
            cv_rect(x, y + 46, 11, 3, C_WHITE);
    }
    fmt_int(b, (int32_t)ui.bank + 1);
    str_cpy(b + str_len(b), "/", 4);
    fmt_int(b + str_len(b), (int32_t)bank_count());
    cv_text(4, 4, &FONT_S, "BANK", C_GRAY);
    cv_text(48, 4, &FONT_S, b, C_HI);
    cv_text(4, 84, &FONT_S, "KEYS: OFF > ON > ACC", C_DIM);
}

static void graph_fx(const track_t *t, uint16_t c)
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        int32_t h = t->p[P_DIST + i] * 80 / 127, x = (int32_t)i * 60 + 28;
        cv_rect(x, 10, 1, 80, C_LINE);
        cv_rect(x, 90 - h, 1, h, c);
        cv_rect(x - 3, 90 - h, 7, 1, c);
    }
}
```
Then copy `graph_slicer` and `graph_scope` **unchanged** from the previous `ui_draw.c` (both only read `track_t` fields that still exist: `p[]`, `sl[]`, `scope_buf`). Then:

```c
static void graph_slots(void)
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        int32_t y = 8 + (int32_t)i * 26;
        char b[4];
        int sel = (int32_t)i + 1 == song.g[G_SLOT];
        b[0] = (char)('1' + i);
        b[1] = 0;
        if (sel)
            cv_rect(4, y + 6, 3, 3, C_WHITE);
        cv_text(14, y, &FONT_S, b, sel ? C_WHITE : C_GRAY);
        cv_text(40, y, &FONT_S, project_used(i) ? "USED" : "EMPTY", project_used(i) ? (sel ? C_WHITE : C_HI) : C_DIM);
    }
}

/* TRACKS mixer: 8 strips of 30 px: number + REC/ARM/MUTE dot, level fader with the output meter,
 * the pattern over LEN with the play head. Each strip redraws only when its signature changes. */
#define MX_Y (Y_GRAPH + 4)
#define MX_H 112
static struct {
    uint32_t sig[NTRK];
    uint8_t meter[NTRK];
} mx;

static int32_t meter_px(int32_t a)                   /* |sample| (Q15) -> px, 6 dB = MX_H / 10 */
{
    int32_t lg = 0, v;
    if (a < 64)
        return 0;
    while ((a >> lg) > 1)
        lg++;
    v = lg * 8 + (((a << 3) >> lg) & 7);
    return clamp((v - 48) * (MX_H - 20) / 80, 0, MX_H - 20);
}

static void draw_mix(void)
{
    uint32_t c;
    if (ui.force) {
        lcd_fill(0, Y_GRAPH, 240, H_GRAPH, C_BLACK);
        for (c = 0; c < NTRK; c++)
            mx.meter[c] = 0;
    }
    for (c = 0; c < NTRK; c++) {
        track_t *t = &trk[c];
        uint32_t sel = c == song.sel, lvl = (uint32_t)t->p[P_LEVEL] & 127u, mute = !lvl || t->p[P_MUTE];
        uint32_t arm = (song.rec >> c) & 1u, len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, sig, i;
        int32_t m = mute ? 0 : meter_px(t->peak), fy;
        char b[2] = {(char)('1' + c), 0};
        t->peak = 0;
        if (m < mx.meter[c] - 3)
            m = mx.meter[c] - 3;
        mx.meter[c] = (uint8_t)(m < 0 ? 0 : m);
        sig = 1u + sel + arm * 2u + mute * 4u + lvl * 8u + mx.meter[c] * 1024u + steps_hash(t) * 131u +
              (song.playing ? t->seq_idx + 1u : 0u) * 2654435761u;
        if (!ui.force && sig == mx.sig[c])
            continue;
        mx.sig[c] = sig;
        cv_begin(29, MX_H, C_BLACK);
        cv_text(2, 0, &FONT_S, b, sel ? C_WHITE : C_GRAY);
        if (arm)
            cv_rect(14, 5, 6, 6, song.playing ? C_WHITE : C_AMB);
        else if (mute)
            cv_rect(14, 7, 6, 2, C_DIM);
        fy = (MX_H - 2) - (int32_t)lvl * (MX_H - 22) / 127;      /* fader + meter, rows 20.. */
        cv_rect(4, 20, 1, MX_H - 20, C_LINE);
        cv_rect(2, fy, 5, 2, sel ? C_HI : C_GRAY);
        if (mx.meter[c])
            cv_rect(8, MX_H - mx.meter[c], 2, mx.meter[c], C_AMB);
        for (i = 0; i < len; i++) {                              /* pattern column x 13..26 */
            int32_t r0 = 20 + (int32_t)(i * (MX_H - 20) / len), r1 = 20 + (int32_t)((i + 1u) * (MX_H - 20) / len);
            if (t->step[i].on)
                cv_rect(t->step[i].acc ? 13 : 15, r0, t->step[i].acc ? 12 : 8, r1 - r0 > 2 ? r1 - r0 - 1 : 1,
                        sel ? (t->step[i].acc ? C_WHITE : C_HI) : C_GRAY);
            if (song.playing && i == t->seq_idx % len)
                cv_rect(26, r0, 2, r1 - r0 > 1 ? r1 - r0 : 1, C_WHITE);
        }
        cv_blit(c * 30u, MX_Y);
    }
}

static uint32_t graph_signature(void)
{
    const page_t *pg = cur_page();
    const track_t *t = TSEL;
    uint32_t h = 2166136261u, i;
    if (ui.hot_t && settings.zoom)
        h = str_hash(str_hash(str_hash(h ^ 0x5555u, ui.focus_v), ui.focus_l), ui.focus_u);
    if (ui.home)
        return h ^ (ui.frame / 2u);                  /* scope: redraw every other frame */
    h ^= (uint32_t)pg->graph * 131u + song.sel * 7777u + ui.bank * 104729u;
    for (i = 0; i < P_COUNT; i++)
        h = (h ^ (uint32_t)t->p[i]) * 16777619u;
    h ^= (uint32_t)song.g[G_SLOT] * 13u;
    if (pg->graph == GR_SLCR && t->p[P_SLCR])
        h ^= (sl[song.sel].idx + 1u) * 2654435761u;
    if (pg->graph == GR_SLOTS)
        for (i = 0; i < 4u; i++)
            h ^= (uint32_t)project_used(i) << (20u + i);
    if (pg->graph == GR_STEPS || pg->graph == GR_GRID)
        h ^= steps_hash(t) + (song.playing ? t->seq_idx + 1u : 0u) * 31u;
    return h;
}

static void draw_graph(void)
{
    const page_t *pg = cur_page();
    const track_t *t = TSEL;
    uint16_t c = ACC;
    uint32_t sig, top = 0;
    if (!ui.home && pg->graph == GR_MIX) {
        draw_mix();
        ui.graph_top = 1;
        return;
    }
    sig = graph_signature();
    if (!ui.force && sig == ui.graph_sig)
        return;
    ui.graph_sig = sig;
    cv_begin(240, H_GRAPH, C_BLACK);
    cv_oy = G_OY;
    if (ui.home) {
        graph_scope(c);
    } else {
        switch (pg->graph) {
        case GR_MODEL:
            graph_model(t, c);
            break;
        case GR_STEPS:
            graph_steps(t, c);
            break;
        case GR_GRID:
            graph_grid(t, c);
            break;
        case GR_FX:
            graph_fx(t, c);
            break;
        case GR_SLCR:
            graph_slicer(t, c);
            break;
        case GR_SLOTS:
            cv_oy = 0;
            top = 1;
            graph_slots();
            break;
        default:
            break;
        }
    }
    cv_oy = 0;
    if (ui.hot_t && settings.zoom) {
        int32_t x;
        top = 1;
        cv_rect(0, 0, 150, 50, C_BLACK);
        cv_text(4, 0, &FONT_S, ui.focus_l, C_GRAY);
        x = cv_text(4, 16, &FONT_L, ui.focus_v, C_WHITE);
        cv_text(x + 4, 30, &FONT_S, ui.focus_u, C_DIM);
    }
    cv_blit_from(0, Y_GRAPH, top || ui.graph_top || ui.force ? 0u : G_OY);
    ui.graph_top = (uint8_t)top;
}

/* footer: the bank's 16 steps; the model, track and page */
static void draw_foot(void)
{
    char ti[20], tn[4];
    const track_t *t = TSEL;
    const page_t *pg = cur_page();
    const char *mn = N_MODEL[(uint32_t)t->p[P_MODEL] % NMODELS];
    uint32_t sig, i;
    if (ui.home) {
        str_cpy(ti, "HOME", sizeof ti);
    } else {
        uint32_t n = 0, k = 0;
        for (i = 0; i < NPAGES; i++)
            if (PAGES[i].fam == pg->fam) {
                n++;
                if (i == ui.page)
                    k = n;
            }
        str_cpy(ti, pg->title, 10);
        if (n > 1) {
            str_cpy(ti + str_len(ti), " ", 4);
            fmt_int(ti + str_len(ti), (int32_t)k);
            str_cpy(ti + str_len(ti), "/", 4);
            fmt_int(ti + str_len(ti), (int32_t)n);
        }
    }
    tn[0] = 'T';
    tn[1] = (char)('1' + song.sel);
    tn[2] = 0;
    sig = str_hash(str_hash(0x9E3779B9u, ti), mn) + song.sel * 7u + steps_hash(t) + ui.bank * 3001u +
          (song.playing && t->seq_idx / 16u == ui.bank ? t->seq_idx + 1u : 0u) * 97u;
    if (!ui.force && sig == ui.foot_sig)
        return;
    ui.foot_sig = sig;
    cv_begin(240, H_FOOT, C_BLACK);
    for (i = 0; i < 16u; i++) {
        uint32_t si = ui.bank * 16u + i;
        int32_t sx = 6 + (int32_t)i * 14 + (int32_t)(i / 4u) * 4;
        const step_t *st = &t->step[si];
        if (si >= (uint32_t)t->p[P_SLEN])
            continue;
        if (st->on)
            cv_rect(sx, st->acc ? 1 : 3, 2, st->acc ? 10 : 8, st->acc ? C_WHITE : C_HI);
        else
            cv_rect(sx, 10, 2, 1, C_DIM);
        if (song.playing && si == t->seq_idx)
            cv_rect(sx - 1, 13, 4, 3, C_WHITE);
    }
    cv_text(4, 20, &FONT_S, mn, C_HI);
    cv_text(60, 20, &FONT_S, tn, C_AMB);
    cv_text(236 - text_w(&FONT_S, ti), 20, &FONT_S, ti, C_GRAY);
    cv_blit(0, Y_FOOT);
}

static void draw_columns(void)
{
    uint32_t c;
    char val[12];
    const char *unit;
    const page_t *pg = cur_page();
    if (ui.home) {
        for (c = 0; c < 4u; c++) {
            int16_t *vp;
            const param_desc_t *d = home_param(c, &vp);
            if (!d->label || d->label[0] == '-') {
                draw_column(c, "", "", "", C_HI, -1, ICON_AUTO);
                continue;
            }
            param_format(d, *vp, val, &unit);
            draw_column(c, d->label, val, unit, VAL(c), RATIO(d, *vp), param_icon(d, *vp));
        }
        return;
    }
    if (pg->scope == SC_MIX) {                          /* TRACK LEVEL LEN PAN of the selected track */
        const track_t *t = TSEL;
        uint32_t lvl = (uint32_t)t->p[P_LEVEL];
        fmt_int(val, (int32_t)song.sel + 1);
        draw_column(0, "TRACK", val, "/8", VAL(0u), (int32_t)song.sel * 1000 / (NTRK - 1), ICON_AUTO);
        if (!lvl || t->p[P_MUTE]) {
            str_cpy(val, "MUTE", 12);
            unit = "";
        } else {
            param_format(&TP[P_LEVEL], (int32_t)lvl, val, &unit);
        }
        draw_column(1, "LEVEL", val, unit, lvl && !t->p[P_MUTE] ? VAL(1u) : C_DIM, (int32_t)lvl * 1000 / 127, ICON_AUTO);
        param_format(&TP[P_SLEN], t->p[P_SLEN], val, &unit);
        draw_column(2, "LEN", val, unit, VAL(2u), RATIO(&TP[P_SLEN], t->p[P_SLEN]), ICON_AUTO);
        param_format(&TP[P_PAN], t->p[P_PAN], val, &unit);
        draw_column(3, "PAN", val, unit, VAL(3u), RATIO(&TP[P_PAN], t->p[P_PAN]), param_icon(&TP[P_PAN], t->p[P_PAN]));
        return;
    }
    if (pg->scope == SC_GRID) {                         /* BANK; the rest: the grid on the keys */
        char u[8];
        fmt_int(val, (int32_t)ui.bank + 1);
        str_cpy(u, "/", 8);
        fmt_int(u + 1, (int32_t)bank_count());
        draw_column(0, "BANK", val, u, VAL(0u), -1, ICON_AUTO);
        for (c = 1; c < 4u; c++)
            draw_column(c, "", "", "", C_HI, -1, ICON_AUTO);
        return;
    }
    for (c = 0; c < 4u; c++) {
        int16_t *vp;
        const param_desc_t *d = page_desc(pg, c, &vp);
        if (!d || !d->label || d->label[0] == '-') {
            draw_column(c, "", "", "", C_HI, -1, ICON_AUTO);
            continue;
        }
        if (pg->id[c] == G_MIDI && pg->scope == SC_GLOBAL) {
            str_cpy(val, !usb.up ? "OFF" : usb.config ? "MIDI" : usb.setups ? "ENUM" : usb.sof_seen ? "BUS" : "WAIT", 12);
            draw_column(c, "USB", val, "USB", C_HI, -1, ICON_AUTO);
            continue;
        }
        if (pg->id[c] == G_INFO && pg->scope == SC_GLOBAL) {
            fmt_int(val, (int32_t)(song.cpu_q8 * 100u / 256u));
            unit = "%";
        } else {
            param_format(d, *vp, val, &unit);
        }
        draw_column(c, d->label, val, unit, VAL(c), d->fmt == F_ENUM && d->max < 2 ? -1 : RATIO(d, *vp),
                    param_icon(d, *vp));
    }
}

static void ui_draw(void)
{
    ui.frame++;
    if (ui.menu) {
        draw_menu();
        ui.force = 0;
        return;
    }
    if (ui.confirm) {
        if (ui.force) {
            char b[16] = "CLEAR TRACK 1?";
            b[12] = (char)('1' + ui.confirm_trk);
            lcd_fill(0, 0, 240, 240, C_BLACK);
            draw_text_box(0, 84, 240, &FONT_S, b, C_WHITE, 1);
            draw_text_box(0, 132, 240, &FONT_S, "OCT- NO    OCT+ YES", C_GRAY, 1);
            ui.force = 0;
        }
        return;
    }
    bank_fix();
    if (ui.force)
        draw_frame();
    felucca_dbg.stage = 3;
    draw_head();
    felucca_dbg.stage = 4;
    draw_columns();
    felucca_dbg.stage = 5;
    draw_graph();
    if (ui.msg_t)
        ui.msg_t--;
    if (ui.bpm_t)
        ui.bpm_t--;
    if (ui.arm_t && !--ui.arm_t)
        ui.arm = 0;
    if (ui.hot_t)
        ui.hot_t--;
    felucca_dbg.stage = 6;
    draw_foot();
    ui.force = 0;
}
```
For the host: `tests/ui_host.h` must provide `felucca_dbg` (a struct with `stage`), `scope_buf[SCOPE_N]`, `scope_w`, `SCOPE_N` (512), and `usb` fields read here (`config`, `suspended`, `up`, `setups`, `sof_seen`) — `usb.c` (already included by `drum_host.h`) defines `usb`; add the rest:
```c
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N];
static uint32_t scope_w;
static struct { uint32_t stage, page, home, ui_frames; } felucca_dbg;
```
(insert before the `#include "../firmware/src/gfx.c"` line; `drum_host.h`'s `fx.c` writes no scope — on the target `audio.c` owns these.)

- [ ] **Step 4: Run the tests and look at the screens**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh && ls build/ui_shots`
Expected: `ui_test: all passed`, 13 PNG files. Open several (e.g. with the Read tool) and check: no text overflowing a column, the STEP grid shows 16 cells with the accent tall, the mixer shows 8 strips, the footer reads model / T# / page.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/ui_draw.c tests/ui_host.h tests/ui_test.c tests/run_drum_tests.sh
git commit -m "ui: drum drawing (model, STEP grid, pattern, 8-track mixer, footer); host screenshots

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Drum project format

**Files:**
- Rewrite: `firmware/src/project.c`
- Modify: `tests/ui_test.c`

**Interfaces:**
- Produces: `project_t` (`"FDR1"`), `proj_slot[4]`, `project_save(slot)`, `project_load(slot)`, `project_used(slot)`, `persist_boot()`, `settings_save()` (same names `main.c` and the UI call).

- [ ] **Step 1: Write the failing tests** (append; register `test_project_roundtrip(); test_project_rejects();`)

```c
static void test_project_roundtrip(void)
{
    ui_host_init();
    drum_set_model(&trk[2], DM_CONGA);
    trk[2].p[P_E0] = -5;
    trk[2].p[P_LLEVEL] = 77;
    trk[2].step[7].on = trk[2].step[7].acc = 1;
    trk[2].p[P_SLEN] = 23;
    song.g[G_BPM] = 133;
    song.sel = 2;
    project_save(1);
    ui_host_init();
    check("project: a saved slot reads as used", project_used(1) && !project_used(0));
    project_load(1);
    check("project: load restores models, params, steps, globals, selection",
          trk[2].p[P_MODEL] == DM_CONGA && trk[2].model == DM_CONGA && trk[2].p[P_E0] == -5 &&
              trk[2].p[P_LLEVEL] == 77 && trk[2].step[7].on && trk[2].step[7].acc && trk[2].p[P_SLEN] == 23 &&
              song.g[G_BPM] == 133 && song.sel == 2);
}

static void test_project_rejects(void)
{
    ui_host_init();
    project_save(0);
    proj_slot[0].t[3].p[P_MODEL] = 99;               /* corrupt the stored data, fix the checksum */
    proj_slot[0].t[3].p[P_E1] = 30000;
    proj_slot[0].t[3].step[0].on = 7;
    proj_slot[0].sum = proj_sum(&proj_slot[0]);
    project_load(0);
    check("project: out-of-range values are clamped on load",
          trk[3].p[P_MODEL] < NMODELS && trk[3].p[P_E1] <= DMODELS[trk[3].p[P_MODEL]].edit[1].max &&
              trk[3].step[0].on == 1);
    proj_slot[1].magic = 0x46554E33u;                /* an old Felucca project ("FUN3") */
    check("project: Felucca projects are not used", !project_used(1));
    project_save(2);
    proj_slot[2].sum ^= 1;
    check("project: a bad checksum is not used", !project_used(2));
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: FAIL — compile errors in the old `project.c` (engines, `TRK_DEF`, `apply_preset_to`, step fields).

- [ ] **Step 3: Rewrite `firmware/src/project.c`**

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 Eugene Vech */
/* Projects: four slots in .noinit RAM, so they survive resets and UBOOT entry. With FELUCCA_FLASH
 * every save also goes to flash through storage.c, and an empty RAM slot is filled from flash.
 * Format "FDR1": the globals, the selected track, and per track every parameter and its 64 steps.
 * Felucca's formats ("FUN1".."FUN3") are not read. Settings + the panel table: as in Felucca. */
#define PROJ_MAGIC 0x31524446u                 /* "FDR1": 8 drum tracks */
typedef struct {
    int16_t p[P_COUNT];
    step_t step[NSTEP];
} proj_trk_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];
    proj_trk_t t[NTRK];
    uint32_t sum;
} project_t;
project_t proj_slot[4] __attribute__((section(".noinit")));

static uint32_t proj_hash(const void *p, uint32_t n)   /* FNV-1a over n bytes */
{
    const uint8_t *b = p;
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}
static uint32_t proj_sum(const project_t *p) { return proj_hash(p, sizeof *p - 4u); }
static int proj_ok(const project_t *q) { return q->magic == PROJ_MAGIC && q->size == sizeof *q && q->sum == proj_sum(q); }

#if FELUCCA_FLASH
static void proj_fetch(uint32_t slot)
{
    project_t *q = &proj_slot[slot & 3u];
    if (st_load(OBJ_PROJECT0 + (slot & 3u), q, sizeof *q) != (int)sizeof *q || !proj_ok(q))
        q->magic = 0;
}
#endif

static void project_save(uint32_t slot)
{
    project_t *p = &proj_slot[slot & 3u];
    uint32_t i;
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    for (i = 0; i < G_COUNT; i++)
        p->g[i] = song.g[i];
    p->sel = song.sel;
    for (i = 0; i < NTRK; i++) {
        memcpy(p->t[i].p, trk[i].p, sizeof trk[i].p);
        memcpy(p->t[i].step, trk[i].step, sizeof trk[i].step);
    }
    p->sum = proj_sum(p);
#if FELUCCA_FLASH
    if (flash_ok) {
        ui_message(st_save(OBJ_PROJECT0 + (slot & 3u), p, sizeof *p) ? "SAVE ERROR" : "SAVED");
        return;
    }
#endif
    ui_message("SAVED (RAM)");
}

static void project_load(uint32_t slot)
{
    project_t *p = &proj_slot[slot & 3u];
    uint32_t i, k;
#if FELUCCA_FLASH
    if (flash_ok && !proj_ok(p))
        proj_fetch(slot);
#endif
    if (!proj_ok(p)) {
        ui_message("EMPTY SLOT");
        return;
    }
    transport_req = 2;
    fm1_irq_off();                                      /* the audio ISR must not see half a project */
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_LOAD && i != G_SAVE)
            song.g[i] = (int16_t)clamp(p->g[i], GP[i].min, GP[i].max);
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p->t[k];
        uint32_t m = (uint32_t)clamp(s->p[P_MODEL], 0, NMODELS - 1);
        drum_cut(t);
        for (i = 0; i < P_COUNT; i++) {                 /* every value back inside its range */
            const param_desc_t *d = i >= P_E0 && i <= P_E7 ? &DMODELS[m].edit[i - P_E0] : &TP[i];
            t->p[i] = (int16_t)clamp(s->p[i], d->min, d->max);
        }
        t->p[P_MODEL] = (int16_t)m;
        t->model = (uint8_t)m;
        for (i = 0; i < NSTEP; i++) {
            t->step[i].on = s->step[i].on ? 1u : 0u;
            t->step[i].acc = s->step[i].acc ? 1u : 0u;
        }
    }
    song.sel = (uint8_t)(p->sel < NTRK ? p->sel : 0u);
    fm1_irq_on();
    ui.force = 1;
    ui_message("LOADED");
}

/* settings + learned panel table: one flash object (format unchanged from Felucca, so a calibrated
 * panel survives the change of firmware). The flash copy wins at boot. */
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
} persist_t;
#define PERSIST_MAGIC 0x50455232u                  /* "PER2" */
#if FELUCCA_FLASH
static persist_t persist_saved;
#endif

static void persist_boot(void)                    /* before settings_init / panel_init */
{
#if FELUCCA_FLASH
    persist_t p;
    uint32_t f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();
    {
        uint32_t k;
        for (k = 0; k < SMP_USER_SLOTS; k++)
            smp_user_scan(k);
    }
    if (st_load(OBJ_SETTINGS, &p, sizeof p) == (int)sizeof p && p.magic == PERSIST_MAGIC) {
        settings.magic = SETTINGS_MAGIC;
        settings.palette = p.palette;
        settings.lowcut = p.lowcut;
        settings.zoom = p.zoom;
        if (p.panel.magic == PANEL_MAGIC)
            panel = p.panel;
        persist_saved = p;
    }
    {
        uint32_t i;
        for (i = 0; i < 4u; i++)
            if (!proj_ok(&proj_slot[i]))
                proj_fetch(i);
    }
#endif
}

static int project_used(uint32_t slot) { return proj_ok(&proj_slot[slot & 3u]); }

static void settings_save(void)
{
#if FELUCCA_FLASH
    persist_t p;
    if (!flash_ok)
        return;
    memset(&p, 0, sizeof p);
    p.magic = PERSIST_MAGIC;
    p.palette = settings.palette;
    p.lowcut = settings.lowcut;
    p.zoom = settings.zoom;
    p.panel = panel;
    if (!memcmp(&p, &persist_saved, sizeof p))
        return;
    if (st_save(OBJ_SETTINGS, &p, sizeof p) == 0)
        persist_saved = p;
#endif
}

#if FELUCCA_FLASH
_Static_assert(sizeof(project_t) <= ST_PAYLOAD_MAX, "project does not fit one flash sector");
#endif
```
In `tests/run_drum_tests.sh` drop `-DUI_NO_PROJECT` from the `ui_test` line (the real `project.c` compiles with `FELUCCA_FLASH 0`; its `settings_save` is then an empty function).

- [ ] **Step 4: Run the tests**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: all `ok`.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/project.c tests/ui_test.c tests/run_drum_tests.sh
git commit -m "projects: drum format FDR1 (8 tracks, models, steps), clamped on load; settings kept

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Firmware integration and the real build

**Files:**
- Modify: `firmware/src/felucca.c`, `firmware/src/audio.c`, `firmware/src/console.c`, `firmware/src/main.c` (allowed region only)
- Delete: `firmware/src/upreset.c`, `firmware/src/editor.c`, `tests/upreset_test.c`

**Interfaces:**
- Consumes: everything above.
- Produces: `./build.sh` builds `build/felucca.bin` + `build/loader/ota.bin`, all `ok` checks, `check_untouched: ok`.

- [ ] **Step 1: The failing check**

Run: `PYTHON=.venv/bin/python ./build.sh`
Expected: FAIL — `felucca.c` includes `engines.c` (removed).

- [ ] **Step 2: `firmware/src/felucca.c`**

Replace the block
```c
#include "core.h"
#include "engines.c"
#include "drums.c"
#include "params.c"
#include "voice.c"
#include "slicer.c"          /* per-track SLICER insert, used by fx.c */
#include "fx.c"
```
with
```c
#include "core.h"
#include "dsp.c"
#include "eng_sample.c"      /* ADPCM decoder + user sample slots */
#include "dmodels.c"         /* drum models */
#include "params.c"
#include "drum_core.c"       /* 8 drum tracks */
#include "slicer.c"          /* per-track SLICER insert, used by fx.c */
#include "fx.c"
```
Replace
```c
#include "panel.c"
#include "ui.c"
```
with
```c
#include "panel.c"
#include "pages.c"
#include "ui.c"
```
Delete the line `#include "upreset.c"          /* user presets (RAM mirror; flash with FELUCCA_FLASH) */`.
Replace
```c
#if FELUCCA_OTA
#include "editor.c"          /* web editor SysEx (needs the OTA SysEx plumbing) */
#endif
```
with
```c
#if FELUCCA_OTA
/* the web editor returns in M5; main.c still calls this. Safe as a no-op: ota_take() (ota_service)
 * frees every SysEx frame, so editor frames cannot block the update handshake. */
static void ed_service(void) {}
#endif
```
Change the header comment's first line to `/* FM-1 drum machine (Felucca fork): one compilation unit (the HAL is header-only). Order matters. */`.

- [ ] **Step 3: `firmware/src/audio.c` — shed a drum voice**

Replace the comment above `shed_req` and the whole `shed_voice` function with:
```c
/* overload: a half that took > 85 % of its time sheds one voice before the next one: the oldest
 * sounding drum voice of any track fades out (drum_shed, the declick tail). */
static volatile uint8_t shed_req;
static uint32_t shed_count;

static void shed_voice(void)
{
    drum_shed();
    shed_count++;
}
```
and in the file's top comment replace `each synth part` with `each drum track` and `-> drums ->` with `->`.

- [ ] **Step 4: `firmware/src/console.c` status**

In `con_status` delete the `const engine_t *e = ...` line, the `voices_given_up` line, and the six lines printing `engine ` / `preset `; add after the `track` line:
```c
    con_puts("model ");
    con_puts(N_MODEL[(uint32_t)TSEL->p[P_MODEL] % NMODELS]);
    con_puts("\r\n");
```
Change `con_puts("felucca ");` to `con_puts("fm1-drums ");`.

- [ ] **Step 5: `firmware/src/main.c` — allowed region only**

Replace the body of `felucca_init` (and its comment line) with:
```c
/* power-on: the drum kit (drum_core.c KIT_DEF), empty patterns */
static void felucca_init(void)
{
    drum_tracks_init();
    ui.home = 1;
    ui.force = 1;
}
```
and the two boot-title lines with:
```c
    draw_text_box(0, 100, 240, &FONT_L, "FM-1 DRUMS", C_HI, 1);
    draw_text_box(0, 130, 240, &FONT_S, "DRUM MACHINE (UNTESTED)", C_GRAY, 1);
```

- [ ] **Step 6: Delete the user-preset and editor sources**

```bash
git rm -q firmware/src/upreset.c firmware/src/editor.c tests/upreset_test.c
```

- [ ] **Step 7: Build and guard**

```bash
PYTHON=.venv/bin/python ./build.sh 2>&1 | tee build/m1b-build.log | tail -8
python3 tools/check_untouched.py
```
Expected: `ok` lines for `.ram_text`, image/RAM/pool and register access; `app ... B (N B free)`; `package  skipped`; `check_untouched: ok`. A compile error names a remaining synth reference — fix it in the non-frozen file it is in (ledger it). Pool headroom must stay ≥ 8 KiB (the build checks).

- [ ] **Step 8: Run the host suite**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: `ALL DRUM HOST TESTS PASSED`.

- [ ] **Step 9: Commit**

```bash
git add -A firmware/src tests/upreset_test.c
git commit -m "firmware: drum build (includes, overload shedding, console, boot titles); editor stub, no user presets

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Whole host suite and target cost budget

**Files:**
- Rewrite: `tests/run_tests.sh`
- Modify: `tests/target_budget.py`, `tests/target_budget.txt` (regenerated)
- Delete: `tests/cpu_baseline.txt` (its reference value lives in `tests/drum_cost_ref.txt`)

**Interfaces:**
- Produces: `sh tests/run_tests.sh` — the full host suite of the drum firmware, after `DRUM_PACKAGE=1 ./build.sh`.

- [ ] **Step 1: Rewrite `tests/run_tests.sh`**

```sh
#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Drum machine fork: 2026 Eugene Vech
# Host tests of the drum firmware (no hardware). Run from anywhere after a packaged build:
#   DRUM_PACKAGE=1 ./build.sh && tests/run_tests.sh
# The package is only for the update-protocol tests; it is never installed (M1).
set -e
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
cd "$(dirname "$0")/.."
OUT=build/host
PKG=build/felucca-UNTESTED.fwsc
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"
fail=0
run() { echo "== $1"; shift; "$@" || fail=1; }

[ -f "$PKG" ] || { echo "run DRUM_PACKAGE=1 ./build.sh first"; exit 1; }

$CC -o "$OUT/storage_test" tests/storage_test.c
run "flash storage (A/B, torn writes)" "$OUT/storage_test"
$CC -o "$OUT/midi_uart_test" tests/midi_uart_test.c
run "TRS MIDI parser" "$OUT/midi_uart_test"
$CC -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry" "$OUT/ota_test" "$PKG"
head -c 200000 build/felucca.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
$CC -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader: other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" "$PKG"
run "drum suite (models, mix, sequencer, UI, guards)" sh tests/run_drum_tests.sh
run "target cost of the render loops" python3 tests/target_budget.py build/felucca.dis tests/target_budget.txt
run "installer CLI (fm1_install.py) against a simulated FM-1" python3 tests/install_test.py
if command -v node >/dev/null 2>&1; then
    run "web pages: editor protocol, samples, packages, update protocol" node web/test_web.mjs
else
    echo "== skip web tests (no node)"
fi
[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED" || { echo "HOST TESTS FAILED"; exit 1; }
```

- [ ] **Step 2: Drum render functions in `tests/target_budget.py`**

Replace the `FUNCS = [...]` list with:
```python
FUNCS = ["body_render", "snare_render", "clap_render", "hat_render", "cymb_render", "cowb_render",
         "rim_render", "smp_render", "metal_make", "track_render", "slicer_track", "fm1_alnk0_irq"]
```

- [ ] **Step 3: Record the budget and run the suite**

```bash
rm -f build/felucca.fwsc tests/cpu_baseline.txt          # stale stock package; old synth CPU baseline
DRUM_PACKAGE=1 PYTHON=.venv/bin/python ./build.sh | tail -3
BUDGET_UPDATE=1 python3 tests/target_budget.py build/felucca.dis tests/target_budget.txt
PATH="$PWD/.venv/bin:$PATH" PYTHON=.venv/bin/python sh tests/run_tests.sh 2>&1 | tee build/m1b-tests.log | grep -E "^==|FAIL|PASSED"
```
Expected: the package line names `build/felucca-UNTESTED.fwsc`; every section runs; `ALL HOST TESTS PASSED`. If a function name in `FUNCS` is inlined away (absent from `felucca.dis`), drop it from the list and ledger it. `ota_test`/`ldr_test` failures mean the update path is affected — STOP and report; never edit a frozen file.

- [ ] **Step 4: Commit**

```bash
git add tests/run_tests.sh tests/target_budget.py tests/target_budget.txt tests/cpu_baseline.txt
git commit -m "tests: whole host suite for the drum firmware (update protocol, drum suite, target cost)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Screens for the user

- [ ] **Step 1:** Run `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh` and open `build/ui_shots/*.png`; give the user the folder path and one line per screen (HOME, SOUND, SOUND 2, TRACK, MIDI, LAYER, FX, SLICER, STEP, PATTERN, GLOBAL, PROJECT, TRACKS). Report the real build's image size, free bytes, RAM and pool from `build/m1b-build.log`. UI changes the user asks for are made in `ui_draw.c` / `pages.c` with the screenshot test still passing.
