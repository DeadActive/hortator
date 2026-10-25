/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE (EXPERIMENT, branch usb8) */
/* Host test of the 10-channel USB audio experiment (FELUCCA_USB8=1: usb_app8.c, fx.c usb8_tap):
 *   descriptors  the 10-channel configuration parsed as a host does (lengths, wTotalLength, interfaces, the UAC1
 *                chain, 10 channels, 16 bit, 44.1 kHz, EP4 920 B <= 1023); byte for byte usb_app.c's except the
 *                channel count, the channel names, wMaxPacketSize and bcdDevice; the strings
 *   safe start   every descriptor request answered exactly as the frozen usb_app.c answers it (tests/usb8_ref.c)
 *   ring         renders at the I2S rate against one packet per 1 ms frame: 43..46 frames, <= 920 B, 44.1 on
 *                average, every frame of every channel delivered once, in order, in its place; underrun /
 *                overrun / restart; SAFE START's 2-channel packets
 *   mix          the tracks' channels add up to the master bus (pans centred, no sends, COMP off), follow MASTER,
 *                a muted track is silent; the ring carries the master and the tracks of audio_block
 *   cost         host instructions of the added work (emulated: the clear, mix_part's added line, the ring)
 *                against the whole audio block
 * The SIE is never touched (uac_service and usb_poll are not called). */
#define FELUCCA_UAC 1
#define FELUCCA_CDC 1
#define FELUCCA_USB8 1
#include "drum_host.h"
#define FM1_AUDIO_HALF 0x80u
#define FM1_TICKS_PER_US 24u
static uint8_t fm1_audio_pending(void) { return 0; }
static uint32_t fm1_ticks(void) { return 0; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return 0; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *b, uint32_t n, void (*f)(void), int pri) { (void)b; (void)n; (void)f; (void)pri; }
void isr_alnk0(void) {}
#include "../firmware/src/audio.c"

int ref_get_desc(uint32_t wvalue, const uint8_t **d, uint16_t *l);   /* usb8_ref.c: the frozen usb_app.c */

static int fails;
static int check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
    return ok;
}

static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }

static int str_is(const uint8_t *d, uint16_t l, const char *s)
{
    uint32_t n = (uint32_t)strlen(s), i;
    if (l != 2u + 2u * n || d[0] != l || d[1] != 3)
        return 0;
    for (i = 0; i < n; i++)
        if (d[2 + 2 * i] != (uint8_t)s[i] || d[3 + 2 * i] != 0)
            return 0;
    return 1;
}

/* ------------------------------------------------------------------ descriptors --- */
static void test_descriptors(void)
{
    const uint8_t *c, *d, *r;
    uint16_t n, rn, l;
    uint32_t off, nif = 0, i, ndiff = 0, diff[16];
    int lens_ok = 1, eps_ok = 1, it_ok = 0, ot_ok = 0, fmt_ok = 0, iso_ok = 0, ac_ok = 0, midi_ok = 0, iad_n = 0;
    int cur_if = -1, cur_neps = 0, got_eps = 0, ac_left = -1, ac_wt = -1;
    uint8_t seen_if[16] = {0}, if_class[16] = {0}, if_sub[16] = {0};
    uint32_t it_at = 0, fmt_at = 0, ep_at = 0;
    char s[120];

    safe_start = 0;
    check("10-channel mode outside SAFE START", usb8_mode() == 1);
    get_desc(0x0200, &c, &n);
    check("configuration: type 2, wTotalLength = the bytes sent", c[1] == 2 && le16(c + 2) == n && n == sizeof CFG8_DESC);
    for (off = 0; off < n;) {
        d = c + off;
        if (d[0] < 2 || off + d[0] > n) {
            lens_ok = 0;
            break;
        }
        if (ac_left > 0 && d[1] == 0x24)
            ac_left -= d[0];
        switch (d[1]) {
        case 0x0B:
            iad_n++;
            break;
        case 4:
            if (cur_if >= 0 && got_eps != cur_neps)
                eps_ok = 0;
            cur_if = d[2];
            cur_neps = d[4];
            got_eps = 0;
            if (cur_if < 16) {
                nif += !seen_if[cur_if];
                seen_if[cur_if] = 1;
                if_class[cur_if] = d[5];
                if_sub[cur_if] = d[6];
            }
            break;
        case 5:
            got_eps++;
            if (d[2] == 0x84) {
                ep_at = off;
                iso_ok = d[0] == 9 && d[3] == 0x05 && le16(d + 4) == UA_MAXF * 2u * UA_NCH && le16(d + 4) <= 1023u &&
                         d[6] == 1 && if_sub[cur_if] == 2;
            }
            if ((d[2] == 0x01 || d[2] == 0x81) && d[3] == 2 && if_sub[cur_if] == 3)
                midi_ok++;
            break;
        case 0x24:
            if (cur_if >= 0 && if_class[cur_if] == 1 && if_sub[cur_if] == 1) {
                if (d[2] == 1) {
                    ac_ok = d[0] == 8u + d[7] && le16(d + 3) == 0x0100u;
                    ac_left = ac_wt = (int)le16(d + 5);
                    ac_left -= d[0];
                } else if (d[2] == 2) {
                    it_at = off;
                    it_ok = d[0] == 12 && d[7] == UA_NCH && le16(d + 8) == 0 && d[10] == USB8_STR_CH;
                } else if (d[2] == 3) {
                    ot_ok = d[0] == 9 && le16(d + 4) == 0x0101u && d[7] == 1;
                }
            } else if (cur_if >= 0 && if_class[cur_if] == 1 && if_sub[cur_if] == 2 && d[2] == 2) {
                fmt_at = off;
                fmt_ok = d[0] == 11 && d[3] == 1 && d[4] == UA_NCH && d[5] == 2 && d[6] == 16 && d[7] == 1 &&
                         (d[8] | d[9] << 8 | d[10] << 16) == 44100;
            }
            break;
        }
        off += d[0];
    }
    if (cur_if >= 0 && got_eps != cur_neps)
        eps_ok = 0;
    check("descriptor lengths add up to wTotalLength", lens_ok && off == n);
    check("bNumInterfaces = the interfaces present (5: AC, MIDI, AS, CDC x2)", c[4] == nif && nif == 5u);
    check("each interface setting has bNumEndpoints endpoints", eps_ok);
    check("AC header 1.00; its wTotalLength = the class-specific AC descriptors", ac_ok && ac_left == 0 && ac_wt == 31);
    check("input terminal: 10 channels, no spatial positions, names from string 4", it_ok);
    check("output terminal: USB streaming, from the input terminal", ot_ok);
    check("type I format: 10 ch, 2-byte subframe, 16 bit, 44100 Hz only", fmt_ok);
    snprintf(s, sizeof s, "EP 0x84: isochronous async, %u B (46 frames x 20 B) <= 1023, every frame", UAC_MAXP);
    check(s, iso_ok);
    check("ep4tx holds the largest packet", sizeof ep4tx >= UAC_MAXP);
    check("MIDI bulk endpoints 0x01 / 0x81, two IADs", midi_ok == 2 && iad_n == 2);

    ref_get_desc(0x0200, &r, &rn);
    for (i = 0; i < n && i < rn; i++)
        if (c[i] != r[i] && ndiff < 16u)
            diff[ndiff++] = i;
    {   /* exactly: the terminal's channels, channel config (L R -> none), names; the format's channels; wMaxPacketSize */
        uint32_t want[7] = {it_at + 7, it_at + 8, it_at + 10, fmt_at + 4, ep_at + 4, ep_at + 5, 0}, k, ok = ndiff == 6;
        for (k = 0; ok && k < 6u; k++)
            ok = diff[k] == want[k];
        snprintf(s, sizeof s, "= usb_app.c's configuration but for those 6 bytes (%u differ)", ndiff);
        check(s, rn == n && ok);
    }
    get_desc(0x0100, &d, &l);
    ref_get_desc(0x0100, &r, &rn);
    for (i = 0, ndiff = 0; i < 18u; i++)
        ndiff += d[i] != r[i];
    check("device descriptor = usb_app.c's but bcdDevice 3.81 (was 3.11)", l == 18 && rn == 18 && ndiff == 1 &&
          d[12] == 0x81 && r[12] == 0x11 && d[13] == 3);
    get_desc(0x0302, &d, &l);
    check("product string \"Hortator 8ch\" (the installers match \"hortator\")", str_is(d, l, "Hortator 8ch"));
    {
        static const char *NAMES[UA_NCH] = {"Master L", "Master R", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8"};
        int ok = 1;
        for (i = 0; i < UA_NCH; i++)
            ok &= get_desc(0x0300 + USB8_STR_CH + i, &d, &l) && str_is(d, l, NAMES[i]);
        check("channel names, strings 4..13: Master L, Master R, T1..T8", ok);
    }
    check("string 14 and 3: none (stall)", !get_desc(0x0300 + USB8_STR_CH + UA_NCH, &d, &l) && !get_desc(0x0303, &d, &l));
}

static void test_safe_start(void)
{
    uint32_t t, i, same = 1, asked = 0;
    safe_start = 1;
    check("SAFE START: 2-channel mode", usb8_mode() == 0);
    for (t = 0; t < 0x100u; t++)
        for (i = 0; i < 24u; i++) {
            uint32_t w = t << 8 | i;
            const uint8_t *a = 0, *b = 0;
            uint16_t la = 0, lb = 0;
            int ra = get_desc(w, &a, &la), rb = ref_get_desc(w, &b, &lb);
            asked++;
            if (ra != rb || (ra && (la != lb || memcmp(a, b, la))))
                same = 0;
        }
    check("SAFE START: every GET_DESCRIPTOR answered as usb_app.c answers it", same && asked == 256u * 24u);
    safe_start = 0;
}

/* --------------------------------------------------------------------- the ring --- */
/* each channel of frame f carries its own code: master L = f, R = ~f, track k = f * 3 + k (16 bits) */
#define FS_DEV 44117.6
static uint32_t prod_n, cons_n, cons_bad, cons_rep, cons_zero, sizes[64], npk, max_bytes, last_frame;
static int32_t rblk[2 * CTL];
static uint16_t code(uint32_t f, uint32_t ch) { return ch == 0 ? (uint16_t)f : ch == 1 ? (uint16_t)~f : (uint16_t)(f * 3u + ch); }

static void render_block(void)
{
    uint32_t i, k;
    for (i = 0; i < CTL; i++, prod_n++) {
        rblk[2 * i] = (int16_t)code(prod_n, 0);
        rblk[2 * i + 1] = (int16_t)code(prod_n, 1);
        for (k = 0; k < NTRK; k++)
            usb8_tap[k][i] = (int16_t)code(prod_n, k + 2u);
    }
    uac_tap(rblk, CTL);
}

static void check_frames(const uint32_t *d, uint32_t n)   /* n frames as the host gets them */
{
    uint32_t i, ch, fw = usb8_mode() ? UA_W : 1u;
    sizes[n]++;
    npk++;
    if (n * fw * 4u > max_bytes)
        max_bytes = n * fw * 4u;
    for (i = 0; i < n; i++) {
        uint16_t h[2 * UA_W];                               /* the channels as the host reads them (little-endian) */
        for (ch = 0; ch < 2u * fw; ch++)
            h[ch] = (uint16_t)(d[i * fw + ch / 2u] >> (16u * (ch & 1u)));
        uint32_t f = h[0], ok = 1, zero = 1;
        for (ch = 0; ch < 2u * fw; ch++)
            zero &= h[ch] == 0;
        if (zero && (!uac.go || !cons_n)) {
            cons_zero++;
            continue;
        }
        for (ch = 0; ch < 2u * fw; ch++)
            ok &= h[ch] == code(f, ch);
        if (!ok) {
            if (zero) {
                cons_zero++;
                continue;
            }
            cons_bad++;
            continue;
        }
        if (cons_n && f == (uint16_t)last_frame)
            cons_rep++;
        else if (cons_n && f != (uint16_t)(last_frame + 1u))
            cons_bad++;
        last_frame = f;
        cons_n++;
    }
}

static void take_packet(void)
{
    uint32_t d[UA_MAXF * UA_W + 8], n = uac_packet(d);
    check_frames(d, n);
}

static uint64_t now_ns, next_half, next_pkt, next_block, rend_len;
static uint32_t blocks_left, lcg = 12345;
#define REND_MIN ((uint64_t)(HALF_FRAMES * 1e9 / FS_DEV * 0.07))
#define REND_MAX ((uint64_t)(HALF_FRAMES * 1e9 / FS_DEV * 0.85))

static void run(uint32_t ms, uint64_t rend_ns, int produce, int consume)
{
    uint64_t end = now_ns + (uint64_t)ms * 1000000u;
    while (now_ns < end) {
        uint64_t t = end;
        if (next_half < t)
            t = next_half;
        if (blocks_left && next_block < t)
            t = next_block;
        if (next_pkt < t)
            t = next_pkt;
        now_ns = t;
        if (now_ns >= end)
            break;
        if (now_ns == next_half) {
            next_half += (uint64_t)((double)HALF_FRAMES / FS_DEV * 1e9);
            if (produce) {
                lcg = lcg * 1103515245u + 12345u;
                rend_len = rend_ns ? rend_ns : REND_MIN + (lcg >> 8) % (REND_MAX - REND_MIN);
                uac_render_start();
                blocks_left = HALF_FRAMES / CTL;
                next_block = now_ns + rend_len / blocks_left;
            }
        } else if (blocks_left && now_ns == next_block) {
            render_block();
            blocks_left--;
            next_block += rend_len / (HALF_FRAMES / CTL);
        } else if (now_ns == next_pkt) {
            lcg = lcg * 1103515245u + 12345u;
            next_pkt = (now_ns / 1000000u + 1u) * 1000000u + (lcg >> 8) % 500000u;   /* in the next frame */
            if (consume)
                take_packet();
        }
    }
}

static void test_ring(void)
{
    uint32_t i, sum, base_n, base_p;
    char s[120];
    safe_start = 0;
    usb.config = 1;
    memset(&uac, 0, sizeof uac);
    ua_w = ua_r = 0;
    uac_stream(1);
    now_ns = 0;
    next_half = 300000;
    next_pkt = 1000000;
    run(3, 0, 1, 1);
    check("not fed before the host reads", uac.overruns == 0 && ua_w == 0);
    uac.flowing = 1;
    run(2000, 0, 1, 1);
    memset(sizes, 0, sizeof sizes);
    npk = max_bytes = 0;
    base_n = cons_n;
    base_p = uac.underruns + uac.overruns;
    run(10000, 0, 1, 1);
    for (i = sum = 0; i < 64u; i++)
        sum += sizes[i] * i;
    snprintf(s, sizeof s, "10 s, random renders: 43..46 frames (44:%u 45:%u 46:%u 43:%u), <= %u B", sizes[44], sizes[45],
             sizes[46], sizes[43], max_bytes);
    check(s, sizes[44] + sizes[45] + sizes[46] + sizes[43] == npk && max_bytes <= UAC_MAXP);
    snprintf(s, sizeof s, "  mean packet %.4f frames (I2S %.1f Hz), %.1f B/ms", (double)sum / npk, FS_DEV,
             (double)sum / npk * 20.0);
    check(s, (double)sum / npk > 44.05 && (double)sum / npk < 44.2);
    check("  every frame once, in order, all 10 channels in place; no underrun / overrun",
          cons_bad == 0 && cons_rep == 0 && uac.underruns + uac.overruns == base_p && cons_n - base_n == sum);
    snprintf(s, sizeof s, "  fill at render start %u..%u (band %u..%u)", uac.fill_lo, uac.fill_hi, UA_LO, UA_HI);
    check(s, uac.fill_lo >= 46u && uac.fill_hi + HALF_FRAMES <= UA_N);
    memset(sizes, 0, sizeof sizes);
    npk = 0;
    for (i = 0; i < 1000u; i++)
        run(1, REND_MAX, 1, 1);
    for (i = sum = 0; i < 64u; i++)
        sum += sizes[i] * i;
    snprintf(s, sizeof s, "1000 packets, %.1f ms renders (85 %%): mean %.3f, no glitch", REND_MAX / 1e6, (double)sum / npk);
    check(s, (double)sum / npk > 44.0 && (double)sum / npk < 44.25 && cons_bad == 0 && cons_rep == 0 &&
          uac.underruns + uac.overruns == base_p);
    base_p = uac.underruns;
    run(30, 0, 0, 1);
    check("producer stopped 30 ms: underruns counted, the last frame repeated (10 ch)", uac.underruns > base_p &&
          cons_bad == 0 && cons_rep > 0);
    base_p = uac.overruns;
    run(40, 0, 1, 0);
    check("consumer stopped 40 ms: overruns counted, the ring never overfills", uac.overruns > base_p && ua_w - ua_r <= UA_N);
    uac_stream(1);
    uac.flowing = 1;
    cons_n = cons_bad = cons_rep = 0;
    base_p = uac.underruns + uac.overruns;
    run(3000, 0, 1, 1);
    check("restart: primed again, in order, no glitch", cons_n > 3000u * 44u && cons_bad == 0 && cons_rep == 0 &&
          uac.underruns + uac.overruns == base_p);

    safe_start = 1;                                     /* SAFE START: usb_app.c's 2-channel packets from the ring */
    uac_stream(1);
    uac.flowing = 1;
    cons_n = cons_bad = cons_rep = 0;
    memset(sizes, 0, sizeof sizes);
    npk = max_bytes = 0;
    base_p = uac.underruns + uac.overruns;
    run(3000, 0, 1, 1);
    snprintf(s, sizeof s, "SAFE START: 2-channel packets <= %u B, every frame in order", max_bytes);
    check(s, max_bytes <= UA_MAXF * 4u && cons_n > 3000u * 44u && cons_bad == 0 && cons_rep == 0 &&
          uac.underruns + uac.overruns == base_p);
    safe_start = 0;
    uac_stream(0);
}

/* ---------------------------------------------------------------------- the mix --- */
static const uint8_t KIT[NTRK] = {DM_K808, DM_S808, DM_C808, DM_HATC, DM_HATO, DM_TOM, DM_RIM, DM_CYMB};

static uint32_t kit_mute;                              /* tracks muted (P_MUTE) before the hits */
static void kit_dry(uint32_t master, uint32_t vel)
{
    uint32_t k;
    host_init();
    for (k = 0; k < NTRK; k++) {
        old_set_model(&trk[k], KIT[k]);
        trk[k].p[P_PAN] = 0;
        trk[k].p[P_CHOR] = trk[k].p[P_DLY] = trk[k].p[P_REV] = 0;
        trk[k].p[P_MUTE] = (int16_t)((kit_mute >> k) & 1u);
    }
    song.g[G_CSRC] = 0;                                 /* COMP off */
    fx_usb_fixed = 0;
    song.master_q12 = master;
    for (k = 0; k < NTRK; k++)
        drum_hit(&trk[k], vel);
}

static void test_mix(void)
{
    static int32_t o[2 * CTL];
    uint32_t b, i, k, w0;
    int32_t maxe = 0, maxo = 0, pk[NTRK] = {0}, maxm = 0;
    double se = 0, sm = 0;
    char s[140];
    int ok;

    kit_dry(4096, 100);                                 /* the knob's top, all 8 hit together */
    for (b = 0; b < 400u; b++) {                        /* 290 ms */
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t sum = 0, e;
            for (k = 0; k < NTRK; k++) {
                sum += usb8_tap[k][i];
                pk[k] = abs(usb8_tap[k][i]) > pk[k] ? abs(usb8_tap[k][i]) : pk[k];
            }
            e = abs(sum - mix_l[i]) > abs(sum - mix_r[i]) ? sum - mix_l[i] : sum - mix_r[i];
            maxe = abs(e) > maxe ? abs(e) : maxe;
            maxm = abs(mix_l[i]) > maxm ? abs(mix_l[i]) : maxm;
            se += (double)e * e;
            sm += (double)mix_l[i] * mix_l[i];
        }
    }
    for (k = 0, ok = 1; k < NTRK; k++)
        ok &= pk[k] > 200;
    check("every track's channel carries its track", ok);
    snprintf(s, sizeof s, "T1+..+T8 = the master bus before the limiter: max error %d LSB (peak %d), %.1f dB", maxe,
             maxm, 10.0 * log10(se / sm));
    check(s, maxe <= 36 && se / sm < 1e-5);           /* floors: 8 x (>> 2) x MASTER/1024 + the >> 10s <= 36 */

    kit_dry(1024, 60);                                  /* MASTER a quarter, softly: master_out nearly linear */
    se = sm = 0;
    for (b = 0; b < 400u; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t sum = 0;
            for (k = 0; k < NTRK; k++)
                sum += usb8_tap[k][i];
            se += (double)(sum - o[2 * i]) * (sum - o[2 * i]);
            sm += (double)o[2 * i] * o[2 * i];
            maxo = abs(o[2 * i]) > maxo ? abs(o[2 * i]) : maxo;
        }
    }
    snprintf(s, sizeof s, "MASTER 1/4: T1+..+T8 vs the USB master L (its DC block, soft clip): %.1f dB (peak %d)",
             10.0 * log10(se / sm), maxo);
    check(s, se / sm < 1e-2 && maxo > 300);           /* close, not equal: the master stage is the master's only */

    {   /* the tracks follow MASTER as the master does */
        int32_t full[CTL], q[CTL];
        uint32_t same = 1;
        kit_dry(4096, 100);
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            full[i] = usb8_tap[0][i];
        kit_dry(1024, 100);
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            q[i] = usb8_tap[0][i];
            same &= abs(q[i] - full[i] / 4) <= 1;
        }
        check("MASTER at a quarter: the track channels at a quarter", same && abs(full[10]) > 100);
        kit_dry(1024, 100);
        fx_usb_fixed = 1;                               /* USB LEVEL FIXED: full level, whatever the knob */
        mix_block(o, CTL);
        for (i = 0, same = 1; i < CTL; i++)
            same &= usb8_tap[0][i] == full[i];
        check("USB LEVEL FIXED: the track channels at full level", same);
        fx_usb_fixed = 0;
    }
    {   /* a muted track (MUTE): silent on its channel, the others heard */
        int32_t m = 0;
        kit_mute = 1u << 2;                             /* T3 */
        kit_dry(4096, 100);
        kit_mute = 0;
        for (b = 0; b < 20u; b++) {
            mix_block(o, CTL);
            for (i = 0; i < CTL; i++)
                m = abs(usb8_tap[2][i]) > m ? abs(usb8_tap[2][i]) : m;
        }
        check("a muted track (T3): its channel silent", m == 0 && abs(usb8_tap[0][5]) + abs(usb8_tap[3][5]) > 0);
    }
    {   /* audio_block: the ring frame = the DAC's master block + the tracks */
        static int32_t out[2u * CTL];
        uint32_t good = 1;
        kit_dry(4096, 100);
        ua_w = ua_r = 0;
        usb.config = 1;
        uac.alt = uac.flowing = 1;
        uac.go = 0;
        uac_render_start();
        w0 = ua_w;
        audio_block(out, CTL);
        good = ua_w == w0 + CTL;
        for (i = 0; i < CTL; i++) {
            const uint32_t *f = &ua_ring[((w0 + i) & (UA_N - 1u)) * UA_W];
            good &= (int16_t)(f[0] & 0xFFFF) * (1 << OUT_SHIFT) == out[2 * i] &&
                    (int16_t)(f[0] >> 16) * (1 << OUT_SHIFT) == out[2 * i + 1];
            for (k = 0; k < NTRK; k++)
                good &= (int16_t)(k & 1u ? f[1 + k / 2] >> 16 : f[1 + k / 2] & 0xFFFF) == usb8_tap[k][i];
        }
        check("audio_block: ring frame = master L R (the DAC's block) + T1..T8", good);
        uac_stream(0);
    }
}

/* ---------------------------------------------------------------------- the cost --- */
static void test_cost(void)
{
    static int32_t out[2u * CTL], b[CTL];
    uint64_t i0, all, extra, two;
    uint32_t blk, k;
    const uint32_t N = 4000;                            /* 2.9 s of audio */
    char s[160];
    kit_dry(4096, 100);
    usb.config = 1;
    uac.alt = uac.flowing = 1;
    uac.go = 0;
    i0 = instr_now();
    for (blk = 0; blk < N; blk++) {
        if (blk % 8u == 0)
            uac_render_start(), ua_r = ua_w;            /* a reader that keeps up */
        if (blk % 200u == 0)
            for (k = 0; k < NTRK; k++)
                drum_hit(&trk[k], 100);
        audio_block(out, CTL);
    }
    all = instr_now() - i0;
    for (k = 0; k < CTL; k++)
        b[k] = (int32_t)(k * 999u) - 16000;
    i0 = instr_now();
    for (blk = 0; blk < N; blk++) {                     /* what USB8 adds: the clear, 8 taps, the 10-channel ring */
        uint32_t t, i;
        for (t = 0; t < NTRK; t++)
            for (i = 0; i < CTL; i++)
                usb8_tap[t][i] = 0;
        for (t = 0; t < NTRK; t++)
            for (i = 0; i < CTL; i++)
                usb8_tap[t][i] = usb8_s16(b[i], (int32_t)song.master_q12);   /* mix_part's added line */
        if (blk % 8u == 0)
            ua_r = ua_w;
        uac_tap(out, CTL);
    }
    extra = instr_now() - i0;
    i0 = instr_now();
    for (blk = 0; blk < N; blk++) {                     /* usb_app.c's tap writes a word a frame: the 10-ch ring, 1/5 */
        if (blk % 8u == 0)
            ua_r = ua_w;
        uac_tap(out, CTL);
    }
    two = (instr_now() - i0) / 5u;
    if (!all) {
        printf("cost: no instruction counter on this host (skipped)\n");
        return;
    }
    snprintf(s, sizeof s, "cost: audio block %.1f instr/sample (8 tracks), USB8 adds %.1f (%.1f %% of it), all 8 sounding",
             (double)all / (N * CTL), (double)(extra - two) / (N * CTL), 100.0 * (double)(extra - two) / (double)all);
    check(s, (double)(extra - two) / (double)all < 0.05);
}

/* ------------------------------------------------------------- the bus timing --- */
/* EP4 as the SIE runs it: one packet buffer. uac_service queues a packet (TxPktRdy) when the last one has gone; the
 * host's IN token, once per 1 ms frame, sends a queued packet, which keeps TxPktRdy set until its last byte is on the
 * wire (12 Mbit/s); a token that finds nothing queued gets a zero-length packet (UnderRun, uac.missed). The device
 * polls at its own clock (TIMER5, 30 ppm off the host's), at USB8_POLL_HZ plus usb_poll's 2 kHz call. The ring and
 * the packets are usb_app8.c's (uac_render_start, uac_tap, uac_packet); the polls mirror uac_service. */
#ifndef USB8_POLL_HZ
#define USB8_POLL_HZ 2000u                              /* (before the fix: usb_poll's 2 kHz only) */
#endif
#ifndef UAC_STALL_POLLS
#define UAC_STALL_POLLS 40u
#endif
static struct { uint32_t rdy, underrun, bytes, zlp, delivered; uint64_t busy_end; uint32_t d[UA_MAXF * UA_W]; } ep;

static void svc(void)                                   /* = uac_service, the SIE replaced by ep */
{
    uint32_t n;
    if (ep.rdy) {
        if (uac.wait < 255u && ++uac.wait == UAC_STALL_POLLS && uac.flowing) {
            uac.flowing = 0;
            uac.stalls++;
        }
        return;
    }
    if (uac.queued) {
        if (ep.underrun)
            uac.missed++;
        uac.flowing = 1;
    }
    uac.queued = 1;
    uac.wait = 0;
    n = uac_packet(ep.d);
    ep.bytes = n * (usb8_mode() ? 2u * UA_NCH : 4u);
    ep.rdy = 1;
    ep.underrun = 0;
}

static uint64_t wire_ns(uint32_t bytes)                 /* token + data packet (SYNC PID CRC EOP, ~3 % stuffing) */
{
    return (uint64_t)((4.0 + bytes + 4.0) * 8.0 * 1.03 / 12e6 * 1e9) + 1500u;   /* + the turnaround */
}

/* ms of bus time; poll_hz: the device's extra uac_service cadence; jit_ns: the token's place in its frame varies by
 * up to this (the host schedules it) */
static void run_bus(uint32_t ms, uint32_t poll_hz, uint32_t jit_ns, uint32_t phase_ns)
{
    uint64_t end = now_ns + (uint64_t)ms * 1000000u, next_tok = (now_ns / 1000000u + 1u) * 1000000u + 20000u;
    double tick = 1e9 / 10000.0 * (1.0 + 30e-6), next_tick = (double)now_ns + phase_ns;
    uint32_t sub = 0, every = 10000u / poll_hz;
    while (now_ns < end) {
        uint64_t t = end, tk = (uint64_t)next_tick;
        if (next_half < t)
            t = next_half;
        if (blocks_left && next_block < t)
            t = next_block;
        if (next_tok < t)
            t = next_tok;
        if (tk < t)
            t = tk;
        if (ep.rdy && ep.busy_end && ep.busy_end < t)
            t = ep.busy_end;
        now_ns = t;
        if (now_ns >= end)
            break;
        if (ep.rdy && ep.busy_end && now_ns == ep.busy_end) {   /* the last byte went: TxPktRdy clears */
            ep.rdy = 0;
            ep.busy_end = 0;
        } else if (now_ns == next_half) {
            next_half += (uint64_t)((double)HALF_FRAMES / FS_DEV * 1e9);
            lcg = lcg * 1103515245u + 12345u;
            rend_len = REND_MIN + (lcg >> 8) % (REND_MAX - REND_MIN);
            uac_render_start();
            blocks_left = HALF_FRAMES / CTL;
            next_block = now_ns + rend_len / blocks_left;
        } else if (blocks_left && now_ns == next_block) {
            render_block();
            blocks_left--;
            next_block += rend_len / (HALF_FRAMES / CTL);
        } else if (now_ns == next_tok) {                /* the host's IN token */
            lcg = lcg * 1103515245u + 12345u;
            next_tok = (now_ns / 1000000u + 1u) * 1000000u + 20000u + (jit_ns ? (lcg >> 8) % jit_ns : 0u);
            if (ep.rdy && !ep.busy_end) {
                ep.busy_end = now_ns + wire_ns(ep.bytes);
                check_frames(ep.d, ep.bytes / (usb8_mode() ? 2u * UA_NCH : 4u));
                ep.delivered += ep.bytes / (usb8_mode() ? 2u * UA_NCH : 4u);
            } else {
                ep.underrun = 1;
                ep.zlp++;
            }
        } else if (now_ns == tk) {                      /* TIMER5 */
            next_tick += tick;
            if (sub % 5u == 0)
                svc();                                  /* usb_poll's (2 kHz) */
            if (every && sub % every == 0)
                svc();                                  /* the extra cadence (USB8_POLL_HZ) */
            if (++sub == 10u)
                sub = 0;
        }
    }
}

static void bus_case(const char *what, uint32_t two, uint32_t poll_hz, uint32_t jit_ns, int expect_ok)
{
    uint32_t ph, rate_min = 1u << 31, bad = 0, ovr = 0, zlp = 0, sum = 0;
    char s[200];
    for (ph = 0; ph < 10u; ph++) {                      /* the device's poll phase against the host's frames */
        uint32_t d0, o0;
        safe_start = (uint8_t)two;                      /* SAFE START: usb_app.c's 2-channel stream */
        usb.config = 1;
        memset(&uac, 0, sizeof uac);
        memset(&ep, 0, sizeof ep);
        ua_w = ua_r = 0;
        uac_stream(1);
        cons_n = cons_bad = cons_rep = 0;
        now_ns = 0;
        next_half = 300000;
        blocks_left = 0;
        run_bus(500, poll_hz, jit_ns, ph * 10000u);     /* (the stream starts; flowing after the first packet) */
        d0 = ep.delivered;
        o0 = uac.overruns;
        cons_bad = cons_rep = 0;
        run_bus(4000, poll_hz, jit_ns, 0);
        if ((ep.delivered - d0) / 4u < rate_min)
            rate_min = (ep.delivered - d0) / 4u;
        sum += (ep.delivered - d0) / 4u;
        bad += cons_bad + cons_rep;
        ovr += uac.overruns - o0;
        zlp += ep.zlp;
    }
    safe_start = 0;
    snprintf(s, sizeof s, "%s: %u frames/s (worst phase %u), %u glitches, %u overruns, %u empty", what, sum / 10u,
             rate_min, bad, ovr, zlp);
    if (expect_ok)
        check(s, rate_min > 44050u && rate_min < 44200u && bad == 0 && ovr == 0);
    else
        check(s, rate_min < 43000u || bad || ovr);   /* the fault reproduced (the recording: ~39.7 k frames/s, tears) */
}

static void test_bus(void)
{
    uint64_t w = wire_ns(UAC_MAXP);
    char s[120];
    snprintf(s, sizeof s, "920 B on the wire: %.2f ms of the 1 ms frame (184 B: %.2f ms)", w / 1e6, wire_ns(184) / 1e6);
    check(s, w > 600000u && w < 700000u);
    bus_case("model check: 2 ch (184 B), 2 kHz (usb_app.c, works on the FM-1)", 1, 0, 100000u, 1);
    bus_case("before the fix: 10 ch (920 B), 2 kHz", 0, 0, 100000u, 0);
    bus_case("10 ch, + USB8_POLL_HZ, token jitter 0.1 ms", 0, USB8_POLL_HZ, 100000u, 1);
    bus_case("10 ch, + USB8_POLL_HZ, token jitter 0.25 ms", 0, USB8_POLL_HZ, 250000u, 1);
    bus_case("SAFE START 2 ch, + USB8_POLL_HZ", 1, USB8_POLL_HZ, 250000u, 1);
}

int main(void)
{
    printf("-- USB8: 10-channel USB audio (EXPERIMENT)\n");
    test_descriptors();
    test_safe_start();
    test_ring();
    test_mix();
    test_bus();
    test_cost();
    printf(fails ? "USB8 TEST FAILED (%d)\n" : "usb8: all ok\n", fails);
    return fails != 0;
}
