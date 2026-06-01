
	.export	_longjmp

	.code

_longjmp:
	ld	ea,4,p1		; return value
	ld	t,ea
	or	a,e
	bnz	retok
	ld	t,=1		; return 1 if 0 is passed
retok:
	ld	ea,2,p1		; get the setjmp buffer
	ld	p2,ea		; pointer to buffer
	ld	ea,4,p2		; get p3
	ld	p3,ea		; restore register variable
	ld	ea,2,p2		; stack pointer recovery
	ld	p1,ea		; stack fixed
	ld	ea,0,p2		; return address
	st	ea,0,p1		; put it back on the stack top
	ld	ea,t		; get return value to use
	ret
