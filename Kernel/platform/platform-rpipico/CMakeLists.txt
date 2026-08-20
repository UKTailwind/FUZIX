cmake_minimum_required(VERSION 3.12)

# Declare the 8 MiB PSRAM to the LINKER, so a variable marked with the
# SDK's __uninitialized_psram("group") lands in the QMI window at
# 0x11000000 and the link exports __psram_start__/__psram_end__.
#
# This is the linker region ONLY. We do not link hardware_psram: bring-up
# stays with psram.c, which is vendored from the MicroPython rp2 port and
# is what makes PSRAM reliable at this overclock. PICO_PSRAM_SIZE_BYTES is
# read by nothing we compile (only hardware_psram/psram.c and the board
# headers reference it), so there is no second initialiser to fight.
#
# Must be set before pico_sdk_init(): pico_standard_link configures
# pico_psram_region.ld from it while the SDK is being pulled in.
# Plain integer, not "(8 * 1024 * 1024)": CMake splits on the spaces and
# the generated region reads LENGTH = (;8;*;1024;*;1024;).
set(PICO_PSRAM_SIZE_BYTES 8388608)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(fuzix C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_BUILD_TYPE Debug)
# NOT copy_to_ram any more.  That put all 90,676 bytes of .text into
# RAM at boot, and RAM is the scarce thing here - it is shared with the
# process pool, and the text primitives needed 4K taken off that pool -
# while flash sits at 22% used.
#
# Placement is now per function, in linker_overrides/
# default_text_excludes.incl and default_rodata_excludes.incl: a name
# listed there runs from flash, anything not listed falls through to the
# SDK's .data section and stays in RAM.  -ffunction-sections makes that
# work by name with no source changes, which matters because 35 of the
# sources below are shared Fuzix core files that other ports compile.
#
# Both lists are EMPTY to begin with, so this build is byte-for-byte
# the same shape as copy_to_ram was.  See PC3-FLASH-PLAN.md.
set(PICO_COPY_TO_RAM 0)

# Hardware debugging kernel: TinyUSB-s enumeration trace on the console.
#   make SUBTARGET=pico2 PC3_USB_TRACE=1
if(DEFINED ENV{PC3_USB_TRACE})
	add_compile_definitions(PC3_USB_TRACE=1)
endif()

# Experiment: no keyboard LED reports (a control transfer on EP0).
if(DEFINED ENV{PC3_NO_KBD_LEDS})
	add_compile_definitions(PC3_NO_KBD_LEDS=1)
endif()
option(TOTALMEM "specify how much TOTALMEM is present" 0)

add_compile_definitions(
		# The PC3 is an RP2350B: 48 GPIOs, ADC on GP40-47.  The pico2
		# board header defaults to the A variant, which makes every
		# SDK GPIO mask operation on pins >= 32 silently wrap (shift
		# by pin & 31) and leaves the high-bank pad pull-downs in
		# place - GP34-37 joystick reads and the GP41-44 ADC were
		# both dead until this.
		PICO_RP2350A=0
		# The PC2 and PC3 always carry 16M and the pico2 board header
		# defaults to 4M.  Nothing sizes a disk from this any more -
		# the flash filesystem is gone, see config.h - but the chip is
		# what it is and the SDK should be told.
		PICO_FLASH_SIZE_BYTES=\(16*1024*1024\)
		PICO_HEAP_SIZE=0x0
		PICO_NO_BINARY_INFO=1
		# The default alarm pool drives the system tick (devices.c).
		# It was disabled here, which is why the tick was hand-rolled on
		# a bare hardware alarm - and a hand-rolled re-arm is what froze
		# the machine: hardware_alarm_set_target leaves the alarm UNarmed
		# when the target has already passed, nothing else re-armed it,
		# and one missed deadline stopped the timer for good.  The pool
		# keeps its entries in an ordered list and re-fires immediately
		# on a miss, so it cannot end up unarmed.  MMBasic runs its own
		# tick this way (PicoMite.c:903, add_repeating_timer_us(-1000)).
		#
		# It costs static RAM only - the default pool and its entries are
		# static objects, not malloc'd, which matters because PICO_HEAP_SIZE
		# is 0 here.  Two entries (24 bytes each) is one for the tick and
		# one spare; the SDK default of 16 would be 384 bytes this port
		# has not got.  Alarm 0 keeps the tick on the same hardware alarm
		# and the same IRQ it has always used.
		PICO_TIME_DEFAULT_ALARM_POOL_MAX_TIMERS=2
		PICO_TIME_DEFAULT_ALARM_POOL_HARDWARE_ALARM_NUM=0
    TOTALMEM=${TOTALMEM}
		)

pico_sdk_init()

# The SDK's TinyUSB must be 0.20 - the USB HOST fixes this machine's
# keyboard and hub depend on, and what the PC3's MMBasic firmware uses.
# It cannot be pinned by git (see usbcheck.sh for why), so it is
# asserted at configure time instead, before anything is compiled.
execute_process(
	COMMAND sh ${CMAKE_CURRENT_LIST_DIR}/usbcheck.sh ${PICO_SDK_PATH}
	RESULT_VARIABLE USBCHECK_RC
	OUTPUT_VARIABLE USBCHECK_OUT
	ERROR_VARIABLE  USBCHECK_ERR)
if(NOT USBCHECK_RC EQUAL 0)
	message(FATAL_ERROR "${USBCHECK_ERR}")
endif()
message(STATUS "${USBCHECK_OUT}")

include_directories(
	.
	../..
	../../dev
	../../include
	../../cpu-armm0
)

add_executable(fuzix
	devices.c
	devsdspi.c
	devtty.c
	elf.c 
	main.c
	misc.c
	console.c
	arena.c
	devpsram.c
	display.c
	fonts.c
	libm_table.c
	ds3231.c
	kbd_decode.c
	usbkbd.c
	usbtrace.c
	psram.c
	rawuart.c
	board.c
	lineedit.c
	sound.c
	swapper.c
	tricks.S
	core1.c
	devgpio.c
	i2cuser.c
	spiuser.c
	pinlock.c
	usbdescriptors.c
	../../dev/devi2c.c
	../../dev/blkdev.c
	../../dev/mbr.c
	../../dev/devsd_discard.c
	../../dev/devsd.c
	../../lib/hexdump.c
	../../blk512.c
	../../devio.c
	../../devsys.c
	../../filesys.c
	../../inode.c
	../../kdata.c
	../../cpu-armm0/lowlevel-armm0.c
	../../mm/malloc.c
	../../mm.c
	../../mm/memalloc_none.c
	../../process.c
	../../start.c
	../../swap.c
	../../syscall_execelf32.c
	../../syscall_exec.c
	../../syscall_fs.c
	../../syscall_fs2.c
	../../syscall_fs3.c
	../../syscall_other.c
	../../syscall_proc.c
	../../timer.c
	../../tty.c
	../../usermem.c
	../../version.c
	../../dev/net/net_native.c
	../../syscall_net.c
	../../network.c
)

target_link_libraries(fuzix
	pico_stdlib
	pico_multicore
	tinyusb_host
	hardware_adc
	hardware_dma
	hardware_exception
	hardware_i2c
	hardware_pio
	hardware_pwm
	hardware_spi
	tinyusb_device
)

pico_set_float_implementation(fuzix none)
pico_set_double_implementation(fuzix pico)
pico_add_linker_script_override_path(fuzix ${CMAKE_CURRENT_LIST_DIR}/linker_overrides)
pico_add_extra_outputs(fuzix)

