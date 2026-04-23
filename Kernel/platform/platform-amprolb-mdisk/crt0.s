        .module crt0

        ; Ordering of segments for the linker.

        .area _CODE
        .area _HOME     ; compiler stores __mullong etc in here if you use them
        .area _CODE2
        .area _CONST
        .area _INITIALIZED
        .area _DATA
        .area _BSEG
        .area _BSS
        .area _HEAP
	; Discard is loaded where process memory wil blow it away
        .area _DISCARD
        ; note that areas below here may be overwritten by the heap at runtime, so
        ; put initialisation stuff in here
        .area _BUFFERS     ; _BUFFERS grows to consume all before it (up to KERNTOP)
	; These get overwritten and don't matter
        .area _INITIALIZER ; binman copies this to the right place for us
        .area _GSINIT      ; unused
        .area _GSFINAL     ; unused
	; The rest grows upwards from C000 starting with the udata so we can
	; swap in one block, ending with the buffers so they can expand up
        .area _COMMONMEM
        .area _COMMONDATA
	.area _SERIALDATA	; basically common for us
	; Page aligned at the top, 256 bytes a port
	.area _SERIAL

        ; imported symbols
        .globl _fuzix_main
        .globl init_hardware
        .globl s__INITIALIZER
        .globl s__COMMONMEM
        .globl l__COMMONMEM
        .globl s__COMMONDATA
        .globl l__COMMONDATA
        .globl s__DISCARD
        .globl l__DISCARD
	.globl s__DATA
        .globl l__DATA
        .globl kstack_top

	.globl interrupt_handler
	.globl nmi_handler

	.include "kernel.def"


	.area _CODE

; Starts at 0x0100
; We are mapped with bank 1 page 1 low and bank 1 page 2 high (fixed)
; Or on a non MDISK system with the 64K RAM low/high
;
; We were loaded as a straight 63K image from 0000-FDFF. The loader
; in FE00-FFFF gets replaced with the serial buffers once we take
; control
;
        di

	ld sp,#kstack_top

        ; move the common memory where it belongs    
        ld hl, #s__DATA
        ld de, #s__COMMONMEM
        ld bc, #l__COMMONMEM
        ldir

        ld de, #s__COMMONDATA
        ld bc, #l__COMMONDATA
        ldir

        ; and the discard
        ld de, #s__DISCARD
        ld bc, #l__DISCARD
        ldir

        ; Zero the data area
        ld hl, #s__DATA
        ld de, #s__DATA + 1
        ld bc, #l__DATA - 1
        ld (hl), #0
        ldir

        ; Hardware setup
        call init_hardware

        ; Call the C main routine
        call _fuzix_main
    
        ; fuzix_main() shouldn't return, but if it does...
        di
stop:   halt
        jr stop

	.area _COMMONMEM

	.globl	siob_txd
	.globl	siob_status
	.globl	siob_rx_ring
	.globl	siob_special
	.globl	sioa_txd
	.globl	sioa_status
	.globl	sioa_rx_ring
	.globl	sioa_special

	.globl	im2_vectors

;
;	Make sure common memory starts with the im2 vectors
;
im2_vectors:
	.word	0
	.word	0
	.word	0
	.word	interrupt_handler	; CTC 3 vector
	.word	0
	.word	0
	.word	0
	.word	0
	.word	siob_txd
	.word	siob_status
	.word	siob_rx_ring
	.word	siob_special
	.word	sioa_txd
	.word	sioa_status
	.word	sioa_rx_ring
	.word	sioa_special
