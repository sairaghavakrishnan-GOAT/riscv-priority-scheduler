#ifndef MUTEX_H
#define MUTEX_H

#include "sched.h"

typedef struct {
    task_t   *owner;          /* NULL if unlocked */
    int       locked;
    uint8_t   owner_base_prio_saved;  /* owner's prio before any boost via this mutex */
    int       boosted;
    task_t   *waiters;        /* intrusive list of blocked waiters (via ->next) */
} pi_mutex_t;

void pi_mutex_init(pi_mutex_t *m);
void pi_mutex_lock(pi_mutex_t *m);
void pi_mutex_unlock(pi_mutex_t *m);

#endif
