;
;	So we end up first in the image
;
; Layered over the BSS
;

		.common

		.byte $02
		.byte $65

		; Entered at 0x0402
entry:
		jmp go

		.code
go:
		sei			; interrupts off
		cld			; decimal off
		ldx #$FF
		txs			; Stack (6502 not C)

		lda #<kstack_top	; C stack
		sta @sp
		lda #>kstack_top
		sta @sp+1 

		lda #<_udata
		sta @tmp
		lda #>_udata
		sta @tmp+1

		lda #0
		tay
		ldx #2
wipeud:
		sta (@tmp),y
		iny
		bne wipeud
		inc @tmp+1
		dex
		bne wipeud

		lda #<__bss
		sta @tmp
		lda #>__bss
		sta @tmp+1

		lda #0
		tay
		ldx #>__bss_size
		beq bss_wipe_tail
bss_wiper_1:	sta (@tmp),y
		iny
		bne bss_wiper_1
		inc @tmp+1
		dex
		bne bss_wiper_1

bss_wipe_tail:
		cpy #<__bss_size
		beq gogogo
		sta (@tmp),y
		iny
		bne bss_wipe_tail

gogogo:
		jsr init_early
		jsr init_hardware
		jsr _fuzix_main		; Should never return
		sei			; Spin
stop:		jmp stop

vectors:
		; To copy into place TODO
		.word	vector
		.word	entry		; does it matter ???
		.word	nmi_handler
