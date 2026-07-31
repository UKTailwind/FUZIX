#!/bin/bash
# Dhrystone 2.1 compiled by the REAL cross compiler (arm-none-eabi-gcc)
# as an ordinary Fuzix application - the reference for how far the
# bytecode/Thumb backend is off a first-class native compiler on the
# same OS, same libc, same clock (PICOIOC_ADVAL via dhry_fuzix.c).
#
#   bash dhry-fuzix.sh    -> /tmp/ccdhry/dhry-fuzix-os, dhry-fuzix-o2
#
# Built exactly like the platform's own userland (Makefile.armm0
# flags and link recipe); -Os is what Fuzix ships, -O2 is the classic
# Dhrystone configuration, so both are produced.

D=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$D/../../../.." && pwd)
LIBS=$ROOT/Library/libs
INC=$ROOT/Library/include
GCCLIB=$(dirname "$(arm-none-eabi-gcc -mcpu=cortex-m33 -print-libgcc-file-name)")
W=/tmp/ccdhry
RUNS=${RUNS:-2000000}
mkdir -p "$W"

CFLAGS="-mcpu=cortex-m33 -ffunction-sections -fdata-sections
	-fno-strict-aliasing -fomit-frame-pointer -fno-builtin
	-std=gnu89 -isystem $INC -DTIME_US -DDHRY_RUNS=$RUNS -w"

build() {
	opt=$1; out=$2
	arm-none-eabi-gcc $CFLAGS $opt -c "$D/dhry_1.c" -o "$W/d1.o" || return 1
	arm-none-eabi-gcc $CFLAGS $opt -c "$D/dhry_2.c" -o "$W/d2.o" || return 1
	arm-none-eabi-gcc $CFLAGS $opt -c "$D/dhry_fuzix.c" -o "$W/df.o" || return 1
	arm-none-eabi-ld "$LIBS/crt0_armm0.o" "$W/d1.o" "$W/d2.o" "$W/df.o" \
		-o "$W/$out" -L"$LIBS" -lcarmm0 -pie -static \
		-no-dynamic-linker -z max-page-size=4 -L"$GCCLIB" -lgcc \
		-T "$ROOT/Library/elfexe32.ld" --no-export-dynamic \
		-Bstatic -lcarmm0 -lgcc || return 1
	arm-none-eabi-strip "$W/$out"
	echo "$out: $(stat -c %s "$W/$out") bytes ($RUNS runs)"
}

build -Os dhry-fuzix-os || exit 1
build -O2 dhry-fuzix-o2 || exit 1
