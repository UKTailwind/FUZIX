    .common
    
    export _plot_char
    export _scroll_down
    export _scroll_up
    export _cursor_on
    export _cursor_off
    export _cursor_disable
    export _clear_lines
    export _clear_across
    export _do_beep
    export _vtattr_notify
    export _vtattr_cap


vt_start:
    push af
    ld a, 0x04
    out (0x7f), a
    pop af
    ret

vt_wait:
    push af
vt_wait_loop:
    in a, (0x77)
    cp 1
    jr z, vt_wait_loop
    pop af
    ret    

    ; Expects character in A
vt_charout:
    ;call vt_wait
    push af
    ; Check inverse
    ld a, (inverse)
    or a
    jr z, vt_charout_noinv
    pop af
    or 0x80
    jr vt_charout_out
vt_charout_noinv:
    pop af
vt_charout_out:
    out (0x76), a

    ; Expects X in D, Y in E
vt_set_pos:
    push af
    ; Set X
    call vt_wait
    ld a, d
    out (0x01), a
    ; Set Y
    call vt_wait
    ld a, e
    out (0x02), a
    pop af
    ret

_vtattr_cap:
    .byte 1     ; Inverse video
inverse:
    .byte 0

_vtattr_notify:
    ld a, (_vtattr)
    and 1
    dec a
    cpl
    ld (inverse), a
    ret

_do_beep:
    ret


_clear_across:
    call vt_start
   
    ld hl, #6
    add hl, sp
    ld a,(hl)
    dec hl
    dec hl
    ld d,(hl)
    dec hl
    dec hl
    ld e,(hl)

    or a
    ret z

    ; Y in E, X in D, count in B 
    call vt_set_pos
 
    
    ld hl, #6
    add hl, sp
    ld b,(hl)

clear_across_loop:
    call vt_wait
    ld a, #' '
    call vt_charout
    inc d
    call vt_set_pos
    djnz clear_across_loop
clear_across_done:
    ret

_clear_lines:
    push bc
    ld hl, #6
    add hl, sp
    ld b,(hl) ; count in B
    dec hl
    dec hl
    ld e,(hl)

    call vt_start

    ; Move cursor
    ld d, 0
    call vt_set_pos
 
clear_lines_loop:
    call vt_set_pos
    call vt_wait
    ; Clear to EOL
    ld a, 1
    out (0x08), a
    ; Next line
    inc e
    ld a, e
    out (0x02), a
    djnz clear_lines_loop
    
    pop bc 

    ret

_scroll_down:
    call vt_start
    call vt_wait
    ld a, 0x01
    out (0x05), a
    ret    

_scroll_up:
    call vt_start
    call vt_wait
    ld a, 0x01
    out (0x04), a
    ret    


_cursor_on:
    ld hl, 2
    add hl, sp
    ld e, (hl)
    inc hl
    inc hl
    ld d, (hl)

    call vt_start

    call vt_set_pos

    call vt_wait
    ld a, 0x01
    out (0x03), a
    ret


_cursor_off:
_cursor_disable:
    call vt_start
    call vt_wait
    ld a, 0x00
    out (0x03), a
    ret

_plot_char:
    call vt_start
 
    ld hl, #6
    add hl, sp
    ld a,(hl)
    dec hl
    dec hl
    ld d,(hl)
    dec hl
    dec hl
    ld e,(hl)

    ; Y is in E, X in D and char in A
    
    call vt_set_pos
    call vt_charout
 
    ret    
