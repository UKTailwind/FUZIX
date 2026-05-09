;
; Startup code for 6502 on FUZIX
;
; Partly based on code by Debrune Jérôme <jede@oric.org>
; and Ullrich von Bassewitz <uz@cc65.org> 2014-08-22, Greg King
;

	.export __syscall_hook
	.export _environ

	.code

__syscall_hook:				; Stubs overlay this
head:
	.word 	$80A8
	.byte	3			; 6502 family
	.byte	0			; 6502 (we don't yet use 65C02 ops)
	.byte	0			; Load address page
	.byte	0			; No hint bits
	.word	__data
	.word	__data_size
	.word	__bss_size
	.byte 	<start			; Offset from load page as entry
	.byte	0			; No size hint
	.byte	0			; No stack hint
	.byte	<__zp_size		; ZP size

	.word	__sighandler		; IRQ path signal handling helper
	.word	0			; Relocations

;
;	First cut at sighandling stubs, horrible on 6502!
;
;	The user stack may be invalid. If we hit mid update then the low
;	byte may be updated but the carry into the high byte not done.
;
;	I think therefore that providing we dec sp+1 we are always ok and
;	will be somewhere between 0 and 255 bytes of valid
;
__sighandler:
	dec	@sp+1		; ensure we are safe C stack wise
	; Now make a 14 byte save area
	ldy	#14
	jsr	__subysp
	jsr	stash_zp	; saves sp etc
	lda	#0x4C		; jmp xxxx
	sta	@tmp
	pla
	sta	@tmp+1		; ZP was swapped
	pla
	sta	@tmp+2		; Patch jmpvec
	pla			; Signa number
	ldx	#0		; signal(sig)
	jsr	@tmp		; no jsr (x) so fake it
	jsr	stash_zp	; recovers everything
	ldy	#14		; 14 bytes to recover
	jsr	__addysp	; Stack back as it was
	inc	@sp+1		; back to old stack
initmainargs:			; Hardcoded compiler dumbness
	rts			; will return via the kernel stub
;
;
;	On entry sp/sp+1 have been set up by the kernel (this has to be
;	done in kernel as we might take a signal very early on). Above the
;	sp are the argument vectors, environment etc. In other words we
;	are basically a straight function call from the kernel stub.
;
;	Our image has been loaded into RAM, any spare memory has been zeroed
;	for us and we are ready to roll.
;
start:
; Push the command-line arguments; and, call main().
;
; Need to save the environment ptr. The rest of the stack should be
; fine.
;
;	Fix our break (it may be off because of relocation packing and
;	only we know the link time value we want
;
	clc
	lda	#<__bss
	adc	#<__bss_size
	sta	@tmp
	lda	#>__bss
	adc	#>__bss_size
	tax
	lda	@tmp
	jsr	__push
	; syscall - trim the break correctly
	jsr	_brk

	lda	sp
	ldx	sp+1
	clc
	adc	#4
	bcc	l1
	inx
l1:	sta	_environ
	stx	_environ+1
	ldy	#2		; 2 bytes of args
        jsr     _main
	jsr	__push
	jmp	_exit		; exit cleanup, AX holds our return code
				; for a fastcall return to nowhere.

;
; Swap the C temporaries - smaller than separate save/loaders
;
stash_zp:
	ldy	#0
stash_loop:
	lda	(@sp),y		; swap stack offset
	pha
	ldx	@sp+2,y		; and ZP variable
	txa
	sta	(@sp),y
	pla
	tax
	stx	@sp+2,y
	iny
	cpy	#14		; 14 bytes of save area
	bne	stash_loop
	rts

	.bss
_environ:	.word	0
