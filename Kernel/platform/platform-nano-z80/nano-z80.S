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
	.export map_proc_di
    ;.export map_proc_a
	.export map_proc_always
	.export map_proc_always_di
	.export map_save_kernel
	.export map_restore
	.export map_kernel_restore
    .export map_for_swap
	.export mpgsel_cache
    .export top_bank
    .export _copy_common 
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



#include "kernelu.def"
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

; -----------------------------------------------------------------------------
; KERNEL MEMORY BANK (may be below 0x8000, only accessible when the kernel is
; mapped)
; -----------------------------------------------------------------------------
	.code
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
    ; Restore memory map
    call map_kernel

	; Map ROM back into lower part of memory
    ld a,0
    out (0x7e),a
    
    ; Reboot form ROM
    jp 0


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
    ld hl, 4032
    ld(_procmem), hl

    ; Enable hardware TTY
    ld a, 0x04
    out (0x7f), a
    ld a, 0x01
    out (0x0d), a
    ;And disable autoscroll
    ld a, 0x00
    out (0x0e), a

    ; Configure keyboard arrow keys for VT52
    ld a,0x02
    out (0x7f), a
    ld a,0x03
    out (0x0b), a

    call _vtinit

    ld hl, 0
	push hl
	call _program_vectors
	pop hl
	
    ; Start timer and enable interrupt output
    ld a, 0x06      ; Select interrupt controler
    out (0x7f), a
    ld a, 0x0F      ; Enable channels 1,2,3,4 (timer, keyboard, uarts)
    out (0x00), a
    //ld a, 0x86      ; Program IM2 vector
    //out (0x10), a

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

    jr map_kernel


;=========================================================================      
; Memory management                                                             
; - kernel pages:     32 - 34                                                   
; - common page:      35 (also init top page)                                   
; - user space pages: 36 - 63                                                   
;=========================================================================      
                                                                                
;=========================================================================      
; map_proc_always - map process pages                                           
; Inputs: page table address in U_DATA__U_PAGE                                  
; Outputs: none; all registers preserved                                        
;=========================================================================      
map_proc_always:                                                                
map_proc_always_di:                                                             
    push hl                                                                     
    ld hl,_udata + U_DATA__U_PAGE                                               
    jr map_proc_2_pophl_ret   

;=========================================================================      
; map_proc - map process or kernel pages                                        
; Inputs: page table address in HL, map kernel if HL == 0                       
; Outputs: none; A and HL destroyed                                             
;=========================================================================      
map_proc:                                                                       
map_proc_di:                                                                    
    ld a,h                                                                      
    or l                ; HL == 0?                                              
    jr nz,map_proc_2        ; HL == 0 - map the kernel                          
                                                                                
;=========================================================================      
; map_kernel - map kernel pages                                                 
; Inputs: none                                                                  
; Outputs: none; all registers preserved                                        
;=========================================================================      
map_kernel:                                                                     
map_kernel_di:                                                                  
map_kernel_restore:                                                             
map_buffers:                                                                    
    push hl                                                                     
    ld hl,_kernel_pages                                                         
    jr map_proc_2_pophl_ret     

;=========================================================================      
; map_proc_2 - map process or kernel pages                                      
; Inputs: page table address in HL                                              
; Outputs: none, HL destroyed                                                   
;=========================================================================      
map_proc_2:                                                                     
    push de                                                                     
    push af                                                                     
                                                                                
    ld de,mpgsel_cache      ; paging registers are write only                   
                    ; so cache their content in RAM                             
    ld a,(hl)           ; memory page number for bank 0                         
    ld (de),a                                                                   
    out (MPGSEL_0),a        ; set bank 0                                        
    inc hl                                                                      
    inc de                                                                      
    ld a,(hl)           ; memory page number for bank 1                         
    ld (de),a                                                                   
    out (MPGSEL_1),a        ; set bank 1                                        
    inc hl                                                                      
    inc de                                                                      
    ld a,(hl)           ; memory page number for bank 2                         
    ld (de),a                                                                   
    out (MPGSEL_2),a        ; set bank 2                                        
                                                                                
    pop af                                                                      
    pop de                                                                      
    ret                

;=========================================================================      
; map_restore - restore a saved page mapping                                    
; Inputs: none                                                                  
; Outputs: none, all registers preserved                                        
;=========================================================================      
map_restore:                                                                    
    push hl                                                                     
    ld hl,map_savearea                                                          
map_proc_2_pophl_ret:                                                           
    call map_proc_2                                                             
    pop hl                                                                      
    ret                                                                         
                                                                                
;=========================================================================      
; map_save_kernel - save the current page mapping to map_savearea and           
; switch to kernel maps                                                         
; Inputs: none                                                                  
; Outputs: none                                                                 
;=========================================================================      
map_save_kernel:                                                                
    push hl                                                                     
    ld hl,(mpgsel_cache)                                                        
    ld (map_savearea),hl                                                        
    ld hl,(mpgsel_cache+2)                                                      
    ld (map_savearea+2),hl                                                      
    ld hl,_kernel_pages                                                         
    jr map_proc_2_pophl_ret    

;=========================================================================      
; map_for_swap - map a page into a bank for swap I/O                            
; Inputs: none                                                                  
; Outputs: none                                                                 
;                                                                               
; The caller will later map_kernel to restore normality                         
;                                                                               
; We use 0x4000-0x7FFF so that all the interrupt stuff is mapped.               
;                                                                               
;=========================================================================      
map_for_swap:                                                                   
    ld (mpgsel_cache + 1),a                                                     
    out (MPGSEL_1),a                                                            
    ret                                                                         
                                                                                
_copy_common:                                                                   
    pop hl                                                                      
    pop de                                                                      
    push de                                                                     
    push hl                                                                     
    ld a,e                                                                      
    call map_for_swap                                                           
    ld hl,0xEA00                                                                
    ld de,0x2A00                                                                
    ld bc,0x1600                                                                
    ldir                                                                        
    jr map_kernel  

; MPGSEL registers are write only, so their content is cached here              
mpgsel_cache:                                                                   
    .byte   0x00, 0x01, 0x02                                                  
top_bank:   ; the shared tricks code needs this name for cache+3                
    .byte   0x03                                                                
                                                                                
; kernel page mapping                                                           
_kernel_pages:                                                                  
    .byte   0x00,0x01,0x02,0x03                                                 
                                                                                
; memory page mapping save area for map_save/map_restore                        
map_savearea:                                                                   
    .byte   0,0,0,0      

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

    ;ld a, (_disk_map)
    ;call map_proc_a
    ; Fix later for mapping user pages
    call map_kernel

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
    push af
    push bc
    push de
    push hl

    ;ld a, (_disk_map)
    ;call map_proc_a
    ; Fix later for user map
    call map_kernel

    call sd_init

    ; Copy data from buffer to FPGA SD-buffer
    ld hl, (_sd_ptr)
    ld d, 0
sd_write_loop:
    ld a, d
    out (0x07), a
    
    ld c, 0x80
    ld b, 0x80
sd_write_loop_inner:
    ld a, (hl)
    out (c), a
    inc hl
    inc c
    djnz sd_write_loop_inner

    inc d
    ld a, 4
    cp d
    jr nz, sd_write_loop
    
    ; Perform write
    ld a, 1
    out (0x06), a

    call sd_wait
    
    pop hl
    pop de
    pop bc
    pop af
 
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
