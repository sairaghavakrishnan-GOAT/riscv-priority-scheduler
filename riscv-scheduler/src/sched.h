#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>

#define MAX_PRIO      8     /* priority levels: 0 = lowest, 7 = highest   */
#define MAX_TASKS     16    /* matches the 16-concurrent-task target      */
#define STACK_WORDS   256   /* 1KB per-task stack                        */

typedef enum { TASK_UNUSED = 0, TASK_READY, TASK_RUNNING, TASK_BLOCKED } task_state_t;

typedef struct task {
    uint32_t     *sp;         /* MUST be first field — asm relies on offset 0 */
    uint8_t       base_prio;
    uint8_t       cur_prio;   /* may be boosted by priority inheritance    */
    task_state_t  state;
    const char   *name;
    struct task  *next;       /* intrusive singly-linked list (ready/wait) */

    /* diagnostics */
    uint32_t      run_count;
    uint64_t      last_switch_in_cycle;
    uint64_t      total_switch_in_latency_cycles;
} task_t;

extern task_t *current_task;
extern volatile uint32_t g_ticks;

void        sched_init(void);
task_t     *task_create_ex(const char *name, void (*entry)(void *arg), void *arg,
                            uint8_t prio, int start_suspended);
task_t     *task_create(const char *name, void (*entry)(void *arg), void *arg, uint8_t prio);
void        task_resume(task_t *t);        /* release a suspended task into the ready queue */
void        sched_start(void);            /* never returns */
void        sched_yield(void);            /* voluntary preemption point */

/* ready-queue bookkeeping, used by sched.c and mutex.c */
void        ready_enqueue(task_t *t);
task_t     *ready_dequeue_highest(void);
void        task_set_priority(task_t *t, uint8_t new_prio);

uint64_t    read_cycles(void);

#endif
