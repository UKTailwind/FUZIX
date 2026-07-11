;
;	    nano-z80 support
;
;

	; exported symbols
	.export init_hardware
	.export _program_vectors
	.export map_buffers
	.export map_kernel
	.export map_kernel_di
	.export map_proc
	.export map_proc_a
	.export map_proc_always
	.export map_proc_always_di
	.export map_save_kernel
	.export map_restore
	.export map_for_swap
	.export plt_interrupt_all
	.export _kernel_flag
	.export _int_disabled
    .export _disk_map
    .export _sd_sector0
    .export _sd_sector1
    .export _sd_sector2
    .export _sd_sector3
    .export _sd_set_sector_regs
    .export _sd_read_block
    .export _sd_write_block
    .export _sd_ptr
	; exported debugging tools
	.export _plt_monitor
	.export _plt_reboot
	.export outchar



#include "kernel.def"
#include "../../cpu-z80u/kernel-z80.def"

;
; Buffers (we use asm to set this up as we need them in a special segment
; so we can recover the discard memory into the buffer pool
;

	.export _bufpool
	.buffers

_bufpool:
	.ds BUFSIZE * NBUFS

; -----------------------------------------------------------------------------
; COMMON MEMORY BANK (kept even when we task switch)
; -----------------------------------------------------------------------------
	.common
;
;	Interrupt flag. This needs to be in common memory for most memory
;	models. It starts as 1 as interrupts start off.
;
_int_disabled:
	.byte 1
;
;	This method is invoked early in interrupt handling before any
;	complex handling is done. It's useful on a few platforms but
;	generally a ret is all that is needed
;
plt_interrupt_all:
	ret

;
;	If you have a ROM monitor you can get back to then do so, if not
;	fall into reboot.
;
_plt_monitor:
;
;	Reboot the system if possible, halt if not. On a system where the
;	ROM promptly wipes the display you may want to delay or wait for
;	a keypress here (just remember you may be interrupts off, no kernel
;	mapped so hit the hardware).
;
_plt_reboot:
	di
	; Map ROM back into lower part of memory?
    rst 0

; -----------------------------------------------------------------------------
; KERNEL MEMORY BANK (may be below 0x8000, only accessible when the kernel is
; mapped)
; -----------------------------------------------------------------------------
	.code

; -----------------------------------------------------------------------------
; DISCARD is memory that will be recycled when we exec init
; -----------------------------------------------------------------------------
	.discard
;
;	After the kernel has shuffled things into place this code is run.
;	It's the best place to breakpoint or trace if you are not sure your
;	kernel is loading and putting itself into place properly.
;
;	It's required jobs are to set up the vectors, ramsize (total RAM),
;	and procmem (total memory free to processs), as well as setting the
;	interrupt mode but *not* enabling interrupts. Many platforms also
;	program up support hardware like PIO and CTC devices here.
;
init_hardware:
    ; set system RAM size
    ld hl, 8192
    ld(_ramsize), hl
    ld hl, 8128
    ld(_procmem), hl

    ; Enable hardware TTY for now, until VT52 support is implemented
    ld a, 0x04
    out (0x7f), a
    ld a, 0x01
    out (0x0d), a
    ; And enable autoscroll
    out (0x0e), a
    ; And clear screen
    out (0x09), a	

    ld hl, 0
	push hl
	call _program_vectors
	pop hl
	
    ; Start timer and enable interrupt output
    ld a, 0x06      ; Select interrupt controler
    out (0x7f), a
    ld a, 0x01      ; Enable channel 1
    out (0x00), a
    ld a, 0x86      ; Program IM2 vector
    out (0x10), a

    ld a, 0x07      ; Select timer
    out (0x7f), a
    ld a, 0x01      ; Start timer, default setting is 100 Hz
    out (0x00), a
    
    ;xor a
    ;ld i, a
	;im 2 ; set CPU interrupt mode
    im 1
	
    ret

;
;	Bank switching unsurprisingly must be in common memory space so it's
;	always available.
;
	.common

;_int_disabled:
;        .byte 1

;spurious:
;        reti

mapreg:
	.byte 0	; Our map register is write only so keep a copy
mapsave:
	.byte 0	; Saved copy of the previous map (see map_save)

_kernel_flag:
	.byte 1	; We start in kernel mode

;
;	This is invoked with a NULL argument at boot to set the kernel
;	vectors and then elsewhere in the kernel when the kernel knows
;	a bank may need vectors writing to it.
;
_program_vectors:
    di
    pop de                          ; temporarily store return address
    pop hl                          ; function argument -- base page number    
    push hl                         ; put stack back as it was
    push de

    ; At this point the common block has already been copied
    call map_proc

    ; now install the interrupt vector at 0x0038 (shouldn't be used)

    ld a, 0xC3                      ; JP instruction
    ld (0x0038), a
    ld hl, interrupt_handler
    ld (0x0039),hl    
    
    ; set restart vector for FUZIX system calls
    ld (0x0030), a   ;  (rst 30h is unix function call vector)
    ld hl, unix_syscall_entry
    ld (0x0031), hl

    ld (0x0000), a
    ld hl, null_handler   ;   to Our Trap Handler
    ld (0x0001), hl




    ;ld (0x0067),hl

    ; IM2 vector for the CTC
    ;ld hl, #spurious
    ;ld (0x80),hl                    ; CTC vectors
    ;ld (0x82),hl
    ;ld (0x84),hl
    ;ld hl, #interrupt_handler       ; Standard tick handler
    ;ld (0x86),hl

	; and fall into map_kernel

;
;	Mapping set up for the SBCv2
;
;	The top 32K bank holds kernel code and pieces of common memory
;	The lower 32K is switched between the various user banks.
;
;	We know the ROM mapping is already off
;
;	The _di versions of the functions are called when we know interrupts
;	are definitely off. In our case it's not useful information so both
;	symbols end up at the same code.
;
map_buffers:
	; for us no difference. We could potentially use a low 32K bank
	; for buffers but it's not clear it would gain us much value
map_kernel_di:
map_kernel:
	push af
    ;ld a, 'k'
    ;call outchar
	; ROMWBW TPA is last but one bank (last bank is high space)
	; so for now we hardcode this. We should ask ROMWBW at boot
	ld a,0x00
	ld (mapreg),a
	out (0x60), a
    inc a
    out (0x62), a
	pop af
	ret
	; map_proc is called with HL either NULL or pointing to the
	; page mapping. Unlike the other calls it's allowed to trash AF
map_proc:
	ld a, h
	or l
	jr z, map_kernel
map_proc_hl:
	ld a, (hl)			; and fall through
	;
	; With a simple bank switching system you need to provide a
	; method to switch to the bank in A without corrupting any
	; other registers. The stack is safe in common memory.
	; For swap you need to provide what for simple banking is an
	; identical routine.
map_for_swap:
map_proc_a:			; used by bankfork
	;dec a		; We bias by 1 because 0 is a valid user

	;push af
    ;ld a, 'm'
    ;call outchar
    ;pop af
;    push af
;    add a,0x30
;    call outchar
;    pop af

    or a
    jr z, map_kernel ; page 0 is kernel
    push af
 
    ;add a,0x30
    ;call outchar
    ;pop af
    ;push af
   
    ld (mapreg), a	; bank
	; Calculate physical page
    add a,a
    add a,2
    ; Set page register
    out (0x60), a
    inc a
    out (0x62), a
	pop af
    ret

	;
	; Map the current process into memory. We do this by extracting
	; the bank value from u_page.
	;
map_proc_always_di:
map_proc_always:
	push af
	push hl
	ld hl, _udata + U_DATA__U_PAGE
	call map_proc_hl
	pop hl
	pop af
	ret

	;
	; Save the existing mapping and switch to the kernel.
	; The place you save it to needs to be in common memory as you
	; have no idea what bank is live. Alternatively defer the save
	; until you switch to the kernel mapping
	;
map_save_kernel:
	push af
    ld a, (mapreg)
	ld (mapsave), a
    call map_kernel
	pop af
	ret
	;
	; Restore the saved bank. Note that you don't need to deal with
	; stacking of banks (we never recursively use save/restore), and
	; that we may well call save and decide not to call restore.
	;
map_restore:
	push af
	ld a, (mapsave)
	ld (mapreg), a
    call map_proc_a
	pop af
	ret


    ; SD-card low level code

_sd_set_sector_regs:
    push af
    ld a, 0x03
    out (0x7f), a
    ld a, (_sd_sector0)
    out (0x00), a
    ld a, (_sd_sector1)
    out (0x01), a
    ld a, (_sd_sector2)
    out (0x02), a
    ld a, (_sd_sector3)
    out (0x03), a

    pop af
    ret

_sd_read_block:
    push af
    push bc
    push de
    push hl

    ld a, (_disk_map)
    call map_proc_a

    ;ld a, 'a'
    ;call outchar

    call sd_init

    ;ld a, 'b'
    ;call outchar

    ld a, 1
    out (0x05), a   ; Perform read

    call sd_init

    ;ld a, 'c'
    ;call outchar

    ; Copy data to correct address
    ;ld hl, _udata + U_DATA__U_ARGN1
    ld hl, (_sd_ptr)
    ld d, 0

sd_read_loop:
    ld a, d
    out (0x07), a
   
    ld c, 0x80
    ld b, 0x80

sd_read_loop_inner:
    in a, (c)
    ld (hl),a
    inc hl
    inc c
    djnz sd_read_loop_inner

    ;ld a, 'd'
    ;call outchar

    inc d 
    ld a,4
    cp d
    jr nz, sd_read_loop
    
    ;ld a, 'e'
    ;call outchar

    pop hl
    pop de
    pop bc
    pop af
    ret


_sd_write_block:
    ret


sd_init:
    ld a, 0x03
    out (0x7f), a
    ; Fall through
sd_wait:
    in a, (0x04)
    cp 0
    jr nz, sd_wait
    ret

_sd_sector0: .byte 0
_sd_sector1: .byte 0
_sd_sector2: .byte 0
_sd_sector3: .byte 0
_sd_ptr: .ds 2
_disk_map: .byte 0
	;
	; Used for low level debug. Output the character in A without
	; corrupting other registers. May block. Interrupts and memory
	; state are undefined
	;
outchar:
	push af
twait:	in a,(0x77)
	or a
	jr nz, twait
	pop af
	out (0x76),a
	ret



;
;	Stub helpers for code compactness: TODO

	.discard
;
;	The first two use an rst as a jump. In the reload sp case we don't
;	have to care. In the pop ix case for the function end we need to
;	drop the spare frame first, but we know that af contents don't
;	matter
;

rstblock:
