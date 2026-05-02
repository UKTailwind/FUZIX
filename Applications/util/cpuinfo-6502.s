	.65c816
	.i8
	.a8

	.export _cpu_identify
	.export _cpu_bcdtest
	.export _cpu_rortest
	.export _cpu_bitop
	.export _cpu_ce02
	.export _cpu_huc
	.export _cpu_740

_cpu_identify:
	lda #0
	sta @tmp
;	inc a
;	cmp #1
;	bmi nmos
;	xba
;	dec a
;	xba
;	inc a
nmos:	rts

_cpu_rortest:
;	lda #0x01
;	ror a
	rts

_cpu_bcdtest:
;	sed
;	lda #0x09
;	clc
;	adc #0x01
;	cld
	rts

; Only called on 65C02

	.65c02

_cpu_bitop:
	stz @tmp
;	.byte 0x87	; smb0
;	.byte @tmp	; no-op on processors without bitops
			; something else on 65C816 so don't use it there!
;	lda @tmp
	rts

_cpu_ce02:
	lda #1
;	.byte #0x43	; asr a (nop on 65C02)
	eor #1
	rts

_cpu_huc:
	ldx #1
	lda #0
;	.byte #0x22	; no-op if a normal 65C02
	rts

;
;	On a 740 0x3C is ldm. On a 65C02 it's bit 0xnnnn,x so we'll not
;	change tmp
;
_cpu_740:
	lda #0				; don't STZ - no stz on a 740
	sta @tmp			; and it decodes otherwise
;	.byte #0x3C			;ldm tmp,2
;	.byte #0x01
;	.byte @tmp
	lda @tmp
	rts

