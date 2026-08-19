#!/bin/bash
# Build the cc-perf board A/B package: three Dhrystone objects from the
# SAME IR, differing only in which translator rewrites are on, plus the
# reclaim-shaped eclipse.  All run under the board's EXISTING bcrun -
# nothing here changes the object format or the runtime.
#
#   dhry-base.bc   both rewrites off  (= pc3 branch code shape)
#   dhry-r4.bc     direct [r4,#off] frame access only
#   dhry-new.bc    + constant-operand folding (the cc-perf default)
#
# On the board (each prints its own Dhrystones/s; >=2s measured run):
#   bcrun dhry-base.bc ; bcrun dhry-r4.bc ; bcrun dhry-new.bc
# Baseline for reference: 103,548/s (2026-08-05, commit 560b16f0b).
D=$(cd "$(dirname "$0")" && pwd)
CC=$(cd "$D/../.." && pwd)
W=/tmp/ccperf-board
RUNS=${BOARD_RUNS:-400000}
mkdir -p "$W"
sed '/#include "dhry.h"/d' "$D/dhry_2.c" > "$W/d2.c"
cat "$D/dhry_1.c" "$W/d2.c" > "$W/one.c"
gcc -E -P -nostdinc -DTIME_US -DDHRY_RUNS=$RUNS \
	-I "$CC/hosttest/ctest-include" -I "$D" "$W/one.c" > "$W/d.pp" || exit 1
"$CC/host-armm0/cc0" < "$W/d.pp" > "$W/d.tok" || exit 1
rm -f "$W/d.ir"
"$CC/host-armm0/cc1" < "$W/d.tok" 1<> "$W/d.ir" 2>/dev/null || exit 1

bld() {
	out=$1; shift
	rm -f "$W/$out.raw" "$W/$out"
	env "$@" THUMB_RECLAIM=1 "$CC/host-armm0/cc2" .symtmp armm0 0 \
		< "$W/d.ir" 1<> "$W/$out.raw" 2> "$W/$out.err" || exit 1
	[ -s "$W/$out.err" ] && { cat "$W/$out.err"; exit 1; }
	{ printf '#!/usr/bin/bcrun\n'; cat "$W/$out.raw"; } > "$W/$out"
	chmod 755 "$W/$out"
	echo "$out: $(stat -c %s "$W/$out") bytes"
}

bld dhry-base.bc THUMB_NOR4=1 THUMB_NOCFOLD=1 THUMB_NORFOLD=1 THUMB_NOSTRSLOT=1 THUMB_NOICOPY=1
bld dhry-r4.bc   THUMB_NOCFOLD=1 THUMB_NORFOLD=1 THUMB_NOSTRSLOT=1 THUMB_NOICOPY=1
bld dhry-cf.bc   THUMB_NORFOLD=1 THUMB_NOSTRSLOT=1 THUMB_NOICOPY=1
bld dhry-nos.bc  THUMB_NOSTRSLOT=1 THUMB_NOICOPY=1
bld dhry-noic.bc THUMB_NOICOPY=1
bld dhry-new.bc  IGNORE=0

# The eclipse the same three ways, via the mmb2c pipeline if present
M=$(cd "$(dirname "$0")/../../../mmb2c" && pwd)
if [ -f "$M/tests/solar_eclipse.bas" ]; then
	python3 "$M/mmb2c.py" "$M/tests/solar_eclipse.bas" --fcc \
		-o "$W/se.c" > /dev/null 2>&1 || exit 1
	gcc -E -P -nostdinc -U__LP64__ -U__LLP64__ -D__ILP32__ -DMM_FCC \
		-I "$M/fcc/include" -I "$CC/hosttest/ctest-include" \
		-I "$M" "$W/se.c" > "$W/se.pp" 2>/dev/null || exit 1
	"$CC/host-armm0/cc0" < "$W/se.pp" > "$W/se.tok" || exit 1
	rm -f "$W/se.ir"
	"$CC/host-armm0/cc1" < "$W/se.tok" 1<> "$W/se.ir" 2>/dev/null || exit 1
	seb() {
		out=$1; shift
		rm -f "$W/$out.raw" "$W/$out"
		env "$@" THUMB_RECLAIM=1 "$CC/host-armm0/cc2" .symtmp armm0 0 \
			< "$W/se.ir" 1<> "$W/$out.raw" 2> "$W/$out.err" || exit 1
		[ -s "$W/$out.err" ] && { cat "$W/$out.err"; exit 1; }
		{ printf '#!/usr/bin/bcrun\n'; cat "$W/$out.raw"; } > "$W/$out"
		chmod 755 "$W/$out"
		echo "$out: $(stat -c %s "$W/$out") bytes"
	}
	seb se-base.bc THUMB_NOR4=1 THUMB_NOCFOLD=1
	seb se-new.bc  IGNORE=0
fi
echo "package in $W - send with devtools/uusend.py, run under the"
echo "board's existing bcrun (object format and runtime unchanged)"
