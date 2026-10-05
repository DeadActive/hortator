/* WAV renders for listening: one file per model, the kit demo, the demo kits (808, 909, Plaits, mixed) and the
 * M2 demos (Grids map / Euclidean, PROB / RATCH).
 * drumsim OUTDIR */
#include "drum_host.h"
#include <sys/stat.h>

static void wav_hdr(FILE *f, uint32_t frames)
{
    uint32_t v;
    uint16_t a = 1, ch = 2, ba = 4, bits = 16;
    uint32_t sr = FS, br = FS * 4;
    fwrite("RIFF", 1, 4, f); v = 36 + frames * 4; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    fwrite(&a, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); v = frames * 4; fwrite(&v, 4, 1, f);
}

static void wav_put(FILE *f, int32_t l, int32_t r)
{
    int16_t s[2] = {(int16_t)clamp(l, -32768, 32767), (int16_t)clamp(r, -32768, 32767)};
    fwrite(s, 2, 2, f);
}

#define GAP (FS * 6 / 10 / CTL * CTL)
static int32_t L[GAP], R[GAP];

static void hit_and_write(FILE *f, track_t *t, uint32_t vel, uint32_t *frames)
{
    uint32_t i;
    drum_hit(t, vel);
    render_mix(L, R, GAP);
    for (i = 0; i < GAP; i++)
        wav_put(f, L[i], R[i]);
    *frames += GAP;
}

/* a demo kit: 8 tracks (model, TUNE, DECAY or -1 = default), a 16-step pattern ('x' hit, 'X' accent) per track,
 * played 4 bars at 120 BPM through the whole mix (FX sends on the snare, clap and hats), written to DIR/NAME */
typedef struct {
    const char *name;
    uint32_t model[NTRK];
    int16_t tune[NTRK], decay[NTRK];
    const char *pat[NTRK];
} demo_kit_t;

static const demo_kit_t KITS[] = {
    {"kit_808.wav", {DM_K808, DM_S808, DM_C808, DM_HATC, DM_HATO, DM_TOM, DM_COWB, DM_CYMB},
     {0, 0, 0, 0, 0, 0, 0, 0}, {-1, -1, -1, -1, -1, -1, -1, -1},
     {"X..x..x...x..x..", "....X.......X...", "....x.......x..x", "x.x.x.x.x.x.x.x.", "..x...x...x...x.",
      "..........x.x.xX", ".x.....x...x....", "X..............."}},
    {"kit_909.wav", {DM_K909, DM_S909, DM_C909, DM_HATC, DM_HATO, DM_RIM, DM_CLAVE, DM_CONGA},
     {0, 0, 0, 0, 0, 0, 0, 0}, {-1, -1, -1, -1, -1, -1, -1, -1},
     {"X...x...x...x...", "....X.......X..x", "....x.......x...", "xxxxxxxxxxxxxxXx", "..x...x...x...x.",
      "...x.....x....x.", ".x....x...x.....", "......x.x.....x."}},
    {"kit_plaits.wav", {DM_KBOOM, DM_SSNAP, DM_SCRAK, DM_HMETL, DM_HNOIS, DM_KPUNC, DM_KBOOM, DM_SSNAP},
     {0, 0, 0, 0, 0, 0, 12, 12}, {-1, -1, -1, -1, -1, 40, 30, 10},
     {"X..x..x...x..x..", "....X.......X...", "....x.......x..x", "x.x.x.x.x.x.x.x.", "..x...x...x...x.",
      "X.......X.......", "..........x.x.x.", ".x.....x...x...."}},
    {"kit_mix.wav", {DM_KBOOM, DM_S808, DM_SCRAK, DM_HATC, DM_HNOIS, DM_K909, DM_TOM, DM_CYMB},
     {0, 0, 0, 0, 0, 0, 0, 0}, {-1, -1, -1, -1, -1, -1, -1, -1},
     {"X...x...x...x...", "....X.......X...", "....x.......x..x", "x.x.x.x.x.x.x.x.", "..x...x...x...x.",
      "..x.......x..x..", "..........x.x.xX", "X..............."}},
};

static void write_kit(const char *dir, const demo_kit_t *kit)
{
    char path[256];
    uint32_t total = 4 * 16 * (FS * 60 / 120 / 4) / CTL * CTL, frames, i, k;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, kit->name);
    f = fopen(path, "wb");
    wav_hdr(f, total);
    host_init();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        drum_set_model(t, kit->model[i]);
        t->p[P_E0] = kit->tune[i];
        if (kit->decay[i] >= 0)
            t->p[P_E1] = kit->decay[i];
        for (k = 0; k < 16; k++) {
            t->step[k].on = kit->pat[i][k] != '.';
            t->step[k].acc = kit->pat[i][k] == 'X';
        }
    }
    trk[1].p[P_REV] = 40;
    trk[2].p[P_REV] = 50;
    trk[3].p[P_DLY] = 25;
    transport_req = 1;
    for (frames = 0; frames < total; frames += GAP < total - frames ? GAP : total - frames) {
        uint32_t n = GAP < total - frames ? GAP : total - frames;
        render_mix(L, R, n);
        for (i = 0; i < n; i++)
            wav_put(f, L[i], R[i]);
    }
    fclose(f);
    printf("drumsim: %s\n", path);
}

/* M2 demos, 120 BPM through the whole mix. grids_map.wav: kick / snare / hats on Grids MAP at four map points
 * (2 bars each, chaos off) then the same with chaos 100; grids_euclid.wav: EUCLID 16/12/8 then 5/7/3 (4 bars each);
 * prob_ratch.wav: a kit with 50 % hats, 3-hit rolls, a 1/2 and 2/2 snare fill and a 1-SHOT crash (8 bars) */
static void write_demo(const char *dir, const char *name, uint32_t bars, void (*bar)(uint32_t b))
{
    char path[256];
    uint32_t bar_len = 16 * (FS * 60 / 120 / 4), total = bars * bar_len / CTL * CTL, frames, i, b;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    wav_hdr(f, total);
    transport_req = 1;
    for (b = 0, frames = 0; frames < total; b++) {
        uint32_t end = (b + 1) * bar_len / CTL * CTL < total ? (b + 1) * bar_len / CTL * CTL : total;
        bar(b);
        while (frames < end) {
            uint32_t n = GAP < end - frames ? GAP : end - frames;
            render_mix(L, R, n);
            for (i = 0; i < n; i++)
                wav_put(f, L[i], R[i]);
            frames += n;
        }
    }
    fclose(f);
    printf("drumsim: %s\n", path);
}

static void grids_kit(void)                          /* 808 kick / snare / closed hat on the three channels */
{
    host_init();
    drum_set_model(&trk[0], DM_K808);
    drum_set_model(&trk[1], DM_S808);
    drum_set_model(&trk[2], DM_HATC);
    trk[0].p[P_SRC] = 1;
    trk[1].p[P_SRC] = 2;
    trk[2].p[P_SRC] = 3;
    trk[1].p[P_REV] = 40;
    song.g[G_GFILL1] = 80;
    song.g[G_GFILL2] = 70;
    song.g[G_GFILL3] = 100;
}

static void map_bar(uint32_t b)
{
    static const int16_t XY[4][2] = {{64, 64}, {0, 0}, {127, 30}, {30, 127}};
    song.g[G_GX] = XY[(b / 2u) % 4u][0];
    song.g[G_GY] = XY[(b / 2u) % 4u][1];
    song.g[G_GCHAOS] = b < 8u ? 0 : 100;
}

static void euclid_bar(uint32_t b)
{
    static const int16_t LN[2][3] = {{16, 12, 8}, {5, 7, 3}};
    uint32_t k;
    song.g[G_GMODE] = 1;
    for (k = 0; k < 3u; k++) {
        song.g[G_GLEN1 + k] = LN[b / 4u % 2u][k];
        song.g[G_GFILL1 + k] = (int16_t)(b / 4u ? 60 : 40);
    }
}

static void prob_bar(uint32_t b) { (void)b; }

static void prob_kit(void)
{
    uint32_t k;
    host_init();
    drum_set_model(&trk[0], DM_K909);
    drum_set_model(&trk[1], DM_S909);
    drum_set_model(&trk[2], DM_HATC);
    drum_set_model(&trk[3], DM_CYMB);
    for (k = 0; k < 16u; k += 4u)
        trk[0].step[k].on = 1;
    trk[1].step[4].on = trk[1].step[12].on = 1;
    trk[1].step[14].on = 1;
    trk[1].step[14].cond = 22;                       /* 1/2: a fill every other bar */
    trk[1].step[15].on = 1;
    trk[1].step[15].cond = 23;                       /* 2/2 */
    trk[1].step[15].rat = 2;
    for (k = 0; k < 16u; k++) {
        trk[2].step[k].on = 1;
        trk[2].step[k].cond = (uint8_t)((k & 1u) ? cond_store(10) : 0u);   /* off-beats 50 % */
    }
    trk[2].step[7].rat = 2;
    trk[2].step[15].rat = 3;
    trk[3].step[0].on = trk[3].step[0].acc = 1;
    trk[3].step[0].cond = COND_1SHOT;
    trk[1].p[P_REV] = 40;
}

/* M3: the same kit dry and pumping: T1 K909 four on the floor keys the compressor; open hats, toms and the snare's
 * reverb are ducked (4 bars each); then a REL sweep (100 ms .. 1 s over 4 bars) and MKUP at the limiter end */
static void pump_kit(int src, int rat, int mkup, int thr)
{
    uint32_t k;
    host_init();
    drum_set_model(&trk[0], DM_K909);
    drum_set_model(&trk[1], DM_S909);
    drum_set_model(&trk[2], DM_HATO);
    drum_set_model(&trk[3], DM_TOM);
    for (k = 0; k < 16u; k += 4u)
        trk[0].step[k].on = 1;
    trk[1].step[4].on = trk[1].step[12].on = 1;
    for (k = 2; k < 16u; k += 4u)
        trk[2].step[k].on = 1;
    trk[2].p[P_E1] = 110;                            /* long open hats: the pump is heard */
    trk[3].step[7].on = trk[3].step[15].on = 1;
    trk[1].p[P_REV] = 70;
    song.g[G_CSRC] = (int16_t)src;
    song.g[G_CRAT] = (int16_t)rat;
    song.g[G_CMKUP] = (int16_t)mkup;
    song.g[G_CTHR] = (int16_t)thr;
    trk[1].p[P_DUCK] = trk[2].p[P_DUCK] = trk[3].p[P_DUCK] = 1;
}

static void rel_bar(uint32_t b) { song.g[G_CREL] = (int16_t)(5 + b * 12); }

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/drum_renders";
    char path[256];
    uint32_t mi, frames, i, k;
    mkdir(dir, 0755);
    for (mi = 0; mi < NMODELS; mi++) {
        static const uint8_t VEL[4] = {127, 96, 64, 32};
        FILE *f;
        snprintf(path, sizeof path, "%s/%02u_%s.wav", dir, mi, N_MODEL[mi]);
        f = fopen(path, "wb");
        wav_hdr(f, 0);
        frames = 0;
        host_init();
        drum_set_model(&trk[0], mi);
        for (i = 0; i < 4; i++)
            hit_and_write(f, &trk[0], VEL[i], &frames);
        for (k = 0; k < 4; k++) {                    /* TUNE, DECAY, TONE, CHAR at low then high */
            const param_desc_t *d = &DMODELS[mi].edit[k];
            int16_t keep = trk[0].p[P_E0 + k];
            trk[0].p[P_E0 + k] = k == 0 ? -12 : d->min;
            hit_and_write(f, &trk[0], 127, &frames);
            trk[0].p[P_E0 + k] = k == 0 ? 12 : d->max;
            hit_and_write(f, &trk[0], 127, &frames);
            trk[0].p[P_E0 + k] = keep;
        }
        fseek(f, 0, SEEK_SET);
        wav_hdr(f, frames);
        fclose(f);
        printf("drumsim: %s\n", path);
    }
    {
        static const char *const PAT[NTRK] = {
            "x...x...x...x..x", "....x.......x...", "....x.......x..x", "x.x.x.x.x.x.x.x.",
            "..x...x...x...x.", "......x....x....", ".x.....x..x.....", "x...............",
        };
        uint32_t total = 8 * 16 * (FS * 60 / 120 / 4) / CTL * CTL;
        FILE *f;
        snprintf(path, sizeof path, "%s/kit.wav", dir);
        f = fopen(path, "wb");
        wav_hdr(f, total);
        host_init();
        for (i = 0; i < NTRK; i++)
            for (k = 0; k < 16; k++)
                trk[i].step[k].on = PAT[i][k] == 'x';
        trk[1].p[P_REV] = 40;
        trk[2].p[P_REV] = 50;
        trk[3].p[P_DLY] = 25;
        transport_req = 1;
        for (frames = 0; frames < total; frames += GAP < total - frames ? GAP : total - frames) {
            uint32_t n = GAP < total - frames ? GAP : total - frames;
            render_mix(L, R, n);
            for (i = 0; i < n; i++)
                wav_put(f, L[i], R[i]);
        }
        fclose(f);
        printf("drumsim: %s\n", path);
    }
    for (i = 0; i < sizeof KITS / sizeof KITS[0]; i++)
        write_kit(dir, &KITS[i]);
    grids_kit();
    write_demo(dir, "grids_map.wav", 16, map_bar);
    grids_kit();
    write_demo(dir, "grids_euclid.wav", 8, euclid_bar);
    prob_kit();
    write_demo(dir, "prob_ratch.wav", 8, prob_bar);
    pump_kit(0, 45, 0, 26);
    write_demo(dir, "pump_dry.wav", 4, prob_bar);
    pump_kit(1, 45, 0, 26);
    write_demo(dir, "pump_on.wav", 4, prob_bar);
    pump_kit(1, 80, 0, 20);
    write_demo(dir, "pump_rel_sweep.wav", 4, rel_bar);
    pump_kit(1, 45, 127, 26);
    write_demo(dir, "pump_limit.wav", 4, prob_bar);
    return 0;
}
