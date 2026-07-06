;
;        amstrad cpc vt primitives
;
        ;imported symbols
        .globl _int_disabled
        .globl _outputtty
        .globl _inputtty
        
        ; exported symbols
        .globl cpc_plot_char
        .globl cpc_scroll_down
        .globl cpc_scroll_up
        .globl cpc_cursor_on
        .globl cpc_cursor_off
	.globl cpc_cursor_disable
        .globl cpc_clear_lines
        .globl cpc_clear_across
        .globl cpc_do_beep
	.globl _fontdata_8x8
	.globl _vtattr
        .globl _screenbase
        .globl _screenpage
        .globl _CRTC_offset


	.if CPCVID_ONLY
_plot_char:
	.endif
cpc_plot_char:
        pop hl
        pop de              ; D = x E = y
        pop bc
        push bc
        push de
        push hl
	push de
        call videopos
        ld h, #0            ; calculating offset in font table
        ld l, c
        add hl,hl
        add hl,hl
        add hl,hl
        ld bc, #_fontdata_8x8
        add hl, bc          ; hl points to first byte of char data
        VIDEO_MAP
        ld bc, #0x7f8a ;RMR ->UROM disable LROM enable
        out (c),c
        ld bc,#0x800
        ex af,af'
        ld a,(_vtattr)
        and #3          ;we only use the two lower bits, underline and inverted
        or a            ;reset carry flag
        rra             ;f' stores c=1 if inverted z=0 if underline
        ex af,af'
        ld a,#7
        ex de,hl
plot_char_line:
        ex af,af'
        ld a,(de)
        ld (hl),a
        ex af,af'
        add hl,bc
        inc de
        dec a
        jr nz,plot_char_line
        ex af,af'
        ld a,(de)
        ld bc, #0x7f8e ;RMR ->UROM disable LROM disable
        out (c),c
	; We do underline and inverted for now - not clear italic or bold are useful
	; with the font we have.
	jr nz, last_ul
plot_ll:
        jr nc,not_inverted
        cpl
not_inverted:    
        ld (hl),a
        ex af,af'
	pop de
	VIDEO_UNMAP
        ret
last_ul:
	ld a,#0xff
	jr plot_ll

	.if CPCVID_ONLY
_clear_across:
	.endif
cpc_clear_across:
        pop hl
        pop de              ; DE = coords 
        pop bc              ; C = count
        push bc
        push de
        push hl
	ld a,c
	or a
	ret z		    ; No work to do - bail out
	push de
        call videopos       ; first pixel line of first character in DE
        ld h,d
        ld l,e
        ld b,c
        ld c,#8
        xor a
        VIDEO_MAP
clear_line:
        push bc
clear_scanline:        ;see firmware SCR_NEXT_BYTE
        ld (de),a
        inc e
        jr nz,clear_scanline_cont
        inc d
        ld a,d
        and #7
        jr nz,clear_scanline_cont_xora
        ld a,d
        sub #8
        ld d,a
clear_scanline_cont_xora:
        xor a        
clear_scanline_cont:
        djnz clear_scanline
        ld bc,#0x800
        add hl,bc
        pop bc
        ld d,h
        ld e,l
        dec c
        jr nz,clear_line
	pop de
	VIDEO_UNMAP
        ret

	.if CPCVID_ONLY
_cursor_on:
	.endif
cpc_cursor_on:
        ld a,(_inputtty)
        ld b,a
        ld a,(_outputtty)
        cp b
        ret nz        
        pop hl
        pop de
        push de
        push hl
        call videopos
        VIDEO_MAP
        ld (cursorpos), de
xor_cursor:
        ld b,#8
        push de
        pop hl
        ld de,#0x800
next_cursor_byte:        
        ld a,(hl)
        cpl
        ld (hl),a
        add hl,de
        djnz next_cursor_byte
	VIDEO_UNMAP
        ret

	.if CPCVID_ONLY
_cursor_off:
_cursor_disable:
	.endif
cpc_cursor_disable:        
cpc_cursor_off:
        ld a,(_inputtty)
        ld b,a
        ld a,(_outputtty)
        cp b
        ret nz
        VIDEO_MAP
        ld de, (cursorpos)              
        jr xor_cursor


cursorpos:
        .dw 0

;kernel and video maps share upper 16K ram bank at 0xc000-0xffff where common and kstack is placed 
map_kernel:
        push bc
	ld bc,#0x7fc2
	out (c),c
        pop bc
	push af
        ld a, (_int_disabled)
	or a
	jp z,no_int
	ei
no_int:
        pop af
	ret

map_video:
        push bc
        push af
        ld bc,#0x7fc3
        ld a,(_outputtty)
        cp #4
        jr z,tty4
	ld c,#0xc1
tty4:        
	di
        out (c),c
        pop af
        pop bc
	ret
    

.area _VIDEO

	.if CPCVID_ONLY
_clear_lines:
	.endif
cpc_clear_lines:
        pop hl
        pop de              ; E = line, D = count
        push de
        push hl
	; This way we handle 0 correctly
	inc d
	jr nextline

clear_next_line:
        push de
        ld d, #0            ; from the column #0
        ld b, d             ; b = 0
        ld c, #80           ; clear 80 cols
        push bc
        push de
        call _clear_across
        pop hl              ; clear stack
        pop hl
        pop de
        inc e

nextline:
        dec d
        jr nz, clear_next_line
        ret

	.if CPCVID_ONLY
_scroll_up:
	.endif
cpc_scroll_up:
        ld hl, (_CRTC_offset)
        ld bc, #40           ; one crtc character are two bytes
        add hl,bc

set_hardware_scroll:
        ld a,h
        and #3
        ld h,a
        ld (_CRTC_offset),hl
        ld a,(_inputtty)
        ld b,a
        ld a,(_outputtty)
        cp b
        ret nz
_ga_set_visible_vt:        
        ld a,(#_screenpage)
        ld bc,#0xbc0c           ;select CRTC R12
        out (c),c
        inc b                
        ld hl,(_CRTC_offset)
        or h
        out (c),a
        ld bc,#0xbc0d           ;select CRTC R13
        out (c),c
        inc b
        out (c),l
        ret 

	.if CPCVID_ONLY
_scroll_down:
	.endif
cpc_scroll_down:
        ld hl, (_CRTC_offset)
        ld bc, #40           ; one crtc character are two bytes
        or a
        sbc hl,bc
        jr set_hardware_scroll

videopos: ;get x->d, y->e => set de address for top byte of char
        ;from firmware function SCR_CHAR_POSITION
        ex de,hl
        ld e,h
        ld d,#0
        ld h,d
        push de
        ld d,h
        ld e,l
        add hl,hl
        add hl,hl
        add hl,de
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        pop de
        add hl,de
        ld de,(_CRTC_offset)
        add hl,de
        add hl,de
        ld a,h
        and #0x7
        ld h,a
        ld a,(_screenbase)
        add a,h
        ld h,a
        ex de,hl
        ret
      

_screenbase:
	.db 0x0
_screenpage:
        .db 0x0
_CRTC_offset:
        .dw 0

	.if CPCVID_ONLY
_do_beep:
	.endif
cpc_do_beep:
        ld e,#4
        ld d,#38        ;channel C 110Hz
        call write_ay_reg
        ld e,#5
        ld d,#2        ;channel C 110Hz
        call write_ay_reg
        ld e,#7          
        ld d,#0x3b       ;mixer->Only channel C
        call write_ay_reg
        ld e,#0xa
        ld d,#0x10      ;Use envelope on C
        call write_ay_reg
        ld e,#0xb
        ld d,#0x86      ;100ms envelope period
        call write_ay_reg
        ld e,#0xc
        ld d,#0x1      ;100ms envelope period
        call write_ay_reg
        ld e,#0xd
        ld d,#0x9         ;Ramp down in one cicle and remain quiet
write_ay_reg: ; E = register, D = data from https://cpctech.cpc-live.com/source/sampplay.html
        ld b,#0xf4
        out (c),e
        ld bc,#0xf6c0
        out (c),c
        ld c,#0
        out (c),c
        ld b,#0xf4
        out (c),d
        ld bc,#0xf680
        out (c),c
        ld c,#0
        out (c),c
        ret