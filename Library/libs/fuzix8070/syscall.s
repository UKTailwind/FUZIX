;
;	Return munging for 8070
;
	.code
	.export __syscall

__syscall:
	call __text	; syscall stub is at text
			; start, set by kernel
	; return in P2, error in EA (E always 0)
	bnz	error
	ld	ea,p2	; return the retunr value
	ret

error:	
	ld	p2,=_errno
	st	ea,0,p2		; error path - save errno
	ld	ea,=-1		; and return -1
	ret
