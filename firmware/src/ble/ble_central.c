/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* BLE-MIDI central on the JieLi SDK's ble_op_* API (reference: SDK apps/common/ble/le_net_central.c). The state
 * machine and the MIDI out queue at the top are host-tested (tests/ble/ble_central_test.c); the SDK glue below. */
#include "ble_scan.h"
enum { BLE_OFF, BLE_STARTING, BLE_IDLE, BLE_SCANNING, BLE_CONNECTING, BLE_DISCOVERING, BLE_SUBSCRIBED,
       BLE_RECEIVING, BLE_FAILED };
typedef enum { EV_INIT_OK, EV_INIT_FAIL, EV_SCAN_DONE, EV_CONNECTED, EV_CONN_FAIL, EV_FOUND_MIDI, EV_NO_MIDI,
               EV_SUBSCRIBED, EV_NOTIFY, EV_DISCONNECTED, EV_TIMEOUT } ble_ev_t;

uint32_t ble_next(uint32_t s, ble_ev_t ev)        /* the new state; unknown pairs keep the state */
{
    if (ev == EV_DISCONNECTED && s >= BLE_CONNECTING && s <= BLE_RECEIVING)
        return BLE_IDLE;
    switch (s) {
    case BLE_STARTING:
        return ev == EV_INIT_OK ? BLE_IDLE : (ev == EV_INIT_FAIL || ev == EV_TIMEOUT) ? BLE_FAILED : s;
    case BLE_SCANNING:
        return ev == EV_SCAN_DONE ? BLE_IDLE : s;
    case BLE_CONNECTING:
        return ev == EV_CONNECTED ? BLE_DISCOVERING : (ev == EV_CONN_FAIL || ev == EV_TIMEOUT) ? BLE_IDLE : s;
    case BLE_DISCOVERING:
        return ev == EV_FOUND_MIDI ? BLE_SUBSCRIBED : (ev == EV_NO_MIDI || ev == EV_TIMEOUT) ? BLE_IDLE : s;
    case BLE_SUBSCRIBED:
    case BLE_RECEIVING:
        return ev == EV_NOTIFY ? BLE_RECEIVING : s;
    default:
        return s;
    }
}

/* the BLE-MIDI characteristic 7772E5DB-3868-4112-A1A9-F2669D106BF3 (MMA BLE-MIDI 1.0). The SDK hands UUIDs over as
 * 16 bytes in an order its headers do not state, so both orders match. */
static const uint8_t MIDI_CHAR_UUID[16] = {0x77, 0x72, 0xE5, 0xDB, 0x38, 0x68, 0x41, 0x12, 0xA1, 0xA9, 0xF2, 0x66,
                                           0x9D, 0x10, 0x6B, 0xF3};
int ble_is_midi_char(const uint8_t u[16])
{
    uint32_t i, fwd = 1, rev = 1;
    for (i = 0; i < 16u; i++) {
        fwd &= u[i] == MIDI_CHAR_UUID[i];
        rev &= u[i] == MIDI_CHAR_UUID[15u - i];
    }
    return (int)(fwd | rev);
}

/* one monitor line for a USB-MIDI packet (cin | status << 8 | d1 << 16 | d2 << 24); 0 = no line (a SysEx start or
 * continuation: *sx counts its bytes, and the end packet prints them) */
static char *put_s(char *o, const char *s) { while (*s) *o++ = *s++; return o; }
static char *put_u(char *o, uint32_t v)
{
    char b[10];
    uint32_t n = 0;
    do b[n++] = (char)('0' + v % 10u); while (v /= 10u);
    while (n)
        *o++ = b[--n];
    return o;
}
static char *put_x(char *o, uint32_t v)
{
    static const char H[] = "0123456789ABCDEF";
    *o++ = H[v >> 4 & 15u];
    *o++ = H[v & 15u];
    return o;
}
static char *put_dev(char *o, const ble_dev_t *d)  /* the name, or the address most significant byte first */
{
    uint32_t i;
    if (d->name[0]) {
        o = put_s(o, d->name);
        *o = 0;
        return o;
    }
    for (i = 6; i--;) {
        o = put_x(o, d->addr[i]);
        if (i)
            *o++ = ':';
    }
    *o = 0;
    return o;
}
int ble_midi_text(uint32_t pkt, uint32_t *sx, char out[32])
{
    static const char *const NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    uint32_t cin = pkt & 15u, b0 = pkt >> 8 & 0xFFu, b1 = pkt >> 16 & 0xFFu, b2 = pkt >> 24, ch = (b0 & 15u) + 1u;
    char *o = out;
    if (cin == 4u) {
        *sx += 3u;
        return 0;
    }
    if (cin >= 5u && cin <= 7u && (*sx || cin > 5u || b0 == 0xF7u)) {
        o = put_u(put_s(o, "SYSEX "), *sx + cin - 4u);
        o = put_s(o, " BYTES");
        *sx = 0;
    } else if (cin == 9u || cin == 8u) {
        o = put_u(put_s(o, cin == 9u && b2 ? "NOTE ON  " : "NOTE OFF "), ch);
        o = put_s(put_s(o, " "), NOTE[b1 % 12u]);
        o = b1 < 12u ? put_s(o, "-1") : put_u(o, b1 / 12u - 1u);
        if (cin == 9u && b2)
            o = put_u(put_s(o, " v"), b2);
    } else if (cin == 0xBu) {
        o = put_u(put_s(put_u(put_s(put_u(put_s(o, "CC "), ch), " #"), b1), " = "), b2);
    } else if (cin == 0xCu) {
        o = put_u(put_s(put_u(put_s(o, "PC "), ch), " "), b1);
    } else if (cin == 0xEu) {
        int32_t v = (int32_t)(b2 << 7 | b1) - 8192;
        o = put_s(put_u(put_s(o, "PB "), ch), v < 0 ? " -" : " +");
        o = put_u(o, (uint32_t)(v < 0 ? -v : v));
    } else if (cin == 0xFu) {
        o = put_x(put_s(o, "RT "), b0);
    } else {
        o = put_x(put_s(put_x(put_s(put_x(put_s(o, "MIDI "), b0), " "), b1), " "), b2);
    }
    *o = 0;
    return 1;
}

/* ---- the console's `ble` words and the states they need (part 1b; pure, host-tested) ---- */
enum { BLE_CMD_STATUS, BLE_CMD_START, BLE_CMD_SCAN, BLE_CMD_LIST, BLE_CMD_CONNECT, BLE_CMD_STOP, BLE_CMD_BAD };

uint32_t ble_cmd_parse(const char *s, uint32_t *n)   /* the words after `ble`; *n = connect's device (1-based) */
{
    static const char *const W[] = {"start", "scan", "list", "connect", "stop"};
    uint32_t i, k;
    while (*s == ' ')
        s++;
    if (!*s)
        return BLE_CMD_STATUS;
    for (i = 0; i < 5u; i++) {
        for (k = 0; W[i][k] && s[k] == W[i][k]; k++)
            ;
        if (W[i][k] || (s[k] && s[k] != ' '))
            continue;
        s += k;
        while (*s == ' ')
            s++;
        if (i != 3u)
            return *s ? BLE_CMD_BAD : BLE_CMD_START + i;
        for (*n = 0, k = 0; k < 3u && s[k] >= '0' && s[k] <= '9'; k++)
            *n = *n * 10u + (uint32_t)(s[k] - '0');
        if (!k)
            return BLE_CMD_BAD;
        for (s += k; *s == ' '; s++)
            ;
        return *s || *n < 1u || *n > BLE_SCAN_MAX ? BLE_CMD_BAD : BLE_CMD_CONNECT;
    }
    return BLE_CMD_BAD;
}

/* 0: the command may run in this state; else why not (one console line). listed = the devices `ble list` shows */
const char *ble_cmd_refuse(uint32_t cmd, uint32_t state, uint32_t listed, uint32_t n)
{
    switch (cmd) {
    case BLE_CMD_START:
        return state == BLE_OFF ? 0 : state == BLE_FAILED ? "ble: the start failed; power off and on to retry"
                                                          : "ble: already started";
    case BLE_CMD_SCAN:
    case BLE_CMD_CONNECT:
        if (state == BLE_OFF)
            return "ble: not started (ble start)";
        if (state != BLE_IDLE)
            return "ble: busy (this needs IDLE)";
        return cmd == BLE_CMD_CONNECT && n > listed ? "ble: no such device (ble list)" : 0;
    case BLE_CMD_STOP:
        return state >= BLE_CONNECTING && state <= BLE_RECEIVING ? 0 : "ble: not connected";
    default:
        return 0;
    }
}

/* the main loop's period: the core's UI frame counter changes once per pass (main.c), ble_service sees it */
typedef struct { uint32_t frames, at_ms, last_ms, max_ms, seen; } ble_loop_t;
void ble_loop_note(ble_loop_t *l, uint32_t frames, uint32_t now_ms)
{
    if (l->seen && frames == l->frames)
        return;
    if (l->seen) {
        l->last_ms = now_ms - l->at_ms;
        if (l->last_ms > l->max_ms)
            l->max_ms = l->last_ms;
    }
    l->seen = 1;
    l->frames = frames;
    l->at_ms = now_ms;
}

/* MIDI out (ble_midi_send, called by the core's main loop): whole messages of 1..3 bytes, queued only while
 * connected; ble_service sends them (ble_out_flush). A disconnect empties the queue (ble_ev). */
static uint32_t st;                                 /* BLE_OFF ... BLE_FAILED */
static struct {
    uint8_t msg[64][3], len[64];
    uint32_t h, t, sent, drops;                     /* sent / drops: the console's `out` / `out_drops` */
} mo;
static int ble_linked(void) { return st == BLE_SUBSCRIBED || st == BLE_RECEIVING; }
static void ble_ev(ble_ev_t ev)                     /* every event's state change; the queue lives only while linked */
{
    st = ble_next(st, ev);
    if (!ble_linked())
        mo.t = mo.h;
}
int ble_midi_send(const uint8_t *m, uint32_t n)    /* 1 = queued, 0 = dropped (not connected, or the queue full) */
{
    uint32_t i;
    if (!ble_linked() || n - 1u > 2u)
        return 0;
    if (mo.h - mo.t >= 64u) {
        mo.drops++;
        return 0;
    }
    for (i = 0; i < n; i++)
        mo.msg[mo.h & 63u][i] = m[i];
    mo.len[mo.h & 63u] = (uint8_t)n;
    mo.h++;
    return 1;
}
uint32_t ble_midi_room(void) { return ble_linked() ? 64u - (mo.h - mo.t) : 0u; }   /* free entries; 0 unlinked */
/* the queue as packets while tx takes them (0 = sent): ble_service's ble_out_flush. A handler run inside tx may
 * empty the queue (a disconnect: ble_ev), so the tail moves only over messages still queued. */
static void ble_out_drain(uint32_t now_ms, int (*tx)(const uint8_t *pkt, uint32_t len))
{
    uint8_t pkt[20];                                /* the default ATT MTU's payload: at most 4 three-byte messages */
    while (mo.h != mo.t && ble_linked()) {
        uint32_t i = mo.t & 63u, n = mo.h - mo.t, len, taken;
        if (n > 64u - i)
            n = 64u - i;                            /* up to the ring's end: the rest in the next packet */
        len = ble_midi_pack(&mo.msg[i], &mo.len[i], n, now_ms, pkt, sizeof pkt, &taken);
        if (tx(pkt, len))
            return;                                 /* no room (or refused): the next pass */
        if (mo.h - mo.t < taken)
            return;                                 /* emptied by a disconnect inside tx */
        mo.t += taken;
        mo.sent += taken;
    }
}

/* ---- reconnecting (spec 3): one decision per ble_service pass, applied by ble_auto (the glue) ---- */
enum { BA_NONE, BA_SCAN, BA_CONNECT, BA_STOP_SCAN, BA_DISCONNECT };
typedef struct {
    uint8_t want;                                   /* the page's BLUETOOTH ON (ble_ui_want) */
    uint8_t paused;                                 /* the page's manual scan: no auto scan / connect until a pick */
    uint8_t have, addr[6], type;                    /* the remembered device (ble_ui_target) */
    uint8_t manual;                                 /* the running scan is the page's or the console's: never cut */
    uint8_t scan_req;                               /* ble_ui_scan: a manual scan once IDLE (after a disconnect) */
    uint8_t tried;                                  /* a connect tried since the last scan start: one per sighting */
    uint8_t console;                                /* the link is the console's `ble connect`: want leaves it */
    uint8_t dropping;                               /* a disconnect asked for this link: not again */
    uint8_t sighted;                                /* the target listed in this scan, since sighted_ms */
    uint32_t seen;                                  /* the state at the last ble_watch */
    uint32_t sighted_ms;
    uint32_t rest_ms;                               /* the last scan end / link drop: auto scans 2 s after it */
} ble_auto_t;
static int ble_live(uint32_t s) { return s >= BLE_CONNECTING && s <= BLE_RECEIVING; }
/* how ble_disconnect asks: CONNECTING cancels the connect (con_handle may still hold a failed link's handle until
 * its disconnection-complete event); DISCOVERING..RECEIVING disconnect the handle */
enum { BD_NONE, BD_CANCEL, BD_DISC };
uint32_t ble_disc_how(uint32_t s, uint32_t handle)
{
    return s == BLE_CONNECTING ? BD_CANCEL : ble_live(s) && handle ? BD_DISC : BD_NONE;
}
/* target_listed: 0 no, 1 listed without a name yet, 2 listed with its name. An auto scan stops for the target once
 * its name is in (the scan response, which AUTO's SMC test reads at link up) or 500 ms after its first listing */
uint32_t ble_auto_step(const ble_auto_t *a, uint32_t state, int target_listed, uint32_t now_ms)
{
    if (state == BLE_IDLE && a->scan_req)
        return BA_SCAN;
    if (!a->want)
        return ble_live(state) && !a->console && !a->dropping ? BA_DISCONNECT : BA_NONE;
    if (!a->have || a->paused)
        return BA_NONE;
    if (state == BLE_IDLE)
        return target_listed && !a->tried ? BA_CONNECT : now_ms - a->rest_ms >= 2000u ? BA_SCAN : BA_NONE;
    if (state == BLE_SCANNING && !a->manual && target_listed && !a->tried &&
        (target_listed > 1 || (a->sighted && now_ms - a->sighted_ms >= 500u)))
        return BA_STOP_SCAN;
    return BA_NONE;
}
void ble_sight(ble_auto_t *a, int listed, uint32_t now_ms)   /* ble_service, scanning: the target's first listing */
{
    if (listed && !a->sighted) {
        a->sighted = 1;
        a->sighted_ms = now_ms;
    }
}
/* ble_service, before the step: link up / down to the core (core_ble_link, once each), the rest times, the flags
 * that end with a scan or a link */
static void ble_watch(ble_auto_t *a, uint32_t now_ms)
{
    uint32_t was = a->seen, up = ble_linked(), wasup = was == BLE_SUBSCRIBED || was == BLE_RECEIVING;
    if (up != wasup)
        core_ble_link((int)up);
    if (st == BLE_SCANNING && was != BLE_SCANNING)
        a->tried = a->sighted = 0;
    if (was == BLE_SCANNING && st != BLE_SCANNING) {
        a->rest_ms = now_ms;
        a->manual = 0;
    }
    if (ble_live(was) && !ble_live(st))
        a->rest_ms = now_ms;
    if (!ble_live(st))
        a->console = a->dropping = 0;
    a->seen = st;
}

#ifndef BLE_CENTRAL_STATE_ONLY
/* ---- the SDK glue: in ble.c's unit, after ble_heap / ble_on / ble_mac. The stack calls ble_profile_init and the
 * three handlers below from its own task (ble_os.c); the main loop calls ble_start / ble_scan / ble_connect /
 * ble_disconnect / ble_tick. Both run on the same CPU, one at a time, so they share these without locking. ---- */
#include "ble/ble_sdk.h"
#define ATT_MTU 128u                                /* le_net_central.c ATT_LOCAL_MTU_SIZE */

static ble_scan_t scan;
static ble_midi_t bm;
static uint16_t con_handle, midi_handle;
static uint8_t conn_dev;                            /* scan.dev index of the device connected to */
static uint32_t deadline_ms;
static const char *err_text = "";                   /* the last error (ble_err) */
static uint32_t err_code;
static uint8_t att_ram[BLE_SDK_ATT_CTRL_BLOCK_SIZE + ATT_MTU + 512u] __attribute__((aligned(4)));
static uint8_t search_ram[512] __attribute__((aligned(4)));
static uint32_t ring[64], ring_h, ring_t, ring_lost;   /* BLE-MIDI as USB-MIDI packets, to the main loop */

uint32_t ble_state(void) { return st; }
const char *ble_err(uint32_t *code) { *code = err_code; return err_text; }
static void set_err(const char *text, uint32_t code) { err_text = text; err_code = code; }
static void set_deadline(uint32_t ms) { deadline_ms = core_ms() + ms; }

static void ring_put(void *ctx, uint32_t pkt)
{
    (void)ctx;
    if (ring_h - ring_t < 64u)
        ring[ring_h++ & 63u] = pkt;
    else
        ring_lost++;
}
int ble_midi_take(uint32_t *pkt)
{
    if (ring_h == ring_t)
        return 0;
    *pkt = ring[ring_t++ & 63u];
    return 1;
}

static void cbk_packet_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)channel;
    (void)size;
    if (type != BLE_SDK_HCI_EVENT_PACKET)
        return;
    switch (packet[0]) {
    case BLE_SDK_GAP_EVENT_ADVERTISING_REPORT:  /* one report: le_net_central.c 960 */
        if (st == BLE_SCANNING) {
            const adv_report_t *r = (const adv_report_t *)(const void *)&packet[2];
            ble_scan_report(&scan, r->address, r->address_type, r->rssi, r->data, r->length);
        }
        break;
    case BLE_SDK_HCI_EVENT_LE_META:              /* le_net_central.c 870-905: status packet[3], handle packet[4..5] */
        if (packet[2] != BLE_SDK_HCI_SUBEVENT_LE_CONNECTION_COMPLETE &&
            packet[2] != BLE_SDK_HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE)
            break;
        if (packet[3]) {
            if (st == BLE_CONNECTING) {
                set_err("CONNECT FAILED", packet[3]);
                ble_ev(EV_CONN_FAIL);
            }
            break;
        }
        con_handle = (uint16_t)(packet[4] | packet[5] << 8);
        if (st != BLE_CONNECTING) {              /* completed after a cancel: not wanted */
            ble_op_disconnect(con_handle);
            break;
        }
        midi_handle = 0;
        ble_midi_reset(&bm);
        ble_op_att_send_init(con_handle, att_ram, sizeof att_ram, ATT_MTU);
        user_client_init(con_handle, search_ram, sizeof search_ram);
        ble_op_search_profile_all();
        ble_ev(EV_CONNECTED);
        set_deadline(10000);
        break;
    case BLE_SDK_HCI_EVENT_DISCONNECTION_COMPLETE:   /* le_net_central.c 917: reason packet[5] */
        if (st >= BLE_CONNECTING && st <= BLE_RECEIVING)
            set_err("DISCONNECTED", packet[5]);
        con_handle = 0;
        midi_handle = 0;
        ble_op_att_send_init(0, 0, 0, 0);
        ble_ev(EV_DISCONNECTED);
        break;
    default:
        break;
    }
}

/* called by the stack while it starts (btstack_main.c, before BT_STATUS_INIT_OK): le_net_central.c 1308-1330,
 * central only, no pairing required (a peripheral that asks still gets Just Works) */
void ble_profile_init(void)
{
    ble_stage(6);
    le_device_db_init();
    ble_stack_gatt_role(1);
    sm_init();
    sm_set_io_capabilities(BLE_SDK_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(0);
    sm_set_encryption_key_size_range(7, 16);
    sm_set_master_request_pair(0);
    gatt_client_init();
    gatt_client_register_packet_handler(cbk_packet_handler);
    hci_event_callback_set(cbk_packet_handler);
    le_l2cap_register_packet_handler(cbk_packet_handler);
    ble_vendor_set_default_att_mtu(ATT_MTU);
}

static void ble_on_event(int from, struct bt_event *e)  /* bt_event_notify (ble_port.c) */
{
    if (from == BLE_SDK_BT_EVENT_FROM_CON && e->event == BLE_SDK_BT_STATUS_INIT_OK && st == BLE_STARTING) {
        ble_stage(7);
        ble_ev(EV_INIT_OK);
    }
}

/* the stack's GATT client reports (client_user.c weak defaults): le_net_central.c 386-415 and 453-480 */
void user_client_report_search_result(search_result_t *r)
{
    if (st != BLE_DISCOVERING)
        return;
    if (r == 0 || r == (search_result_t *)-1) {  /* (void *)-1: the search is over */
        if (midi_handle) {
            static const uint8_t on[2] = {1, 0};   /* notifications on: the CCCD after the value */
            ble_op_att_send_data(midi_handle + 1u, (void *)on, 2, BLE_SDK_ATT_OP_WRITE);
            ble_ev(EV_FOUND_MIDI);
        } else {
            set_err("NO BLE-MIDI SERVICE", 0);
            ble_op_disconnect(con_handle);
            ble_ev(EV_NO_MIDI);
        }
        return;
    }
    if (!r->characteristic.uuid16 && ble_is_midi_char(r->characteristic.uuid128))
        midi_handle = r->characteristic.value_handle;
}

void user_client_report_data_callback(att_data_report_t *d)
{
    if (d->packet_type == BLE_SDK_GATT_EVENT_NOTIFICATION && midi_handle && d->value_handle == midi_handle &&
        (st == BLE_SUBSCRIBED || st == BLE_RECEIVING)) {
        ble_midi_parse(&bm, d->blob, d->blob_length, ring_put, 0);
        ble_ev(EV_NOTIFY);
    }
}

/* ---- the main loop's side ---- */
void ble_start(void)
{
    uint32_t i, x;
    if (st != BLE_OFF)
        return;
    if (!ble_rf_trim_load()) {                     /* no stored radio calibration: do not start (the trim hangs) */
        set_err("NO RF TRIM IN FLASH", 0);
        st = BLE_FAILED;
        return;
    }
    x = fm1_ticks() ^ core_ms() * 2654435761u;    /* the time of the key press */
    for (i = 0; i < 6u; i++) {
        x = x * 1103515245u + 12345u;
        ble_mac[i] = (uint8_t)(x >> 16);
    }
    ble_mac[0] |= 0xC0u;                           /* random static: the top two bits set, at whichever end */
    ble_mac[5] |= 0xC0u;                           /* the stack keeps the most significant byte */
    ble_os_init(ble_heap, sizeof ble_heap);
    ble_delay_calibrate();                         /* (counted-loop delays for the radio calibration) */
    ble_os_trace = &ble_diag.task;
    ble_stage(1);
    core_ble_stack_window();                       /* the task stacks are in .pool: inside the stack-limit window */
    ble_stage(2);
    ble_on = 1;
    st = BLE_STARTING;
    set_deadline(5000);
    le_controller_set_mac(ble_mac);
    ble_stage(3);
    ble_os_after_run = ble_irq_check;
    task_create(ble_app_core, 0, "app_core");      /* stages 4-5 there: btstack_init in a task */
}

void ble_scan(void)
{
    if (st != BLE_IDLE)
        return;
    ble_scan_clear(&scan);
    ble_op_set_scan_param(BLE_SDK_SCAN_ACTIVE, 48, 48);   /* 30 ms window every 30 ms: continuous */
    ble_op_scan_enable2(1, 0);
    st = BLE_SCANNING;
    set_deadline(10000);
}

void ble_connect(uint32_t sorted_index)
{
    static struct create_conn_param_t c;           /* (static, as the SDK's scan_buffer) */
    uint8_t idx[BLE_SCAN_MAX];
    uint32_t n = ble_scan_sorted(&scan, idx);
    if (st != BLE_IDLE || sorted_index >= n)
        return;
    conn_dev = idx[sorted_index];
    c.conn_interval = 12;                          /* 15 ms */
    c.conn_latency = 0;
    c.supervision_timeout = 400;                   /* 4 s */
    c.peer_address_type = scan.dev[conn_dev].addr_type;
    memcpy(c.peer_address, scan.dev[conn_dev].addr, 6);
    set_err("", 0);
    ble_op_create_connection(&c);
    st = BLE_CONNECTING;
    set_deadline(10000);
}

void ble_disconnect(void)
{
    uint32_t how = ble_disc_how(st, con_handle);
    if (how == BD_CANCEL) {
        ble_op_create_connection_cancel();
        ble_ev(EV_CONN_FAIL);
    } else if (how == BD_DISC) {
        ble_op_disconnect(con_handle);
    }
}

static void ble_tick(void)                         /* the deadlines of the waiting states */
{
    if (st != BLE_STARTING && st != BLE_SCANNING && st != BLE_CONNECTING && st != BLE_DISCOVERING)
        return;
    if ((int32_t)(core_ms() - deadline_ms) < 0)
        return;
    switch (st) {
    case BLE_SCANNING:
        ble_op_scan_enable2(0, 0);
        ble_ev(EV_SCAN_DONE);
        break;
    case BLE_STARTING:
        set_err("BT START TIMEOUT", 0);
        ble_ev(EV_TIMEOUT);
        break;
    case BLE_CONNECTING:
        ble_op_create_connection_cancel();
        set_err("CONNECT TIMEOUT", 0);
        ble_ev(EV_TIMEOUT);
        break;
    default:                                       /* BLE_DISCOVERING */
        ble_op_disconnect(con_handle);
        set_err("DISCOVERY TIMEOUT", 0);
        ble_ev(EV_TIMEOUT);
        break;
    }
}

static int ble_out_tx(const uint8_t *pkt, uint32_t len)   /* ble_out_drain's sender: 0 = sent */
{
    uint32_t room = 0;
    return !midi_handle || ble_op_att_get_remain(&room) || room < len ||
           ble_op_att_send_data(midi_handle, (void *)pkt, len, BLE_SDK_ATT_OP_WRITE_WITHOUT_RESPOND);
}
static void ble_out_flush(void) { ble_out_drain(core_ms(), ble_out_tx); }   /* ble_service: MIDI out */

/* ---- reconnecting and the Bluetooth page's entry points (ble_core.h ble_ui_*; the core's main loop). None of
 * them starts the stack: the page calls ble_user_start (gate 3). ---- */
_Static_assert(BLE_ST_OFF == BLE_OFF && BLE_ST_STARTING == BLE_STARTING && BLE_ST_IDLE == BLE_IDLE &&
               BLE_ST_SCANNING == BLE_SCANNING && BLE_ST_CONNECTING == BLE_CONNECTING &&
               BLE_ST_DISCOVERING == BLE_DISCOVERING && BLE_ST_SUBSCRIBED == BLE_SUBSCRIBED &&
               BLE_ST_RECEIVING == BLE_RECEIVING && BLE_ST_FAILED == BLE_FAILED, "ble_core.h's states");
static ble_auto_t au;
static uint32_t ble_listed(void)
{
    uint8_t idx[BLE_SCAN_MAX];
    return ble_scan_sorted(&scan, idx);
}
static int ble_target_at(int *named)               /* the target's index in ble_scan_sorted's order; -1 not listed */
{
    uint8_t idx[BLE_SCAN_MAX];
    uint32_t i, n;
    *named = 0;
    if (!au.have)
        return -1;                                 /* (no sort per pass without a target) */
    n = ble_scan_sorted(&scan, idx);
    for (i = 0; i < n; i++)
        if (scan.dev[idx[i]].addr_type == au.type && !memcmp(scan.dev[idx[i]].addr, au.addr, 6)) {
            *named = scan.dev[idx[i]].name[0] != 0;
            return (int)i;
        }
    return -1;
}
static void ble_drop(void)                         /* disconnect the link or cancel the connect, once */
{
    if (ble_live(st) && !au.dropping) {
        au.dropping = 1;
        ble_disconnect();
    }
}
static void ble_scan_stop(void)
{
    if (st == BLE_SCANNING) {
        ble_op_scan_enable2(0, 0);
        ble_ev(EV_SCAN_DONE);
    }
}
static void ble_auto(void)                         /* ble_service: the step applied */
{
    int named, at = ble_target_at(&named);
    ble_watch(&au, core_ms());
    if (st == BLE_SCANNING)
        ble_sight(&au, at >= 0, core_ms());
    switch (ble_auto_step(&au, st, at < 0 ? 0 : named ? 2 : 1, core_ms())) {
    case BA_SCAN:
        au.manual = au.scan_req;
        au.scan_req = 0;
        ble_scan();
        break;
    case BA_CONNECT:                               /* (BA_CONNECT only when listed: at >= 0) */
        au.tried = 1;
        ble_connect((uint32_t)at);
        break;
    case BA_STOP_SCAN:                             /* IDLE now: the next pass connects */
        ble_scan_stop();
        break;
    case BA_DISCONNECT:
        ble_drop();
        break;
    default:
        break;
    }
}
uint32_t ble_ui_state(void) { return st; }
const char *ble_ui_err(uint32_t *code) { return ble_err(code); }
uint32_t ble_ui_scan_left(void)                    /* whole seconds, rounded up */
{
    int32_t left = (int32_t)(deadline_ms - core_ms());
    return st == BLE_SCANNING && left > 0 ? ((uint32_t)left + 999u) / 1000u : 0;
}
uint32_t ble_ui_devs(void) { return ble_listed(); }
int ble_ui_dev(uint32_t i, ble_ui_dev_t *out)
{
    uint8_t idx[BLE_SCAN_MAX];
    const ble_dev_t *d;
    if (i >= ble_scan_sorted(&scan, idx))
        return 0;
    d = &scan.dev[idx[i]];
    memcpy(out->addr, d->addr, 6);
    out->type = d->addr_type;
    out->midi = d->midi;
    out->rssi = d->rssi;
    memcpy(out->name, d->name, sizeof out->name);
    return 1;
}
void ble_ui_scan(void)                             /* a manual scan once IDLE; pauses reconnecting */
{
    au.paused = au.scan_req = 1;
    ble_scan_stop();                               /* (a running scan restarts: the full 10 s, a fresh list) */
    ble_drop();
}
void ble_ui_target(const uint8_t addr[6], uint32_t type)
{
    uint32_t i, any = 0;
    for (i = 0; i < 6u; i++)
        any |= addr[i];
    au.have = any != 0;
    memcpy(au.addr, addr, 6);
    au.type = (uint8_t)type;
    au.paused = au.manual = au.tried = 0;          /* a pick ends the pause; a running scan stops once it is seen */
    if (ble_live(st) && (!au.have || scan.dev[conn_dev].addr_type != au.type ||
                         memcmp(scan.dev[conn_dev].addr, au.addr, 6)))
        ble_drop();                                /* another device than the one connected */
}
void ble_ui_want(int on)
{
    au.want = on != 0;
    if (!on) {
        au.scan_req = 0;
        ble_scan_stop();
        ble_drop();
    }
}
void ble_ui_pause(int on)
{
    au.paused = on != 0;
    if (!on)
        au.manual = 0;                             /* the page closed: the list need not stay */
}
int ble_ui_conn_name(char out[24])
{
    if (!ble_linked())
        return 0;
    memcpy(out, scan.dev[conn_dev].name, 24);      /* (no scan while connected: the entry stays) */
    return 1;
}

/* ---- the console's `ble` (moved here from fm1-lsdj's ble_ui.c: Hortator has no BLE screen) ---- */
static const char *const STATE_NAME[] = {"OFF", "STARTING", "IDLE", "SCANNING", "CONNECTING", "DISCOVERING",
                                         "SUBSCRIBED", "RECEIVING", "FAILED"};
static char *put_i(char *o, int32_t v) { if (v < 0) { *o++ = '-'; v = -v; } return put_u(o, (uint32_t)v); }
static ble_loop_t loop;                             /* ble_service (ble.c): the main loop's period */
void ble_list(void)                                /* console `ble list`: numbered as `ble connect N` takes them */
{
    char b[96], *o;
    uint32_t i, n;
    uint8_t idx[BLE_SCAN_MAX];
    n = ble_scan_sorted(&scan, idx);
    if (!n)
        core_con_puts("  (no devices: ble scan)\r\n");
    for (i = 0; i < n; i++) {
        const ble_dev_t *d = &scan.dev[idx[i]];
        uint32_t k;
        o = put_s(put_u(put_s(b, "  "), i + 1u), "  ");
        for (k = 6; k--;)
            o = put_x(o, d->addr[k]);
        o = put_u(put_s(o, " t"), d->addr_type);
        o = put_i(put_s(o, " "), d->rssi);
        o = put_s(o, d->midi ? " MIDI " : " ");
        o = put_s(o, d->name);
        o = put_s(o, "\r\n");
        *o = 0;
        core_con_puts(b);
    }
}
void ble_status(void)                              /* console `ble` */
{
    char b[224], *o;
    uint32_t code, i;
    const char *err = ble_err(&code);
    o = put_s(put_s(b, "ble: state "), STATE_NAME[st]);
    o = put_u(put_s(o, " reports "), scan.reports);
    o = put_u(put_s(o, " msgs "), bm.msgs);
    o = put_u(put_s(o, " pkts "), bm.packets);
    o = put_u(put_s(o, " errs "), bm.errors);
    o = put_u(put_s(o, " lost "), ring_lost);
    o = put_u(put_s(o, " out "), mo.sent);
    o = put_u(put_s(o, " out_drops "), mo.drops);
    o = put_u(put_s(o, " in_drops "), core_ble_in_drops());
    o = put_u(put_s(o, " audio_late "), core_audio_late());
    o = put_s(o, "\r\n");
    *o = 0;
    core_con_puts(b);
    o = put_s(b, "  rf trim ");
    o = ble_rf_trim_at ? put_x(put_x(put_x(put_s(o, "stored, in the block at 0x"), ble_rf_trim_at >> 16), ble_rf_trim_at >> 8),
                               ble_rf_trim_at) : put_s(o, "not loaded");
    o = put_s(o, "\r\n");
    *o = 0;
    core_con_puts(b);
    o = put_s(b, "  mac ");
    for (i = 6; i--;)
        o = put_x(o, ble_mac[i]);
    if (err[0])
        o = put_x(put_s(put_s(put_s(o, "  error "), err), " 0x"), code);
    o = put_s(o, "\r\n");
    *o = 0;
    core_con_puts(b);
    if (ble_diag.magic == BLE_DIAG_MAGIC) {     /* kept over a reboot: where the last start got to */
        o = put_u(put_s(b, "  starts "), ble_diag.starts);
        o = put_u(put_s(o, " stage "), ble_diag.stage);
        o = put_u(put_s(o, " task "), ble_diag.task);
        o = put_u(put_s(o, " trim "), ble_diag.trim);
        o = put_u(put_s(o, " delays "), ble_diag.delays);
        o = put_u(put_s(o, " last_us "), ble_diag.last_us);
        o = put_u(put_s(o, " t4_stalls "), ble_diag.t4_stalls);
        o = put_u(put_s(o, " irq_leaks "), ble_diag.irq_leaks);
        o = put_u(put_s(o, " loops/us "), ble_loops_per_us);
        o = put_x(put_x(put_x(put_x(put_s(o, " irqs 0x"), ble_irqs_requested[1] >> 8), ble_irqs_requested[1]),
                        ble_irqs_requested[0] >> 24), ble_irqs_requested[0] >> 16);
        o = put_x(put_x(o, ble_irqs_requested[0] >> 8), ble_irqs_requested[0]);
        o = put_s(o, "\r\n");
        *o = 0;
        core_con_puts(b);
        o = put_u(put_s(b, "  fatal "), ble_diag.fatal);   /* 1 assert, 2 reset request, 3 a stack overflow */
        o = put_u(put_s(o, " irq_lowered "), ble_diag.irq_lowered);
        o = put_s(o, "\r\n");
        *o = 0;
        core_con_puts(b);
    }
    if (fm1_crash.magic == 0x43525348u) {        /* the core's crash record (fm1_irq.h), all of it */
        static const char *const F[] = {"vec", "pc", "rets", "emu", "dbg", "sp", "psr", "icfg", "uptime_ms",
                                        "etm0", "etm1", "etm2", "etm3", "early"};
        const uint32_t *v = &fm1_crash.vec;
        core_con_puts("  crash");
        for (i = 0; i < 14u; i++) {
            o = put_s(put_s(put_s(b, " "), F[i]), " ");
            o = put_x(put_x(put_x(put_x(o, v[i] >> 24), v[i] >> 16), v[i] >> 8), v[i]);
            *o = 0;
            core_con_puts(b);
        }
        core_con_puts("\r\n");
    }
    if (ble_diag.magic == BLE_DIAG_MAGIC) {     /* (.noinit: nothing to show before the first start) */
        o = put_u(put_s(b, "  heap high "), ble_diag.heap_high);
        o = put_u(put_s(o, " of "), sizeof ble_heap);
        o = put_u(put_s(o, ", failed allocations "), ble_diag.alloc_fails);
        o = put_s(o, " (kept over a reboot)\r\n");
        *o = 0;
        core_con_puts(b);
    } else {
        core_con_puts("  heap: no record yet (ble start)\r\n");
    }
    o = put_u(put_s(b, "  loop last "), loop.last_ms);
    o = put_u(put_s(o, " ms, max "), loop.max_ms);
    o = put_u(put_s(o, " ms; longest BT task run "), ble_os_run_max_us(1));
    o = put_s(o, " us (both since the last `ble`)\r\n");
    *o = 0;
    core_con_puts(b);
    loop.max_ms = 0;
    for (i = 0; i < 4u; i++) {                     /* the task stacks' unused bytes */
        const char *name;
        uint32_t f = ble_os_stack_free(i, &name);
        if (!name)
            continue;
        o = put_u(put_s(put_s(put_s(b, "  task "), name), " stack free "), f);
        o = put_s(o, "\r\n");
        *o = 0;
        core_con_puts(b);
    }
    ble_list();
}

/* ---- the console (part 1b): the core's `ble …` line comes here (ble_core.h ble_console) ---- */
/* the user's trigger, the only way to ble_start (gate 3: tools/check_ble_boot.py); not inlined, so the gate sees it */
__attribute__((noinline)) void ble_user_start(void) { ble_start(); }
static void say_state(void)
{
    char b[96], *o;
    uint32_t code;
    const char *err = ble_err(&code);
    o = put_s(put_s(b, "ble: "), STATE_NAME[st]);
    if (err[0])
        o = put_x(put_s(put_s(put_s(o, "  error "), err), " 0x"), code);
    o = put_s(o, "\r\n");
    *o = 0;
    core_con_puts(b);
}
void ble_console(const char *args)
{
    uint32_t n = 0, cmd = ble_cmd_parse(args, &n);
    const char *no = ble_cmd_refuse(cmd, st, ble_listed(), n);
    if (no) {
        core_con_puts(no);
        core_con_puts("\r\n");
        return;
    }
    switch (cmd) {
    case BLE_CMD_STATUS: ble_status(); return;
    case BLE_CMD_LIST: ble_list(); return;
    case BLE_CMD_START: ble_user_start(); break;
    case BLE_CMD_SCAN: au.manual = 1; ble_scan(); break;          /* (the console's scan runs its 10 s) */
    case BLE_CMD_CONNECT:                          /* want leaves it; a drop reconnects after a scan (tried) */
        au.console = au.tried = 1;
        ble_connect(n - 1u);
        break;
    case BLE_CMD_STOP: ble_disconnect(); break;
    default:
        core_con_puts("ble [start | scan | list | connect N | stop]\r\n");
        return;
    }
    say_state();
}
#endif
