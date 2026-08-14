#include "sched.h"
#include "uart.h"

/* ---- CLINT (Core-Local Interruptor) on QEMU virt ---- */
#define CLINT_BASE      0x02000000UL
#define CLINT_MTIMECMP  (CLINT_BASE + 0x4000UL)      /* hart 0 */
#define CLINT_MTIME     (CLINT_BASE + 0xBFF8UL)
#define TIMER_HZ        10000000UL                   /* QEMU virt CLINT freq */
/* NOTE: empirically calibrated against this QEMU/TCG environment rather
 * than assumed wall-clock math — in a heavily virtualized TCG session,
 * mtime does not advance 1:1 with guest instruction throughput, so a
 * "5ms at 10MHz" interval measured on real silicon can be wildly off
 * here. 20000 mtime ticks gives a responsive, visibly preemptive demo. */
#define TICK_INTERVAL   20000UL

static volatile uint64_t *mtime    = (volatile uint64_t *)CLINT_MTIME;
static volatile uint64_t *mtimecmp = (volatile uint64_t *)CLINT_MTIMECMP;

/* ---- static task pool ---- */
static task_t       task_pool[MAX_TASKS];
static uint32_t      task_stacks[MAX_TASKS][STACK_WORDS];
static int            n_tasks = 0;

task_t *current_task = 0;
volatile uint32_t g_ticks = 0;

/* ---- ready queues, one FIFO list per priority level ---- */
static task_t *ready_head[MAX_PRIO];
static task_t *ready_tail[MAX_PRIO];
static uint32_t ready_bitmap = 0;   /* bit i set => ready_head[i] non-NULL */

void ready_enqueue(task_t *t) {
    t->state = TASK_READY;
    t->next = 0;
    uint8_t p = t->cur_prio;
    if (ready_tail[p]) {
        ready_tail[p]->next = t;
        ready_tail[p] = t;
    } else {
        ready_head[p] = ready_tail[p] = t;
    }
    ready_bitmap |= (1u << p);
}

task_t *ready_dequeue_highest(void) {
    if (ready_bitmap == 0) return 0;
    int p = 31 - __builtin_clz(ready_bitmap);   /* highest set priority */
    task_t *t = ready_head[p];
    ready_head[p] = t->next;
    if (!ready_head[p]) {
        ready_tail[p] = 0;
        ready_bitmap &= ~(1u << p);
    }
    t->next = 0;
    return t;
}

void task_set_priority(task_t *t, uint8_t new_prio) {
    /* Only meaningful while READY (requeue into the right bucket) or
     * RUNNING (just relabel; ready_enqueue will place it correctly next
     * time it's descheduled). Priority-inheritance boosting/restoring
     * calls this directly. */
    if (t->state == TASK_READY) {
        /* remove from its current bucket first */
        uint8_t old = t->cur_prio;
        task_t **pp = &ready_head[old];
        task_t *prev = 0;
        while (*pp && *pp != t) { prev = *pp; pp = &(*pp)->next; }
        if (*pp == t) {
            *pp = t->next;
            if (ready_tail[old] == t) ready_tail[old] = prev;
            if (!ready_head[old]) ready_bitmap &= ~(1u << old);
        }
        t->cur_prio = new_prio;
        ready_enqueue(t);
    } else {
        t->cur_prio = new_prio;
    }
}

uint64_t read_cycles(void) {
    uint32_t lo, hi, hi2;
    do {
        __asm__ volatile ("csrr %0, mcycleh" : "=r"(hi));
        __asm__ volatile ("csrr %0, mcycle"  : "=r"(lo));
        __asm__ volatile ("csrr %0, mcycleh" : "=r"(hi2));
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
}

static void set_next_timer_interrupt(void) {
    uint64_t now = *mtime;
    *mtimecmp = now + TICK_INTERVAL;
}

void sched_init(void) {
    for (int i = 0; i < MAX_PRIO; i++) { ready_head[i] = ready_tail[i] = 0; }
    ready_bitmap = 0;
    n_tasks = 0;
    current_task = 0;
}

/* Build an initial trap frame at the top of a fresh stack so that the
 * first "restore" in trap_entry drops straight into entry(arg) with
 * interrupts enabled. See start.S for the frame layout. */
task_t *task_create_ex(const char *name, void (*entry)(void *arg), void *arg,
                        uint8_t prio, int start_suspended) {
    if (n_tasks >= MAX_TASKS || prio >= MAX_PRIO) return 0;
    task_t *t = &task_pool[n_tasks];
    uint32_t *stack_top = task_stacks[n_tasks] + STACK_WORDS;
    n_tasks++;

    uint8_t *frame_bytes = (uint8_t *)stack_top - 128;
    uint32_t *frame = (uint32_t *)frame_bytes;
    for (int i = 0; i < 32; i++) frame[i] = 0;

    frame[32 / 4] = (uint32_t)arg;         /* a0 = arg, offset 32   */
    frame[120 / 4] = (uint32_t)entry;      /* mepc = entry point    */
    /* mstatus: MPP = 11 (stay in M-mode), MPIE = 1 (interrupts on after mret) */
    frame[124 / 4] = (3u << 11) | (1u << 7);

    t->sp = (uint32_t *)frame;
    t->base_prio = prio;
    t->cur_prio = prio;
    t->name = name;
    t->next = 0;
    t->run_count = 0;
    t->last_switch_in_cycle = 0;
    t->total_switch_in_latency_cycles = 0;

    if (start_suspended) {
        t->state = TASK_BLOCKED;   /* not in any ready queue yet */
    } else {
        t->state = TASK_READY;
        ready_enqueue(t);
    }
    return t;
}

task_t *task_create(const char *name, void (*entry)(void *arg), void *arg, uint8_t prio) {
    return task_create_ex(name, entry, arg, prio, 0);
}

void task_resume(task_t *t) {
    if (t->state == TASK_BLOCKED) {
        ready_enqueue(t);
    }
}

/* Called from trap_entry for BOTH the timer ISR and voluntary yields
 * (ecall). Picks the next task to run and updates `current_task`. */
static void schedule(void) {
    task_t *prev = current_task;
    if (prev && prev->state == TASK_RUNNING) {
        ready_enqueue(prev);   /* still runnable: goes to the back of its queue */
    }
    task_t *next = ready_dequeue_highest();
    if (!next) next = prev;    /* nothing else ready: keep running prev */
    next->state = TASK_RUNNING;
    current_task = next;
    current_task->run_count++;
}

void trap_handler_c(uint32_t *frame) {
    (void)frame;
    uint32_t mcause;
    __asm__ volatile ("csrr %0, mcause" : "=r"(mcause));

    uint64_t t_enter = read_cycles();

    if (mcause == 0x80000007UL) {              /* machine timer interrupt */
        g_ticks++;
        set_next_timer_interrupt();
        schedule();
    } else if (mcause == 11UL) {                /* ecall from M-mode = yield */
        /* skip past the ecall instruction so we don't re-trap on return */
        current_task->sp[120 / 4] += 4;
        schedule();
    }

    uint64_t t_exit = read_cycles();
    current_task->last_switch_in_cycle = t_exit;
    current_task->total_switch_in_latency_cycles += (t_exit - t_enter);
}

void sched_yield(void) {
    extern void yield(void);
    yield();
}

void sched_start(void) {
    current_task = ready_dequeue_highest();
    current_task->state = TASK_RUNNING;
    current_task->run_count++;

    set_next_timer_interrupt();
    /* enable machine timer interrupt + global machine interrupt enable */
    uint32_t mie;
    __asm__ volatile ("csrr %0, mie" : "=r"(mie));
    mie |= (1u << 7);           /* MTIE */
    __asm__ volatile ("csrw mie, %0" :: "r"(mie));

    /* Jump into the first task by faking a trap return: load its saved
     * frame and mret. We reuse trap_entry's restore path by pointing sp
     * at the task's frame and executing the same tail sequence. */
    __asm__ volatile (
        "mv sp, %0\n"
        "lw t0, 120(sp)\n"
        "csrw mepc, t0\n"
        "lw t0, 124(sp)\n"
        "csrw mstatus, t0\n"
        "lw ra,   0(sp)\n"
        "lw gp,   4(sp)\n"
        "lw tp,   8(sp)\n"
        "lw t0,  12(sp)\n"
        "lw t1,  16(sp)\n"
        "lw t2,  20(sp)\n"
        "lw s0,  24(sp)\n"
        "lw s1,  28(sp)\n"
        "lw a0,  32(sp)\n"
        "lw a1,  36(sp)\n"
        "lw a2,  40(sp)\n"
        "lw a3,  44(sp)\n"
        "lw a4,  48(sp)\n"
        "lw a5,  52(sp)\n"
        "lw a6,  56(sp)\n"
        "lw a7,  60(sp)\n"
        "lw s2,  64(sp)\n"
        "lw s3,  68(sp)\n"
        "lw s4,  72(sp)\n"
        "lw s5,  76(sp)\n"
        "lw s6,  80(sp)\n"
        "lw s7,  84(sp)\n"
        "lw s8,  88(sp)\n"
        "lw s9,  92(sp)\n"
        "lw s10, 96(sp)\n"
        "lw s11, 100(sp)\n"
        "lw t3,  104(sp)\n"
        "lw t4,  108(sp)\n"
        "lw t5,  112(sp)\n"
        "lw t6,  116(sp)\n"
        "addi sp, sp, 128\n"
        "mret\n"
        :: "r"(current_task->sp) : "t0", "memory"
    );
    __builtin_unreachable();
}
