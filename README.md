# Preemptive Priority Scheduler for Bare-Metal RISC-V

A small preemptive, priority-based task scheduler for bare-metal RISC-V
(RV32IMA), built from scratch: boot code, trap/context-switch assembly,
a priority-ready-queue scheduler, and a priority-inheritance mutex to
fix priority inversion. Runs on QEMU's `virt` machine - no physical
board required.

## What this demonstrates

- **Preemptive multitasking** via the machine-mode timer (CLINT):
  each task gets its own stack and full register context; a timer
  interrupt (or a voluntary `sched_yield()`) saves the running task's
  registers, lets the scheduler pick the next task, and restores it.
- **Priority scheduling** with 8 priority levels and per-level FIFO
  ready queues, selected in O(1) via a priority bitmap (`clz`).
- **Priority inheritance** to solve classic priority inversion: when a
  high-priority task blocks on a mutex held by a lower-priority task,
  the holder is temporarily boosted to the blocked task's priority so
  a medium-priority task can't indefinitely delay the handoff.
- **12 concurrent tasks** (2 scenario tasks + 8 filler workers +
  1 mutex owner + 1 reporter), matching the target task count.

## Design overview

**Trap frame.** All 32 GP registers except `x0`/`sp` are saved to the
interrupted task's own stack on trap entry, along with `mepc` and
`mstatus` (128-byte frame, see `src/start.S` for the exact layout).
Because this project stays in machine mode throughout (no U-mode
split), a "context switch" is just: save frame, call C scheduler,
reload `sp` from whichever task the scheduler picked, restore frame,
`mret`.

**Scheduler.** `src/sched.c` implements strict fixed-priority
scheduling: the highest-priority *ready* task always runs; tasks at
the same priority round-robin. Ready queues are 8 singly-linked FIFO
lists indexed by priority, with a 32-bit bitmap for O(1) "find the
highest non-empty queue."

**Priority-inheritance mutex.** `src/mutex.c` implements a mutex
where, on contention, if the blocked task outranks the current owner,
the owner's priority is boosted for the duration of the critical
section and restored on unlock. This is the standard fix for the
failure mode where an unrelated medium-priority task starves a
low-priority lock holder and thereby indirectly blocks a high-priority
waiter (the same class of bug behind the 1997 Mars Pathfinder
priority-inversion incident).

**Demo scenario** (`src/main.c`): `task_low` (priority 1) takes the
mutex first (it's the only ready task at boot, so this is
deterministic, no race). Only once it holds the lock does it release
`task_med` (priority 4) and `task_high` (priority 7) into the ready
queue. `task_high` immediately blocks on the mutex; the log shows the
priority boost, `task_low` finishing and restoring its own priority,
and `task_high` finally acquiring the mutex with a measured wait.

## Build & run

Requires `gcc-riscv64-unknown-elf` (bare-metal cross toolchain) and
`qemu-system-misc` (provides `qemu-system-riscv32`):
