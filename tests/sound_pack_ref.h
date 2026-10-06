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
