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

/* GHOST: the pump kit with the kick heard, then muted with KEEP (ghost), muted with MUTE (no pump), HIDE unmuted */
static void ghost_kit(int ghost, int muted)
{
    pump_kit(1, 45, 0, 26);
    song.g[G_CGHOST] = (int16_t)ghost;
    trk[0].p[P_MUTE] = (int16_t)muted;
}

/* LFO demos (4 bars at 120 BPM each): a hat pattern with LFO 1 on TONE, a cymbal with LFO 1 on LVL, a kick with
 * LFO 1 on PAN: every waveform in turn, one bar each, SYNC 1/4 .. */
static void lfo_kit(uint32_t dest)
{
    uint32_t k;
    host_init();
    drum_set_model(&trk[0], dest == 9u ? DM_CYMB : dest == 10u ? DM_K909 : DM_HATC);
    for (k = 0; k < 16u; k += dest == 3u ? 1u : 4u)
        trk[0].step[k].on = 1;
    if (dest == 9u)
        trk[0].p[P_E1] = 120;
    trk[0].p[P_LFO1 + LF_DEST] = (int16_t)dest;
    trk[0].p[P_LFO1 + LF_DEPTH] = 56;
    trk[0].p[P_LFO1 + LF_RATE] = 52;                 /* 1/4 */
    trk[0].p[P_LFO1 + LF_MORPH] = 40;
}

static void lfo_wave_bar(uint32_t b) { trk[0].p[P_LFO1 + LF_WAVE] = (int16_t)(b % LW_COUNT); }

/* RESON demos (120 BPM): a kick / snare / hat kit with RESON on the snare and hats */
static void reson_kit(uint32_t model)
{
    uint32_t k;
    host_init();
    drum_set_model(&trk[0], DM_K909);
    drum_set_model(&trk[1], DM_S909);
    drum_set_model(&trk[2], DM_HATC);
    for (k = 0; k < 16u; k += 4u)
        trk[0].step[k].on = 1;
    trk[1].step[4].on = trk[1].step[12].on = 1;
    for (k = 2; k < 16u; k += 4u)
        trk[2].step[k].on = 1;
    for (k = 1; k < 3u; k++) {
        trk[k].p[P_RMODEL] = (int16_t)model;
        trk[k].p[P_RDECAY] = 96;
        trk[k].p[P_RMIX] = 90;
    }
    trk[2].p[P_RTUNE] = 72;
}
static void reson_model_bar(uint32_t b) { trk[1].p[P_RMODEL] = trk[2].p[P_RMODEL] = (int16_t)(RS_STRNG + b % 3u); }
static void reson_chord_bar(uint32_t b) { trk[1].p[P_RSTRCT] = (int16_t)((b % RS_NCHORD) * 128u / RS_NCHORD + 3u); }
static void reson_dist_bar(uint32_t b) { trk[1].p[P_DIST] = trk[2].p[P_DIST] = (int16_t)(b & 1u ? 110 : 0); }
static void reson_none_bar(uint32_t b) { (void)b; }

/* sound pack demos (120 BPM, whole mix). sp_speaker_eq.wav: the 808 kit, 2 bars each FLAT, LOWCUT, BASS+;
 * sp_reverb.wav: a snare on 2 and 4 into the reverb, 2 bars each ROOM, SPRING at SIZE 0 / 64 / 127 (DAMP 60), SPRING
 * SIZE 127 DAMP 0 and DAMP 127; sp_slow_div.wav: a 1/16 kick with a hat on 2BAR (4 bars) then 4BAR (8 bars), the
 * snare's delay at TIME 1/2 */
static void sp_kit(void)
{
    static const char *const PAT[3] = {"X...x...x...x...", "....X.......X...", "x.x.x.x.x.x.x.x."};
    static const uint32_t MODEL[3] = {DM_K808, DM_S808, DM_HATC};
    uint32_t i, k;
    host_init();
    for (i = 0; i < 3u; i++) {
        drum_set_model(&trk[i], MODEL[i]);
        for (k = 0; k < 16u; k++) {
            trk[i].step[k].on = PAT[i][k] != '.';
            trk[i].step[k].acc = PAT[i][k] == 'X';
        }
    }
}
static void sp_eq_bar(uint32_t b) { fx_lowcut = (uint8_t)(b / 2u < 3u ? b / 2u : 2u); }
static void sp_rev_bar(uint32_t b)
{
    static const int16_t TYPE[6] = {0, 1, 1, 1, 1, 1}, SIZE[6] = {90, 0, 64, 127, 127, 127}, DAMP[6] = {60, 60, 60, 60, 0, 127};
    uint32_t s = b / 2u < 6u ? b / 2u : 5u;
    song.g[G_RTYPE] = TYPE[s];
    song.g[G_RSIZE] = SIZE[s];
    song.g[G_RDAMP] = DAMP[s];
}
static void sp_div_bar(uint32_t b) { trk[2].p[P_SDIV] = b < 4u ? 8 : 9; }
static void write_sound_pack(const char *dir)
{
    sp_kit();
    write_demo(dir, "sp_speaker_eq.wav", 6, sp_eq_bar);
    fx_lowcut = 0;
    sp_kit();
    drum_set_model(&trk[0], DM_K808);
    memset(trk[0].step, 0, sizeof trk[0].step);      /* the snare alone */
    memset(trk[2].step, 0, sizeof trk[2].step);
    trk[1].p[P_REV] = 110;
    write_demo(dir, "sp_reverb.wav", 12, sp_rev_bar);
    song.g[G_RTYPE] = 0;
    sp_kit();
    trk[1].p[P_DLY] = 90;
    song.g[G_DTIME] = 6;                             /* 1/2 */
    for (uint32_t k = 0; k < 16u; k++)
        trk[2].step[k].on = 1;
    write_demo(dir, "sp_slow_div.wav", 12, sp_div_bar);
}

/* PERFORM demos (120 BPM, the sound pack's 808 kit with a reverb / delay send): 4 bars each, the effect (a key or a
 * knob) held over bars 2 and 3: pf_<name>.wav */
static const struct { const char *name; uint32_t held; int8_t k[4]; } PF_DEMO[] = {
    {"pf_repeat_8.wav", 1u << PF_R8, {0}}, {"pf_repeat_16.wav", 1u << PF_R16, {0}},
    {"pf_repeat_32.wav", 1u << PF_R32, {0}}, {"pf_reverse.wav", 1u << PF_REV, {0}},
    {"pf_tape_stop.wav", 1u << PF_TAPE, {0}}, {"pf_lpf.wav", 1u << PF_LPF, {0}}, {"pf_hpf.wav", 1u << PF_HPF, {0}},
    {"pf_freeze.wav", 1u << PF_FRZ, {0}}, {"pf_oct_up.wav", 1u << PF_OUP, {0}}, {"pf_oct_dn.wav", 1u << PF_ODN, {0}},
    {"pf_oct_up_shimmer.wav", 1u << PF_OUP, {0, 0, 0, 60}}, {"pf_mute_kick.wav", 1u << PF_M1, {0}},
    {"pf_crush.wav", 0, {0, 80, 0, 0}}, {"pf_throw.wav", 0, {0, 0, 100, 0}}, {"pf_filter_knob.wav", 0, {-70, 0, 0, 0}},
};
static uint32_t pf_cur;
static void pf_bar(uint32_t b)
{
    uint32_t on = b == 1u || b == 2u, k;
    perf_held = on ? PF_DEMO[pf_cur].held : 0u;
    for (k = 0; k < 4u; k++)
        perf_k[k] = on ? PF_DEMO[pf_cur].k[k] : 0;
}
static void write_perform(const char *dir)
{
    for (pf_cur = 0; pf_cur < sizeof PF_DEMO / sizeof PF_DEMO[0]; pf_cur++) {
        sp_kit();
        trk[1].p[P_REV] = 60;
        trk[2].p[P_DLY] = 50;
        write_demo(dir, PF_DEMO[pf_cur].name, 4, pf_bar);
        perf_held = 0;
    }
}

/* MOTION demo (120 BPM): the 808 kit, the kick's DECAY recorded as a sweep over a bar (step k: 10 + 7 k); bar 1
 * PLAY OFF (the patch), bars 2..4 PLAY ON: motion_decay.wav */
static void motion_bar(uint32_t b) { mo.s.on = b ? 1u : 0u; }
static void write_motion(const char *dir)
{
    uint32_t k;
    sp_kit();
    for (k = 0; k < 16u; k++)
        trk[0].step[k].on = (k & 1u) == 0u;           /* the kick on every 1/8: the sweep is heard */
    for (k = 0; k < 16u; k++)
        motion_add(0, k, P_E1, (int32_t)(10u + 7u * k));
    write_demo(dir, "motion_decay.wav", 4, motion_bar);
    memset(&mo, 0, sizeof mo);
}

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
    ghost_kit(CG_KEEP, 0);
    write_demo(dir, "ghost_keep_heard.wav", 4, prob_bar);
    ghost_kit(CG_KEEP, 1);
    write_demo(dir, "ghost_keep_muted.wav", 4, prob_bar);
    ghost_kit(CG_MUTE, 1);
    write_demo(dir, "ghost_mute_muted.wav", 4, prob_bar);
    ghost_kit(CG_HIDE, 0);
    write_demo(dir, "ghost_hide.wav", 4, prob_bar);
    lfo_kit(3);
    write_demo(dir, "lfo_tone.wav", LW_COUNT, lfo_wave_bar);
    lfo_kit(9);
    write_demo(dir, "lfo_level.wav", LW_COUNT, lfo_wave_bar);
    lfo_kit(10);
    write_demo(dir, "lfo_pan.wav", LW_COUNT, lfo_wave_bar);
    reson_kit(RS_STRNG);
    write_demo(dir, "reson_models.wav", 3, reson_model_bar);   /* STRNG, PIPE, CHORD, one bar each */
    reson_kit(RS_CHORD);
    write_demo(dir, "reson_chords.wav", 8, reson_chord_bar);   /* a chord type per bar */
    reson_kit(RS_STRNG);
    trk[1].p[P_LFO1 + LF_DEST] = 11;                           /* R.TUN: a slow triangle sweep */
    trk[1].p[P_LFO1 + LF_DEPTH] = 48;
    trk[1].p[P_LFO1 + LF_WAVE] = LW_TRI;
    trk[1].p[P_LFO1 + LF_RATE] = 16;                           /* SYNC 2 bars (16 * 17 / 128 = 2) */
    write_demo(dir, "reson_sweep.wav", 4, reson_none_bar);
    reson_kit(RS_PIPE);
    write_demo(dir, "reson_dist.wav", 4, reson_dist_bar);      /* RESON into DIST, every other bar */
    write_sound_pack(dir);
    write_perform(dir);
    write_motion(dir);
    return 0;
}
