	.export _scroll_up
	.export _scroll_down
	.export _plot_char
	.export _clear_lines
	.export _clear_across
	.export _cursor_on
	.export _cursor_off
	.export _cursor_disable
	.export _vtattr_notify

	.export _vdp_init
	.export _vdp_type
	.export _vdp_load_font
	.export _vdp_wipe_consoles
	.export _vdp_restore_font
	.export _vdp_text40
	.export _vdp_text32
	.export _vdp_setup40
	.export _vdp_setup32
	.export _vdp_set
	.export _vdp_readb
	.export _vdp_out
	.export _vdp_setup
	.export _vdp_set_console
	.export _vdp_setborder
	.export _vdp_setcolour

_vdp_text40:
	.byte 0x00, 0xD0, 0x00, 0x00, 0x02, 0x00, 0x00, 0xF4
_vdp_text32:
	.byte 0x00, 0xC2, 0x00, 0x80, 0x02, 0x76, 0x03, 0xF4

_vdp_setup:
	push	p3
	ld	ea,2,p1
	ld	p2,ea
vdp_setup:
	ld	p3,=0xFE98		; control port
	ld	ea,=0x8008		; E = 0x80 (reg), A = 0x08 (count)
	st	a,:__tmp
	; P3 is the registers
	; P2 is the table
setupl: ld	a,@1,p2			; get table entry
	st	a,1,p3
	xch	a,e
	st	a,0,p3
	add	a,=1
	xch	a,e
	dld	:__tmp
	bnz	setupl
	pop	p3
	ret

_vdp_readb:
	jsr	_vdp_set
	ld	a,0,p3
	ret

_vdp_out:
	ld	p3,=0xFE98
	ld	a,0,p3
	ret

_vdp_init:
_vdp_setup40:
	push	p3
	ld	p2,=_vdp_text40
	jsr	vdp_setup
	ld	p2,=scrollconf40
setscroll
	ld	p3,=_scrolld_base
	ld	ea,@2,p2
	st	ea,@2,p3
	ld	ea,@2,p2
	st	ea,@2,p3
	ld	ea,@2,p2
	st	ea,@2,p3
	ld	ea,@2,p2
	st	ea,@2,p3
	ld	ea,@2,p2
	st	ea,@2,p3
	pop	p3
	ret

_vdp_setup32:
	ld	p2,=_vdp_text32
	jsr	vdp_setup
	ld	p2,=scrollconf32
	bra	setscroll

_vdp_type:
	push	p3
	ld	p3,=0xFE98
	ld	a,=0x07
	st	a,1,p3
	ld	a,=0x8F
	st	a,1,p3
	ld	a,1,p3
	ld	a,=0x02
	st	a,1,p3
	ld	a,=0x8F
	st	a,1,p3
vdpwait:
	ld	a,1,p3
	ld	e,a
	and	a,=0x60
	bnz	not9918a		; bit 5 or 6 went set
	ld	a,e
	bp	vdpwait
	;  blank happened so it's a 9918A or similar 
	ld	a,=0x00
	ret
not9918a:	; Version register time
	ld	a,=0x01
	st	a,1,p3
	ld	a,#0x8F
	st	a,1,p3
	ld	a,1,p3
	rr	a
	and	=0x1F
	add	=1
	ld	e,a		; save result
	ld	a,=0
	st	a,1,p3
	ld	a,=0x8F
	st	a,1,p3
	ld	a,e
	pop	p3
	ret

;
;	Load font to stash
;
_vdp_load_font
	push	p3
	ld	p3,=0xFE98
	ld	ea,=0x7C00
	st	a,1,p3
	xch	a,e
	st	a,1,p3
	ld	p2,=_fontdata_6x8
	; Write 256 bytes of clear
	ld	ea ,=0
wipelow:
	xch	a,e
	st	a,0,p3
	xch	a,e
	sub	a,=1
	bnz	wipelow
	jsr	lf256
	jsr	lf256
lf256:
	ld	a,0
l256:
	xch	a,e
	ld	a,@1,p2
	sl	a
	sl	a
	st	a,0,p3
	xch	a,e
	sub	a,=1
	bnz	l256
	pop	p3
	ret

_vdp_restore_font:
	push	p3
	ld	p3,=0xFE98
	ld	a,1,p3
	ld	p2,=__tmp	; we will use tmp/tmp2/hireg as a workspace
	ld	a,=4		; pages to do (128 characters input)
	st	a,4,p2		; counter of pages
	ld	a,=0
	st	a,0,p3		; low pointer count
	ld	a,=0x3C
	st	a,1,p3		; source 3C00
	ld	a,=0x50		; destination 0x1000 (and write) char 0+
	st	a,2,p3
	ld	a,=0x54		; destination 0x1400 (and write) char 128+
	st	a,3,p3
fontnext:
	ld	a,0,p2		; set the read pointer
	st	a,1,p3
	ld	a,1,p2
	st	a,1,p3
	ld	a,0,p3		; read byte
	ld	a,0,p2
	ld	e,a
	st	a,1,p3
	ld	a,2,p2		; target 1
	st	a,1,p3
	ld	a,e
	st	a,0,p3		; write it in
	ld	a,0,p2
	st	a,1,p3
	ld	a,3,p2		; target 2 (inverted)
	st	a,1,p3
	ld	a,e
	xor	a,=255
	st	a,0,p3
	ild	a,0,p2
	bnz	fontnext
	; Bumped over 256 bytes
	ild	a,1,p3		; bump source page
	ild	a,2,p3		; dest page
	ild	a,3,p3		; inverted page
	dld	a,4,p3		; count
	bnz	fontnext
	pop	p3
	ret

_vdp_wipe_consoles:
	push	p3
	ld	p3,=0xFE98
	ld	ea,=0x4000	; 0x0000, write
	st	a,1,p3
	ld	a,e
	st	a,1,p3
	; Now write 4096 spaces
	ld	ea,=0x1000
	st	ea,:__tmp
wipe_con:
	ld	a,=0x20
	st	a,0,p3
	dld	a,:__tmp
	bnz	wipe_con
	dld	a,:__tmp+1
	bnz	wipe_con
	pop	p3
	ret

_vdp_setcolour:
	ld	p3,=0xFE98
	ld	ea,=0x6000		; 0x2000 in the VDP, for write
	st	a,1,p3
	ld	a,e
	st	a,1,p3
	; now write 32 entries
	ld	a,=0x20
set_cmap:
	xch	a,e
	ld	a,2,p1			; colour to use
	st	a,0,p3
	xch	a,e
	sub	a,=1
	bnz	set_cmap
	ret

_vdp_setborder:
	push	p3
	ld	ea,=0x8700
	ld	a,2,p1
	bra	vdp_set
_vdp_set:
	ld	ea,2,p1
vdp_set:
	ld	p3,=0xFE98
	st	a,1,p3
	ld	a,e
	st	a,1,p3
	pop	p3
	ret

videopos:
	ld	p3,=0xFE98
	st	ea,:__tmp		; Save YX in __tmp
	ld	a,=0
	xch	a,e			; E = 0
	sl	a
	sl	a
	sl	a			; Y x 8
	st	ea,:__tmp2		; save in case we need for x40
	ld	p2,=_vidmode
	ld	a,0,p2
	bnz	pos32			; are we in 32 or 40 column ?
	ld	ea,:__tmp2		; 40 column
	sl	ea
	sl	ea			; Y x 32
	add	ea,:__tmp2
	st	ea,:__tmp2		; Y x 40 in __tmp2
postty:
	ld	p2,=_outputtty		; console
	ld	a,0,p2
	sub	a,=1
	sl	a
	sl	a			; * 4 (1K per screen)
	xch	a,e
	ld	a,:__tmp		; load X into the low bits (won't carry - CHECK THIS)
	add	ea,:__tmp2		; add in Y * 40
	add	ea,=0x4000		; and set for write
	ret
pos32:
	ld	ea,:__tmp2
	sl	ea
	sl	ea
	bra	postty

_plot_char:
	push	p3			; save C reg var
	ld	a,s			; copy and save IRQ flag
	push	a
	and	s,=0xFE			; interrupts off
	ld	a,5,p1			; Y
	xch	a,e
	ld	a,7,p1			; X
	; Coordinates now in EA
	jsr	videopos
	st	a,1,p3			; EA is the values for the 9918A
	xch	a,e
	st	a,1,p3			; coordinates set
	ld	a,9,p1
	st	a,0,p3			; write symbol
popret:
	pop	a
	ld	s,a			; recover irq state
	pop	p3			; restore C reg var
	ret

_clear_lines:
	push	p3
	ld	a,s
	push	a
	and	s,=0xFE
	ld	a,7,p1
	bz	popret			; No work ?
	ld	a,5,p1
	xch	a,e
	ld	a,=0
	jsr	videopos		; starting line
	st	a,1,p3
	ld	a,e
	st	a,1,p3
	ld	p2,=_scrollu_w		; Width
nline:
	ld	a,0,p2
	st	a,:__tmp
doline:
	ld	a,0x20
	st	a,0,p3			; Space
	dld	a,:__tmp
	bnz	doline
	dld	a,7,p1			; Count down lines
	bnz	nline
	bra	popret

_scroll_up:
	push	p3
	ld	a,s
	push	a
	and	s,=0xFE
	ld	a,=23			; Always 24 lines so 23 to scroll (1
					; to clear)
	st	a,:__tmp2+1		; line counter
	ld	p2,=_outputtty
	ld	a,0,p2
	sub	a,=1
	sl	a
	sl	a
	ld	e,a
	ld	a,=0
	ld	p2,=_scrollu_w
	add	ea,0,p2
	; Second line in EA
	ld	t,ea
	ld	ea,0,p2		; width
	st	ea,:__tmp2	; save width for use as we walk
	st	a,__tmp		; and initialize count for first pass
	ld	p3,=0xFE98
	ld	ea,t

downline:
	ld	p2,=scrollbuf
	st	a,1,p3
	ld	a,e
	st	a,1,p3		; read line 2
up_0:
	ld	a,0,p3
	st	a,@1,p2
	dld	a,:__tmp
	bnz	up_0

	; Data is now in the working buffer
	ld	ea,t		; address we are working from
	ld	p2,=_scrollu_mov
	add	ea,0,p2		; now points to the target and is in write
	st	a,1,p3
	ld	a,e
	st	a,1,p3		; write mode pointer set
	ld	a,:__tmp2
	st	a,:__tmp	; counter
	ld	p2,=scrollbuf
up_1:
	ld	a,@1,p2
	st	a,0,3
	dld	a,:__tmp
	bnz	up_1

	ld	a,:__tmp2
	st	a,:__tmp	; counter back
	;	get the address back (input side)
	ld	ea,t
	add	ea,:__tmp2	; add the width to move down a line
	dld	a,@tmp+1	; lines
	bnz	downline
	jmp	popret		; and done

_scroll_down:
	push	p3
	ld	a,s
	push	a
	and	s,=0xFE
	ld	a,=23			; Always 24 lines so 23 to scroll (1
					; to clear)
	st	a,:__tmp2+1		; line counter
	ld	p2,=_outputtty
	ld	a,0,p2
	sub	a,=1
	sl	a
	sl	a
	ld	e,a
	ld	a,=0
	ld	p2,=scrolld_base	; start of bottom line
	add	ea,0,p2
	; Bottom but one line in EA
	ld	t,ea
	ld	p2,=scrollu_w	; width
	ld	ea,0,p2		; width
	st	ea,:__tmp2	; save width for use as we walk
	st	a,__tmp		; and initialize count for first pass
	ld	p3,=0xFE98
	ld	ea,t

upline:
	ld	p2,=scrollbuf
	st	a,1,p3
	ld	a,e
	st	a,1,p3		; read line 2
down_0:
	ld	a,0,p3
	st	a,@1,p2
	dld	a,:__tmp
	bnz	down_0

	; Data is now in the working buffer
	ld	ea,t		; address we are working from
	ld	p2,=_scrolld_s1
	add	ea,0,p2		; now points to the target and is in write
	st	a,1,p3
	ld	a,e
	st	a,1,p3		; write mode pointer set
	ld	a,:__tmp2
	st	a,:__tmp	; counter
	ld	p2,=scrollbuf
down_1:
	ld	a,@1,p2
	st	a,0,3
	dld	a,:__tmp
	bnz	down_1

	ld	a,:__tmp2
	st	a,:__tmp	; counter back
	;	get the address back (input side)
	ld	ea,t
	sub	ea,:__tmp2	; subtract the width to move up a line
	dld	a,@tmp+1	; lines
	bnz	downline
	jmp	popret		; and done

_clear_acrosss:
	push	p3
	ld	a,s
	push	a
	and	s,=0xFE
	ld	a,5,p1			; Y
	xch	a,e
	ld	a,7,p1			; X
	jsr	videopos
	st	a,1,p3
	ld	a,e
	st	a,1,p3			; set position
nchar:
	ld	a,=0x20
	st	a,0,p3
	dld	a,9,p1
	bnz	nchar
	bra	popret

chktty:
	ld	p2,=_outputtty
	ld	a,0,p2
	ld	p2,=_inputtty
	sub	a,0,p2
notvis:
	ret

_cursor_on:
	jsr	chktty
	bnz	notvis
	push	p3
	ld	a,s	
	push	a
	and	s,=0xFE
	ld	p2,=_cursorpos
	ld	a,5,p1
	ld	e,a
	ld	a,7,p1		; EA is now Y,X
	st	ea,0,p2
	jsr	videopos
	ld	t,ea
	st	a,1,p3
	ld	a,e
	sub	a,=0x40		; we want read mode
	st	a,1,p3
	ld	a,0,p3
	ld	p2,=cursorpeek
	st	a,0,p2
	xor	a,=0x80
putcur:
	push	a
	ld	ea,t
	st	a,1,p3
	ld	a,e
	st	a,1,p3
	pop	a
	st	a,0,p3		; and put the character
	bra	popret

_cursor_off:
	jsr	chktty
	push	p3
	bnz	notvis
	ld	a,s
	push	a
	and	s,=0xFE
	ld	p2,=_cursorpos
	ld	ea,0,p2
	ld	t,ea
	ld	p2,=cursorpeek
	ld	a,0,p2
	bra	putcur

_vtattr_notify:
	ret

_vdp_set_console:
	push	p3
	ld	p2,=_inputtty
	ld	p3,=0xFE98
	ld	a,0,p2
	sub	a,=1
	st	a,1,p2
	ld	a,=0x82
	st	a,1,p2
	pop	p3
	ret

;
;	Figure out where these belong
;
	.data

cursorpos:	.word	0
cursorpeek:	.byte	0
_scrolld_base:	.word	0x03C0
_scrolld_mov:	.word	0xFFD8
_scrolld_s1:	.word	0x4028
_scrollu_w:	.word	0x0028
_scrollu_mov:	.word	0x3FD8

	.literal

scrollconf40:
	.dw 0x03C0, 0xFFD8, 0x4028, 0x0028, 0x3FD8
scrollconf32:
	.dw 0x02E0, 0xFFE0, 0x4020, 0x0020, 0x3FE0
