#
;
;	TI TMS7000 series low level code
;
#include "../cpu-tms7000/kernel-tms7000.def"

;
;	CPU setup and properties. For our purposes all TMS7000 is the same
;
		.data

.export _sys_cpu
.export _sys_cpu_feat
.export _sys_stubs

_sys_cpu:
	.byte 22		; TMS7000
_sys_cpu_feat:
	.byte 0

_sys_stubs:
	br	@unix_syscall_entry
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop

.export deliver_signals_2
.export deliver_signals

		.common

justret:
	rets

deliver_signals:
	movd	%U_DATA__U_CURSIG,r13
	lda	*r13
	jz	justret
	; signal code is in A
deliver_signals_2:
	movd	%U_DATA__U_SIGVEC+1,r13
	mov	a,b	; save signal
	add	a,a
	add	a,r14
	adc	%0,r13
	; Pointer is now correct
	lda	*r13		; high
	mov	a,r2
	clr	a
	sta	*r13
	
	decd	r13
	lda	*r13
	mov	a,r3		; vector is now r2/r3
	clr	a
	sta	*r13		; and cleared in the table

	; r2,r3 is the vector, b is the signal, vector has been cleared
	mov	r2,a
	or	r3,a
	jz	raced

	; Build the C return frame
	decd	r15
	mov	b,a
	sta	*r15
	dec	r15
	clr	a
	sta	*r15

	movd	%U_DATA__U_CURSIG,r13
	clr	a
	sta	*r13

	; TODO - need to deal with C and CPU stacks being separated
	eint
	call	*r3		; call vector
	dint
raced:
	movd	%U_DATA__U_SYSCALL_SP+1,r13
	mov	r15,a
	sta	*r13
	decd	r13
	mov	r14,a
	sta	*r13		; Saved SP
	movd	%kstack_top,r15

;FIXME	clr	_int_disabled	; we keep this in register space as its asm only
	call	@map_kernel_di
	call	@_chksigs
	call	@map_proc_always_di
	movd	%U_DATA__U_SYSCALL_SP+1,r13
	lda	*r13
	mov	a,r15
	decd	r13
	lda	*r13
	mov	a,r14
	jmp	deliver_signals

.export unix_syscall_entry

;
;	On entry our call frame looks like
;
;	arguments
;
;	the caller expects r6-r9 to be preserved
;	on entry b is the call num
;	on exit r2,r3 is the error r4,r5 is the code, c is set if error
;
unix_syscall_entry:
	dint
	movd	%U_DATA__U_CALLNO,r13
	mov	b,a
	sta	*r13			; save call number

	; Now block copy the args
	movd	%U_DATA__U_ARGN+7,r13
	movd	r15,r11			; and copy SP
	add	%7,r11
	adc	%0,r10			; go from end as only have decd

	lda	*r11
	sta	*r13
	decd	r11
	decd	r13
	lda	*r11
	sta	*r13
	decd	r11
	decd	r13
	lda	*r11
	sta	*r13
	decd	r11
	decd	r13
	lda	*r11
	sta	*r13
	decd	r11
	decd	r13
	lda	*r11
	sta	*r13
	decd	r11
	decd	r13
	lda	*r11
	sta	*r13
	decd	r11
	decd	r13
	lda	*r11
	sta	*r13
	decd	r11
	decd	r13
	lda	*r11
	sta	*r13

	movd	%U_DATA__U_INSYS,r13
	mov	%1,a
	; We are now in kernel space
	sta	*r13
	
	; Switch stacks
	movd	%U_DATA__U_SYSCALL_SP+1,r13
	mov	r15,a
	sta	*r13
	decd	r13
	movd	r14,a
	movd	%kstack_top,r15

	;
	; Now map the kernel and call it
	;
	call	@map_kernel_di
	eint
	call	@_unix_syscall

	; r2,r3 is the return from unix_syscall

	;
	; Remember fork and execve don't necessarily return this way and fork
	; can do it twice
	;
	dint
	call	@map_proc_always
	clr	a
	movd	%U_DATA__U_INSYS,r13
	sta	*r13
	; Switch stack back
	movd	%U_DATA__U_SYSCALL_SP+1,r13
	lda	*r13
	mov	a,r15
	decd	r13
	lda	*r13
	mov	a,r14

	; Get the return info
	movd	%U_DATA__U_RETVAL+3,r13		; actually U_ERROR low byte
	lda	*r13
	mov	a,r3
	decd	a
	lda	*r13
	mov	a,r2
	decd	a
	lda	*r13
	mov	a,r5
	decd	a
	lda	*r13
	mov	a,r4

	movd	%U_DATA__U_CURSIG,r13
	lda	*r13
	jnz	via_signal
unix_return:
	mov	r3,r0				; check errno is set ?
	jz	not_error
	eint					; sets carry for some reason
	rets
not_error:
	eint
	clrc					; force carry off
	rets

via_signal:
	;
	; Stack the state (a signal doing a syscall will change the
	; U_DATA fields but we must return the old error/status)
	;
	push	r5
	push	r4
	push	r3
	push	r2
	;
	; And into the signal delivery path
	;
	call	@deliver_signals_2
	pop	r2
	pop	r3
	pop	r4
	pop	r5
	jmp	unix_return

;
;	Called when execve() completes to transition to the user, as we
;	don't return from execve() via the syscall path
;
;
.export _doexec

_doexec:
	dint
	call	@map_proc_always
	; Get the argument
	movd	r15,r13
	add	%1,r13
	adc	%0,r12
	lda	*r13
	mov	a,r3
	decd	r13
	lda	*r13
	mov	a,r2		; address in r2/r3

	; set up the stack
	movd	%U_DATA__U_ISP+1,r13
	lda	*r13
	mov	a,r15
	decd	r13
	lda	*r13
	mov	a,r14

	; TODO set up the asm stack
	; mov	%CPUSTK,b
	; LDSP
	
	; No longer in syscalls
	movd	%U_DATA__U_INSYS,r13
	clr	a
	sta	*r13
	eint
	; jump to the passed address
	br	*r3
;
;	NULL trap. Must live in common space
;
;	FIXME: Rewrite 68000 style as a synchronous trap
;
.export null_handler

null_handler:
	movd	%U_DATA__U_INSYS,r13
	lda	*r13
	jnz	trap_illegal
	movd	%U_DATA__U_ININTERRUPT,r13
	lda	*r13
	jnz	trap_illegal

	; do a bus error
	mov	%7,a
	decd	r15
	sta	*r15
	clr	a
	decd	r15
	sta	*r15
	movd	%U_DATA__U_PTAB+1,r13
	lda	*r13
	mov	a,r5
	decd	r13
	lda	*r13
	mov	a,r4
	add	%P_TAB__P_PID_OFFSET+1,r5
	adc	%0,r4
	lda	*r5
	decd	r15
	sta	*r15
	decd	r5
	lda	*r5
	decd	r15
	sta	*r15
	mov	%39,b
	; signal
	call	@unix_syscall_entry
	mov	%0xff,a
	decd	r15
	sta	*r15
	decd	r15
	sta	*r15
	clr	b		; exit(-1);
	call	@unix_syscall_entry
	; Never returns

trap_illegal:
	movd	%illegalmsg,r13
trapr14:
	call	@outstring
	call	@_plt_monitor

.export nmi_handler

nmi_handler:
	call	@map_kernel_di
	movd	%nmimsg,r13
	jmp	trapr14

illegalmsg:
	.ascii '[illegal]'
	.byte 0
nmimsg:
	.ascii '[NMI]'
	.byte 0

;
;	Interrupts are similar to Z8 but the dual stack is complicated and
;	we have no alt banks
;
.export interrupt_handler

reinterrupt:
	; Interrupt during interrupt but on alt stack (eg swap)
;TODO	mov	%1,_int_disabled
	push	r0
	push	r1
	push	r2
	push	r3
	push	r4
	push	r5
	push	r10
	push	r11
	push	r12
	push	r13

	call	@_plt_reinterrupt

	pop	r13
	pop	r12
	pop	r11
	pop	r10
	pop	r5
	pop	r4
	pop	r3
	pop	r2
	pop	r1
	pop	r0
	reti

interrupt_handler:
	push	r0
	push	r1
	push	r2
	push	r3
	push	r4
	push	r5
	push	r10
	push	r11
	push	r12
	push	r13

	call	@plt_interrupt_all

	movd	%U_DATA__U_ININTERRUPT,r13
	lda	*r13
	jnz	reinterrupt

;TODO	movd	r15,istack_switched_sp+1
	movd	%istack_top,r15

	;
	; Map the kernel
	;
	call	@map_save_kernel
	;
	; Set up state and enter kernel
	;
	movd	%U_DATA__U_ININTERRUPT,r13
	mov	%1,a
	sta	*r13
;TODO	mov	a,_int_disabled

	call	@_plt_interrupt

	;
	; Do we need to task switch ?
	;
	movd	%_need_resched,r13
	lda	*r13
	jnz	preemption
	;
	; Switch stacks back
	;
	call	@map_restore
;TODO	movd	istack_switched_sp+1,15
intout:
	movd	%U_DATA__U_ININTERRUPT,r13
	clr	a
	sta	*r13

	movd	%U_DATA__U_INSYS,r13
	lda	*r13
	jnz	interrupt_pop
	call	@deliver_signals
	;
	; Restore registers and done
	;
interrupt_pop:
;FIXME	clr	_int_disabled
	pop	r13
	pop	r12
	pop	r11
	pop	r10
	pop	r5
	pop	r4
	pop	r3
	pop	r2
	pop	r1
	pop	r0
	reti

trap_signal:
	; r4,r5 is signal
	mov	r5,a
	decd	r15
	sta	*r15
	mov	r4,a
	decd	r15
	sta	*r15
	mov	%<U_DATA__U_PTAB,a
	decd	r15
	sta	*r15
	mov	%>U_DATA__U_PTAB,a
	decd	r15
	sta	*r15
	br	@_ssig

;
;	Now the scary stuff - preempting
;	
preemption:
	movd	%_need_resched,r13
	clr	a
	sta	*r13
	;
	;	Save our original stack in syscall_s
	;	Move to our kernel stack (free because we don't preempt
	;	in kernel
	;
;TODO
	movd	%U_DATA__U_SYSCALL_SP+1,r13
;FIXME	mov	istack_switched_sp+1,a
	sta	*r13
	decd	r13
;FIXME	mov	istack_switched_sp+1,a
	sta	*r13
	movd	%kstack_top,r15
	;
	;	Mark ourselves as in a system call
	;
	movd	%U_DATA__U_INSYS,r13
	mov	%1,a
	sta	*r13
	call	@_chksigs
	movd	%U_DATA__U_PTAB+1,r13
	lda	*r13
	mov	a,r3
	decd	r13
	lda	*r13
	mov	a,r2
	lda	*r3		; process state byte
	cmp	%P_RUNNING,a
	jnz	not_running
	mov	%P_READY,a
	sta	*r3
	;
	;	Punish the process for using all of its time.
	;
	add	%1,r3
	adc	%0,r2
	lda	*r2
	or	%PFL_BATCH,a
	sta	*r2

not_running:
	;
	;	We will disappear into this and reappear somewhere else. In
	;	time we will reappear here
	;
	call	@_plt_switchout
	;
	;	We are back in the land of the living so no longer in
	;	syscall or interrupt state
	;
	clr	a
	movd	%U_DATA__U_ININTERRUPT,r13
	sta	*r13
	movd	%U_DATA__U_INSYS,r13
	sta	*r13
	;
	;	Get our mapping back
	;
	call	@map_proc_always_di
	;
	;	And our stack
	;
	movd	%U_DATA__U_SYSCALL_SP+1,r13
	lda	*r13
	mov	a,r15
	decd	r13
	lda	*r13
	mov	a,r14

	movd	%U_DATA__U_CURSIG,r13
	lda	*r13
	jz	nosigcall
	call	@deliver_signals_2
nosigcall:
	jmp	interrupt_pop

;
;	Debug code
;
.export outstring

outstring:		; print string in *r13
	lda	*r13
	jz	outsdone
	call	@outchar
	add	%1,r13
	adc	%0,r12
	jmp	outstring
outsdone:
	rets

.export outstringhex

outstringhex:
	lda	*r13
	jz	outsdone
	call	@outcharhex
	mov	%' ',a
	call	@outchar
	add	%1,r13
	adc	%0,r12
	jmp	outstringhex

.export outnewline

outnewline:
	mov	%0x0d,a
	call	@outchar
	mov	%0x0a,a
	jmp	outchar

; TODO outregs - just dump 0-15 ?

.export outr2

outr2:
	push	r3
	mov	r2,r3
	call	@outcharhex
	pop	r3
	call	@outcharhex
	rets

.export outr14

outr14:
	push	r3
	mov	r14,r3
	call	@outcharhex
	mov	r15,r3
	call	@outcharhex
	pop	r3
	rets

.export outcharhex

outcharhex:
	; Print hex of a
	push	st
	push	a
	swap	a
	call	@outnibble
	pop	a
	push	a
	call	@outnibble
	pop	a
	pop	st
	rets

outnibble:
	and	%0x0F,a
	cmp	%10,a
	jnc	numeral
	add	%7,a
numeral:
	add	%0x30,a		; '0'
	jmp	outchar


.export ___hard_ei

___hard_ei:
;FIXME	clr	_int_disabled
	eint
	rets

.export ___hard_di

___hard_di:
;FIXME	mov	_int_disabled,r5
	clr	r4
	dint
;FIXME	mov	%1,_int_disabled
	rets

.export ___hard_irqrestore

___hard_irqrestore:
	; need to grab low byte of first arg TODO check w v b
	lda	*r15
;FIXME	mov	a,_int_disabled
	dint
	or	a,a
	jnz	diret
	eint
diret:
	br	@__cleanup2

;
;	We have no variants to care about
;
.export _cpu_detect

_cpu_detect:
	rets

.export _set_cpu_type

_set_cpu_type:
	rets

	.export _memcpy
	.export _memset
	.export _strlen
	.export _swab
	.export _swab32

; FIXME can't use cleanup helpers if not switching return to stacks

_memcpy:
	clr	r13
	call	@__gargr4
	mov	%4,r13
	call	@__garg10r2
	; r10/11 is length
	; r2/r3 dest
	; r4/r5 src
	decd	r11		; so we wrap on done
	jnc	mdn
	add	r11,r3		; work from end to use decd
	adc	r10,r2
	add	r11,r5
	adc	r10,r4
mcloop:
	lda	*r5
	sta	*r3
	decd	r5
	decd	r3
	decd	r11
	jc	mcloop
mdn:
	; r2/r3 is dest so return iit
	movd	r3,r5
	jmp	__cleanup6
	
_memset:
	clr	r13
	call	@__gargr4	; r2/r3 is pointer r5 is write value
	mov	%4,r13
	call	@__garg10r2
	; r12/13 is length
	mov	r5,a
	mov	r10,r4
	or	r11,r4
	jz	msdone
	decd	r11		; we want to catch ffff wrap
	jnc	msdone
	add	r11,r3
	adc	r10,r2		; do from end to start
msloop:
	sta	*r3
	decd	r3		; move back
	decd	r11
	jc	msloop
	;	At this point r3 points to the start so
	;	return it
msdone:
	movd	r3,r5
	br	@__cleanup6

_strlen:
	clr	r13
	call	@__garg10r2
	; r10,r11 is now ptr
	movd	%0,r5
count:
	lda	*r11
	jz	strlen_done
	add	%1,r11
	adc	%0,r10
	add	%1,r5
	adc	%0,r4
	jmp	count
strlen_done:
	br	@__cleanup2

_swab:
	clr	r13
	call	@__gargr2
	mov	r3,a
	mov	r2,r3
	mov	a,r2
	br	@__cleanup2

_swab32:
	clr	r13
	call	@__gargr4
	mov	r5,a
	mov	r2,r5
	mov	a,r2
	mov	r4,a
	mov	r3,r4
	mov	a,r3
	br	@__cleanup4
