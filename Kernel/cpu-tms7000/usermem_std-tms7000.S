#include "kernel-tms7000.def"

;
;	Simple implementation for now. Should be optimized
;

	.common

.export __uputc

__uputc:
	clr	r13
	call	@__gargr4	; r2-r3 val, r4-r5 ptr
	call	@map_proc_always
	mov	r3,a
	sta	*r5
	movd	%0,r5
	call	@map_kernel
	br	@__cleanup4

.export __uputw

__uputw:
	clr	r13
	call	@__gargr4	; r2-r3 val, r4-r5 ptr
	call	@map_proc_always
	mov	r2,a
	sta	*r5
	add	%1,r5
	adc	%0,r4
	mov	r3,a
	sta	*r5
	movd	%0,r5
	call	@map_kernel
	br	@__cleanup4

.export __ugetc

__ugetc:
	clr	r13
	call	@__garg10r2
	call	@map_proc_always
	lda	*r11
	mov	a,r5
	clr	r4
	call	@map_kernel
	br	@__cleanup2

.export __ugetw

__ugetw:
	clr	r13
	call	@__garg10r2
	call	@map_proc_always
	lda	*r11
	mov	a,r5
	add	%1,r11
	adc	%0,r10
	lda	*r11
	mov	a,r4
	call	@map_kernel
	br	@__cleanup2

.export __uget

;
;	Stacked arguments are src.w, dst.w, count.w
;
__uget:
	clr	r13
	call	@__gargr4	; r2/r3 = src r4/r5 = dest
	mov	%2,r13
	call	@__garg10r2	; r10/r11 = count

	; Now see if there is work to do
	decd	r11
	jnc	nowork
	add	r11,r3		; FIXME do decd trick on memcpy/memset
	adc	r10,r2
	add	r11,r5
	adc	r10,r4
	;
	;	Simple unoptimized copy loop for now. Horribly slow for
	;	things like 512 byte disk blocks
	;
ugetcopy:
	call	@map_proc_always
	lda	*r3
	call	@map_kernel
	sta	*r5
	decd	r3
	decd	r5
	decd	r11
	jnc	ugetcopy
	movd	%0,r5
	br	@__cleanup6

.export __uput

__uput:
	clr	r13
	call	@__gargr4	; r2/r3 = src r4/r5 = dest
	mov	%2,r13
	call	@__garg10r2	; r10/r11 = count

	; Now see if there is work to do
	decd	r11
	jnc	nowork
	add	r11,r3		; FIXME do decd trick on memcpy/memset
	adc	r10,r2
	add	r11,r5
	adc	r10,r4
	;
	;	Simple unoptimized copy loop for now. Horribly slow for
	;	things like 512 byte disk blocks
	;
uputcopy:
	lda	*r3
	call	@map_proc_always
	sta	*r5
	call	@map_kernel
	decd	r3
	decd	r5
	decd	r11
	jnc	ugetcopy
nowork:
	movd	%0,r5
	br	@__cleanup6

.export __uzero

__uzero:
	clr	r13
	call	@__gargr4	;	2-3 ptr 4-5 length

	decd	r5
	jnc	nowork

	add	r5,r3
	adc	r4,r2
;
;	Simple loop. Wants unrolling a bit and to use djnz
;
	call	@map_proc_always
	clr	a
zeroloop:
	sta	*r3
	decd	r3
	decd	r5
	jnc	zeroloop
	movd	%0,r5
	call	@map_kernel
	br	@__cleanup4
