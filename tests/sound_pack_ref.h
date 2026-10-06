/* Upstream Felucca 1.0.2 (db70550) firmware/src/fx.c, the reference the sound pack's port is checked against
 * (tests/sound_pack_test.c). The arithmetic is upstream's text; only the state's names (ref_*) differ, and
 * rev_spring (below) takes SIZE / DAMP as arguments and has its own buffers, so the reference runs beside the port.
 * Drum machine fork: 2026 DEADACTIVE */
static int32_t ref_lp1, ref_lp2, ref_lp3, ref_lp4, ref_env, ref_h1, ref_h2, ref_hl;
static int32_t ref_spk_bass(int32_t m)
{
    int32_t a, t, u;
    ref_lp1 += ((m - ref_lp1) * 692) >> 15;
    ref_lp2 += ((ref_lp1 - ref_lp2) * 692) >> 15;
    ref_lp3 += ((ref_lp2 - ref_lp3) * 692) >> 15;
    ref_lp4 += ((ref_lp3 - ref_lp4) * 692) >> 15;
    a = ref_lp4 < 0 ? -ref_lp4 : ref_lp4;
    if (a > ref_env)
        ref_env += (a - ref_env) >> 2;
    else if (ref_env > 0)
        ref_env -= (ref_env >> 11) + 1;
    t = clamp(ref_lp4 * 8, -ref_env, ref_env);
    ref_h1 += (t - ref_h1) >> 5;
    u = t - ref_h1;
    ref_h2 += (u - ref_h2) >> 5;
    u -= ref_h2;
    ref_hl += (u - ref_hl) >> 3;
    return ref_hl * 3;
}
static void ref_bass_reset(void) { ref_lp1 = ref_lp2 = ref_lp3 = ref_lp4 = ref_env = ref_h1 = ref_h2 = ref_hl = 0; }
#define REF_SP_LEN 4096u
#define REF_SP_MASK (REF_SP_LEN - 1u)
#define REF_SP_N 10u
#define REF_SP_A 2867
static int16_t ref_ln[REF_SP_LEN];
static int32_t ref_ap[4u * (REF_SP_N + 1u)];
static struct { uint16_t sp_w; int32_t sp_lp, sp_hp, sp_he, sp_size; uint32_t sp_ph; } ref_fx;
static void ref_spring_reset(void)
{
    memset(ref_ln, 0, sizeof ref_ln);
    memset(ref_ap, 0, sizeof ref_ap);
    memset(&ref_fx, 0, sizeof ref_fx);
}
static void ref_rev_spring(const int32_t *rev_in, int32_t *out, uint32_t n, int32_t rsize, int32_t rdamp)
{
    uint32_t i, k, s = (uint32_t)rsize;
    int32_t g = 19661 + (int32_t)s * 85;
    int32_t kl = 26000 - rdamp * 160;
    int32_t len = (int32_t)(1323u + ((s * 1323u) >> 7)) << 8, L, L2, L3, f, w;
    int16_t *ln = ref_ln;
    int32_t *ap = ref_ap;
    if (!ref_fx.sp_size)
        ref_fx.sp_size = len;
    ref_fx.sp_size += clamp(len - ref_fx.sp_size, -256, 256);
    L = ref_fx.sp_size >> 8;
    ref_fx.sp_ph += 2u * LFO_INC[24];
    w = (ref_fx.sp_size >> 1) + ((osc_sine(ref_fx.sp_ph) * 3) >> 8);
    L2 = w >> 8;
    f = w & 255;
    L3 = (L * 3) >> 2;
    for (i = 0; i < n; i++) {
        uint32_t wp = ref_fx.sp_w, j = (wp & 3u) * (REF_SP_N + 1u);
        int32_t x = mulq15(rev_in[i], 2580), r = ln[(wp - (uint32_t)L) & REF_SP_MASK], p, o;
        int32_t t0 = ln[(wp - (uint32_t)L2) & REF_SP_MASK], t1 = ln[(wp - (uint32_t)L2 - 1u) & REF_SP_MASK];
        ref_fx.sp_lp += mulq15(r - ref_fx.sp_lp, kl);
        o = ref_fx.sp_lp * g;
        x += (o + ((o >> 31) & 32767)) >> 15;
        o = x - ref_fx.sp_hp + ref_fx.sp_he;
        ref_fx.sp_he = o & 63;
        ref_fx.sp_hp += o >> 6;
        x -= ref_fx.sp_hp;
        p = ap[j];
        ap[j] = x;
        for (k = 1; k <= REF_SP_N; k++) {
            int32_t v = (x - ap[j + k]) * REF_SP_A;
            o = ap[j + k];
            x = ((v + ((v >> 31) & 4095)) >> 12) + p;
            p = o;
            ap[j + k] = x;
        }
        ln[wp & REF_SP_MASK] = (int16_t)clamp(x, -32768, 32767);
        ref_fx.sp_w = (uint16_t)(wp + 1u);
        out[i] += (t0 + (((t1 - t0) * f) >> 8)) * 4 + ln[(wp - (uint32_t)L3) & REF_SP_MASK] * 2;
    }
}
