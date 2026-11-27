/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef GRIND_CONTROLLER_H
#define GRIND_CONTROLLER_H

#include "grind.h"

/* Portable state machine of the Grind controller: one latest-wins request, one stage with explicit ownership, the
 * revisions that keep a stale candidate from ever being published. No target calls, no heap, no generation inside it:
 * the caller (runtime.c) generates between gm_ctl_begin and gm_ctl_finish and wraps the short transitions in the
 * firmware's interrupt guard. A revision is compared by equality only, so a 32-bit wrap is harmless. */

typedef enum { GM_BOUNDARY_STEP, GM_BOUNDARY_BAR, GM_BOUNDARY_PATTERN } gm_boundary;
typedef enum {
    GM_REQUEST_MORPH, GM_REQUEST_STYLE, GM_REQUEST_MUTATE, GM_REQUEST_UNDO, GM_REQUEST_EDIT
} gm_request_kind;

typedef struct {
    uint32_t revision;          /* the request's own identity, the owner token of its stage */
    uint32_t base_revision;     /* the live pattern it was made from */
    gm_boundary at;
    gm_request_kind kind;
    uint8_t follow_live;        /* 1: an intent (a knob), made from whatever is live when it starts */
    gm_config config;
} gm_request;

/* FREE -> WRITING -> READY -> READING -> RETIRED -> FREE; only READING belongs to the audio side. */
typedef enum {
    GM_STAGE_FREE = 0, GM_STAGE_WRITING, GM_STAGE_READY, GM_STAGE_READING, GM_STAGE_RETIRED
} gm_stage_state;

typedef struct {
    uint32_t native_revision;   /* every accepted change of the live pattern: a manual edit, an import, a commit */
    uint32_t active_revision;   /* the request whose result is live (0: none yet) */
    gm_config active_config;
    gm_request pending;         /* the one latest request; has_pending says whether it exists */
    gm_request stage_req;       /* what the stage was made for */
    uint8_t has_pending, stage_state;
    gm_result status;           /* the last rejection, for the screen: requested versus committed */
    uint32_t acked_revision;    /* the last commit the audio side acknowledged */
    uint32_t commits;
} gm_controller;

void gm_ctl_init(gm_controller *c, const gm_config *active);
/* Latest wins: an older pending request is replaced, a READY stage made for another request is dropped. A stage
 * being read keeps going. Invalid configs are refused here, before they can queue. */
gm_result gm_ctl_submit(gm_controller *c, const gm_request *r);
/* Moves the pending request into the stage (FREE -> WRITING) and copies it out. GM_NO_CHANGE: nothing to do.
 * GM_STALE: its base is not the live pattern any more (the request is dropped). */
gm_result gm_ctl_begin(gm_controller *c, gm_request *out);
/* The generated candidate is complete (result GM_OK) or failed: WRITING -> READY, or back to FREE. A candidate whose
 * base moved or whose request was cancelled while it was being made is dropped (GM_STALE). */
gm_result gm_ctl_finish(gm_controller *c, uint32_t revision, gm_result result);
/* A manual edit, an import, a load: the live pattern changed. A READY stage of the old base is dropped at once. */
void gm_ctl_native_edit(gm_controller *c);
/* STOP, a context reset: nothing pending, nothing READY. A stage already being read completes. */
void gm_ctl_cancel(gm_controller *c);
/* Audio side, at the boundary: 1 and READING only for the READY stage of this request on the live base. */
int gm_ctl_consume(gm_controller *c, uint32_t revision);
/* Audio side, after the swap: READING -> RETIRED, the config is live and the base moved on. */
void gm_ctl_commit(gm_controller *c, uint32_t revision);
/* Main loop: RETIRED -> FREE. Returns the acknowledged revision, or 0 if nothing new. */
uint32_t gm_ctl_reconcile(gm_controller *c);

#endif
