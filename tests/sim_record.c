/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The landing page's reel (docs/superpowers/specs/2026-10-07-websim-hero-design.md): the simulator core, headless,
 * plays demo 1 while a scripted hand visits the pages and turns knobs; 30 s at 15 fps of the screen, the LEDs and the
 * knob turns, written as "FM1R" (screen changes only), plus the last screen's hash for the test and the clips (each
 * page's stretch, named: the landing page plays them per feature) as JSON.
 *   sim_record OUT.bin OUT.hash OUT.json */
#include "sim_core.c"
#include <stdio.h>

#define FPS 15u
#define SECONDS 34u
#define FRAME_SAMPLES (FS / FPS)                          /* 2940 */

typedef struct { float t; char what; uint8_t id; int8_t val; const char *clip; } act_t;   /* b = tap button,
                                                          * e = turn encoder, m = a clip starts here */
static const act_t SCRIPT[] = {                           /* HOME's scope first; every page a few seconds */
    {0.0f, 'm', 0, 0, "home"},
    {3.6f, 'b', B_SEQ, 0}, {3.6f, 'm', 0, 0, "seq"},     /* the step grid of track 1, the playhead running */
    {6.4f, 'e', EN_ALGO, 1},                              /* track 2's steps */
    {8.8f, 'b', B_EDIT, 0}, {8.8f, 'm', 0, 0, "sound"},  /* track 2's sound: a knob swept up and back */
    {9.8f, 'e', EN_K2, 2}, {10.1f, 'e', EN_K2, 2}, {10.4f, 'e', EN_K2, 2}, {10.7f, 'e', EN_K2, 2},
    {11.2f, 'e', EN_K2, -2}, {11.5f, 'e', EN_K2, -2}, {11.8f, 'e', EN_K2, -2}, {12.1f, 'e', EN_K2, -2},
    {12.8f, 'b', B_FX, 0}, {12.8f, 'm', 0, 0, "fx"},     /* FX, PRESETS through to REVERB, SIZE swept */
    {13.4f, 'e', EN_PRESET, 1}, {13.8f, 'e', EN_PRESET, 1}, {14.2f, 'e', EN_PRESET, 1}, {14.6f, 'e', EN_PRESET, 1},
    {15.0f, 'e', EN_PRESET, 1},
    {15.8f, 'e', EN_K2, 3}, {16.2f, 'e', EN_K2, 3}, {16.8f, 'e', EN_K2, -3}, {17.2f, 'e', EN_K2, -3},
    {18.0f, 'b', B_ARP, 0}, {18.0f, 'm', 0, 0, "grids"}, /* Grids: X and Y wander */
    {18.8f, 'e', EN_K2, 4}, {19.3f, 'e', EN_K3, 4}, {19.8f, 'e', EN_K2, 4}, {20.3f, 'e', EN_K3, -4},
    {20.8f, 'e', EN_K2, -4}, {21.3f, 'e', EN_K2, -4},
    {22.0f, 'e', EN_ALGO, -1},
    {22.4f, 'b', B_HOME, 0},                              /* HOME, PRESETS to COMP, THR down and back */
    {23.2f, 'e', EN_PRESET, 1}, {23.2f, 'm', 0, 0, "comp"},
    {24.0f, 'e', EN_K2, -3}, {24.5f, 'e', EN_K2, -3}, {25.0f, 'e', EN_K2, 3}, {25.4f, 'e', EN_K2, 3},
    {26.0f, 'b', B_REC, 0}, {26.0f, 'm', 0, 0, "tracks"}, /* TRACKS: the mixer's meters */
    {29.2f, 'b', B_LFO, 0}, {29.2f, 'm', 0, 0, "lfo"},    /* LFO 1 of track 1, its knobs turned */
    {29.8f, 'e', EN_K2, 3}, {30.4f, 'e', EN_K3, 2}, {31.2f, 'e', EN_K2, -3}, {31.8f, 'e', EN_K3, -2},
    {32.4f, 'b', B_HOME, 0}, {32.4f, 'm', 0, 0, "end"},   /* back to HOME for the loop */
};
#define NACT (sizeof SCRIPT / sizeof SCRIPT[0])

static uint16_t prev[240 * 240];

static void put16(FILE *f, uint32_t v) { uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)}; fwrite(b, 1, 2, f); }
static void put32(FILE *f, uint32_t v) { put16(f, v & 0xFFFFu); put16(f, v >> 16); }

static void write_frame(FILE *f, const int8_t enc[8], int full)
{
    const uint16_t *cur = sim_fb();
    uint32_t i, runs = 0, starts[2048], lens[2048];
    for (i = 0; i < 240u * 240u && !full;) {              /* changed pixels; gaps under 4 pixels joined */
        uint32_t s, last;
        if (cur[i] == prev[i]) {
            i++;
            continue;
        }
        s = last = i;
        while (i < 240u * 240u && (cur[i] != prev[i] || i - last < 4u)) {
            if (cur[i] != prev[i])
                last = i;
            i++;
        }
        if (runs == 2048u) {                              /* too scattered: one run over the rest */
            lens[runs - 1u] = 240u * 240u - starts[runs - 1u];
            break;
        }
        starts[runs] = s;
        lens[runs++] = last - s + 1u;
    }
    if (full) {
        starts[0] = 0;
        lens[0] = 240u * 240u;
        runs = 1;
    }
    put32(f, sim_leds());
    put32(f, sim_key_leds());
    fwrite(enc, 1, 8, f);
    put32(f, runs);
    for (i = 0; i < runs; i++) {
        uint32_t k;
        put16(f, starts[i]);
        put16(f, lens[i]);
        for (k = 0; k < lens[i]; k++)
            put16(f, cur[starts[i] + k]);
    }
    memcpy(prev, cur, sizeof prev);
}

static void tap_btn(uint32_t b)                          /* held 0.12 s, as a finger: HOME / REC read the level */
{
    uint32_t left = FS * 12u / 100u;
    sim_btn(b, 1);
    while (left) {
        uint32_t n = left < SIM_MAX_FRAMES ? left : SIM_MAX_FRAMES;
        sim_render(n);
        left -= n;
    }
    sim_btn(b, 0);
}

int main(int argc, char **argv)
{
    FILE *f, *h;
    uint32_t frame, next = 0, i;
    const uint8_t *p;
    uint32_t hash = 2166136261u;
    if (argc != 4) {
        fprintf(stderr, "usage: sim_record OUT.bin OUT.hash OUT.json\n");
        return 2;
    }
    sim_init(1);                                          /* the first visit: demo 1 loaded */
    tap_btn(B_PLAY);
    for (i = 0; i < FS * 2u / SIM_MAX_FRAMES; i++)        /* 2 s in: the pattern runs, LOADED has gone */
        sim_render(SIM_MAX_FRAMES);
    f = fopen(argv[1], "wb");
    if (!f)
        return 1;
    fwrite("FM1R", 1, 4, f);
    put16(f, 1);
    put16(f, FPS);
    put32(f, FPS * SECONDS);
    for (frame = 0; frame < FPS * SECONDS; frame++) {
        int8_t enc[8] = {0};
        uint32_t left = FRAME_SAMPLES;
        while (next < NACT && SCRIPT[next].t * FPS <= (float)frame) {
            const act_t *a = &SCRIPT[next++];
            if (a->what == 'm')
                continue;
            if (a->what == 'b')
                tap_btn(a->id);
            else {
                sim_enc(a->id, a->val);
                enc[a->id] = (int8_t)(enc[a->id] + a->val);
            }
        }
        while (left) {
            uint32_t n = left < SIM_MAX_FRAMES ? left : SIM_MAX_FRAMES;
            sim_render(n);
            left -= n;
        }
        write_frame(f, enc, frame == 0u);
    }
    fclose(f);
    for (p = (const uint8_t *)sim_fb(), i = 0; i < 240u * 240u * 2u; i++)
        hash = (hash ^ p[i]) * 16777619u;
    h = fopen(argv[2], "w");
    if (!h)
        return 1;
    fprintf(h, "%u\n", hash);
    fclose(h);
    h = fopen(argv[3], "w");                              /* the clips: [{name, from, to}], frames */
    if (!h)
        return 1;
    fprintf(h, "[");
    {
        uint32_t k, first = 1;
        for (i = 0; i < NACT; i++) {
            uint32_t from, to = FPS * SECONDS;
            if (SCRIPT[i].what != 'm')
                continue;
            from = (uint32_t)(SCRIPT[i].t * FPS + 0.999f);
            for (k = i + 1; k < NACT; k++)
                if (SCRIPT[k].what == 'm') {
                    to = (uint32_t)(SCRIPT[k].t * FPS + 0.999f);
                    break;
                }
            fprintf(h, "%s\n  {\"name\": \"%s\", \"from\": %u, \"to\": %u}", first ? "" : ",", SCRIPT[i].clip, from, to);
            first = 0;
        }
    }
    fprintf(h, "\n]\n");
    fclose(h);
    printf("sim_record: %u frames at %u fps -> %s\n", FPS * SECONDS, FPS, argv[1]);
    return 0;
}
