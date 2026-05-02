;
;	6502 we have a live value in XA and we have a soft stack pointer
;	in SP. Y doesn't matter and XA is replaced by the return code
;
	.code

	.export __setjmp

__setjmp:
	ldy	#1
	jsr	__gloytmp
	; @tmp is now the pointer
	lda	@sp
	sta	(@tmp),y
	iny
	lda	@sp+1
	sta	(@tmp),y
	iny
	tsx
	txa
	sta	(@tmp),y
	iny
	; We have to save the top of the CPU stack - our return
	pla
	tax
	sta	(@tmp),y
	iny
	pla
	sta	(@tmp),y
	pha
	txa
	pha
	lda	#0
	tax
	ldy	#2
	jmp	__addysp

; Our buffer now contains
;	0-1:	old C sp
;	2:	old S
;	3-4:	old TOS
