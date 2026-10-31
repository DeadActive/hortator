/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE
 * Protocol shape after upstream Felucca's editor protocol (hugelton/Felucca 5d9694d, editor.c / EDITOR_PROTOCOL.md,
 * GPL-3.0-only, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments). */
/* Hortator editor protocol 1: the constants the firmware's editor files share (web/EDITOR_PROTOCOL.md is the
 * specification; web/editor/proto.mjs the page's side of the same numbers, web/editor/test_editor.mjs checks both
 * agree). Header only: no storage, no code but prototypes.
 *
 * Unity build order (felucca.c, inside #if FELUCCA_OTA, after project.c / motion.c / cue.c):
 *     #include "editor_proto.h"
 *     #include "editor_backup.c"     optional: defines ED_HAS_BACKUP 1   (LIST, GET, RESTORE)
 *     #include "editor_pattern.c"    optional: defines ED_HAS_PATTERN 1  (STEPS, LOCKS, CUE, SAVE, NAMES, PRESET_LOAD)
 *     #include "editor.c"            the core: framing, dispatch, INFO .. WATCH, TEXT, pushes, the update-silence rule
 * editor.c calls the hooks below only when the file that defines them was included (#ifdef ED_HAS_*), and sets the
 * matching ED_CAP_* bits in INFO. The optional files use only the framing API below (editor.c defines it). */
#ifndef EDITOR_PROTO_H
#define EDITOR_PROTO_H
#include <stdint.h>

/* ---------------------------------------------------------------- framing --- */
#define ED_H0 0x7Du                 /* F0 7D 48 54 <cmd> <seq> ... F7: non-commercial id, "HT" */
#define ED_H1 0x48u
#define ED_H2 0x54u
#define ED_HDR_N 3u                 /* sx_frame holds the bytes between F0 and F7: [0..2] header, [3] cmd, [4] seq */
#define ED_MIN_FRAME 5u             /* header + cmd + seq; shorter frames are dropped unanswered */
#define ED_PROTO_MAJOR 1u
#define ED_PROTO_MINOR 1u             /* 1: NUDGE (ED_CAP_NUDGE: STEPS_GET / STEPS_SET carry each step's micro) */
#define ED_MAGIC "HORTATOR"         /* INFO's first field */
#define ED_SEQ_PUSH 0x7Fu           /* a push's seq; requests use 0..126 */
#define ED_NONE 0x7Fu               /* "none" / "the current one" in a u7 field (slot, track, variation) */
#define ED_U14_NONE 0x3FFFu         /* "none" in a u14 field (preset id, page column) */
#define ED_REQ_MAX 640u             /* a request's bytes between F0 and F7 (usb_app.c sx_frame) */
#define ED_REPLY_MAX 400u           /* a reply / push, F0 .. F7 included (the editor's reply buffer) */
#define ED_PIECE 256u               /* bulk data bytes per BACKUP_GET / RESTORE DATA piece (pack7: 293 on the wire) */
#define ED_PUSH_MAX 4u              /* pushes per ed_service pass, only into a half-empty SysEx out ring */

/* ------------------------------------------------------------ timing (ms) --- */
#define ED_SILENCE_MS 10000u        /* update traffic: no replies, no pushes, an uncommitted restore dropped */
#define ED_WATCH_MS 3000u           /* WATCH ends this long after the last request of any kind */
#define ED_RESTORE_IDLE_MS 3000u    /* a restore with no request this long: aborted (RAM homes reloaded from flash) */
#define ED_DUP_MS 3000u             /* the last (cmd, seq) is forgotten after this idle time, and on INFO */

/* --------------------------------------------------------------- commands --- */
enum {
    ED_INFO = 1, ED_PAGES, ED_DESC, ED_GET, ED_SET, ED_DUMP, ED_SELECT, ED_WATCH,   /* 1..8 */
    ED_NAMES, ED_PRESET_LOAD,                                                       /* 9, 10 */
    ED_STEPS_GET, ED_STEPS_SET, ED_LOCKS_GET, ED_LOCK_EDIT, ED_CUE, ED_SAVE,         /* 11..16 */
    ED_BACKUP_LIST, ED_BACKUP_GET, ED_RESTORE,                                      /* 17..19 */
    ED_TEXT,                                                                        /* 20 */
    ED_CMD_N
};
enum { ED_P_CHANGED = 0x40, ED_P_STATE, ED_P_STEPS_CHANGED, ED_P_LOCKS_CHANGED };   /* pushes (seq 0x7F) */

/* request argument bytes after the seq, per command (fixed-size requests; ed_args_ok). Variable ones: the minimum */
#define ED_LEN_INFO 0u
#define ED_LEN_PAGES 1u             /* first */
#define ED_LEN_DESC 5u              /* scope, track, id u14, first */
#define ED_LEN_GET 5u               /* scope, track, id u14, count */
#define ED_LEN_SET 6u               /* scope, track, id u14, value v14 */
#define ED_LEN_DUMP 1u
#define ED_LEN_SELECT 1u
#define ED_LEN_WATCH 1u
#define ED_LEN_NAMES 2u             /* bank, first */
#define ED_LEN_PRESET_LOAD 3u       /* track, id u14 */
#define ED_LEN_STEPS_GET 3u         /* slot, var, track */
#define ED_LEN_STEPS_SET_MIN 5u     /* slot, var, track, first, count; then count x 3; NUDGE: then count x 1 (micro) */
#define ED_LEN_LOCKS_GET 4u         /* slot, track, first u14 */
#define ED_LEN_LOCK_EDIT 6u         /* op, track, step, param, value v14 */
#define ED_LEN_CUE 2u               /* kind, n */
#define ED_LEN_SAVE 1u
#define ED_LEN_BACKUP_LIST 0u
#define ED_LEN_BACKUP_GET 8u        /* obj, offset u14, seq u35 */
#define ED_LEN_TEXT 6u              /* scope, track, id u14, value v14 */
#define ED_LEN_RESTORE_BEGIN 29u    /* op, kind, slot, magic u35, 3 x (size u14, crc u35) */
#define ED_LEN_RESTORE_DATA_MIN 4u  /* op, part, offset u14; then the packed piece */
#define ED_LEN_RESTORE_OP 1u        /* op (COMMIT, ABORT) */

/* ------------------------------------------------------------ result codes --- */
enum { ED_OK, ED_E_BAD, ED_E_REFUSED, ED_E_STOP, ED_E_FLASH, ED_E_STALE, ED_E_BUSY };   /* rc 0..6 */
/* detail codes (an error reply: rc, detail, message). 1..11: cue.c CUE_E_* as they are (CUE_E_PARK + c: SAVE c FIRST) */
enum {
    ED_D_NONE = 0,
    ED_D_CUE_FULL = 1, ED_D_CUE_SONG = 2, ED_D_CUE_NOSLOT = 3, ED_D_CUE_EMPTY = 4, ED_D_CUE_PARK = 8,   /* + slot */
    ED_D_EMPTY_SLOT = 16, ED_D_PRESET_EMPTY, ED_D_ACID_CAP, ED_D_NOT_LOCKABLE, ED_D_LOCKS_FULL, ED_D_VARS_FULL,
    ED_D_NO_SLOT, ED_D_NEWER, ED_D_BAD_OBJECT, ED_D_CRC, ED_D_INVALID, ED_D_INCOMPLETE, ED_D_NO_FLASH, ED_D_BLOCKED,
    ED_D_NOT_PRESENT,                                   /* 16..30 (rc 2) */
    ED_D_CUE_FIRST = 32, ED_D_CHANGED, ED_D_ORDER,       /* rc 5 */
    ED_D_RESTORING = 40, ED_D_NOT_RESTORING,             /* rc 6 / rc 1 */
    ED_D_STOP = 42,                                      /* rc 3 */
    ED_D_UNKNOWN = 43, ED_D_LENGTH, ED_D_RANGE, ED_D_NO_CAP   /* rc 1 */
};

/* -------------------------------------------------------- capability bits --- */
#define ED_CAP_LIVE 0x01u           /* PAGES DESC GET SET TEXT DUMP SELECT WATCH, pushes CHANGED STATE (editor.c) */
#define ED_CAP_PRESETS 0x02u        /* NAMES PRESET_LOAD (editor_pattern.c) */
#define ED_CAP_PATTERNS 0x04u       /* STEPS_GET STEPS_SET LOCKS_GET LOCK_EDIT CUE SAVE, pushes STEPS / LOCKS_CHANGED */
#define ED_CAP_BACKUP 0x08u         /* BACKUP_LIST BACKUP_GET (editor_backup.c) */
#define ED_CAP_RESTORE 0x10u        /* RESTORE (editor_backup.c) */
#define ED_CAP_NUDGE 0x20u          /* STEPS_GET / STEPS_SET carry each step's NUDGE (minor 1, editor_pattern.c) */

/* --------------------------------------------------------------- fields --- */
enum { ED_SC_TRACK, ED_SC_GLOBAL, ED_SC_MODEL };      /* DESC / GET / SET / TEXT scope (MODEL: DESC only) */
#define ED_DF_SPLIT 0x01u           /* DESC flags: F_INT whose range is split evenly over the names */
#define ED_DF_ORDER 0x02u           /*   the values are shown in another order (DESC first = 0x7F gives it) */
#define ED_DF_LOCK 0x04u            /*   lockable (motion.c motion_param) */
#define ED_DF_RO 0x08u              /*   SET refuses it (rc 1) */
#define ED_DF_CONTEXT 0x10u         /*   depends on the track's MODEL / INSERT TYPE / RESON MODEL: re-read after them */
#define ED_DESC_ORDER 0x7Fu         /* DESC first: the shown order instead of names */
#define ED_SF_RESTORING 0x01u       /* state flags */
#define ED_SF_SONG 0x02u
#define ED_SF_NOFLASH 0x04u
#define ED_STATE_N 17u              /* playing slot var qslot qvar sel flags used dirty, vmask 4 x u14 */
#define ED_STEP_ON 0x01u            /* a step on the wire: flags, cond (0..58), note (nt + 60) */
#define ED_STEP_ACC 0x02u
#define ED_STEP_SLIDE 0x04u
#define ED_STEP_TIE 0x08u
#define ED_STEP_RAT_SHIFT 4u        /* bits 4..5: RATCH hits - 1 */
#define ED_NT_OFFSET 60u
#define ED_MICRO_OFFSET 64u         /* NUDGE on the wire: micro + 64 (-50..+50: 14..114), after the steps */
enum { ED_CUE_SLOT, ED_CUE_VAR };
enum { ED_LK_SET, ED_LK_CLEAR, ED_LK_CLEAR_STEP, ED_LK_CLEAR_TRACK };   /* LOCK_EDIT ops */
enum { ED_LK_KIND_MOTION = 1, ED_LK_KIND_LOCK = 2 };
enum { ED_RS_BEGIN, ED_RS_DATA, ED_RS_COMMIT, ED_RS_ABORT };            /* RESTORE ops */
enum { ED_RK_PRESETS, ED_RK_PROJECT };                                   /* RESTORE kinds */
enum { ED_PART_RECORD, ED_PART_LOCKS, ED_PART_VARS, ED_PART_N };         /* RESTORE parts (presets: part 0) */

/* --------------------------------------------------------------- objects --- */
enum { ED_OBJ_PRESETS = 0, ED_OBJ_PROJECT = 1, ED_OBJ_LOCKS = 5, ED_OBJ_VARS = 9, ED_OBJ_N = 13 };   /* + slot */
#define ED_BF_UNSAVED 0x01u         /* BACKUP_LIST flags: RAM differs from flash (slot_dirty / upr_pending) */
#define ED_BF_LOST 0x02u            /*   the record links an object no flash copy matches */
#define ED_BF_OLD 0x04u             /*   a record in an older format (converted at load) */
#define ED_SZ_PRESETS 1284u         /* upr_rec_t ("UPR1") */
#define ED_SZ_PROJECT 3840u         /* project_t ("FDRG", "FDRF"); older records: their own sizes (project.c proj_fetch) */
#define ED_SZ_LOCKS 3208u           /* lk_bank_t ("LKB1") */
#define ED_SZ_VARS_MIN 22u          /* var_bank_t ("VAR1"): 8 + used, 22 (7 empty entries) .. 3584 */
#define ED_SZ_VARS_MAX 3584u

/* ------------------------------------- framing API (editor.c defines these) --- */
/* a = the request's argument bytes (after the seq), n = their count */
static void ed_reply(uint32_t cmd, uint32_t rc);    /* start a reply: F0 7D 48 54 cmd seq rc */
static void ed_push(uint32_t cmd);                  /* start a push: F0 7D 48 54 cmd 7F */
static void ed_b(uint32_t v);                       /* u7 */
static void ed_u14(uint32_t v);
static void ed_v14(int32_t v);                      /* v + 8192, two 7-bit bytes, LSB first */
static void ed_u35(uint32_t v);                     /* five 7-bit bytes, LSB first */
static void ed_str(const char *s);                  /* ASCII 32..126, then 0 */
static void ed_pack(const void *d, uint32_t n);     /* pack7 groups */
static void ed_state(void);                         /* the ED_STATE_N state bytes */
static uint32_t ed_room(void);                      /* bytes left before ED_REPLY_MAX (the F7 kept) */
static void ed_send(void);                          /* F7, into the SysEx out ring */
static void ed_fail(uint32_t cmd, uint32_t rc, uint32_t detail, const char *msg);   /* a whole error reply */
static uint32_t ed_get_u14(const uint8_t *a);
static int32_t ed_get_v14(const uint8_t *a);
static uint32_t ed_get_u35(const uint8_t *a);
static uint32_t ed_unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max);   /* bytes out, 0 malformed */

/* hooks editor.c calls (#ifdef ED_HAS_BACKUP / ED_HAS_PATTERN), for reference:
 *   static int ed_backup_cmd(uint32_t cmd, const uint8_t *a, uint32_t n);   every request, first: a restore kept
 *                                               alive; 1: a backup command, handled (replied)
 *   static void ed_backup_poll(void);           every ed_service pass: the restore's idle timeout
 *   static void ed_backup_drop(void);           update traffic: an uncommitted restore dropped, homes reloaded
 *   static int ed_restoring(void);              1 between BEGIN and COMMIT / ABORT / timeout (the RESTORING lock)
 *   static int ed_pattern_cmd(uint32_t cmd, const uint8_t *a, uint32_t n);  1: handled (replied)
 *   static uint32_t ed_pattern_push(uint32_t budget);   STEPS_CHANGED / LOCKS_CHANGED pushes sent (<= budget)
 *   static void ed_pattern_sync(void);          the step / lock shadows := now (WATCH on, the editor's own writes) */
#endif
