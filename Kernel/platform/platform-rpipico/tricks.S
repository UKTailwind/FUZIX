#include "kernel-armm0.def"

.global udata
.cpu cortex-m0

udata = progbase

.text
.thumb_func
/* weak: the PC3 display/keyboard config overrides this with a C version
   that pumps the USB host stack before napping (usbkbd.c) */
.weak plt_idle
plt_idle:
    wfi
    bx lr

.text
.thumb_func
fatal_exception_handler_trampoline:
    mrs r0, IPSR
    mov r1, sp
    push {r0, r1, r4, r5, r6, r7}
    mov r0, r8
    mov r1, r9
    mov r2, r10
    mov r3, r11
    push {r0, r1, r2, r3}
    mov r0, sp
    ldr r1, =__StackTop
    mov sp, r1
    bl fatal_exception_handler

.global isr_hardfault
isr_hardfault = fatal_exception_handler_trampoline
    
.global isr_svcall
.text
.thumb_func
# On entry, sp points at the stack frame.
isr_svcall:
    # This is executing in handler mode. We can't run system calls in handler
    # mode because weird things happen with interrupts, so instead we fake a call
    # to isr_svcall_user and return back to thread mode.

    ldr r0, [sp, #6*4]  /* Load old PC */
    ldr r1, =svc_pc_stash
    str r0, [r1]        /* Save old PC */
    ldr r0, =isr_svcall_user /* Load user-mode ISR */
    str r0, [sp, #6*4]  /* Update old PC */
    bx lr               /* Return out of Handler mode */

isr_svcall_user:
    # At this point, we're running in thread mode. All registers (with the
    # exception of PC) are as they were when the user program called svc,
    # Essentially, they called us as if they were a normal procedure call,
    # except we need to preserve lr.

    push {r0, r1, r2, r3}       /* push parameters */
    mov r0, r12
    ldr r1, =svc_pc_stash
    ldr r1, [r1]
    push {r0, r1, lr}           /* save r12, PC, LR */

    mov r0, sp                  /* get pointer to svc frame */
    ldr r1, =(udata + UDATA_SIZE_ASM) /* switch to kernel stack */
    mov sp, r1

    push {r0}
    bl syscall_handler          /* call C system call handler */
    pop {r0}

    mov sp, r0                  /* restore stack */
    bl deliver_signals          /* may not return if signal handler longjmps out */

    pop {r0, r2, r3}            /* r2 is new PC, r3 is the old lr */
    mov lr, r3
    mov r12, r0
    pop {r0, r1}                /* pop return values */
    add sp, #8                  /* discard the old r2 and r3 */
    mov pc, r2                  /* can't use bx as r2 doesn't have the thumb bit set */

.comm svc_pc_stash, 4

# ---------------------------------------------------------------------------
# Pre-emption. The timer tick pends PendSV when the running user process is
# due for a reschedule or has a pending signal. As with isr_svcall we cannot
# do kernel work in handler mode, so - if and only if the interrupted PC is
# in user space (preempt_lo..preempt_hi, set by preempt_init) - we redirect
# the stacked PC to the preempt_user trampoline, which runs the same
# switchout / chksigs / deliver_signals path a syscall would, then resumes
# the interrupted code with all registers and flags intact. The user-space
# PC check also protects every kernel window (the svc stash, the idle loop,
# this trampoline itself) from being re-entered.

.global isr_pendsv
.text
.thumb_func
isr_pendsv:
    ldr r0, =preempt_armed
    ldrb r1, [r0]
    cmp r1, #0
    bne 1f
    ldr r1, [sp, #6*4]          /* stacked PC */
    ldr r2, =preempt_lo
    ldr r2, [r2]
    cmp r1, r2
    blo 1f
    ldr r2, =preempt_hi
    ldr r2, [r2]
    cmp r1, r2
    bhs 1f
    movs r2, #1                 /* arm the trampoline */
    strb r2, [r0]
    ldr r0, =pendsv_pc_stash
    str r1, [r0]
    ldr r0, =preempt_user
    movs r2, #1                 /* the .thumb_func symbol has bit 0 set, but a
                                   stacked exception-return PC must have it
                                   clear (INVSTATE hard fault otherwise) */
    bic r0, r2
    str r0, [sp, #6*4]
1:
    bx lr

.thumb_func
preempt_user:
    /* Thread mode, on the process stack, at an arbitrary interrupted
       instruction: preserve everything, including the flags. */
    sub sp, #4                  /* reserved slot for the resume PC */
    push {r0, r1, r2, r3}
    mrs r0, apsr
    mov r1, r12
    mov r2, lr
    push {r0, r1, r2}
    ldr r0, =pendsv_pc_stash
    ldr r0, [r0]
    movs r1, #1
    orr r0, r1                  /* thumb bit for the final pop {pc} */
    push {r0}
    mov r0, sp
    ldr r1, =(udata + UDATA_SIZE_ASM)
    mov sp, r1                  /* kernel stack, as for a syscall */
    push {r0}
    bl preempt_handler
    pop {r0}
    mov sp, r0                  /* back onto the process stack */
    bl deliver_signals          /* may not return if a handler longjmps */
    pop {r3}                    /* resume PC (thumb bit set) */
    pop {r0, r1, r2}            /* apsr, r12, lr */
    mov r12, r1
    mov lr, r2
    ldr r1, =preempt_armed      /* disarm: a new PendSV between here and */
    movs r2, #0                 /* the pop sees a kernel PC and bails */
    strb r2, [r1]
    msr apsr_nzcvq, r0
    str r3, [sp, #16]           /* resume PC into the reserved slot */
    pop {r0, r1, r2, r3, pc}

.comm pendsv_pc_stash, 4
.comm preempt_armed, 4

.global plt_switchout
.global switchin
.text
.thumb_func
plt_switchout:
	/* Save the callee-saved registers onto the stack in the same order
	 * that switchout will do it, so switchin will restore it correctly.
	 * r4 included: switchin clobbers it (ptab scratch), and a preempted
	 * context's r4 is live user state - AAPCS requires it preserved. */

    push {r4, r5, r6, r7, lr}
    mov r1, r8
    mov r2, r9
    mov r3, r10
    mov r4, r11
    mov r5, r12
    push {r1, r2, r3, r4, r5}

	/* Dummy value used as the return value for fork. */

    push {r0}

	/* Save stack pointer */

    ldr r1, =udata
    mov r2, sp
    str r2, [r1, #U_DATA__U_SP_OFFSET]

    /* Find the next process to run. */

    bl getproc

	/* Fall through into switchin */

    /* On entry, r0 is the ptab ptr of the process to run */
.thumb_func
switchin:
    ldr r1, =__StackTop     /* switch to swapper stack */
    mov sp, r1

    mov r4, r0              /* save r0 (ptab ptr) in callee-saved registers */
    bl contextswitch        /* may swap a process in */
	isb

	/* Make the new process runnable. */

    mov r0, #P_RUNNING
    strb r0, [r4, #P_TAB__P_STATUS_OFFSET]

	/* Restore process stack pointer and return. */

    ldr r0, =udata
    ldr r1, [r0, #U_DATA__U_SP_OFFSET]
    mov sp, r1

    mov r0, #0
    ldr r1, =runticks
    strh r0, [r1]

    pop {r0}                /* pushed pid */
    pop {r1, r2, r3, r4, r5}
    mov r8, r1
    mov r9, r2
    mov r10, r3
    mov r11, r4
    mov r12, r5
    pop {r4, r5, r6, r7, pc}

.global dofork
.text
.thumb_func
# r0 = ptab of the new child process
dofork:
	/* Save the callee-saved registers onto the stack in the same order
	 * that switchout will do it, so switchin will restore it correctly
	 * (r4 included, see plt_switchout). */

    push {r4, r5, r6, r7, lr}
    mov r1, r8
    mov r2, r9
    mov r3, r10
    mov r4, r11
    mov r5, r12
    push {r1, r2, r3, r4, r5}

	/* Store child's pid so the parent can return it. */

    ldrh r1, [r0, #P_TAB__P_PID_OFFSET]
    push {r1}

	/* Save parent's stack pointer */

    ldr r1, =udata
    mov r2, sp
    str r2, [r1, #U_DATA__U_SP_OFFSET]

	/* Copy the current process to the new parent. */

    push {r0, r1}
    mov r4, sp          /* save SP in callee-saved register */
    ldr r1, =__StackTop /* switch to the swapper stack */
    mov sp, r1
    bl clonecurrentprocess
    isb
    mov sp, r4          /* restore old stack */
    pop {r0, r1}

	/* We are now going to become the child; associate the child's p_tab
	 * pointer with the current udata. */

	/* r0 is already the child's p_tab pointer; r1 points at udata. */
    bl makeproc
	
	/* We are now ready to return. */

    mov r0, #0
    ldr r1, =runticks
    strh r0, [r1]

    pop {r0}            /* pushed pid */
    mov r0, #0          /* Child process returns 0 */
    pop {r1, r2, r3, r4, r5}
    mov r8, r1
    mov r9, r2
    mov r10, r3
    mov r11, r4
    mov r12, r5
    pop {r5, r6, r7, pc}

// vim: sw=4 ts=4 et:

