/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The SAMPLE model's envelope and loop (dm_sample.c): ATK SUS REL LOOP on its SOUND page 5/5. The defaults play as
 * before, bit for bit; ATK ramps from silence; SUS holds the note while its gate is open (a step: one step, longer
 * through its TIE steps; the keys and MIDI: while held), then REL; LOOP FWD repeats START..END with a short crossfade
 * over the seam, PING-PONG goes back and forth, both exactly as the decoder plays the sample (the reverse leg decoded
 * forward chunk by chunk); a loop longer than PING-PONG holds plays FWD. Stored in projects (xp2), user presets (the
 * FX bytes' spare top bit), LOCK / MOTION / MIDI LEARN. The firmware's real project.c / presets.c on a host flash. */
#define FELUCCA_FLASH 1
#include <stdint.h>
static uint8_t hflash[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
#define VAR_XIP(off) ((const void *)(hflash + (off)))
#include "ui_host.h"

static int fails;
static void check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}
#define SECS(s) ((uint32_t)((s) * FS) / CTL * CTL)
static int32_t wa[SECS(2)], wb[SECS(2)];
static int32_t peak(const int32_t *x, uint32_t n)
{
    int32_t p = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        if (abs(x[i]) > p)
            p = abs(x[i]);
    return p;
}
static void power_cycle(void)
{
    memset(&mo, 0, sizeof mo);
    memset(proj_slot, 0, sizeof proj_slot);
    memset(lk_slot, 0, sizeof lk_slot);
    memset(var_buf, 0, sizeof var_buf);
    memset(var_who, 0, sizeof var_who);
    var_play = var_dirty = var_lost = lk_lost = 0;
    persist_note = 0;
    host_init();
    memset(&ui, 0, sizeof ui);
    transport_req = 0;
    persist_boot();
}
static void fresh(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    power_cycle();
}
static void midi_in(uint32_t st, uint32_t ch, uint32_t d1, uint32_t d2)
{
    midi_in_q[mi_w % MQ] = 0x09u | (st | ch) << 8 | d1 << 16 | d2 << 24;
    mi_w++;
}

/* ---- a user slot holding pcm (one zone over all keys, 44.1 kHz), encoded as the page encodes (the decoder in step) */
static void enc_slot(uint32_t k, const int16_t *pcm, uint32_t n)
{
    uint8_t *s = hflash + 0xA0000u + k * 0x14000u;
    uint32_t i, len = (n + 1u) / 2u, w[5] = {0, n, 0, n - 1u, 65536u};
    int32_t p = 0, x = 0;
    int16_t r16 = 60 * 16;
    memset(s, 0xFF, 0x14000u);
    memset(s, 0, 512);
    memset(s + 512, 0, len);
    memcpy(s, "FSMP", 4);
    s[4] = 1;
    s[6] = 1;
    memcpy(s + 8, "ENV", 3);
    memcpy(s + 16, &len, 4);
    memcpy(s + 32, w, 20);
    memcpy(s + 52, &r16, 2);
    s[32 + 26] = 127;
    for (i = 0; i < n; i++) {
        int32_t st = IMA_STEP[x], d = pcm[i] - p, vd;
        uint32_t c = 0;
        if (d < 0) {
            c = 8;
            d = -d;
        }
        if (d >= st) {
            c |= 4;
            d -= st;
        }
        if (d >= st >> 1) {
            c |= 2;
            d -= st >> 1;
        }
        if (d >= st >> 2)
            c |= 1;
        vd = st >> 3;
        if (c & 4u)
            vd += st;
        if (c & 2u)
            vd += st >> 1;
        if (c & 1u)
            vd += st >> 2;
        p = clamp(p + ((c & 8u) ? -vd : vd), -32768, 32767);
        x = clamp(x + IMA_IDX[c & 7u], 0, 88);
        s[512u + (i >> 1)] |= (uint8_t)((i & 1u) ? c << 4 : c);
    }
    smp_user_scan(k);
    smp_slots_sync();
}
/* a user set of nz samples (pcm[i] of len[i], 44.1 kHz), the keys as the page lays them out (36 + i), each zone's data
 * from predictor 0 / index 0 at its own byte; seek: the page's START table after the zones (the header's rsv2: "SEEK",
 * its offset): each zone's decoder state (n * s) >> 7 nibbles in, s 1..127, 3 bytes (predictor, step index) */
static void enc_set(uint32_t k, const int16_t *const *pcm, const uint32_t *len, uint32_t nz, int seek)
{
    uint8_t *s = hflash + 0xA0000u + k * 0x14000u;
    uint32_t z, i, off = 0;
    memset(s, 0xFF, 0x14000u);
    memset(s, 0, 512);
    memcpy(s, "FSMP", 4);
    s[4] = 1;
    s[6] = (uint8_t)nz;
    memcpy(s + 8, "SET", 3);
    for (z = 0; z < nz; z++) {
        uint8_t *e = s + 32u + 28u * z, *d = s + 512u + off;
        uint32_t w[5] = {off, len[z], 0, len[z] - 1u, 65536u}, nb = (len[z] + 1u) / 2u;
        int16_t r16 = (int16_t)((36u + z) * 16u);
        int32_t p = 0, x = 0;
        memcpy(e, w, 20);
        memcpy(e + 20, &r16, 2);
        e[25] = (uint8_t)(z ? 36u + z : 0u);
        e[26] = (uint8_t)(z + 1u < nz ? 36u + z : 127u);
        memset(d, 0, nb);
        for (i = 0; i < len[z]; i++) {               /* the page's encoder (sample.mjs adpcmEncode) */
            int32_t st = IMA_STEP[x], df = pcm[z][i] - p, vd;
            uint32_t c = 0;
            if (df < 0) {
                c = 8;
                df = -df;
            }
            if (df >= st) {
                c |= 4;
                df -= st;
            }
            if (df >= st >> 1) {
                c |= 2;
                df -= st >> 1;
            }
            if (df >= st >> 2)
                c |= 1;
            vd = st >> 3;
            if (c & 4u)
                vd += st;
            if (c & 2u)
                vd += st >> 1;
            if (c & 1u)
                vd += st >> 2;
            p = clamp(p + ((c & 8u) ? -vd : vd), -32768, 32767);
            x = clamp(x + IMA_IDX[c & 7u], 0, 88);
            d[i >> 1] |= (uint8_t)((i & 1u) ? c << 4 : c);
        }
        off += nb;
    }
    if (seek) {
        uint32_t magic = 0x4B454553u, t = off;       /* "SEEK" */
        memcpy(s + 24, &magic, 4);
        memcpy(s + 28, &t, 4);
        for (z = 0, off = 0; z < nz; z++) {
            const uint8_t *d = s + 512u + off;
            uint32_t pos = 0, st;
            int32_t p = 0, x = 0;
            for (st = 1; st < 128u; st++) {
                uint8_t *q = s + 512u + t + (z * 127u + st - 1u) * 3u;
                for (; pos < (len[z] * st) >> 7; pos++)
                    smp_nib(d, pos, &p, &x);
                q[0] = (uint8_t)p;
                q[1] = (uint8_t)((uint32_t)p >> 8);
                q[2] = (uint8_t)x;
            }
            off += (len[z] + 1u) / 2u;
        }
        off = t + nz * 127u * 3u;
    }
    memcpy(s + 16, &off, 4);
    smp_user_scan(k);
    smp_slots_sync();
}

/* the slot's samples as the firmware decodes them (the reference: sample_next's rule written out) */
static int16_t ref[0x28000];
static void ref_decode(uint32_t k)
{
    const smp_zone_t *z = &usr_zone[k][0];
    const uint8_t *d = (const uint8_t *)((uintptr_t)SMP_DATA + z->off);
    uint32_t i;
    int32_t p = 0, x = 0;
    for (i = 0; i < z->n; i++) {
        uint32_t b = d[i >> 1], c = (i & 1u) ? b >> 4 : b & 15u;
        int32_t st = IMA_STEP[x], vd = st >> 3;
        if (c & 4u)
            vd += st;
        if (c & 2u)
            vd += st >> 1;
        if (c & 1u)
            vd += st >> 2;
        p = clamp(p + ((c & 8u) ? -vd : vd), -32768, 32767);
        x = clamp(x + IMA_IDX[c & 7u], 0, 88);
        ref[i] = (int16_t)p;
    }
}
/* test signals: a sine with a noisy tail that falls to silence (the decoder's step index at its floor, where a
 * backward decode is ambiguous), and a plain sine */
static int16_t sig[0x28000];
static void make_sig(uint32_t n, int noisy)
{
    uint32_t i, r = 12345;
    for (i = 0; i < n; i++) {
        double t = (double)i / FS, s = sin(2 * M_PI * 441.0 * t) * 12000.0;
        if (noisy) {
            r = r * 1103515245u + 12345u;
            s = (i % 3000u < 1500u ? s : 0) + (double)((int32_t)(r >> 16) % 3000) * (i % 5000u < 2500u ? 1.0 : 0.002);
        }
        sig[i] = (int16_t)s;
    }
}

/* a SAMPLE track 0 on user slot USR1, SLOT 1, TUNE 0, DECAY 127, TONE 127, the envelope and loop as given */
static void smpl_on_usr(int32_t st, int32_t en, int32_t atk, int32_t sus, int32_t rel, int32_t loop)
{
    host_init();
    drum_set_model(&trk[0], DM_SMPL);
    trk[0].p[P_E0] = 0;
    trk[0].p[P_E1] = 127;
    trk[0].p[P_E2] = 127;
    trk[0].p[P_E3] = 0;
    trk[0].p[P_E4] = SMP_NSETS;
    trk[0].p[P_E5] = SMP_SLOT_V(0);
    trk[0].p[P_E6] = (int16_t)st;
    trk[0].p[P_E7] = (int16_t)en;
    trk[0].p[P_SATK] = (int16_t)atk;
    trk[0].p[P_SSUS] = (int16_t)sus;
    trk[0].p[P_SREL] = (int16_t)rel;
    trk[0].p[P_SLOOP] = (int16_t)loop;
    trk[0].p[P_LLEVEL] = 0;
}
/* the source samples the voice plays, one a call (TUNE 0 at 44.1 kHz: one source sample an output sample): sv.s[3] */
static int32_t got[0x40000];
static uint32_t stream(uint32_t n)
{
    dvoice_t *v = &trk[0].v[0];
    uint32_t i;
    int32_t o;
    drum_play(&trk[0], 127, 0);
    for (i = 0; i < NDV; i++)
        if (trk[0].v[i].active && trk[0].v[i].age > v->age)
            v = &trk[0].v[i];
    if (!v->active)
        return 0;
    got[0] = v->sv.s[3];
    for (i = 1; i < n && v->active; i++) {
        o = 0;
        DMODELS[DM_SMPL].render(&trk[0], v, &o, 1);
        if (v->active)
            got[i] = v->sv.s[3];
    }
    return v->active ? n : i - 1u;
}

/* ---------------------------------------------------------------- the knobs and their page */
static void t_knobs(void)
{
    static const char *const L[4] = {"ATK", "SUS", "REL", "LOOP"};
    uint8_t ids[20];
    uint32_t i, ok = 1, n;
    char b[16];
    const char *u;
    for (i = 0; i < 4u; i++)
        ok &= !strcmp(TP[P_SATK + i].label, L[i]) && str_len(TP[P_SATK + i].label) <= 5u;
    check("ATK SUS REL LOOP: their labels", ok);
    check("defaults: ATK 0, SUS 0, REL 40, LOOP OFF", TP[P_SATK].def == 0 && TP[P_SSUS].def == 0 &&
                                                          TP[P_SREL].def == 40 && TP[P_SLOOP].def == 0 &&
                                                          TP[P_SLOOP].max == 4);
    param_format(&TP[P_SLOOP], 0, b, &u);
    ok = !strcmp(b, "OFF");
    param_format(&TP[P_SLOOP], 1, b, &u);
    ok &= !strcmp(b, "FWD");
    param_format(&TP[P_SLOOP], 2, b, &u);
    ok &= !strcmp(b, "PING");
    check("LOOP reads OFF / FWD / PING", ok);
    ui_host_init();
    old_set_model(TSEL, DM_SMPL);
    n = snd_list(TSEL, ids);
    check("SAMPLE's sound list: 5 pages, the fifth ATK SUS REL LOOP (the fourth CHOKE PRIO PLAY CUT)",
          n == 20u && ids[16] == P_SATK && ids[17] == P_SSUS && ids[18] == P_SREL && ids[19] == P_SLOOP &&
              ids[12] == P_CHOKE && ids[13] == P_PRIO && ids[14] == P_SPLAY && ids[15] == P_SCUT);
    {
        uint32_t pos, np = fam_pages(FAM_SND, &pos), i5 = page_first(FAM_SND) + 4u;
        check("SOUND has 5 pages on a SAMPLE track, the fifth shown", np == 5u && !page_hidden(i5) &&
                                                                          PAGES[i5].fam == FAM_SND);
        old_set_model(TSEL, DM_K909);
        n = snd_list(TSEL, ids);
        ok = 1;
        for (i = 0; i < n; i++)
            ok &= ids[i] != P_SATK && ids[i] != P_SLOOP && ids[i] != P_SPLAY;
        check("another model: no ATK .. LOOP, 4 pages at most, the fifth hidden",
              ok && n <= 16u && page_hidden(i5) && fam_pages(FAM_SND, &pos) <= 4u);
    }
    ok = 1;
    for (i = 0; i < 4u; i++)
        ok &= motion_param(P_SATK + i) && ml_param_ok(P_SATK + i);
    check("ATK SUS REL LOOP: LOCK / MOTION and MIDI LEARN take them", ok);
    old_set_model(&trk[2], DM_SMPL);
    ui.ml.act = 0;
    ui.ml.trk = 2;
    ui.ml.id = P_SATK;
    ml_name(b);
    ok = !strcmp(b, "T3 ATK");
    ui.ml.id = P_SLOOP;
    ml_name(b);
    ok &= !strcmp(b, "T3 LOOP");
    check("MIDI LEARN names: T3 ATK, T3 LOOP", ok);
}

/* ---------------------------------------------------------------- the envelope */
static void hit(int32_t *w, uint32_t n)
{
    drum_play(&trk[0], 127, 0);
    render_track(&trk[0], w, n);
}
static void t_defaults(void)
{
    uint32_t r, ok = 1;
    static int32_t w0[SECS(1)];
    make_sig(30000, 1);
    enc_slot(0, sig, 30000);
    smpl_on_usr(0, 127, 0, 0, 40, 0);
    hit(w0, SECS(1));
    for (r = 0; r <= 127u; r += 127u) {
        smpl_on_usr(0, 127, 0, 0, (int32_t)r, 0);
        hit(wa, SECS(1));
        ok &= !memcmp(w0, wa, sizeof w0);
    }
    check("SUS 0: REL does nothing (one-shot as before, bit for bit)", ok && peak(w0, SECS(1)) > 0);
    smpl_on_usr(0, 127, 0, 0, 40, 0);
    trk[0].v[0].active = 0;
    hit(wa, SECS(1));
    check("the defaults: the voice's envelope state untouched (no ATK / SUS bits)", !trk[0].v[0].k[2] ||
                                                                                        !trk[0].v[1].k[2]);
}
static void t_attack(void)
{
    int32_t p0, p1;
    make_sig(60000, 0);
    enc_slot(0, sig, 60000);
    smpl_on_usr(0, 127, 70, 0, 40, 0);
    hit(wa, SECS(1));
    p0 = peak(wa, 32);
    p1 = peak(wa + SECS(0.3), SECS(0.2));
    smpl_on_usr(0, 127, 0, 0, 40, 0);
    hit(wb, SECS(1));
    printf("     ATK 70 (%u samples): first 32 samples peak %d, 0.3..0.5 s %d (ATK 0: %d)\n", smpl_atk_len(70), p0, p1,
           peak(wb, 32));
    check("ATK: starts from silence and rises", p0 * 20 < p1 && peak(wb, 32) * 2 > p1);
    {
        uint32_t i, rising = 1;
        for (i = 1; i < smpl_atk_len(70) / 512u; i++)   /* the ramp's blocks of 512: each louder than the last */
            rising &= peak(wa + i * 512u, 512) >= peak(wa + (i - 1u) * 512u, 512) - 200;
        check("ATK: the ramp rises block by block", rising);
    }
}
static uint32_t silent_after(const int32_t *w, uint32_t n)   /* the last sample louder than 1/1000 of the peak */
{
    int32_t pk = peak(w, n);
    while (n && abs(w[n - 1u]) * 1000 <= pk)
        n--;
    return n;
}
static void t_gate(void)
{
    uint32_t step, end1, end2, end3;
    make_sig(80000, 0);
    enc_slot(0, sig, 80000);
    song.g[G_BPM] = 120;
    smpl_on_usr(0, 127, 0, 100, 0, 1);              /* SUS 100, REL 0 (5 ms), LOOP FWD: a held tone */
    step = smpl_gate_len(&trk[0]);
    hit(wa, SECS(1));
    end1 = silent_after(wa, SECS(1));
    printf("     SUS 100 LOOP FWD: a hit's gate %u samples, sound to %u\n", step, end1);
    check("a hit (a step, MIDI, the pads): its gate one step of the track (1/16 at 120 BPM), then REL",
          step >= 5512u && step <= 5512u + CTL && end1 > step && end1 < step + 2000u);
    smpl_on_usr(0, 127, 0, 100, 90, 1);
    hit(wb, SECS(1));
    end2 = silent_after(wb, SECS(1));
    check("a longer REL: a longer tail", end2 > end1 + 4000u);
    smpl_on_usr(0, 127, 0, 0, 0, 1);                /* SUS 0, LOOP FWD: the DECAY (127) carries it on */
    hit(wb, SECS(1));
    check("SUS 0 with LOOP: one-shot, the loop goes on with DECAY's fall (no gate)", silent_after(wb, SECS(1)) > SECS(0.9));
    /* KEYS: held while the key is */
    smpl_on_usr(0, 127, 0, 100, 0, 1);
    song.sel = 0;
    song.octave = 0;
    keys_on(7u);
    render_track(&trk[0], wa, SECS(0.5));
    check("KEYS: the note holds while the key is held (0.5 s on)", peak(wa + SECS(0.45), SECS(0.05)) > 1000);
    keys_off();
    render_track(&trk[0], wa, SECS(0.2));
    check(".. let go: REL, then silence", silent_after(wa, SECS(0.2)) < 3000u);
    /* MIDI: held until its note-off */
    smpl_on_usr(0, 127, 0, 100, 0, 1);
    song.g[G_ROUTE] = RT_NOTE;
    midi_in(0x90, (uint32_t)song.g[G_DRCH] - 1u, (uint32_t)trk[0].p[P_NOTE], 100);
    events_block(CTL);
    render_track(&trk[0], wa, SECS(0.5));
    check("MIDI: the note holds until its note-off (0.5 s on)", peak(wa + SECS(0.45), SECS(0.05)) > 1000);
    midi_in(0x80, (uint32_t)song.g[G_DRCH] - 1u, (uint32_t)trk[0].p[P_NOTE], 0);
    events_block(CTL);
    render_track(&trk[0], wa, SECS(0.2));
    check(".. its note-off: REL, then silence", silent_after(wa, SECS(0.2)) < 3000u);
    smpl_on_usr(0, 127, 0, 100, 0, 1);
    midi_in(0x90, (uint32_t)song.g[G_DRCH] - 1u, (uint32_t)trk[0].p[P_NOTE], 100);
    events_block(CTL);
    midi_in(0x90, (uint32_t)song.g[G_DRCH] - 1u, (uint32_t)trk[0].p[P_NOTE], 0);   /* a note-on at 0: a note-off */
    events_block(CTL);
    render_track(&trk[0], wa, SECS(0.2));
    check(".. a note-on at velocity 0 ends it too", silent_after(wa, SECS(0.2)) < 3000u);
    /* the steps: a hit holds its step, longer through the TIE steps after it */
    smpl_on_usr(0, 127, 0, 100, 0, 1);
    memset(trk[0].step, 0, sizeof trk[0].step);
    trk[0].p[P_SLEN] = 16;
    trk[0].step[0].on = 1;
    trk[0].step[1].tie = 1;
    trk[0].step[2].tie = 1;
    seq_start();
    render_mix(wa, 0, SECS(0.6));
    seq_stop();
    end3 = silent_after(wa, SECS(0.6));
    printf("     a step with 2 TIE steps: sound to %u (3 steps: %u)\n", end3, 3u * 5512u);
    check("a step and its 2 TIEs: the note held 3 steps, then REL", end3 > 3u * 5512u && end3 < 3u * 5512u + 3000u);
    song.g[G_ROUTE] = 0;
}

/* ---------------------------------------------------------------- the loop */
static void t_loop_fwd(void)
{
    uint32_t n = 40000, ls, le, L, i, got_n, ok = 1, xf, at;
    int32_t worst = 0, jump;
    make_sig(n, 0);
    enc_slot(0, sig, n);
    ref_decode(0);
    smpl_on_usr(20, 40, 0, 0, 40, 1);               /* START 20, END 40 */
    ls = (n * 20u) >> 7;
    le = (n * 41u) >> 7;
    L = le - ls;
    got_n = stream(ls ? 3u * L + 400u : 0);
    for (i = 0; i + 1u < le - ls && i < got_n; i++)  /* the first leg: START .. END */
        ok &= got[i] == ref[ls + i];
    check("FWD: the first pass plays START .. END as the decoder does", ok && got_n == 3u * L + 400u);
    xf = L / 2u < 64u ? L / 2u : 64u;
    at = le - ls;                                    /* the seam: the crossfade's first sample */
    for (ok = 1, i = 0; i < 2u * L; i++) {           /* after the crossfade: START + xf on, exactly, each pass */
        uint32_t t = at + i, ph = i % L;
        if (ph < xf)
            continue;
        ok &= got[t] == ref[ls + ph];
    }
    check("FWD: after the 64-sample crossfade the loop is the sample's own START .. END again, pass after pass", ok);
    for (i = at - 4u; i < at + xf + 4u; i++) {       /* the seam: no step larger than the signal's own */
        int32_t d = abs(got[i] - got[i - 1u]);
        worst = d > worst ? d : worst;
    }
    jump = abs(ref[le - 1u] - ref[ls]);
    printf("     FWD seam: the hard jump would be %d, the largest step through the crossfade %d\n", jump, worst);
    check("FWD: the crossfade takes the click out of the seam (no step above the sine's own, ~1700)", worst < 2200);
}
static uint32_t ping_check(uint32_t n, uint32_t st, uint32_t en, uint32_t passes)
{
    uint32_t ls = (n * st) >> 7, le = en >= 127u ? n : (n * (en + 1u)) >> 7, L = le - ls, i, t = 0, ok = 1, p;
    uint32_t total = 1u + (L - 1u) + passes * 2u * (L - 1u), got_n;
    smpl_on_usr((int32_t)st, (int32_t)en, 0, 0, 40, 2);
    got_n = stream(total);
    if (got_n != total)
        return 0;
    for (i = 0; i < L; i++)                          /* forward START .. END - 1 */
        ok &= got[t++] == ref[ls + i];
    for (p = 0; p < passes; p++) {
        for (i = L - 1u; i-- > 0;)                   /* back: END - 2 .. START */
            ok &= got[t++] == ref[ls + i];
        for (i = 1; i < L; i++)                      /* forward: START + 1 .. END - 1 */
            ok &= got[t++] == ref[ls + i];
    }
    return ok;
}
static void t_loop_ping(void)
{
    uint32_t n = 60000;
    make_sig(n, 1);
    enc_slot(0, sig, n);
    ref_decode(0);
    check("PING: a loop of one 1/128 step (START 64, END 64: 468 samples, 12 chunks), exact both ways",
          ping_check(n, 64, 64, 3));
    check("PING: a loop over several chunks (START 30, END 33), exact both ways", ping_check(n, 30, 33, 3));
    check("PING: a loop over several segments (START 10, END 30: 9844 samples), exact both ways, the noisy tail too",
          ping_check(n, 10, 30, 2));
    check("PING: from the sample's start (START 0, END 20), exact", ping_check(n, 0, 20, 2));
    n = 3000;                                        /* a loop shorter than a chunk: 23 samples */
    make_sig(n, 1);
    enc_slot(0, sig, n);
    ref_decode(0);
    check("PING: a loop shorter than a chunk (23 samples), exact both ways", ping_check(n, 64, 64, 4));
    n = 10240;                                       /* the longest PING-PONG loop: the whole sample */
    make_sig(n, 1);
    enc_slot(0, sig, n);
    ref_decode(0);
    check("PING: the longest loop it holds (10240 samples, START 0 .. END 127), exact", ping_check(n, 0, 127, 1));
    n = 30000;
    make_sig(n, 1);
    enc_slot(0, sig, n);
    ref_decode(0);
    {
        uint32_t ls = 0, le = n, ok = 1, i, got_n;
        smpl_on_usr(0, 127, 0, 0, 40, 2);
        got_n = stream(n + 200u);
        for (i = 64; i < 200u && n + i < got_n; i++)
            ok &= got[n + i] == ref[ls + i];
        check("PING: a loop longer than it holds (30000 samples) plays FWD", got_n == n + 200u && ok && le == n);
    }
}
static void t_loop_guard(void)
{
    uint32_t n = 20000, k, i;
    make_sig(n, 0);
    for (k = 1; k <= 2u; k++) {
        enc_slot(0, sig, n);
        smpl_on_usr(0, 127, 0, 0, 40, (int32_t)k);
        drum_play(&trk[0], 127, 0);
        render_track(&trk[0], wa, SECS(0.1));
        for (i = 0; i < 16u; i++)                     /* an upload into it: its zones read 0 samples (editor_sample.c) */
            usr_zone[0][i].n = 0;
        usr_nz[0] = 0;
        render_track(&trk[0], wa, SECS(0.1));
        check(k == 1u ? "FWD: a slot being rewritten ends the voice" : "PING: a slot being rewritten ends the voice",
              !trk[0].v[0].active && !trk[0].v[1].active);
    }
}

/* ---------------------------------------------------------------- a set's slots: START END LOOP in each */
static int16_t zsig[3][24000];
static uint32_t zlen[3] = {14000, 24000, 9001};
static void zref(uint32_t z)                         /* ref[] = zone z of USR1 as the firmware decodes it */
{
    const smp_zone_t *q = &usr_zone[0][z];
    const uint8_t *d = (const uint8_t *)((uintptr_t)SMP_DATA + q->off);
    uint32_t i;
    int32_t p = 0, x = 0;
    for (i = 0; i < q->n; i++)
        ref[i] = (int16_t)smp_nib(d, i, &p, &x);
}
static void set_on(uint32_t z, int32_t st, int32_t en, int32_t loop)
{
    smpl_on_usr(st, en, 0, 0, 40, loop);
    trk[0].p[P_E5] = SMP_SLOT_V(z);
}
static int zone_check(uint32_t z, uint32_t st, uint32_t en, int32_t loop)
{
    uint32_t n = zlen[z], ls = (n * st) >> 7, le = en >= 127u ? n : (n * (en + 1u)) >> 7, L = le - ls, i, t = 0, ok = 1;
    uint32_t got_n, xf = L / 2u < 64u ? L / 2u : 64u;
    zref(z);
    set_on(z, (int32_t)st, (int32_t)en, loop);
    got_n = stream(loop ? 3u * L : L + 50u);
    for (i = 0; i < L && t < got_n; i++)              /* the first leg: START .. END of this slot */
        ok &= got[t++] == ref[ls + i];
    if (!loop)
        return ok && got_n == L;                      /* (one-shot: ends at END) */
    if (loop == 2) {                                  /* PING: back to START, forward again */
        for (i = L - 1u; i-- > 0 && t < got_n;)
            ok &= got[t++] == ref[ls + i];
        for (i = 1; i < L && t < got_n; i++)
            ok &= got[t++] == ref[ls + i];
        return ok && got_n == 3u * L;
    }
    for (i = xf; i < L; i++)                          /* FWD: after the crossfade START + xf on, exactly */
        ok &= got[L + i] == ref[ls + i];
    return ok && got_n == 3u * L;
}
static void t_set_slots(void)
{
    static const char *const W[3] = {"first", "middle", "last"};
    const int16_t *pc[3] = {zsig[0], zsig[1], zsig[2]};
    uint32_t z, i, r = 777;
    char what[160];
    for (z = 0; z < 3u; z++)
        for (i = 0; i < zlen[z]; i++) {
            r = r * 1103515245u + 12345u;
            zsig[z][i] = (int16_t)(sin((double)i * (0.03 + 0.02 * z)) * 9000.0 * (i % 4000u < 3000u ? 1.0 : 0.01) +
                                   (double)((int32_t)(r >> 16) % 2000) * (z == 1u ? 1.0 : 0.2));
        }
    enc_set(0, pc, zlen, 3, 1);
    check("a set of 3 with the page's START table: 3 samples, SLOT order = key order", smp_slot_count(SMP_NSETS) == 3u);
    for (z = 0; z < 3u; z++) {
        snprintf(what, sizeof what, "set, the %s sample (SLOT %u/3): START 40 .. END 127, exact from its own START", W[z],
                 (unsigned)z + 1u);
        check(what, zone_check(z, 40, 127, 0));
        snprintf(what, sizeof what, "set, the %s sample: START 0 .. END 50, exact, ends at its own END", W[z]);
        check(what, zone_check(z, 0, 50, 0));
        snprintf(what, sizeof what, "set, the %s sample: LOOP FWD START 30 .. END 60, exact pass after pass", W[z]);
        check(what, zone_check(z, 30, 60, 1));
        snprintf(what, sizeof what, "set, the %s sample: LOOP PING START 50 .. END 70, exact both ways", W[z]);
        check(what, zone_check(z, 50, 70, 2));
    }
    enc_set(0, pc, zlen, 3, 0);                       /* a set without the table (an older page, a backup) */
    zref(0);
    check("a set without the table: the first sample's START as before (worked out in the main loop)", zone_check(0, 40, 127, 0));
    {
        uint32_t ok;
        zref(1);
        set_on(1, 40, 127, 0);
        ok = stream(100) == 100u && got[0] == ref[0];
        check("a set without the table: another sample starts from its beginning (no state for its START)", ok);
    }
}

/* ---------------------------------------------------------------- PLAY: KEY / CHOP */
static dvoice_t *newest(track_t *t)                  /* the voice t's last hit started (0: none) */
{
    dvoice_t *v = 0;
    uint32_t i;
    for (i = 0; i < NDV; i++)
        if (t->v[i].active && (!v || t->v[i].age > v->age))
            v = &t->v[i];
    return v;
}
static uint32_t played_zone(void)                    /* the zone the newest voice of track 0 plays (+1, 0 none) */
{
    dvoice_t *v = newest(&trk[0]);
    return v ? (uint32_t)v->sv.s[4] + 1u : 0u;
}
static void play_on(int32_t play)
{
    smpl_on_usr(0, 127, 0, 0, 40, 0);
    trk[0].p[P_SPLAY] = (int16_t)play;
    trk[0].p[P_E5] = SMP_SLOT_V(1);                  /* (SLOT 2: CHOP ignores it) */
    song.sel = 0;
    song.seq_mode = 3;
}
static void t_play(void)
{
    const int16_t *pc[3] = {zsig[0], zsig[1], zsig[2]};
    uint8_t ids[SND_N];
    char b[16];
    const char *u;
    uint32_t z0 = 0x8000u, ok;
    int32_t q1;
    check("PLAY: its label, KEY / CHOP, KEY by default", !strcmp(TP[P_SPLAY].label, "PLAY") && TP[P_SPLAY].def == 0 &&
                                                         TP[P_SPLAY].max == 1);
    param_format(&TP[P_SPLAY], 1, b, &u);
    ok = !strcmp(b, "CHOP");
    param_format(&TP[P_SPLAY], 0, b, &u);
    check("PLAY reads KEY / CHOP", ok && !strcmp(b, "KEY"));
    old_set_model(TSEL, DM_SMPL);
    snd_list(TSEL, ids);
    check("PLAY on SOUND 4/5 (CHOKE PRIO PLAY)", ids[12] == P_CHOKE && ids[13] == P_PRIO && ids[14] == P_SPLAY &&
                                                 ids[15] == P_SCUT);
    check("PLAY: LOCK / MOTION, MIDI LEARN take it", motion_param(P_SPLAY) && ml_param_ok(P_SPLAY));
    enc_set(0, pc, zlen, 3, 1);
    /* KEY: the selected SLOT's sample, chromatic from its root */
    play_on(0);
    song.octave = 0;
    keys_on(7u);                                     /* C4: at its root */
    q1 = newest(&trk[0])->sv.s[5];
    ok = played_zone() == (z0 | 1u) + 1u;
    keys_off();
    keys_on(19u);                                    /* C5: an octave up */
    check("KEY: KEYS play the selected SLOT's sample chromatically (C4 its root, C5 an octave up)",
          ok && played_zone() == (z0 | 1u) + 1u && newest(&trk[0])->sv.s[5] == 2 * q1);
    keys_off();
    /* CHOP: the 16 white keys are chops 1..16, on any octave, each at its root + TUNE */
    play_on(1);
    {
        uint32_t oct, good = 1;
        for (oct = 0; oct < 2u; oct++) {
            play_on(1);
            song.octave = oct ? 2 : -1;
            keys_on(0u);                             /* F3, the first white key: chop 1 */
            good &= played_zone() == (z0 | 0u) + 1u && newest(&trk[0])->sv.s[5] == q1;
            keys_off();
            keys_on(4u);                             /* the third white key: chop 3 */
            good &= played_zone() == (z0 | 2u) + 1u;
            keys_off();
        }
        check("CHOP: white key i plays chop i + 1 (SLOT ignored) at its root, the same on any octave", good);
    }
    song.octave = 0;
    play_on(1);
    keys_on(6u);                                     /* the fourth white key: past the set's 3 samples */
    ok = !played_zone();
    keys_off();
    keys_on(1u);                                     /* a black key */
    check("CHOP: a key past the set's samples and the black keys play nothing", ok && !played_zone());
    keys_off();
    play_on(1);
    trk[0].p[P_E0] = 12;
    keys_on(2u);
    check("CHOP: TUNE pitches the chops", played_zone() == (z0 | 1u) + 1u && newest(&trk[0])->sv.s[5] == 2 * q1);
    keys_off();
    play_on(1);
    keys_on(2u);
    ui_input();
    check("CHOP: the screen says which chop a key plays (CHOP 2/3)", str_eq(ui.msg, "CHOP 2/3"));
    keys_off();
    /* MIDI and the steps: a note's chop is (note - 60) mod 16 + 1 */
    play_on(1);
    song.g[G_ROUTE] = RT_SEL;
    midi_in(0x90, (uint32_t)song.g[G_DRCH] - 1u, 62, 100);   /* D4: chop 3 */
    events_block(CTL);
    ok = played_zone() == (z0 | 2u) + 1u;
    midi_in(0x90, (uint32_t)song.g[G_DRCH] - 1u, 60 + 16 + 1, 100);   /* chop 2, an octave and a fourth up */
    events_block(CTL);
    ok &= played_zone() == (z0 | 1u) + 1u;
    midi_in(0x90, (uint32_t)song.g[G_DRCH] - 1u, 60 - 16, 100);   /* chop 1 */
    events_block(CTL);
    check("CHOP: MIDI note n plays chop (n - 60) mod 16 + 1", ok && played_zone() == (z0 | 0u) + 1u);
    song.g[G_ROUTE] = 0;
    play_on(1);
    drum_play(&trk[0], 127, 2);                      /* a step at D4 (NOTE + 2) */
    ok = played_zone() == (z0 | 2u) + 1u;
    drum_play(&trk[0], 127, -14);                    /* -14 mod 16 = 2 */
    check("CHOP: a step plays the chop of its note", ok && played_zone() == (z0 | 2u) + 1u);
    /* recording: the key's chop into the step */
    play_on(1);
    memset(trk[0].step, 0, sizeof trk[0].step);
    song.rec = 1;
    seq_start();
    keys_on(4u);
    keys_off();
    seq_stop();
    song.rec = 0;
    {
        uint32_t i, got = 0;
        for (i = 0; i < NSTEP; i++)
            if (trk[0].step[i].on)
                got = (uint32_t)(trk[0].step[i].nt + 100);
        check("CHOP: a key recorded live stores its chop (the step's note + 2: chop 3)", got == 102u);
    }
    ui_host_init();
    play_on(1);
    ui.sr_cur = 0;
    srec_note(4u);
    check("CHOP: step record stores the key's chop (+ 2)", trk[0].step[0].on && trk[0].step[0].nt == 2);
    ui.sr_key = 0;
    trk[0].step[1].nt = 0;
    trk[0].step[1].on = 0;
    ui.sr_cur = 1;
    srec_note(1u);
    check("CHOP: step record: a black key writes nothing", !trk[0].step[1].on);
    song.seq_mode = 0;
}

/* ---------------------------------------------------------------- LOOP LOOPI / ONE, CUT */
static uint32_t voices_on(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NDV; i++)
        n += t->v[i].active;
    return n;
}
static void t_loopi_one_cut(void)
{
    char b[16];
    const char *u;
    uint8_t ids[SND_N];
    int32_t pk, pk2;
    uint32_t end;
    param_format(&TP[P_SLOOP], 3, b, &u);
    pk = !strcmp(b, "LOOPI");
    param_format(&TP[P_SLOOP], 4, b, &u);
    check("LOOP: OFF FWD PING LOOPI ONE (the old ids kept, the new appended)", pk && !strcmp(b, "ONE") &&
                                                                               TP[P_SLOOP].max == 4);
    check("CUT: OFF / ON, OFF by default; MIDI LEARN takes it", !strcmp(TP[P_SCUT].label, "CUT") &&
                                                                  TP[P_SCUT].def == 0 && TP[P_SCUT].max == 1 &&
                                                                  ml_param_ok(P_SCUT));
    old_set_model(TSEL, DM_SMPL);
    snd_list(TSEL, ids);
    check("SOUND 4/5: CHOKE PRIO PLAY CUT", ids[14] == P_SPLAY && ids[15] == P_SCUT);
    make_sig(40000, 0);
    enc_slot(0, sig, 40000);
    /* LOOPI: no release, the gate's end does nothing; ATK then the full level (SUS 0) */
    song.g[G_BPM] = 120;
    smpl_on_usr(30, 60, 0, 0, 127, 3);
    hit(wa, SECS(2));
    pk = peak(wa + SECS(0.05), SECS(0.1));
    pk2 = peak(wa + SECS(1.85), SECS(0.1));
    printf("     LOOPI SUS 0: peak just after the hit %d, 1.85 s later (the gate long gone, REL 127) %d\n", pk, pk2);
    check("LOOPI: a step's gate ends, REL 127: no release, the loop goes on at the full level", pk2 * 10 >= pk * 9 &&
                                                                                                  trk[0].v[0].active +
                                                                                                          trk[0].v[1].active ==
                                                                                                      1);
    smpl_on_usr(30, 60, 0, 64, 0, 3);
    hit(wa, SECS(2));
    pk = peak(wa + SECS(0.05), SECS(0.1));
    pk2 = peak(wa + SECS(1.85), SECS(0.1));
    printf("     LOOPI SUS 64: %d then %d\n", pk, pk2);
    check("LOOPI with SUS: DECAY falls to SUS and holds there (REL 0 never cuts it)", pk2 > pk / 4 && pk2 < pk);
    smpl_on_usr(30, 60, 0, 0, 0, 3);
    hit(wa, SECS(0.2));
    drum_play(&trk[0], 127, 0);
    render_track(&trk[0], wa, SECS(0.05));
    check("LOOPI: the next hit ends the looping voice (even with CUT OFF): one voice", voices_on(&trk[0]) == 1u);
    drum_cut(&trk[0]);
    check("LOOPI: a choke ends it", voices_on(&trk[0]) == 0u);
    smpl_on_usr(30, 60, 0, 0, 0, 3);
    seq_start();
    drum_play(&trk[0], 127, 0);
    render_track(&trk[0], wa, SECS(0.1));
    seq_stop();
    render_track(&trk[0], wa, SECS(0.05));
    check("LOOPI: STOP ends it (nothing drones on)", voices_on(&trk[0]) == 0u);
    /* ONE: START to END whatever the gate */
    smpl_on_usr(0, 127, 0, 100, 0, 4);
    hit(wa, SECS(1));
    end = silent_after(wa, SECS(1));
    printf("     ONE SUS 100 REL 0, a step's gate: sound to %u (the sample 40000)\n", end);
    check("ONE: a short gate, REL 0: the sample still plays to its END", end > 39000u && end <= 40100u);
    smpl_on_usr(0, 127, 0, 100, 0, 0);
    hit(wb, SECS(1));
    check("(OFF with SUS: the gate's end releases it)", silent_after(wb, SECS(1)) < 8000u);
    smpl_on_usr(0, 127, 0, 0, 40, 4);
    hit(wa, SECS(1));
    smpl_on_usr(0, 127, 0, 0, 40, 0);
    hit(wb, SECS(1));
    check("ONE with ATK 0 SUS 0: as OFF, bit for bit", !memcmp(wa, wb, sizeof(int32_t) * SECS(1)));
    /* CUT */
    smpl_on_usr(0, 127, 0, 0, 40, 0);
    drum_play(&trk[0], 127, 0);
    render_track(&trk[0], wa, SECS(0.05));
    drum_play(&trk[0], 127, 0);
    render_track(&trk[0], wa, SECS(0.05));
    check("CUT OFF (the default): a new hit overlaps the sounding one (2 voices), as before", voices_on(&trk[0]) == 2u);
    smpl_on_usr(0, 127, 0, 0, 40, 0);
    trk[0].p[P_SCUT] = 1;
    drum_play(&trk[0], 127, 0);
    render_track(&trk[0], wa, SECS(0.05));
    drum_play(&trk[0], 127, 0);
    check("CUT ON: a new hit cuts the sounding one (one voice, its tail declicked)", voices_on(&trk[0]) == 1u &&
                                                                                       trk[0].dtail != 0);
    {
        const int16_t *pc[3] = {zsig[0], zsig[1], zsig[2]};
        enc_set(0, pc, zlen, 3, 1);
        smpl_on_usr(0, 127, 0, 0, 40, 0);
        trk[0].p[P_SPLAY] = 1;
        trk[0].p[P_SCUT] = 1;
        drum_play(&trk[0], 127, 0);
        drum_play(&trk[0], 127, 2);
        check("CUT ON, CHOP: a new chop cuts the previous one", voices_on(&trk[0]) == 1u &&
                                                                  played_zone() == (0x8000u | 2u) + 1u);
    }
}

/* ---------------------------------------------------------------- projects, presets, locks */
static void t_project(void)
{
    fresh();
    drum_set_model(&trk[2], DM_SMPL);
    trk[2].p[P_SATK] = 12;
    trk[2].p[P_SSUS] = 99;
    trk[2].p[P_SREL] = 3;
    trk[2].p[P_SLOOP] = 2;
    trk[2].p[P_SPLAY] = 1;
    trk[2].p[P_SCUT] = 1;
    trk[3].p[P_SLOOP] = 3;
    trk[4].p[P_SLOOP] = 4;
    trk[5].p[P_SREL] = 127;
    project_save(0);
    power_cycle();
    project_load(0);
    check("a project keeps ATK SUS REL LOOP", trk[2].p[P_SATK] == 12 && trk[2].p[P_SSUS] == 99 &&
                                                trk[2].p[P_SREL] == 3 && trk[2].p[P_SLOOP] == 2 &&
                                                trk[5].p[P_SREL] == 127 && trk[2].p[P_SPLAY] == 1 &&
                                                trk[2].p[P_SCUT] == 1 && trk[3].p[P_SLOOP] == 3 && trk[4].p[P_SLOOP] == 4);
    check(".. the other parameters past the block where they were (xp)", proj_slot[0].xp[2][0] == 0 &&
                                                                       proj_xp_get(&proj_slot[0], 2, P_IMIX) == 127);
    memset(proj_slot[0].xp2, 0, sizeof proj_slot[0].xp2);   /* a record from before them: 0 there */
    memset(proj_slot[0].xp3, 0, sizeof proj_slot[0].xp3);
    proj_slot[0].sum = proj_sum(&proj_slot[0]);
    proj_load(0, 0);
    check("a project from before: ATK 0, SUS 0, REL 40, LOOP OFF, PLAY KEY", trk[2].p[P_SATK] == 0 &&
                                                                             trk[2].p[P_SSUS] == 0 &&
                                                                             trk[2].p[P_SREL] == 40 &&
                                                                             trk[2].p[P_SLOOP] == 0 &&
                                                                             trk[2].p[P_SPLAY] == 0 &&
                                                                             trk[2].p[P_SCUT] == 0);
}
static void t_presets(void)
{
    preset_t pr;
    uint32_t i, same = 1;
    upr_slot_t before;
    fresh();
    host_init();
    drum_set_model(&trk[0], DM_SMPL);
    preset_user_save(4, &trk[0]);                   /* the defaults: the slot as before (no bit set) */
    before = upr.s[4];
    for (i = 0; i < UPR_NFX; i++)
        same &= upr_fxv(&upr.s[4], i) == upr.s[4].f[i];
    check("a SAMPLE preset at the defaults: its bytes as before (the FX bytes' top bits 0)", same);
    trk[0].p[P_SATK] = 127;
    trk[0].p[P_SSUS] = 1;
    trk[0].p[P_SREL] = 0;
    trk[0].p[P_SLOOP] = 4;
    trk[0].p[P_SPLAY] = 1;
    trk[0].p[P_SCUT] = 1;
    trk[0].p[P_E1] = 100;                            /* (DECAY, TONE: their bytes' top bits hold LOOP's 3rd bit, CUT) */
    trk[0].p[P_E2] = 127;
    trk[0].p[P_E3] = 127;                            /* (DRIVE: its byte's top bit holds PLAY) */
    trk[0].p[P_FENV] = -64;
    trk[0].p[P_REV] = 127;
    preset_user_save(5, &trk[0]);
    check("the record keeps its size (3204 B, 96 slots)", sizeof upr == 3204u);
    power_cycle();
    host_init();
    drum_set_model(&trk[3], DM_K909);
    preset_load(&trk[3], PRE_NFACT + 5u);
    check("a SAMPLE user preset brings ATK SUS REL LOOP back (after a power cycle)",
          trk[3].p[P_SATK] == 127 && trk[3].p[P_SSUS] == 1 && trk[3].p[P_SREL] == 0 && trk[3].p[P_SLOOP] == 4 &&
              trk[3].p[P_SPLAY] == 1 && trk[3].p[P_SCUT] == 1 && trk[3].p[P_E3] == 127 && trk[3].p[P_E1] == 100 &&
              trk[3].p[P_E2] == 127);
    check(".. and its FX as saved (FENV -64, REV 127)", trk[3].p[P_FENV] == -64 && trk[3].p[P_REV] == 127);
    preset_load(&trk[3], PRE_NFACT + 4u);
    check("a SAMPLE preset saved at the defaults loads them", trk[3].p[P_SATK] == 0 && trk[3].p[P_SSUS] == 0 &&
                                                                trk[3].p[P_SREL] == 40 && trk[3].p[P_SLOOP] == 0 &&
                                                                trk[3].p[P_SPLAY] == 0 && trk[3].p[P_SCUT] == 0);
    trk[3].p[P_SLOOP] = 1;
    for (i = 0; i < PRE_NFACT; i++)
        if (preset_get(i, &pr) && pr.model == DM_SMPL)
            break;
    preset_load(&trk[3], i);
    check("a factory SAMPLE preset: ATK SUS REL LOOP at their defaults", trk[3].p[P_SLOOP] == 0 &&
                                                                         trk[3].p[P_SREL] == 40);
    check("ATK .. LOOP mark a preset edited (SOUND knobs)", preset_marks(&trk[3], P_SATK) &&
                                                             preset_marks(&trk[3], P_SLOOP));
    drum_set_model(&trk[1], DM_K909);
    trk[1].p[P_SATK] = 50;
    preset_user_save(6, &trk[1]);
    check("another model's preset stores none (its bytes as before)", upr_env(&upr.s[6]) == 0);
    (void)before;
}
static void t_locks(void)
{
    host_init();
    old_set_model(&trk[0], DM_SMPL);
    memset(&mo.b, 0, sizeof mo.b);
    trk[0].step[3].on = 1;
    lk_put(&mo.b, lk_key(0, 3, P_SATK), 30, 1);
    lk_put(&mo.b, lk_key(0, 3, P_SLOOP), 2, 1);
    motion_step(&trk[0], 3, MS_ALL);
    check("a step's LOCK of ATK and LOOP plays on its step", trk[0].p[P_SATK] == 30 && trk[0].p[P_SLOOP] == 2);
    motion_step(&trk[0], 4, MS_ALL);
    check(".. and lets go after it", trk[0].p[P_SATK] == 0 && trk[0].p[P_SLOOP] == 0);
    memset(&mo.b, 0, sizeof mo.b);
}

int main(void)
{
    fresh();
    t_knobs();
    fresh();
    t_defaults();
    t_attack();
    t_gate();
    t_loop_fwd();
    t_loop_ping();
    t_loop_guard();
    fresh();
    t_set_slots();
    fresh();
    t_play();
    fresh();
    t_loopi_one_cut();
    t_project();
    t_presets();
    t_locks();
    printf("sampenv_test: %s (%d failed)\n", fails ? "FAILED" : "ok", fails);
    return fails ? 1 : 0;
}
