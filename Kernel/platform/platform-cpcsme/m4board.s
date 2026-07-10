;For reference: https://www.spinpoint.org/cpc/m4info.txt
;Used code from https://github.com/M4Duke/M4examples


.module m4board

    .include "kernel.def"
    .include "../../cpu-z80/kernel-z80.def"

.if CONFIG_M4BOARD

C_ROMLOW                    .equ 0x433D
C_TIME			            .equ 0x4324
C_VERSION			        .equ 0x4326
C_SDREAD			        .equ 0x4314			
C_SDWRITE			        .equ 0x4315
C_OPEN                      .equ 0x4301
C_CLOSE                     .equ 0x4304
C_SEEK                      .equ 0x4305
C_READ                      .equ 0x4302
C_WRITE2                    .equ 0x431B
C_NMI                       .equ 0x431D
C_NETSTAT			        .equ 0x4323
C_UPGRADE			        .equ 0x4327
C_NETSOCKET		            .equ 0x4331
C_NETCONNECT		        .equ 0x4332
C_NETCLOSE		            .equ 0x4333
C_NETSEND			        .equ 0x4334
C_NETRECV        	        .equ 0x4335
C_NETHOSTIP    	            .equ 0x4336
C_NETRSSI                   .equ 0x4337
C_NETBIND			        .equ 0x4338
C_NETLISTEN                 .equ 0x4339
C_NETACCEPT		            .equ 0x433A
C_GETNETWORK		        .equ 0x433B
C_SETNETWORK		        .equ 0x4321

rom_version                 .equ 0xFF00
rom_response_ptr            .equ 0xFF02
rom_config_ptr              .equ 0xFF04
sock_info_ptr               .equ 0xFF06

DATAPORT					.equ 0xFE00
ACKPORT						.equ 0xFC00

FA_READ 					.equ 1
FA_WRITE					.equ 2
FA_CREATE_NEW				.equ 4
FA_CREATE_ALWAYS			.equ 8
FA_OPEN_ALWAYS				.equ 16
FA_REALMODE					.equ 128

m4_read_transfer_stub       .equ 0x0069 ;FIXME, make it not hardcoded if possible

;imported
    .globl _int_disabled
    .globl _td_raw
    .globl _td_page
    .globl _vtborder
    .globl _td_io_data_reg
    .globl _td_io_data_count
    .globl _td_io_wblock
    .globl __ugetc

    .globl outstring
    .globl outcharhex
    .globl outchar
	.globl outstringhex

    .globl a_map_to_bc

    .globl m4_sd_read_return

;exported
    .globl _m4_time_str
    .globl _m4_present
    .globl _m4_gettime
    .globl _m4_plt_rtc_secs
    .globl _m4_init
    .globl _read_lba
    .globl _write_lba
    .globl _m4_sd_read_block
    .globl _m4_sd_write_block
    .globl _block_data_ptr
    .globl _m4_open_mode
    .globl _m4_img_fd
    .globl _m4_img_lba
    .globl _m4_img_write_fd
    .globl _m4_img_read_fd
    .globl _m4_is_img
    .globl _m4_img_open
    .globl _m4_img_seek
    .globl _m4_img_close    
    .globl _m4_img_close_fd
   



.area _VIDEO        ; avoid being shadowed by rom

_m4_init:			; M4 detection via C_ROMLOW command (only from FW v2.0.7 )

            di
            ld      a,(_int_disabled)
            ld      (int_disabled_backup),a
            ld      a,#1
            ld      (_int_disabled),a
            ld      hl,#init_msg
            call    outstring      
			ld		hl,#cmd_select_hack_low_rom
            call    m4_send_command            
            ld		bc,#0x7f8a              ;enable LROM disable UROM
            out		(c),c
			ld 		a,(#0x100)
            cp 		#0x4D					; detect 'M' from "MV - SNA" string, to determine if M4 present
            ld      bc, #0x7f8e             ;RMR ->UROM disable LROM disable
            out     (c),c   
			jr		z, m4_found
            ld      hl,#not_found_msg_start
            push    af
            call    outstring
            pop     af
            call    outcharhex
            ld      hl,#not_found_msg_end
            call    outstring
            jr      popret      

m4_found:
            ld      a,#1
            ld      (_m4_present),a
			ld		hl,#cmd_select_hack_low_rom
            call    m4_send_command
            ld		bc,#0x7f8a              ;enable LROM disable UROM
            out		(c),c            
			ld 		a,(0x0)					; get M4 rom number
            ld      (m4rom_number),a
			ld		hl,#cmd_select_default_low_rom
            call    m4_send_command
            ld      hl,#ret_to_init0
            jr      m4rom_enable
ret_to_init0:
            ld      hl,(rom_response_ptr)
            ld      de,#9              ;secs offset in time response buffer
            add     hl,de
            ld      (m4_secs_rom_ptr),hl
            ld      bc, #0x7f8e             ;RMR ->UROM disable LROM disable
            out     (c),c
            call    _m4_gettime            
            ld      hl,#found_msg_start
            call    outstring
            ld      hl,#_m4_time_str
            call    outstring
            ld      hl,#found_msg_end
            call    outstring
popret:
            ld      a,(int_disabled_backup)
            ld      (_int_disabled),a
            or      a
            ret     nz
            ei
            ret

m4_send_command:
            ld		bc,#DATAPORT
  			ld		d,(hl)
			inc		d
sendloop:	inc		b
			outi
			dec		d
			jr		nz,sendloop
			ld		bc,#ACKPORT
			out		(c),c
			ret         

m4_retvalue:
            ld      hl,(rom_response_ptr)
            inc     hl
            inc     hl
            inc     hl
            ld      l,(hl)
m4rom_disable:
            ld      bc, #0x7f8e             ;RMR ->UROM disable LROM disable
            out     (c),c
            ld      a,(_int_disabled)
            or      a
            ret     nz
            ei
            ret 
			
m4rom_enable:			; select M4 upperrom
                        ;this cannot be called as ret address is lost in the stack shadowed by rom
            di
            ld		bc,#0xdf00
			ld      a,(m4rom_number)
			out		(c),a
			ld		bc,#0x7f86   ; enable upper rom & disable lower
			out		(c),c
            jp      (hl)

_m4_gettime:
            ld      bc,#19      ;time str len
            ld      de,#_m4_time_str
            ld      h,d
            ld      l,e
            inc     hl
            ld      (hl),#0     ;null terminated
            exx
            ld      hl,#cmd_m4_get_time
m4_getdata:
            call    m4_send_command
            ld      hl,#ret_to_getdata0
            jr      m4rom_enable
ret_to_getdata0:
            exx
            ld      hl,(rom_response_ptr)
            inc     hl
            inc     hl
            inc     hl
            ldir
            jr      m4rom_disable

_m4_plt_rtc_secs:                      
            di
            ld      a,(_m4_present)
            or      a
            jr      nz,has_m4
            ld      l,#0xff
            ret
has_m4:            
            ld      hl,#cmd_m4_get_time
            call    m4_send_command
            ld		bc,#0xdf00
			ld      a,(m4rom_number)
			out		(c),a
			ld		bc,#0x7f86   ; enable upper rom & disable lower
			out		(c),c
            ld      hl,(m4_secs_rom_ptr)
            ld      a,(hl)              ;tens
            sub     #48                 ;'0'
            inc     hl
            ld      d,a
            ld      a,(hl)              ;ones
            sub     #48                 ;'0'
            ld      e,a
            ld      a,d                 ;tens
            add     a                   ;x2
            add     a                   ;x4
            add     a                   ;x8
            add     d                   ;x9
            add     d                   ;x10            
            add     e                   ;+ones
            ld      l,a
            jr      m4rom_disable

_m4_sd_read_block:
            di
            ld      hl,#cmd_m4_read_sd_block
            ld      a,(_m4_is_img)
            or      a
            jr      z,read_sd_raw
            ld      hl,#cmd_m4_img_read
read_sd_raw:
            ld      a, (_td_page)
            exx
            call    a_map_to_bc
            exx
            ex      af,af'
            ld      a,(_td_raw)
            ex      af,af'
            ld      bc,#0x7f10
            out     (c),c
            ld      c,#0x51 ; Sea Green
            out     (c),c
            ld		bc,#DATAPORT
  			ld		d,(hl)
			inc		d
read_command_loop:
            inc		b
			outi
			dec		d
			jr		nz,read_command_loop
            ld      bc,#ACKPORT
            out     (c),c
            ld		bc,#0xdf00
			ld      a,(m4rom_number)
			out		(c),a
			ld		bc,#0x7f86   ; enable upper rom & disable lower
			out		(c),c
            ld      hl,(rom_response_ptr)
            inc     hl
            inc     hl
            inc     hl
            ld      a,(hl)
            ld      (#patch_err+1),a
            or      a
            jr      nz,m4_sd_read_return        ;we've got an error
            inc     hl
            ld      de,(_block_data_ptr)
            ld      bc,#512
            ex      af,af' 			    ; I/O type ?            
            or      a                          ; test is user or not
            jr      z,m4_sd_read_k             ; to copy out of kernel page we need common code
            ld      iy,#m4_sd_read_return       ; and only low stubs are not shadowed by rom here
            jp      m4_read_transfer_stub
m4_sd_read_k:            
            ldir
m4_sd_read_return:
            ex      af,af'
            ld      bc, #0x7f8e             ;RMR ->UROM disable LROM disable
            out     (c),c
patch_err:
            ld      l,#0
            ld      bc,#0x7f10
            out     (c),c                                    
            ld      a,(_vtborder)
            out     (c),a            
            ld      a,(_int_disabled)
            or      a
            ret     nz
            ei
            ret 

_m4_sd_write_block:
            di
            ld      hl,#cmd_m4_write_sd_block
            ld      a,(_m4_is_img)
            or      a
            jr      z,write_sd_raw
            ld      hl,#cmd_m4_img_write
write_sd_raw:            
            ld		bc,#DATAPORT
  			ld		d,(hl)
			inc		d
write_command_loop:
            inc		b
			outi
			dec		d
			jr		nz,write_command_loop
            ld      (_td_io_data_reg),bc
            ld      a,#64
            ld      (_td_io_data_count),a
            ld      hl,(_block_data_ptr)
            push    hl
            call    _td_io_wblock
            pop     hl
            ld      bc,#ACKPORT
            out     (c),c
            ld      hl,#m4_retvalue
            jp      m4rom_enable

_m4_img_open:
            ld      hl,#cmd_m4_img_open
            call    m4_send_command
            ld      hl,#ret_to_m4_img_open
            jp      m4rom_enable
ret_to_m4_img_open: 
            ld      hl,(rom_response_ptr)
            inc     hl
            inc     hl
            inc     hl
            ld      e,(hl)
            inc     hl
            ld      l,(hl)
            ld      a,l
            or      a
            jp      nz,m4rom_disable
            ld      a,e
            ld      (_m4_img_fd),a
            ld      (_m4_img_close_fd),a
            ld      (_m4_img_read_fd),a
            ld      (_m4_img_write_fd),a
            jp      m4rom_disable

_m4_img_close:
            ld      a,#0xff
            ld      (_m4_open_mode),a
            ld      hl,#cmd_m4_img_close
            jr      m4_cmd

_m4_img_seek:
            ld      hl,#cmd_m4_img_seek

m4_cmd:
            call    m4_send_command
            ld      hl,#m4_retvalue
            jp      m4rom_enable


.if CONFIG_NET_M4BOARD 

    .globl _m4_net_socket
    .globl _m4_net_connect
    .globl _m4_net_connect_socket
    .globl _m4_net_connect_ip
    .globl _m4_net_connect_port
    .globl _m4_net_close
    .globl _m4_net_close_socket
    .globl _m4_net_bind
    .globl _m4_net_bind_ip
    .globl _m4_net_bind_port
    .globl _m4_net_bind_socket
    .globl _m4_net_listen
    .globl _m4_net_listen_socket
    .globl _m4_net_accept
    .globl _m4_net_accept_socket
    .globl _m4_net_send
    .globl _m4_net_send_socket
    .globl _m4_net_send_len
    .globl _m4_net_recv
    .globl _m4_net_recv_socket
    .globl _m4_net_recv_len
    .globl _m4_net_raw
    .globl _m4_net_hostip_start
    .globl _m4_net_sock_info
    .globl _m4_net_sock_info_socket
    .globl _m4_net_getnetwork
    .globl _m4_network_config
    .globl _m4_net_setnetwork


_m4_net_socket:
            ld hl,#cmd_m4_net_socket
            jr m4_cmd

_m4_net_connect:
            ld hl,#cmd_m4_net_connect
            jr m4_cmd

_m4_net_close:
            ld hl,#cmd_m4_net_close
            jr m4_cmd

_m4_net_bind:
            ld hl,#cmd_m4_net_bind
            jr m4_cmd

_m4_net_listen:
            ld hl,#cmd_m4_net_listen
            jr m4_cmd

_m4_net_accept:
            ld hl,#cmd_m4_net_accept
            jr m4_cmd

_m4_net_getnetwork:
            ld      bc,#196      ;net config data struct len
            ld      de,#_m4_network_config
            exx
            ld      hl,#cmd_m4_net_getnetwork
            jp      m4_getdata

_m4_net_hostip_start:
            pop     de
            pop     hl 
            push    hl
            push    de
            ld      de,#C_NETHOSTIP
            call    m4_send_cmd_str
            ld      hl,#m4_retvalue
            jp      m4rom_enable

_m4_net_setnetwork:
            pop     de
            pop     hl
            push    hl
            push    de
            ld      de,#C_SETNETWORK

m4_send_cmd_str:    ;de=m4 command, hl=pointer to null terminated str
            ld		bc,#DATAPORT
  			ld		a,#2 ; this value is neglected by the M4Board
			out     (c),a
            out     (c),e
            out     (c),d
send_str_loop:            
            ld      a,(hl)
            out     (c),a
            inc     hl
            or      a
            jr      nz,send_str_loop
			ld		bc,#ACKPORT
			out		(c),c
            ret

_m4_net_sock_info_socket:
    .db 0
_m4_net_sock_info:
            pop     hl
            pop     de
            push    de
            push    hl
            ld      hl,#ret_to_net_sock_info
            jp      m4rom_enable
ret_to_net_sock_info: 
            ld      hl,(sock_info_ptr)
            ld      bc,#16
            ld      a,(_m4_net_sock_info_socket)
calc_socket_info_ptr:            
            or      a
            jr      z,net_sock_info_read
            add     hl,bc
            dec     a
            jr      calc_socket_info_ptr
net_sock_info_read:            
            ld      bc,#10
            ldir
            jp      m4rom_disable            

_m4_net_raw:
    .db 0
_m4_net_send:           ;first we send len mod 8 and then we send remaining (multiple of 8)
            di
            pop     hl
            pop     iy
            push    iy
            push    hl
            ld      hl,#cmd_m4_net_send
            ld		bc,#DATAPORT
  			ld		d,(hl)
			inc		d
m4_net_send_command_loop:
            inc		b
			outi
			dec		d
			jr		nz,m4_net_send_command_loop
            inc     b
            push    iy
            pop     hl
            ld      de,(_m4_net_send_len)
            ld      a,e
            and     #7  ;a=de mod 8
            or      a
            jr      z,m4_net_send_check_remaining_data
            ex      af,af'
            ld      a,(_m4_net_raw)
            or      a
            jr      z,m4_net_send_data_k
            ex      af,af'
m4_net_send_data_loop_u:
            ex      af,af'
            call    __ugetc
            out     (c),l
            inc     iy
            push    iy
            pop     hl
            ex      af,af'
            dec     a
            jr      nz,m4_net_send_data_loop_u
            jr      m4_net_send_check_remaining_data
m4_net_send_data_k:
            ex      af,af'
m4_net_send_data_loop_k:
            inc     b
            outi
            dec     a
            jr      nz,m4_net_send_data_loop_k
            inc     b
m4_net_send_check_remaining_data:
            srl     e
            srl     e
            srl     e
            sla     d
            sla     d
            sla     d
            sla     d
            sla     d
            jr      nc,m4_net_send_lt_2048            
            xor     a       ;len was 2048 so call to _td_io_wblock
                            ;with a=0 loops 256 times sending 256*8=2048 bytes
            jr      m4_net_send_remaining_data
m4_net_send_lt_2048:
            xor     a            
            or      e
            or      d   ;now a = de/8
            jr      z,m4_net_send_end
m4_net_send_remaining_data:
            ld      (_td_io_data_reg),bc
            ld      (_td_io_data_count),a
            ld      a,(_m4_net_raw)
            ld      (_td_raw),a
            ld      a,(_udata + U_DATA__U_PAGE)
            ld      (_td_page),a
            push    hl
            call    _td_io_wblock
            pop     hl
m4_net_send_end:            
            ld      bc,#ACKPORT
            out     (c),c
            ld      hl,#m4_retvalue
            jp      m4rom_enable

_m4_net_recv:
            di
            pop     hl
            pop     iy
            push    iy
            push    hl
            ld      hl,#cmd_m4_net_recv
            ld		bc,#DATAPORT
  			ld		d,(hl)
			inc		d
m4_net_recv_command_loop:
            inc		b
			outi
			dec		d
			jr		nz,m4_net_recv_command_loop
            ld      bc,#ACKPORT
            out     (c),c
            push    iy
            pop     de
            ld		bc,#0xdf00
			ld      a,(m4rom_number)
			out		(c),a
			ld		bc,#0x7f86   ; enable upper rom & disable lower
			out		(c),c
            ld      hl,(rom_response_ptr)
            inc     hl
            inc     hl
            inc     hl
            inc     hl
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            inc     hl
            push    bc          ;we have upper rom mapped, we can write to but not read from the stack
            ld      a,(_m4_net_raw)
            or      a
            jr      z,m4_net_recv_data_k
            ld      a,(_udata + U_DATA__U_PAGE)
            exx
            bit     6,a
            jr      nz,lower_512
            set     6,a
            ld      b,#0x7e
            jr      ld_ca
lower_512:
            ld      b,#0x7f
ld_ca:
            ld      c,a
            exx
            ld      iy,#m4rom_disable
            jp      m4_read_transfer_stub
m4_net_recv_data_k:
            ldir
            jp      m4rom_disable

cmd_m4_net_socket:
    .db 5
    .dw C_NETSOCKET
    .db 0       ;domain not used
    .db 0       ;type not used
    .db 6       ;protocol TCP only

cmd_m4_net_connect:
    .db 9
    .dw C_NETCONNECT
_m4_net_connect_socket:
    .ds 1
_m4_net_connect_ip:
    .ds 4
_m4_net_connect_port:
    .ds 2

cmd_m4_net_close:
    .db 3
    .dw C_NETCLOSE
_m4_net_close_socket:
    .ds 1

cmd_m4_net_bind:
    .db 9
    .dw C_NETBIND
_m4_net_bind_socket:
    .ds 1
_m4_net_bind_ip:
    .ds 4
_m4_net_bind_port:
    .ds 2

cmd_m4_net_listen:
    .db 3
    .dw C_NETLISTEN
_m4_net_listen_socket:
    .ds 1

cmd_m4_net_accept:
    .db 3
    .dw C_NETACCEPT
_m4_net_accept_socket:
    .ds 1

cmd_m4_net_send:
    .db 5
    .dw C_NETSEND
_m4_net_send_socket:
    .ds 1
_m4_net_send_len:
    .ds 2

cmd_m4_net_recv:
    .db 5
    .dw C_NETRECV
_m4_net_recv_socket:
    .ds 1
_m4_net_recv_len:
    .ds 2

cmd_m4_net_getnetwork:
    .db 2
    .dw C_GETNETWORK

_m4_network_config:
    .ds 196

.endif

cmd_select_hack_low_rom:
    .db 3
    .dw C_ROMLOW
    .db 2

cmd_select_default_low_rom:
    .db 3
    .dw C_ROMLOW
    .db 1

cmd_m4_get_time:
    .db 2
    .dw C_TIME

cmd_m4_read_sd_block:
    .db 7
    .dw C_SDREAD
_read_lba:    
    .ds 4
    .db 1       ;num sectors

cmd_m4_write_sd_block:
    .db 7
    .dw C_SDWRITE
_write_lba:
    .ds 4
    .db 1       ;num sectors

cmd_m4_img_open:
    .db 14
    .dw C_OPEN
_m4_open_mode:
    .db 0xff
    .ascii "/FUZIX.IMG"
    .db 0

cmd_m4_img_close:
    .db 3
    .dw C_CLOSE
_m4_img_close_fd:
    .ds 1

cmd_m4_img_seek:
    .db 8
    .dw C_SEEK
_m4_img_fd:
    .ds 1
    .db 0       ;seek offset is LBA<<9 as BLOCKSIZE=512, low byte is always 0
_m4_img_lba:    ;here we store LBA<<1, as fuzix disk size is small we never overflow MSB
    .ds 4

cmd_m4_img_write:
    .db 5
    .dw C_WRITE2
_m4_img_write_fd:
    .ds 1
    .db 0x00  ;size (0x200) low byte
    .db 0x2   ;size (0x200) high byte

cmd_m4_img_read:
    .db 5
    .dw C_READ
_m4_img_read_fd:
    .ds 1   
    .db 0   ;size (0x200) low byte
    .db 2   ;size (0x200) high byte

_m4_is_img:
    .db 0
_block_data_ptr:
    .dw 0
m4rom_number:
    .db 0xff
_m4_time_str:   ;hh:mm:ss yyyy-mm-dd
    .ds 19
    .db 0
m4_secs_rom_ptr:
    .dw 0
_m4_present:
    .db 0
init_msg:
    .ascii "Detecting M4 Board: "
    .db 0
found_msg_start:
    .ascii "M4Board found, date: "
    .db 0
found_msg_end: 
    .db 10
    .db 13
    .db 0
not_found_msg_start:
    .ascii "M4Board not found, read: "
    .db 0
not_found_msg_end:
    .ascii " from address 0x100 at low rom."
    .db 10
    .db 13
    .db 0
int_disabled_backup:
    .db 0

.endif