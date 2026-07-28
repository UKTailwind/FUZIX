cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(fuzix C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_BUILD_TYPE Debug)
set(PICO_COPY_TO_RAM 1)
option(TOTALMEM "specify how much TOTALMEM is present" 0)

add_compile_definitions(
		# The PC3 is an RP2350B: 48 GPIOs, ADC on GP40-47.  The pico2
		# board header defaults to the A variant, which makes every
		# SDK GPIO mask operation on pins >= 32 silently wrap (shift
		# by pin & 31) and leaves the high-bank pad pull-downs in
		# place - GP34-37 joystick reads and the GP41-44 ADC were
		# both dead until this.
		PICO_RP2350A=0
		PICO_HEAP_SIZE=0x0
		PICO_NO_BINARY_INFO=1
		PICO_TIME_DEFAULT_ALARM_POOL_DISABLED
    TOTALMEM=${TOTALMEM}
		)

pico_sdk_init()

include_directories(
	.
	../..
	../../dev
	../../include
	../../cpu-armm0
)

add_executable(fuzix
	devices.c
	devflash.c
	devsdspi.c
	devtty.c
	elf.c 
	main.c
	misc.c
	rawflash.c
	console.c
	devpsram.c
	display.c
	ds3231.c
	kbd_decode.c
	usbkbd.c
	psram.c
	rawuart.c
	board.c
	sound.c
	swapper.c
	tricks.S
	core1.c
	devgpio.c
	usbdescriptors.c
	../../dev/blkdev.c
	../../dev/mbr.c
	../../dev/devsd_discard.c
	../../dev/devsd.c
	../../lib/dhara/error.c
	../../lib/dhara/journal.c
	../../lib/dhara/map.c
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
	hardware_flash
	hardware_i2c
	hardware_pio
	hardware_spi
	tinyusb_device
)

pico_set_float_implementation(fuzix none)
pico_set_double_implementation(fuzix none)
pico_add_linker_script_override_path(fuzix ${CMAKE_CURRENT_LIST_DIR}/linker_overrides)
pico_add_extra_outputs(fuzix)

