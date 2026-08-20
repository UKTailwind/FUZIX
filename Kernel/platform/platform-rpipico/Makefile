# Set one of these depending.
export PICO_SDK_FETCH_FROM_GIT = yes
#export PICO_SDK_PATH = ${HOME}/.pico-sdk/sdk/1.5.1

# Set 'SUBTARGET' to one of pico-sdk compatible boards
# valid values: pico or pico2
ifndef SUBTARGET
SUBTARGET=pico # set default subtarget
endif

ifeq ($(SUBTARGET),pico)
TOTALMEM=160
endif
ifeq ($(SUBTARGET),pico_w)
TOTALMEM=160
endif
# 312, not 320: the kernel has taken two 4K bites out of the pool. The
# first was to stop progbase floating; the second pays for the C
# library's allocator, which is now the PSRAM heap (arena.c overrides
# _sbrk) - malloc/free/realloc cost 2432 bytes of SRAM here because the
# kernel is PICO_COPY_TO_RAM and library code is RAM-resident.
# The first bite was cheap when PROGSIZE capped a process at 256K and the
# top of the pool was unreachable slack.  It no longer does - a process
# may use all of user memory - so both bites now come out of what a big
# program could have had.
# linker_overrides/memory_ram.incl must agree - it carves the same
# split, and nothing checks that the two match.
ifeq ($(SUBTARGET),pico2)
TOTALMEM=336
endif
ifeq ($(SUBTARGET),pico2_w)
TOTALMEM=336
endif

# CYW43 Wi-Fi.  OFF unless asked:
#
#	rm -rf build && make SUBTARGET=pico2 PC3_NET=1
#
# The rm matters.  build/Makefile is only regenerated when
# CMakeLists.txt or a source file is newer than it, so changing this
# value alone will NOT re-run cmake and the old cached setting stays.
# See PC3-NET-PLAN.md for what it costs (one 4K block of the pool).
ifndef PC3_NET
PC3_NET=0
endif

PICOTOOLFLAGS= --family rp2040
ifeq (${SUBTARGET}, pico2)
PICOTOOLFLAGS= --family data
endif

PICOTOOL=picotool

ifeq (, $(shell which picotool))
PICOTOOL=build/_deps/picotool/picotool
endif

include ../../../version.mk

build/fuzix.elf: ../../version.c build/Makefile FORCE
	$(MAKE) -C build

FORCE:

../../version.c:
	$(MAKE) -C ../.. TARGET=rpipico version.c

build/Makefile: CMakeLists.txt $(wildcard ../../*.[chS]) $(wildcard ../../*/*.[chS])
	mkdir -p build
	(cd build && cmake -DPICO_BOARD=${SUBTARGET} -DTOTALMEM=${TOTALMEM} -DPC3_NET=${PC3_NET} ..)

# The flash disk is everything above FLASH_OFFSET (globals.h, 1M), and
# these numbers describe the same region devflash.c does:
#
#   -s 15360        PICO_FLASH_SIZE_BYTES (16M) - FLASH_OFFSET, in KB
#   -o 0x10100000   XIP base + FLASH_OFFSET
#
# mkftl must be given the SAME device devflash.c computes, or the FTL's
# metadata describes a different disk from the one the kernel mounts.
#
# The offset was 0x10018000 - the 96K FLASH_OFFSET used before the
# bricking described in globals.h - so flashing this uf2 would have
# written the disk over the kernel and done it again.  Change one of
# these and you must change all of them.
#
# filesystem.uf2 is ~30M because a uf2 carries 256 payload bytes per
# 512-byte record and mkftl emits the whole region.  It is not a
# release asset and is written once.
image:: world
	PC3_NET=${PC3_NET} ./update-flash.sh
	../../../Standalone/mkftl -s 15360 -e 0x1000 -g 10 filesystem.img -o filesystem.ftl
	${PICOTOOL} uf2 convert filesystem.ftl -t bin filesystem.uf2 -o 0x10100000 ${PICOTOOLFLAGS}

diskimage:

clean:
	rm -rf build
	rm -f uf2conv
	$(MAKE) -C ../../../Library/libs -f Makefile.armm0 clean
	$(MAKE) -C ../../../Applications/util -f Makefile.armm0 clean
	$(MAKE) -C ../../../Applications/V7/cmd -f Makefile.armm0 clean
	$(MAKE) -C ../../../Applications/V7/cmd/sh -f Makefile.armm0 clean
	$(MAKE) -C ../../../Applications/games -f Makefile.armm0 clean
	$(MAKE) -C ../../../Applications/levee -f Makefile.armm0 clean
	$(MAKE) -C ../../../Applications/cave -f Makefile.armm0 clean
	$(MAKE) -C ../../../Applications/cursesgames -f Makefile.armm0 clean
	$(MAKE) -C ../../../Applications/bbcbasic -f Makefile.armm0 clean
	$(MAKE) -C ../../../Standalone clean
	$(MAKE) -C utils clean

world: build/fuzix.elf
	$(MAKE) -C ../../../Library/libs -f Makefile.armm0
	$(MAKE) -C ../../../Applications/util -f Makefile.armm0
	$(MAKE) -C ../../../Applications/V7/cmd -f Makefile.armm0
	$(MAKE) -C ../../../Applications/V7/cmd/sh -f Makefile.armm0
	$(MAKE) -C ../../../Applications/games -f Makefile.armm0
	$(MAKE) -C ../../../Applications/levee -f Makefile.armm0
	$(MAKE) -C ../../../Applications/cave -f Makefile.armm0
	$(MAKE) -C ../../../Applications/cursesgames -f Makefile.armm0
	$(MAKE) -C ../../../Applications/bbcbasic -f Makefile.armm0
	$(MAKE) -C ../../../Standalone
	$(MAKE) -C utils
