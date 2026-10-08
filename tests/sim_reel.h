/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The FM1R reel writer (web/sim/reel.js reads it), shared by the hero's recorder (sim_record.c) and the features'
 * (sim_feature_reel.c): a frame is the LEDs, the encoder turns and the screen's changed runs; frame 0 the whole screen.
 * Include after sim_core.c. */
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

