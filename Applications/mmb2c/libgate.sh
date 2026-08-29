#!/bin/sh
#
# libgate.sh - every C library function the program-side headers call
# must be one bcrun can resolve.
#
#   sh libgate.sh
#
# WHY THIS EXISTS.  bcrun binds a NAMED SUBSET of the C library at load
# time (lib_fast[] in bcrun.c, the mm_* wrappers in bcrun_mm.c, the math
# and eqop tables).  A header that calls anything else compiles, links
# and passes every host gate - the gates link glibc, where the function
# exists - and then dies ON THE BOARD, at LOAD, before a line of the
# program runs:
#
#	bcrun: no runtime function "sscanf"
#
# That is what SPRITE LOAD did, reported from the board 2026-08-29.  One
# sscanf in mmb_sprite.h, in a statement no test program used, and the
# whole program was dead - not the statement, the program.
#
# A test would have caught that one statement.  This catches the CLASS:
# it does not care whether anything calls the code, only that what the
# headers call can be resolved.  -DMMG_FN= turns the headers' static
# functions into real ones so gcc emits them all, and nm lists what they
# left undefined.
#
# It cannot see a call made through a function POINTER, and it cannot
# see the runtime in mmb_runtime.c - that is compiled INTO bcrun and may
# call whatever the Fuzix libc has.  It covers the headers, which are
# the part compiled into every generated program.

M=$(cd "$(dirname "$0")" && pwd)
CC=$(cd "$M/../CC" && pwd)
W=${TMPDIR:-/tmp}/libgate.$$

die() {
	echo "libgate: $*" >&2
	rm -rf "$W"
	exit 2
}

command -v gcc >/dev/null 2>&1 || die "no gcc"
command -v nm  >/dev/null 2>&1 || die "no nm"
[ -r "$CC/bcrun.c" ] || die "no $CC/bcrun.c"

mkdir -p "$W" || die "cannot make $W"

# What bcrun can bind: all four tables have the same shape, { "name", fn }.
grep -ho '{ "[A-Za-z_0-9.]*"' "$CC/bcrun.c" "$CC/bcrun_mm.c" |
	sed 's/.*{ "//; s/"//' | sort -u > "$W/known"
[ -s "$W/known" ] || die "found no names in bcrun's tables"

# One translation unit naming every header.
{
	echo '#include "mmb_runtime.h"'
	for h in "$M"/mmb_*.h; do
		b=$(basename "$h")
		[ "$b" = "mmb_runtime.h" ] && continue
		echo "#include \"$b\""
	done
} > "$W/tu.c"

# -fno-stack-protector: __stack_chk_fail is the host toolchain's, not a
# call anything in here made.
gcc -std=c99 -w -fno-stack-protector -c -DMMG_FN= -I"$M" \
    -o "$W/tu.o" "$W/tu.c" 2>"$W/err" || {
	echo "libgate: the headers did not compile" >&2
	tail -20 "$W/err" >&2
	rm -rf "$W"
	exit 2
}

# Undefined symbols.
#
# TWO NAMED EXCLUSIONS, NOT A PATTERN.  The first version of this
# dropped everything beginning "__", which read as "the toolchain's own
# names" and silently threw away THE BUG THIS GATE EXISTS FOR: glibc
# does not emit a call to sscanf, it emits one to __isoc99_sscanf, so
# the gate passed on the very header that killed programs on the board.
# The C99 aliases are unwrapped instead, and the two host artefacts are
# excluded by name:
#
#   __stack_chk_fail    the stack protector, not a call anything made
#   __errno_location    glibc's errno macro; the board's libc spells it
#                       differently and bcrun has an errno of its own
nm -u "$W/tu.o" | awk '{ print $NF }' |
	sed 's/@.*//; s/^__isoc99_//; s/^__builtin_//' |
	grep -vx '__stack_chk_fail' |
	grep -vx '__errno_location' | sort -u > "$W/used"

comm -23 "$W/used" "$W/known" > "$W/gaps"
n=$(wc -l < "$W/used" | tr -d ' ')
if [ -s "$W/gaps" ]; then
	echo "libgate: the headers call functions bcrun cannot resolve:" >&2
	sed 's/^/  /' "$W/gaps" >&2
	echo "  (each one kills any program using that statement, at load)" >&2
	rm -rf "$W"
	exit 1
fi
echo "libgate: $n functions called, all resolvable by bcrun"
rm -rf "$W"
exit 0
