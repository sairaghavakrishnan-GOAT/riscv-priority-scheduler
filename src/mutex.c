#include "mutex.h"
#include "uart.h"

void pi_mutex_init(pi_mutex_t *m) {
    m->owner = 0;
    m->locked = 0;
    m->boosted = 0;
    m->waiters = 0;
}

static void waiters_push(pi_mutex_t *m, task_t *t) {
    t->next = m->waiters;
    m->waiters = t;
}

/* Remove and return the highest-priority waiter, or NULL. */
static task_t *waiters_pop_highest(pi_mutex_t *m) {
    if (!m->waiters) return 0;
    task_t **best_pp = &m->waiters;
    task_t **pp = &m->waiters;
    while (*pp) {
        if ((*pp)->base_prio > (*best_pp)->base_prio) best_pp = pp;
        pp = &(*pp)->next;
    }
    task_t *best = *best_pp;
    *best_pp = best->next;
    best->next = 0;
    return best;
}

void pi_mutex_lock(pi_mutex_t *m) {
    task_t *self = current_task;

    while (m->locked) {
        /* Priority inheritance: boost the owner up to our priority so a
         * low-priority holder can't be preempted by an unrelated
         * medium-priority task while a high-priority task waits on it
         * (classic priority-inversion fix). */
        if (self->base_prio > m->owner->cur_prio) {
            if (!m->boosted) {
                m->owner_base_prio_saved = m->owner->cur_prio;
                m->boosted = 1;
            }
            task_set_priority(m->owner, self->base_prio);
            uart_printf("[PI] %s inherits priority %d from %s (was blocking on mutex)\n",
                        m->owner->name, self->base_prio, self->name);
        }

        self->state = TASK_BLOCKED;
        waiters_push(m, self);
        sched_yield();          /* blocks here until woken by unlock() */
    }

    m->owner = self;
    m->locked = 1;
}

void pi_mutex_unlock(pi_mutex_t *m) {
    task_t *self = current_task;
    if (m->owner != self) return;   /* not the owner: no-op (guard) */

    if (m->boosted) {
        uart_printf("[PI] %s restored to base priority %d\n", self->name, m->owner_base_prio_saved);
        task_set_priority(self, m->owner_base_prio_saved);
        m->boosted = 0;
    }

    m->owner = 0;
    m->locked = 0;

    task_t *w = waiters_pop_highest(m);
    if (w) {
        ready_enqueue(w);   /* wakes it; it will re-attempt the lock loop */
    }
}
