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


_vtattr_cap:
    .byte 0
    .byte 0

_vtattr_notify:
_do_beep:
    ret


_clear_across:
    push bc
    ld a, 0x04
    out (0x7f), a
    
    ld hl, #8
    add hl, sp
    ld b, (hl)
    dec hl
    dec hl
    ld d, (hl)
    dec hl
    dec hl
    ld e, (hl)
    ; Y in E, X in D, count in B

    ; Move cursor
    ld a, d
    out (0x01), a
    ld a, e
    out (0x02), a
    
clear_across_loop:
    ld a, ' '
    out (0x76), a
    djnz clear_across_loop
    pop bc
 
    ret

_clear_lines:
    push bc
    ld hl, #6
    add hl, sp
    ld b,(hl) ; count in B
    dec hl
    dec hl
    ld e,(hl)

    ld a, 0x04
    out (0x7f), a
    ; Move cursor
    ld a, 0
    out (0x01), a
    ld a, e
    out (0x02), a
    
clear_lines_loop:
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
    ld a, 0x04
    out (0x7f), a
    ld a, 0x01
    out (0x05), a
    ret    

_scroll_up:
    ld a, 0x04
    out (0x7f), a
    ld a, 0x01
    out (0x04), a
    ret    


_cursor_on:
    ld a, 0x04
    out (0x7f), a
    ld a, 0x01
    out (0x03), a
    ret


_cursor_off:
_cursor_disable:
    ld a, 0x04
    out (0x7f), a
    ld a, 0x00
    out (0x03), a
    ret

_plot_char:
    ld a, 0x04
    out (0x7f), a
    
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
    
    push af
    ; Move cursor
    ld a, d
    out (0x01), a
    ld a, e
    out (0x02), a
    
    pop af   
 
    ; Write character
;plot_char_wait:
;    in a, (0x76)
;    cp 1
;    jr z, plot_char_wait

;    ld a, c
    out (0x76), a

    ret    
