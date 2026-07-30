#!/bin/bash
# Dhrystone 2.1 (netlib dhry-c, unpacked from dhry-c.shar) through the
# FCC pipeline and natively.  Local changes to dhry_1.c are the three
# marked islands: -DTIME_US microsecond clock, -DDHRY_RUNS fixed run
# count (no scanf in the bytecode world), nothing else.
#
# The bytecode world has no linker, so the two source files become one
# translation unit: dhry_2.c minus its duplicate #include "dhry.h".
#
#   bash dhry.sh          -> native + host bcrun numbers
#                            /tmp/ccdhry/dhry-board.bc for the PC3

D=$(cd "$(dirname "$0")" && pwd)
CC=$(cd "$D/../.." && pwd)/Applications/CC
[ -d "$CC" ] || CC=$(cd "$D/../.." && pwd)   # tree layout tolerance
BIN=$CC/host-armm0
INC=$CC/hosttest/ctest-include
W=/tmp/ccdhry
mkdir -p "$W"

build_bc() {
	runs=$1; out=$2
	sed '/#include "dhry.h"/d' "$D/dhry_2.c" > "$W/dhry_2_body.c"
	cat "$D/dhry_1.c" "$W/dhry_2_body.c" > "$W/dhry_one.c"
	gcc -E -P -nostdinc -DTIME_US -DDHRY_RUNS=$runs \
		-I "$INC" -I "$D" "$W/dhry_one.c" > "$W/dhry.pp" || return 1
	"$BIN/cc0" < "$W/dhry.pp" > "$W/dhry.tok" 2> "$W/cc0.err" \
		|| { cat "$W/cc0.err"; return 1; }
	rm -f "$W/dhry.ir"
	"$BIN/cc1" < "$W/dhry.tok" 1<> "$W/dhry.ir" 2> "$W/cc1.err" \
		|| { cat "$W/cc1.err"; return 1; }
	grep ' - ' "$W/cc1.err" && return 1
	rm -f "$W/$out"
	"$BIN/cc2" .symtmp armm0 0 < "$W/dhry.ir" 1<> "$W/$out" 2> "$W/cc2.err"
	[ -s "$W/cc2.err" ] && { cat "$W/cc2.err"; return 1; }
	return 0
}

build_bc 1500000 dhry-host.bc  || exit 1
build_bc 30000   dhry-board.bc || exit 1

gcc -O2 -w -std=gnu89 -DTIME_US -DDHRY_RUNS=500000000 -o "$W/dhry.native" \
	"$D/dhry_1.c" "$D/dhry_2.c" "$D/dhry_shim.c" || exit 1

echo "== gcc -O2 native (500,000,000 runs) =="
"$W/dhry.native" | tail -6
echo
echo "== FCC bytecode under host bcrun (1,500,000 runs, $(stat -c %s "$W/dhry-host.bc") bytes) =="
"$BIN/bcrun" "$W/dhry-host.bc" | tail -6
echo
echo "board image: $W/dhry-board.bc (30,000 runs)"
