/* SPDX-License-Identifier: GPL-3.0-only */
/* T03 diagnostics for the pinned host fixtures. Exit 1 when an invariant fails. */
#define main original_cue_test_main
#include "cue_test.c"
#undef main

int main(void)
{
    int queue, sw, requeue, preserved;
    four_slots();
    play();
    trk[1].step[31].on = 1;
    queue = cue_slot(1);
    sw = cue_switch(0, 0);
    if (queue || sw || proj_loaded() != 1 || cue.out == STG_NONE ||
        !cue_stg()->st[1][31].on) {
        fprintf(stderr, "invalid cue probe setup\n");
        return 2;
    }
    requeue = cue_slot(2);
    cue_poll();
    preserved = proj_slot[0].t[1].step[31].on;
    printf("cue requeue=%d outgoing step preserved=%d\n", requeue, preserved);

    fresh();
    lk_put(&mo.b, lk_key(0, 4, P_E1), 100, 1);
    project_save(1);
    lk_bank_t next = lk_slot[1];
    if (proj_bank_fetch(1) != 1 || !next.count) {
        fprintf(stderr, "invalid lock probe setup\n");
        return 2;
    }
    lk_put(&next, lk_key(0, 4, P_E1), 50, 1);
    if (st_save(OBJ_LOCKS0 + 1u, &next, sizeof next) || proj_bank_fetch(1) != 1) {
        fprintf(stderr, "first interruption did not leave original record and bank\n");
        return 2;
    }
    lk_put(&next, lk_key(0, 4, P_E1), 25, 1);
    if (st_save(OBJ_LOCKS0 + 1u, &next, sizeof next)) {
        fprintf(stderr, "invalid second lock write setup\n");
        return 2;
    }
    int old_link = proj_bank_fetch(1);
    printf("second interrupted save old linked lock bank status=%d\n", old_link);
    return preserved && old_link == 1 ? 0 : 1;
}
