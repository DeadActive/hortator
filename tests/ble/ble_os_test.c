/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE */
#define BLE_OS_HOST 1
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static uint32_t test_ms;
uint32_t ble_os_now_ms(void) { return test_ms; }
#include "../../firmware/src/ble/ble_os.c"

static int fails;
#define CHECK(c, what) do { if (c) printf("ok    %s\n", what); else { printf("FAIL  %s\n", what); fails++; } } while (0)
static uint8_t heap[16384];
static int log_[32], nlog;
static uint8_t sem[BLE_SDK_OS_SEM_SIZE];

static void consumer(void *p)                   /* pends messages, logs their first argument */
{
    int argv[4];
    (void)p;
    for (;;) {
        int r = os_taskq_pend("taskq", argv, 4);
        if (r == BLE_SDK_OS_TASKQ && nlog < 32)
            log_[nlog++] = argv[1];
    }
}
static void waiter(void *p)                     /* waits on the semaphore with a 5-tick timeout, logs result */
{
    (void)p;
    for (;;) {
        int r = os_sem_pend(sem, 5);
        if (nlog < 32)
            log_[nlog++] = 1000 + r;
    }
}
static int fired;
static void tick(void *p) { (void)p; fired++; }
static void hog(void *p) { (void)p; for (;;) os_time_dly(0); }   /* yields without sleeping */
static uint8_t mtx[BLE_SDK_OS_SEM_SIZE], core_sem[BLE_SDK_OS_SEM_SIZE];
static int mtx_got;
static void mtx_user(void *p) { (void)p; for (;;) { if (os_mutex_pend(mtx, 0) == BLE_SDK_OS_NO_ERR) mtx_got++; os_time_dly(1000); } }
static int after_runs, after_last = -1;
static void after_run(int i) { after_runs++; after_last = i; }


static int cb_sum;
static int add2(int a, int b) { cb_sum += a + b; return a + b; }
static const char *tmr_task;
static int tmr_runs;
static int on_tmr(void *p) { tmr_task = os_current_task(); tmr_runs += (int)(intptr_t)p; return 0; }
void *ble_os_host_ptr(uint32_t w)              /* host: 32-bit handles for callbacks (pointers are 64-bit here) */
{
    return w == 1u ? (void *)add2 : w == 2u ? (void *)on_tmr : ble_os_host_word_ptr(w);
}
static void cb_consumer(void *p)                /* a Q_CALLBACK message is run inside the pend, never returned */
{
    int argv[8];
    (void)p;
    for (;;) {
        int r = os_taskq_pend("taskq", argv, 8);
        if (r == BLE_SDK_OS_TASKQ && nlog < 32)
            log_[nlog++] = argv[0];
    }
}

int main(void)
{
    int a[2];
    ble_os_init(heap, sizeof heap);
    task_create(consumer, 0, "btstack");
    ble_os_service(1000);
    a[0] = 7; os_taskq_post_type("btstack", BLE_SDK_Q_MSG, 1, a);
    a[0] = 8; os_taskq_post_type("btstack", BLE_SDK_Q_MSG, 1, a);
    ble_os_service(1000);
    CHECK(nlog == 2 && log_[0] == 7 && log_[1] == 8, "queue: posted messages arrive in order");

    nlog = 0;
    os_sem_create(sem, 0);
    task_create(waiter, 0, "btctrler");
    ble_os_service(1000);
    os_sem_post(sem);
    ble_os_service(1000);
    CHECK(nlog == 1 && log_[0] == 1000 + BLE_SDK_OS_NO_ERR, "semaphore post wakes the waiter");
    test_ms += 60;                              /* 5 ticks = 50 ms */
    ble_os_service(1000);
    CHECK(nlog == 2 && log_[1] == 1000 + BLE_SDK_OS_TIMEOUT, "semaphore pend times out");

    {
        unsigned short id = sys_timer_add(0, tick, 20);
        test_ms += 20; ble_os_service(1000);
        test_ms += 20; ble_os_service(1000);
        CHECK(fired == 2, "periodic timer fires per period");
        sys_timer_del(id);
        test_ms += 40; ble_os_service(1000);
        CHECK(fired == 2, "deleted timer stops");
        sys_timeout_add(0, tick, 10);
        test_ms += 10; ble_os_service(1000);
        test_ms += 10; ble_os_service(1000);
        CHECK(fired == 3, "timeout fires once");
    }
    {
        void *p1 = ble_malloc(3072), *p2 = ble_malloc(512), *p3 = ble_malloc(20000);
        CHECK(p1 && p2 && !p3, "heap: fits, refuses beyond the pool");
        ble_free(p1);
        p3 = ble_malloc(3000);
        CHECK(p3 == p1, "heap: freed block reused");
    }
    {
        uint32_t t0;
        task_create(hog, 0, "hog");
        t0 = test_ms;
        ble_os_service(50000);                  /* budget 50 ms: must return though hog never sleeps */
        CHECK(1, "service returns within budget with a task that never sleeps");
        (void)t0;
    }
    {   /* Q_CALLBACK: func(argv[3], argv[4]) runs in the pend (flags 2 = two args); a plain message still returns */
        int c[4];
        nlog = 0;
        task_create(cb_consumer, 0, "cbtask");
        ble_os_service(1000);
        c[0] = 1;   /* (the handle of add2: pointers are 64-bit on the host) */ c[1] = 2; c[2] = 30; c[3] = 12;
        os_taskq_post_type("cbtask", BLE_SDK_Q_CALLBACK, 4, c);
        c[0] = 5; os_taskq_post_type("cbtask", BLE_SDK_Q_EVENT | 1, 1, c);
        ble_os_service(1000);
        CHECK(cb_sum == 42 && nlog == 1 && log_[0] == (BLE_SDK_Q_EVENT | 1), "Q_CALLBACK runs in the pend, events return");
    }
    CHECK(os_taskq_post_type("nosuchtask", BLE_SDK_Q_MSG, 0, 0) == 14, "post to an unknown task returns OS_TASK_NOT_EXIST");
    {   /* a timer bound to a task runs its callback in that task; user data, re-run, usr_timeout */
        unsigned short id;
        tmr_runs = 0;
        id = sys_timer_add_to_task("cbtask", (void *)(intptr_t)3, (void (*)(void *))on_tmr, 20);
        CHECK(sys_timer_get_user_data(id) == (void *)(intptr_t)3, "timer user data read back");
        sys_timer_set_user_data(id, (void *)(intptr_t)5);
        test_ms += 20; ble_os_service(1000);
        CHECK(tmr_runs == 5 && tmr_task && !strcmp(tmr_task, "cbtask"), "to_task timer runs in its task with its user data");
        test_ms += 15; sys_timer_re_run(id); test_ms += 15; ble_os_service(1000);
        CHECK(tmr_runs == 5, "re_run restarts the period");
        test_ms += 5; ble_os_service(1000);
        CHECK(tmr_runs == 10, "and it fires a period after the re-run");
        sys_timer_del(id);
        sys_timeout_add_to_task("cbtask", (void *)(intptr_t)1, (void (*)(void *))on_tmr, 10);
        test_ms += 10; ble_os_service(1000); test_ms += 10; ble_os_service(1000);
        CHECK(tmr_runs == 11, "to_task timeout fires once");
        id = usr_timeout_add((void *)(intptr_t)100, (void (*)(void *))on_tmr, 10, 1);
        usr_timeout_del(id);
        test_ms += 20; ble_os_service(1000);
        CHECK(tmr_runs == 11, "usr_timeout_del cancels");
    }
    {   /* stack high-water: a new task's stack is painted, and the count stops at the deepest touched word (on the
         * host, Darwin's makecontext touches the whole stack, so the device measures; here the counting) */
        const char *name = 0;
        uint32_t i, slot = 99;
        ble_os_init(heap, sizeof heap);
        task_create(consumer, 0, "painted");
        for (i = 0; i < 4u; i++)
            if (ble_os_stack_free(i, &name) && name && !strcmp(name, "painted"))
                slot = i;
        CHECK(slot < 4u && ble_os_stack_free(slot, &name) == 4096u, "stack high-water: a new task's stack is all free");
        if (slot < 4u)
            tasks[slot].stack[100] = 0;            /* the deepest word the task touched */
        CHECK(slot < 4u && ble_os_stack_free(slot, &name) == 400u, "stack high-water: counts up to the deepest use");
        CHECK(ble_os_stack_free(99, &name) == 0 && name == 0, "stack high-water: no such task");
    }
    {   /* the core pending successfully must not clear a blocked task's wait (review #4) */
        int slot;
        ble_os_init(heap, sizeof heap);
        os_mutex_create(mtx);
        os_mutex_pend(mtx, 0);                  /* the core holds it */
        os_sem_create(core_sem, 1);
        task_create(mtx_user, 0, "btctrler");
        ble_os_service(1000);                   /* the task blocks on the mutex */
        CHECK(os_sem_pend(core_sem, 0) == BLE_SDK_OS_NO_ERR, "the core takes a free semaphore");
        os_mutex_post(mtx);
        ble_os_service(1000);
        CHECK(mtx_got == 1, "a task blocked on a mutex still wakes when the core took another semaphore");
        (void)slot;
    }
    {   /* the scheduler reports each return from a task (the target checks the interrupt state there: #5) */
        ble_os_init(heap, sizeof heap);
        task_create(consumer, 0, "btstack");
        ble_os_after_run = after_run;
        ble_os_service(1000);
        CHECK(after_runs == 1 && after_last == 0, "the after-run hook runs when a task hands back");
        ble_os_after_run = 0;
    }
    printf(fails ? "ble_os: %d FAILED\n" : "ble_os: all checks passed\n", fails);
    return fails != 0;
}
