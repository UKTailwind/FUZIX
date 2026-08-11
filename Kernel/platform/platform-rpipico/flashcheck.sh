#!/bin/sh
#
# Refuse to build a kernel image that would eat itself.
#
# The dhara flash disk starts at FLASH_OFFSET.  If the kernel image
# reaches it, dhara's journal "repair" erases the image's own tail on
# first boot and the board is dead until reflashed - found the hard way
# when the kernel outgrew the old 96K offset (globals.h says more).
#
#	sh flashcheck.sh <globals.h> <fuzix.bin>
#
# FLASH_OFFSET is READ from globals.h rather than repeated here.  It
# used to be a hand-copied $((512*1024)) in CMakeLists.txt, so raising
# the offset in globals.h left the build still checking against the old
# number - a limit that silently disagrees with the thing it is meant to
# protect is worse than no limit at all.
set -e

GH=$1
BIN=$2
[ -r "$GH" ]  || { echo "flashcheck: cannot read $GH" >&2; exit 1; }
[ -r "$BIN" ] || { echo "flashcheck: cannot read $BIN" >&2; exit 1; }

# #define FLASH_OFFSET (1024*1024)  ->  1024*1024  ->  the product.
# awk does the arithmetic: the value is an expression, not a number,
# and $(( )) on a variable holding one is not portable to dash.
expr=$(sed -n 's/^#define[ 	]*FLASH_OFFSET[ 	]*(\(.*\))[ 	]*$/\1/p' "$GH")
[ -n "$expr" ] || { echo "flashcheck: no FLASH_OFFSET in $GH" >&2; exit 1; }
lim=$(awk "BEGIN { printf \"%d\", $expr }")
[ "$lim" -gt 0 ] || { echo "flashcheck: FLASH_OFFSET is $lim" >&2; exit 1; }

sz=$(stat -c %s "$BIN")
if [ "$sz" -ge "$lim" ]; then
	echo "$BIN ($sz bytes) reaches FLASH_OFFSET ($lim): the flash disk"
	echo "would erase the kernel image.  Raise FLASH_OFFSET in globals.h -"
	echo "and with it mkftl's -s and the uf2 offset in the Makefile."
	exit 1
fi
echo "flash footprint: $sz of $lim bytes ($((100 * sz / lim))% of FLASH_OFFSET)"
