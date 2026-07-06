;
;	Initial 6303/6803 crt0.
;

	.code
	.setcpu 6803

	.export _environ
	.export head

head:
	.word	$80A8
	.byte	2		;	6800 series
	.byte	0		;	Needs only 6800 features
	.byte   1		;	Load at 0x100
	.byte	0		;	No hints
	.word	__data-0x0100	;	Code
	.word	__data_size	;	Data
	.word	__bss_size
	.byte	<start		;	Offset to execute from
	.byte	0		;	No size hint
	.byte	0		;	No stack hint
	.byte	0		;	No hint bits

	.word   __sighandler	;	TODO: signals

;
;	This function is called when we need to deliver a signal. We can't
;	just blindly stack stuff as we can on big machines because we have
;	non-reentrancy issues in the compiler temporary and regvar usage
;
;	On entry
;	B = signal number
;	X = undefined
;
;	Return address is the correct route back to the kernel. Above it is
;	a copy of the vector and an RTI frame.
;
__sighandler:
	; Save compiler temporaries and dp register variables
	ldx @tmp
	pshx
	ldx @tmp1
	pshx
	ldx @tmp2
	pshx
	ldx @hireg
	pshx
	ldx @tmp3
	pshx
	ldx @tmp4
	pshx
	pshb		; signal number
	clra
	psha		; extended to 16bits

	;
	;	Fishing time. Our vector is up the stack above all the
	;	stuff we pushed

	tsx
	ldx 16,x
	jsr ,x
	pulx		; signal number
	pulx
	stx @tmp4
	pulx
	stx @tmp3
	pulx
	stx @hireg
	pulx
	stx @tmp2
	pulx
	stx @tmp1
	pulx
	stx @tmp
	rts

start:
	clrb
	stab	@zero
	stab	@zero+1
	incb
	stab	@one+1
	tsx
	ldab	#4
	abx
	stx	_environ
	; Now call main
	jsr	_main
	pshb
	psha
	jmp	_exit

	.bss
_environ:
	.word	0
