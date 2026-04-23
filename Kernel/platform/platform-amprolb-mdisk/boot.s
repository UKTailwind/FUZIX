;
;	Bootstrap for the Ampro LB
;
;	We do this raw so we can work it off floppy even if the machine
;	doesn't have a 4.3 BIOS with a bugfree direct SCSI driver. We should
;	figure out some kind of HGEN compatible installation that somewhere
;	still has partition info
;
;	TODO: Would actually probably be better load a packed image from CP/M
;	in the MDISK case
;
	.area _MAIN(ABS)

	.org 0xFE00

        .include "kernel.def"

;	We actually get loaded as a CP/M file at 0x0100 then blow up CP/M

bootstrap:
	; Move to the top of RAM from 0x9000 so we have an easy memory
	; load.
	di
	ld	sp,#0xFFFF	; Out of the way
	ld	hl,#0x0100	; CP/M load address
	ld	de,#0xFE00
	ld	bc,#0x01F0
	ldir

	jp	go		; into the copied version

go:
	; Configure serial first of all

	ld hl,#dart_setup
	ld bc,#0xA00 + DARTA_C		; 10 bytes to SIOA_C
	otir
	ld hl,#dart_setup
	ld bc,#0x0C00 + DARTB_C		; and to SIOB_C
	otir

	; Shut the CTC up just in case

	ld a,#0x03
	out (CTC_CH0),a
	out (CTC_CH1),a
	out (CTC_CH2),a
	out (CTC_CH3),a

	; Set for 9600 baud serial

	ld a,#0x47
	out (CTC_CH0),a
	ld a,#0x13
	out (CTC_CH0),a
	ld a,#0x47
	out (CTC_CH1),a
	ld a,#0x13
	out (CTC_CH1),a

	call	outs
	.ascii	"FUZIX Loader"
	.byte	13,10
	.byte	0

	;	Now load the image.

	in	a,(0x29)
	and	#7
	cp	#7
	jr	nz, no_reset
	call	outs
	.ascii	"Resetting SCSI bus ... "
	.byte	0
	ld	a, #B_RST
	out	(ncr_cmd),a
	ld	b,#0
r_wait:
	djnz	r_wait
	xor	a
	out	(B_RST),a
	ld	hl,#0
re_wait:
	dec	hl
	ld	a,h
	or	l
	jr	nz, re_wait
	; Hopefully all settled
	call	outs
	.ascii	"OK"
	.byte	13,10
	.byte	0
no_reset:
retry:
	xor	a
	out	(ncr_mod),a
	out	(ncr_cmd),a
	out	(ncr_tgt),a

	in	a,(0x29)
	and	#7
	ld	b,a
	ld	a,#1
	jr	z, dev0
self_bit:
	rlca
	djnz	self_bit
;
;	No multimastering so this is the short cut
;
dev0:
	ld	c,a
	or	#1		; target 0
	out	(ncr_data),a
	ld	a,#B_ABUS
	out	(ncr_cmd),a
	ld	a,#B_ASEL|B_ABUS
	out	(ncr_cmd),a

	call	outs
	.ascii	"Drive 0"
	.byte	0
;
;	Wait for selection
;
	ld	hl,#0
selwait:
	in	a,(ncr_bus)
	and	#B_BUSY
	jr	nz, selected
	dec	hl
	ld	a,h
	or	l
	jr	nz, selwait
fail:
	call	outs
	.ascii	" retrying."
	.byte	13,10,0
	jp	retry

scsi_read6:
	.byte	0x08
	.byte	0x00
	.byte	0x00
	.byte	0x02		; Start at block 2
	.byte	0x7E		; load 63K straight
	.byte	0x00

selected:	
	;	Drop SEL keep data on bus
	ld	a,#B_ABUS
	out	(ncr_cmd),a
	xor	a
	;	and then drop bus
	out	(ncr_cmd),a
 	call	outs
	.ascii	": loading "
	.byte	0
	ld	hl,#0x0100	; Start of load (0100-FCFF)
	ld	de,#scsi_read6	; Read command for lots of blocks
	ld	bc,#ncr_data	; Force a print of the hex, set port
phase:
	in	a,(ncr_bus)
	and	#B_MSG|B_CD|B_IO
	rra
	rra
	out	(ncr_tgt),a
	cp	#1
	jr	z,data_in
	cp	#2
	jr	z,cmdout
	cp	#3
	jr	z, statusin
	call	outs
	.byte	13,10
	.ascii	"Drive 0 u/s - retrying."
	.byte	13,10,0
	jp	retry

statusin:
	ld	hl,#0		; throw status away
data_in:
	in	a,(ncr_bus)
	bit	5,a		; REQ ?
	jr	nz, data_r
	and	#B_BUSY
	jr	z, complete	; Busy dropped = end of command
	jr	data_in		; spin our wheels waiting
data_r:
	in	a,(ncr_st)	; Did the phase change ?
	and	#B_PHAS
	jr	z, phase	; Yes - go to new phase
	ini			; Get data
	ld	a,h
	cp	b		; Have we changed 256 byte block ?
	jr	nz, noout
	ld	b,a		; Remember new page
	call	outh
	ld	a,#8		; Backspace over it
	call	outc
	ld	a,#8
	call	outc
noout:
	ld	a,#B_AACK	; Ack pulse
	out	(ncr_cmd),a
	xor	a
	out	(ncr_cmd),a
	jr	data_in
cmdout:
	ld	a,#B_ABUS
	out	(ncr_cmd),a
	in	a,(ncr_bus)
	bit	5,a		; Wait for REQ
	jr	nz, data_w
	and	#B_BUSY		; Dropped busy means the game is up
	jr	z, complete
	jr	cmdout		; Keep waiting for REQ
data_w:
	in	a,(ncr_st)	; Check we didn't change phase
	and	#B_PHAS
	jp	z,phase
	ld	a,(de)
	out	(c),a		; Put command byte on the bus
	inc	de
	ld	a,#B_AACK|B_ABUS	; Waggle ACK
	out	(ncr_cmd),a
	xor	a
	out	(ncr_cmd),a
	jr	cmdout

complete:
	call	outs
	.byte	13,10
	.ascii  "Starting FUZIX"
	.byte	13,10,0
	jp	0x0100		; TODO put discardable bootstrap code lower

outh:
	push	af
	rra
	rra
	rra
	rra
	call	outhd
	pop	af
outhd:
	and	#0x0F
	add	#'0'
	cp	#'9' + 1
	jr	c, outc
	add	#7
outc:
	push	af
twait:
	xor	a
	out	(DARTA_C),a
	in	a,(DARTA_C)
	and	#0x04
	jr	z, twait
	pop	af
	out	(DARTA_D),a
	ret
outs:
	pop	hl
outsl:
	ld	a,(hl)
	inc	hl
	or	a
	jr	z, outsdone
	call	outc
	jr	outsl
outsdone:	
	jp	(hl)

dart_setup:
	.byte 0x00
	.byte 0x18		; Reset
	.byte 0x04
	.byte 0x44		; x16 (9600 baud) 8N1
	.byte 0x01
	.byte 0x1F		; interrupts for IM2
	.byte 0x03
	.byte 0xC1		; 8 bits
	.byte 0x05
	.byte 0xEA		; DTR low tx enable
	.byte 0x02
dart_irqv:
	.byte 0x10		; IRQ vector (write to port B only)
