#!/bin/bash
#
# Translate a BASIC program and compile it with the Fuzix C compiler to
# PC3 bytecode, on the development machine.
#
#   bash fcc/fccbuild.sh tests/t1.bas          # -> /tmp/fccbuild/t1.bc
#   bash fcc/fccbuild.sh tests/t1.bas run      # ... and run it
#   bash fcc/fccbuild.sh tests/t1.bas run < input
#
# The mm_* runtime is native inside bcrun (phase 1): the program is
# compiled alone and every mm_* call resolves as a named libcall at
# load.  RTBC=1 restores the old phase-0 shape - runtime concatenated
# and compiled to bytecode - for differential debugging.
# Header search: our fcc/include first, then the compiler's own C89 test
# headers for stdio/string/stddef/limits/assert.
#
# FCC points at the FUZIX Applications/CC tree (host-armm0 must have
# been built there: make -f Makefile.host).

M=$(cd "$(dirname "$0")/.." && pwd)
FCC=${FCC:-$(cd "$(dirname "$0")/../../CC" && pwd)}
BIN=${BIN:-$FCC/host-armm0}
INC=$M/fcc/include
CINC=$FCC/hosttest/ctest-include
W=${W:-/tmp/fccbuild}

src=$1
act=$2
[ -f "$src" ] || { echo "usage: fccbuild.sh prog.bas|prog.c [run]" >&2; exit 1; }
mkdir -p "$W"

# A .c file skips the translator: for pipeline debugging, a hand written
# driver against the runtime goes through exactly the same compile.
case "$src" in
*.c)
	b=$(basename "$src" .c)
	cp "$src" "$W/$b.c"
	;;
*)
	b=$(basename "$src" .bas)
	# MMB2C overrides the translator, e.g. MMB2C=$M/mmbc/mmbc to run
	# the C rewrite through the same harness
	${MMB2C:-python3 "$M/mmb2c.py"} "$src" --fcc -o "$W/$b.c" \
		> "$W/$b.mmb2c.out" 2>&1 \
		|| { echo "TRANSLATE FAIL"; cat "$W/$b.mmb2c.out"; exit 1; }
	;;
esac

if [ "${RTBC:-0}" = 1 ]; then
	# phase-0 shape: runtime concatenated, compiled as one unit
	cat "$M/mmb_runtime.c" "$W/$b.c" > "$W/$b.one.c"
	RTDEF="-DMM_NO_DIRS"
else
	RTDEF=
	cp "$W/$b.c" "$W/$b.one.c"
fi

if ! gcc -E -P -nostdinc -U__LP64__ -U__LLP64__ -D__ILP32__ \
		-DMM_FCC $RTDEF \
		-I "$INC" -I "$CINC" -I "$M" -I "$W" \
		"$W/$b.one.c" > "$W/$b.pp" 2> "$W/$b.cpp.err"; then
	echo "CPP FAIL"; cat "$W/$b.cpp.err"; exit 1
fi

"$BIN/cc0" < "$W/$b.pp" > "$W/$b.tok" 2> "$W/$b.cc0.err"
if [ $? != 0 ] || grep -q ' - ' "$W/$b.cc0.err"; then
	echo "CC0 FAIL"; cat "$W/$b.cc0.err"; exit 1
fi

rm -f "$W/$b.ir"
if ! "$BIN/cc1" < "$W/$b.tok" 1<> "$W/$b.ir" 2> "$W/$b.cc1.err"; then
	echo "CC1 FAIL"; cat "$W/$b.cc1.err"; exit 1
fi
# cc1 prints progress dots on stderr; anything else is a diagnostic
if grep -qv '^\.*$' "$W/$b.cc1.err"; then
	echo "CC1 SAID:"; grep -v '^\.*$' "$W/$b.cc1.err"
fi

rm -f "$W/$b.raw" "$W/$b.bc"
"$BIN/cc2" .symtmp armm0 0 < "$W/$b.ir" 1<> "$W/$b.raw" 2> "$W/$b.cc2.err"
if [ $? != 0 ] || [ -s "$W/$b.cc2.err" ]; then
	echo "CC2 FAIL"; cat "$W/$b.cc2.err"; exit 1
fi
# The "#!" line the board's cc writes ahead of the object, so a program
# copied to the PC3 runs as ./prog.bc and not only under bcrun.  Every
# bcrun and bcdump skips it and offsets it; without it the shell reads
# the object as a script and says "syntax error at line 1".  cc2 keeps
# stdout open read-write, so the line is prepended afterwards rather
# than seeded into the file it writes.
{ printf '#!/usr/bin/bcrun\n'; cat "$W/$b.raw"; } > "$W/$b.bc"
chmod 755 "$W/$b.bc"

echo "built $W/$b.bc ($(stat -c %s "$W/$b.bc") bytes)"
if [ "$act" = run ]; then
	exec "$BIN/bcrun" "$W/$b.bc"
fi
