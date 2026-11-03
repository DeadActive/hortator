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
                                                          TP[P_SLOOP].max == 2);
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
    check("SAMPLE's sound list: 5 pages, the fifth ATK SUS REL LOOP (the fourth CHOKE PRIO)",
          n == 20u && ids[16] == P_SATK && ids[17] == P_SSUS && ids[18] == P_SREL && ids[19] == P_SLOOP &&
              ids[12] == P_CHOKE && ids[13] == P_PRIO && ids[14] == 0xFFu && ids[15] == 0xFFu);
    {
        uint32_t pos, np = fam_pages(FAM_SND, &pos), i5 = page_first(FAM_SND) + 4u;
        check("SOUND has 5 pages on a SAMPLE track, the fifth shown", np == 5u && !page_hidden(i5) &&
                                                                          PAGES[i5].fam == FAM_SND);
        old_set_model(TSEL, DM_K909);
        n = snd_list(TSEL, ids);
        ok = 1;
        for (i = 0; i < n; i++)
            ok &= ids[i] != P_SATK && ids[i] != P_SLOOP;
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

/* ---------------------------------------------------------------- projects, presets, locks */
static void t_project(void)
{
    fresh();
    drum_set_model(&trk[2], DM_SMPL);
    trk[2].p[P_SATK] = 12;
    trk[2].p[P_SSUS] = 99;
    trk[2].p[P_SREL] = 3;
    trk[2].p[P_SLOOP] = 2;
    trk[5].p[P_SREL] = 127;
    project_save(0);
    power_cycle();
    project_load(0);
    check("a project keeps ATK SUS REL LOOP", trk[2].p[P_SATK] == 12 && trk[2].p[P_SSUS] == 99 &&
                                                trk[2].p[P_SREL] == 3 && trk[2].p[P_SLOOP] == 2 &&
                                                trk[5].p[P_SREL] == 127);
    check(".. the other parameters past the block where they were (xp)", proj_slot[0].xp[2][0] == 0 &&
                                                                       proj_xp_get(&proj_slot[0], 2, P_IMIX) == 127);
    memset(proj_slot[0].xp2, 0, sizeof proj_slot[0].xp2);   /* a record from before them: 0 there */
    proj_slot[0].sum = proj_sum(&proj_slot[0]);
    proj_load(0, 0);
    check("a project from before: ATK 0, SUS 0, REL 40, LOOP OFF", trk[2].p[P_SATK] == 0 && trk[2].p[P_SSUS] == 0 &&
                                                                     trk[2].p[P_SREL] == 40 && trk[2].p[P_SLOOP] == 0);
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
    trk[0].p[P_SLOOP] = 2;
    trk[0].p[P_FENV] = -64;
    trk[0].p[P_REV] = 127;
    preset_user_save(5, &trk[0]);
    check("the record keeps its size (3204 B, 96 slots)", sizeof upr == 3204u);
    power_cycle();
    host_init();
    drum_set_model(&trk[3], DM_K909);
    preset_load(&trk[3], PRE_NFACT + 5u);
    check("a SAMPLE user preset brings ATK SUS REL LOOP back (after a power cycle)",
          trk[3].p[P_SATK] == 127 && trk[3].p[P_SSUS] == 1 && trk[3].p[P_SREL] == 0 && trk[3].p[P_SLOOP] == 2);
    check(".. and its FX as saved (FENV -64, REV 127)", trk[3].p[P_FENV] == -64 && trk[3].p[P_REV] == 127);
    preset_load(&trk[3], PRE_NFACT + 4u);
    check("a SAMPLE preset saved at the defaults loads them", trk[3].p[P_SATK] == 0 && trk[3].p[P_SSUS] == 0 &&
                                                                trk[3].p[P_SREL] == 40 && trk[3].p[P_SLOOP] == 0);
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
    t_project();
    t_presets();
    t_locks();
    printf("sampenv_test: %s (%d failed)\n", fails ? "FAILED" : "ok", fails);
    return fails ? 1 : 0;
}
