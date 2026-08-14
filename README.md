# Preemptive Priority Scheduler for Bare-Metal RISC-V

A small preemptive, priority-based task scheduler for bare-metal RISC-V
(RV32IMA), built from scratch: boot code, trap/context-switch assembly,
a priority-ready-queue scheduler, and a priority-inheritance mutex to
fix priority inversion. Runs on QEMU's `virt` machine — no physical
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
split), a "context switch" is just: save frame → call C scheduler →
reload `sp` from whichever task the scheduler picked → restore frame →
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
deterministic — no race). Only once it holds the lock does it release
`task_med` (priority 4) and `task_high` (priority 7) into the ready
queue. `task_high` immediately blocks on the mutex; the log shows the
priority boost, `task_low` finishing and restoring its own priority,
and `task_high` finally acquiring the mutex with a measured wait.

## Build & run

Requires `gcc-riscv64-unknown-elf` (bare-metal cross toolchain) and
`qemu-system-misc` (provides `qemu-system-riscv32`):

```
sudo apt-get install gcc-riscv64-unknown-elf qemu-system-misc
make
make run
```

`make run` boots the ELF directly in QEMU with no bootloader/BIOS
(`-bios none`) and prints everything over the QEMU virt UART.

## Sample output

```
RISC-V preemptive priority scheduler booting (QEMU virt)...
12 tasks, priority-inheritance mutex demo

Created 12 tasks, starting scheduler.
[t=0] task_low: acquired mutex, doing critical work...
[t=1] task_high: requesting mutex
[PI] task_low inherits priority 7 from task_high (was blocking on mutex)
[t=3] task_low: done, releasing mutex
[PI] task_low restored to base priority 1
[t=4] task_high: acquired mutex (waited 3 ticks)
[t=47] filler-1: finished 20 loops
...
===== Context-switch latency report =====
Total timer ticks observed: 65
-------------------------------------------------------
task          switches   avg latency (cycles)
task_low      4          83309
task_med      16         13570
filler        26         70313
...
task_high     2          457165
reporter      1          217642
=========================================================
Priority inversion check: task_high acquired mutex at tick 4 (requested at tick 1)
```

## A note on the latency numbers

`avg latency (cycles)` is measured with the RISC-V `mcycle` CSR around
the trap-handling path (from trap entry to the scheduler's decision
being made). These are **QEMU TCG-emulated cycle counts**, not real
silicon timings — QEMU's instruction-count-driven `mcycle` does not
advance at a fixed real-world clock rate the way a physical RV32 core
would, and this sandbox's virtualized CPU adds its own variance. Take
the *relative* comparison between tasks as meaningful (e.g. `task_med`
switching faster than `task_high`), but don't quote the absolute
cycle numbers as if they were measured on real hardware.

## Project structure

```
linker/link.ld    Memory layout for QEMU virt RAM (0x80000000+)
src/start.S       Boot code, trap entry, and the hand-written context switch
src/sched.h/.c    Task control blocks, ready queues, scheduler, timer setup
src/mutex.h/.c    Priority-inheritance mutex
src/uart.h/.c     Minimal polling UART driver + tiny printf
src/main.c        Demo: 12 tasks, priority-inversion scenario, latency report
Makefile          Cross-compile + QEMU run targets
```

## Possible extensions

- Port the same trap/context-switch design to a real RV32 board
  (e.g. via OpenOCD/JTAG) to get real-silicon latency numbers.
- Add a proper sleep/timer-wheel API instead of busy-spin workloads.
- Extend priority inheritance to full priority-ceiling protocol.
