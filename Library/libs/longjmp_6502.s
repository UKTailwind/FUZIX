        .export         _longjmp
	.a8
	.i8
	.6502

_longjmp:
	ldy	#1
	jsr	__gloytmp	; pointer into @tmp
	ldy	#3
	lda	(@sp),y
	sta	@tmp2
	iny
	lda	(@sp),y
	sta	@tmp2+1
	ora	@tmp2
	bne	retok		; Non zero
	inc	@tmp2		; Make it 1
retok:
	; Restore the old @sp
	ldy	#0
	lda	(@tmp),y
	sta	@sp
	iny
	lda	(@tmp),y
	sta	@sp+1
	iny
	; Ok next is S but that's complicated
;
; Detection code based on an idea by Jody Bruchon
;
; Party time we have three cases
;
; 6502/65C02/65WD02 etc		- S is 8bit
; 65C816 in compat mode		- S is 8bit
; 65C816 in native mode		- S is 16bit and we must merge the saved low
;				  8 with the current high 8 with IRQs off
;				  due to the sucky CPU design
;
	.65c02

	lda #$00
	inc a			; 65C02 or later will add one
	cmp #$01
	bmi is_8bit		; 6502so skip

	.65c816
;
;	We can now safely play with xba to see if it's an 816.
; 
	xba			; 65c02 the xba's do nothing		
	dec a			; so a goes to 0
	xba			; while 65C816 keeps it as one
	cmp #$01
	bmi is_8bit
;
;	16bit mode. Please ensure your barf bucket is to hand
;
;	If we are in compat mode then the rep/sep do nothing, we load
;	the 8bit stack pointer into X and then A, we rewrite it with the
;	8bit new S value and we write it back into S
;
;	If we are in native mode the rep/sep do stuff so we load a 16bit
;	S into X and then A, we overwrite the low 8bits with the saved S
;	and we stick the lot back together and put it into X and then S
;
	sei			; Interrupts off
	rep #$30
	.a16
	.i16
	tsx			; 16 bit stack pointer into A
	txa
	sep #$20
	.a8
        lda     (@tmp),y	; Restore low 8bits of SP only
	rep #$20
	.a16

        iny			; Move down struct

	tax			; Restore stack pointer 16bits
	txs

	sep #$30
	.a8
	.i8

	cli			; Interrupts back on
	bra pop_out

	.6502
;
; 8bit is nice and simple
;
is_8bit:	
; Get the old stack pointer
        lda     (@tmp),y
        iny
        tax
        txs

pop_out:
; Get the return address and push it on the stack
        lda     (@tmp),y
	pha
        iny
        lda     (@tmp),y
	pha

; Load the return value and return to the caller

        lda     @tmp2
        ldx     @tmp2+1
	ldy	#4
	jmp	__addysp
