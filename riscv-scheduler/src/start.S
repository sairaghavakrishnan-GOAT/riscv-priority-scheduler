/* start.S — boot entry, trap vector, and context switch for the scheduler.
 *
 * We stay in machine mode the whole time (no U-mode split), so a "context
 * switch" here means: on a trap (timer interrupt or an ecall used as a
 * voluntary yield), save the interrupted task's registers onto its OWN
 * stack, hand control to C to pick the next task, then restore whichever
 * task's frame the scheduler selected and mret back into it.
 */

.section .text.init
.global _start
.extern main
.extern _boot_stack_top

_start:
    /* Boot stack only — real tasks get their own stacks once running. */
    la      sp, _boot_stack_top

    /* Zero .bss */
    la      t0, _bss_start
    la      t1, _bss_end
1:
    bge     t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b
2:
    /* Install trap handler (direct mode: mtvec[1:0] = 00) */
    la      t0, trap_entry
    csrw    mtvec, t0

    call    main
hang:
    wfi
    j       hang

/* ---- Trap frame layout (128 bytes), saved at the interrupted task's sp ----
 * offset  reg
 *   0     ra
 *   4     gp
 *   8     tp
 *  12     t0
 *  16     t1
 *  20     t2
 *  24     s0
 *  28     s1
 *  32     a0
 *  36     a1
 *  40     a2
 *  44     a3
 *  48     a4
 *  52     a5
 *  56     a6
 *  60     a7
 *  64     s2
 *  68     s3
 *  72     s4
 *  76     s5
 *  80     s6
 *  84     s7
 *  88     s8
 *  92     s9
 *  96     s10
 * 100     s11
 * 104     t3
 * 108     t4
 * 112     t5
 * 116     t6
 * 120     mepc
 * 124     mstatus
 */
.equ FRAME_SIZE, 128

.global trap_entry
.align 4
trap_entry:
    addi    sp, sp, -FRAME_SIZE
    sw      ra,   0(sp)
    sw      gp,   4(sp)
    sw      tp,   8(sp)
    sw      t0,  12(sp)
    sw      t1,  16(sp)
    sw      t2,  20(sp)
    sw      s0,  24(sp)
    sw      s1,  28(sp)
    sw      a0,  32(sp)
    sw      a1,  36(sp)
    sw      a2,  40(sp)
    sw      a3,  44(sp)
    sw      a4,  48(sp)
    sw      a5,  52(sp)
    sw      a6,  56(sp)
    sw      a7,  60(sp)
    sw      s2,  64(sp)
    sw      s3,  68(sp)
    sw      s4,  72(sp)
    sw      s5,  76(sp)
    sw      s6,  80(sp)
    sw      s7,  84(sp)
    sw      s8,  88(sp)
    sw      s9,  92(sp)
    sw      s10, 96(sp)
    sw      s11, 100(sp)
    sw      t3,  104(sp)
    sw      t4,  108(sp)
    sw      t5,  112(sp)
    sw      t6,  116(sp)

    csrr    t0, mepc
    sw      t0, 120(sp)
    csrr    t0, mstatus
    sw      t0, 124(sp)

    /* current_task->sp = sp  (task_t.sp is the struct's first field) */
    la      t0, current_task
    lw      t0, 0(t0)
    sw      sp, 0(t0)

    mv      a0, sp              /* pass interrupted frame ptr, mostly unused */
    call    trap_handler_c      /* C code updates current_task if switching */

    /* sp = current_task->sp  (may now point at a different task's stack) */
    la      t0, current_task
    lw      t0, 0(t0)
    lw      sp, 0(t0)

    lw      t0, 120(sp)
    csrw    mepc, t0
    lw      t0, 124(sp)
    csrw    mstatus, t0

    lw      ra,   0(sp)
    lw      gp,   4(sp)
    lw      tp,   8(sp)
    lw      t0,  12(sp)
    lw      t1,  16(sp)
    lw      t2,  20(sp)
    lw      s0,  24(sp)
    lw      s1,  28(sp)
    lw      a0,  32(sp)
    lw      a1,  36(sp)
    lw      a2,  40(sp)
    lw      a3,  44(sp)
    lw      a4,  48(sp)
    lw      a5,  52(sp)
    lw      a6,  56(sp)
    lw      a7,  60(sp)
    lw      s2,  64(sp)
    lw      s3,  68(sp)
    lw      s4,  72(sp)
    lw      s5,  76(sp)
    lw      s6,  80(sp)
    lw      s7,  84(sp)
    lw      s8,  88(sp)
    lw      s9,  92(sp)
    lw      s10, 96(sp)
    lw      s11, 100(sp)
    lw      t3,  104(sp)
    lw      t4,  108(sp)
    lw      t5,  112(sp)
    lw      t6,  116(sp)
    addi    sp, sp, FRAME_SIZE
    mret

/* void yield(void) — voluntary preemption point, used e.g. while blocking
 * on a mutex. Traps via ecall into the same handler as the timer ISR. */
.global yield
yield:
    ecall
    ret
