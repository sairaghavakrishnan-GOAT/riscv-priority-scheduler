#include "sched.h"
#include "mutex.h"
#include "uart.h"

/* ---------------------------------------------------------------------
 * Priority-inversion demo:
 *   task_low  (prio 1) grabs shared_mutex, then does slow "work"
 *   task_med  (prio 4) never touches the mutex, just burns CPU
 *   task_high (prio 7) wants the mutex almost immediately
 *
 * Without priority inheritance, task_med (higher prio than task_low)
 * would keep starving task_low of CPU time, so task_low could never
 * finish and release the lock — task_high stays blocked indefinitely
 * even though it outranks task_med. pi_mutex_lock() fixes this by
 * boosting task_low to task_high's priority for the duration.
 * ------------------------------------------------------------------- */

static pi_mutex_t shared_mutex;
static volatile uint32_t critical_section_done = 0;
static volatile uint32_t high_acquired_tick = 0;
static volatile uint32_t high_blocked_since_tick = 0;

static task_t *g_task_med = 0;
static task_t *g_task_high = 0;
static task_t *g_task_reporter = 0;
static task_t *g_fillers[8];
#define N_FILLERS 8

static void busy_spin(volatile uint32_t iters) {
    while (iters--) { __asm__ volatile ("nop"); }
}

static void task_low(void *arg) {
    (void)arg;
    /* task_low is the only task ready at boot, so it's guaranteed to run
     * first and grab the mutex uncontested. Only THEN do we release the
     * medium-priority task, the fillers, and the high-priority task into
     * the ready queue — reproducing the classic inversion setup: a
     * higher-priority task becomes runnable while task_low is inside its
     * critical section holding the lock. */
    pi_mutex_lock(&shared_mutex);
    uart_printf("[t=%u] task_low: acquired mutex, doing critical work...\n", g_ticks);

    task_resume(g_task_med);
    for (int i = 0; i < N_FILLERS; i++) task_resume(g_fillers[i]);
    task_resume(g_task_high);
    task_resume(g_task_reporter);

    busy_spin(2000000);   /* simulate slow critical-section work */
    critical_section_done = 1;
    uart_printf("[t=%u] task_low: done, releasing mutex\n", g_ticks);
    pi_mutex_unlock(&shared_mutex);
    for (;;) { busy_spin(1500000); sched_yield(); }
}

static void task_med(void *arg) {
    (void)arg;
    /* Deliberately never touches the mutex — this is the task that
     * WOULD starve task_low out of a priority-inversion bug if
     * task_low weren't temporarily boosted above it. */
    for (int i = 0; i < 15; i++) {
        busy_spin(50000);
        sched_yield();
    }
    /* Work finished: step out of the way at idle priority so lower
     * -priority tasks (fillers, reporter) can eventually run. */
    task_set_priority(current_task, 0);
    for (;;) { busy_spin(1500000); sched_yield(); }
}

static void task_high(void *arg) {
    (void)arg;
    /* By the time this task is resumed (by task_low, right after it takes
     * the lock), the mutex is already held — so this deterministically
     * exercises the blocking + priority-inheritance path every run. */
    high_blocked_since_tick = g_ticks;
    uart_printf("[t=%u] task_high: requesting mutex\n", g_ticks);
    pi_mutex_lock(&shared_mutex);
    high_acquired_tick = g_ticks;
    uart_printf("[t=%u] task_high: acquired mutex (waited %u ticks)\n",
                g_ticks, g_ticks - high_blocked_since_tick);
    pi_mutex_unlock(&shared_mutex);

    /* Demo is done: a highest-priority task that never blocks again would
     * otherwise starve every lower-priority task forever (correct strict
     * -priority behavior, but useless for the rest of the demo), so it
     * drops itself down to idle priority once its work is finished. */
    task_set_priority(current_task, 0);
    for (;;) { busy_spin(1500000); sched_yield(); }
}

/* Generic filler task so the scheduler is genuinely juggling many
 * runnable tasks at once (target: 12 concurrent, matching the
 * project's validated task count). */
static void task_filler(void *arg) {
    uint32_t id = (uint32_t)(uintptr_t)arg;
    uint32_t loops = 0;
    for (int i = 0; i < 20; i++) {
        busy_spin(300000 + id * 30000);
        loops++;
        sched_yield();
    }
    uart_printf("[t=%u] filler-%u: finished %u loops\n", g_ticks, id, loops);
    task_set_priority(current_task, 0);
    for (;;) { busy_spin(1500000); sched_yield();
    }
}

/* Small helper so task_reporter can walk every task without exposing
 * the static pool directly — keeps main.c self-contained. */
static task_t *g_all_tasks[3 + N_FILLERS + 1];
static int g_all_count = 0;

static void register_task(task_t *t) {
    if (t) g_all_tasks[g_all_count++] = t;
}

static void print_final_report(void) {
    uart_puts("\n===== Context-switch latency report =====\n");
    uart_printf("Total timer ticks observed: %u\n", g_ticks);
    uart_puts("-------------------------------------------------------\n");
    uart_puts("task          switches   avg latency (cycles)\n");
    for (int i = 0; i < g_all_count; i++) {
        task_t *t = g_all_tasks[i];
        uint32_t sw = t->run_count;
        uint64_t avg = sw ? (t->total_switch_in_latency_cycles / sw) : 0;
        uart_printf("%-12s  %-9u  %u\n", t->name, sw, (uint32_t)avg);
    }
    uart_puts("=========================================================\n");
    uart_printf("Priority inversion check: task_high acquired mutex at tick %u "
                "(requested at tick %u)\n", high_acquired_tick, high_blocked_since_tick);
}

static void task_reporter(void *arg) {
    (void)arg;
    /* Let the system run for a while so we get a meaningful sample of
     * context switches, then print a latency summary and stop. */
    while (g_ticks < 60) {
        sched_yield();
    }
    print_final_report();
    for (;;) { busy_spin(1000000); }
}

int main(void) {
    uart_puts("RISC-V preemptive priority scheduler booting (QEMU virt)...\n");
    uart_puts("12 tasks, priority-inheritance mutex demo\n\n");

    sched_init();
    pi_mutex_init(&shared_mutex);

    /* task_low starts READY (the only one) so it is guaranteed to run
     * first and take the mutex uncontested. Everything else starts
     * SUSPENDED and is released by task_low once it holds the lock,
     * which is what makes the inversion scenario reproduce every run. */
    register_task(task_create("task_low", task_low, 0, 1));

    g_task_med = task_create_ex("task_med", task_med, 0, 4, 1);
    register_task(g_task_med);

    for (int i = 0; i < N_FILLERS; i++) {
        g_fillers[i] = task_create_ex("filler", task_filler, (void *)(uintptr_t)i, 3, 1);
        register_task(g_fillers[i]);
    }

    g_task_high = task_create_ex("task_high", task_high, 0, 7, 1);
    register_task(g_task_high);

    g_task_reporter = task_create_ex("reporter", task_reporter, 0, 2, 1);
    register_task(g_task_reporter);

    uart_printf("Created %d tasks, starting scheduler.\n", g_all_count);

    sched_start();   /* never returns */
    return 0;
}
