#include "../build/kernel.def"
#include "kernel02.def"

		.export __uget
		.export __ugetc
		.export __ugetw
		.export __uput
		.export __uputc
		.export __uputw
		.export __uzero

;
;	These are intended as reference implementations to get a platform
;	booting. There are several things to consider once you are running
;
;	If your common memory is in RAM you could make uget/uput/uzero all
;	a single self modifying routine. We don't do it here as it's
;	reference code
;
;	If you have flexibility in the banking then odds-on it will be much
;	faster to map a bank somewhere and copy via the mapping while taking
;	care to deal with moving between banks
;
;	If you don't have the flexibility then depending upon your map
;	routines it is likely to be faster to double buffer.
;
;	These methods are intended for a 6502. The 6509 has far copiers
;	using the (foo),y addressing. In those cases you can remove the map_
;	calls but need to deal with the lack of (foo),x to do bank to bank.
;
;	@tmp and @tmp1 are reserved for map_* functions
;
;	This code relies upon the fact that the ZP is not switched when
;	switching between user and kernel for the same process.
;
;
		.common


ugparg:		; uget/put argument fetch
		; leave Y as 0
		ldy	#0
		lda	(@sp),y		; User low
		iny
		sta	@tmp4
		lda	(@sp),y		; User high
		iny
		sta	@tmp4+1
		lda	(@sp),y		; Kernel low
		iny
		sta	@tmp3
		lda	(@sp),y		; Kernel high
		iny
		sta	@tmp3+1
		lda	(@sp),y		; Length low
		iny
		sta	@tmp2
		lda	(@sp),y		; Length high
		sta	@tmp2+1

		ldy	#0
		rts

; user, dst, count(count in ax)
;
;	Decidedly unoptimised (even the 6502 could manage a word a switch)
;
__uget:
		jsr	ugparg
		; Y is now 0 for counter

		ldx	@tmp2+1		; how many 256 byte blocks
		beq	__uget_tail	; if none skip to the tail

__uget_blk:
		jsr	map_proc_always	; map the user process in
		lda	(@tmp4), y	; get a byte of user data
		jsr	map_kernel	; map the kernel back in
		sta	(@tmp3), y	; save it to the kernel buffer
		iny			; move on one
		bne	__uget_blk	; not finished a block ?
		inc	@tmp3+1		; move src ptr 256 bytes on
		inc	@tmp4+1		; move dst ptr the same
		dex			; one less block to do
		bne	__uget_blk	; out of blocks ?

__uget_tail:	cpy @tmp2		; finished ?
		beq __uget_done

		jsr map_proc_always	; map the user process
		lda (@tmp4),y		; get a byte of user data
		jsr map_kernel		; map the kernel back in
		sta (@tmp3),y		; save it to the kernel buffer
		iny			; move on
		bne __uget_tail		; always taken (y will be non zero)

__uget_done:
		lda	#0
		tax
		ldy	#6
		jmp	__addysp	; And clean up stack

__ugetc:	jsr	__pop
		sta	@tmp3
		stx	@tmp3+1
		jsr	map_proc_always
		ldy	#0
		lda	(@tmp3),y
		jmp	map_kernel

__ugetw:	jsr	__pop
		sta	@tmp3
		stx	@tmp3+1
		jsr	map_proc_always
		ldy	#1
		lda	(@tmp3),y
		tax
		dey
		lda	(@tmp3),y
		jmp	map_kernel

__uput:		jsr	ugparg

		; Y is 0 for counter

		ldx	@tmp2+1
		beq	__uput_tail
__uput_blk:
		lda	(@tmp4), y
		jsr	map_proc_always
		sta	(@tmp3), y
		jsr	map_kernel
		iny
		bne	__uput_blk
		inc	@tmp3+1
		inc	@tmp4+1
		dex
		bne	__uput_blk

__uput_tail:	cpy	@tmp2
		beq	__uget_done
		lda	(@tmp4),y
		jsr	map_proc_always
		sta	(@tmp3),y
		jsr	map_kernel
		iny
		bne	__uput_tail
		beq	__uget_done

__uputc:	ldy	#3
		lda	(@sp),y
		sta	@tmp3+1
		dey
		lda	(@sp),y
		sta	@tmp3
		dey
		lda	(@sp),y
		tax
		dey
		lda	(@sp),y
		jsr	map_proc_always
		ldy	#0		; Needed ??? Y is 0 before MPA
		sta	(@tmp3),y
		jsr	map_kernel
		tya
		tax
		ldy	#4
		jmp	__addysp

__uputw:	ldy	#3
		lda	(@sp),y
		sta	@tmp3+1
		dey
		lda	(@sp),y
		sta	@tmp3
		dey
		lda	(@sp),y
		tax
		dey
		lda	(@sp),y
		jsr	map_proc_always
		ldy	#0		; Needed ??? Y is 0 before MPA
		sta	(@tmp3),y
		iny
		txa
		sta	(@tmp3),y
		jsr	map_kernel
		lda	#0
		tax
		ldy	#4
		jmp	__addysp


__uzero:	ldy	#3
		lda	(@sp),y
		dey
		sta	@tmp2+1
		lda	(@sp),y
		dey
		sta	@tmp2
		lda	(@sp),y
		dey
		sta	@tmp3+1
		lda	(@sp),y
		sta	@tmp3

		; Our C stack vanishes at this point
		jsr map_proc_always

		ldy	#0
		tya

		ldx	@tmp2+1		; more than 256 bytes
		beq	__uzero_tail	; no - just do dribbles
__uzero_blk:
		sta	(@tmp3),y
		iny
		bne	__uzero_blk
		inc	@tmp3+1		; next 256 bytes
		dex			; are we done with whole blocks ?
		bne	__uzero_blk

__uzero_tail:
		cpy	@tmp2
		beq	__uzero_done
		sta	(@tmp3),y
		iny
		bne	__uzero_tail
__uzero_done:	jsr	map_kernel
		ldy	#4
		jmp	__addysp
