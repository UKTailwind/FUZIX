#!/bin/bash
# Bisect a regcache miscompile: build dhry with caching allowed only
# in one function at a time, run native vs forced-bytecode under
# qemu-arm, and report which function's caching breaks the diff.
D=$(cd "$(dirname "$0")" && pwd)
CC=$(cd "$D/../.." && pwd)
W=/tmp/regbisect
Q="qemu-arm $CC/qemu-armm0/bcrun"
mkdir -p "$W"
sed '/#include "dhry.h"/d' "$D/dhry_2.c" > "$W/d2.c"
cat "$D/dhry_1.c" "$W/d2.c" > "$W/one.c"
gcc -E -P -nostdinc -DTIME_US -DDHRY_RUNS=1000 \
	-I "$CC/hosttest/ctest-include" -I "$D" "$W/one.c" > "$W/d.pp"
"$CC/host-armm0/cc0" < "$W/d.pp" > "$W/d.tok"
rm -f "$W/d.ir"
"$CC/host-armm0/cc1" < "$W/d.tok" 1<> "$W/d.ir" 2>/dev/null

try() {
	local tag=$1; shift
	rm -f "$W/d.raw"
	env "$@" "$CC/host-armm0/cc2" .symtmp armm0 0 \
		< "$W/d.ir" 1<> "$W/d.raw" 2>/dev/null
	$Q "$W/d.raw" > "$W/n.out" 2>&1
	BCRUN_BYTECODE=1 $Q "$W/d.raw" > "$W/b.out" 2>&1
	if diff <(grep -v -i -e microsec -e dhrystone "$W/n.out") \
		<(grep -v -i -e microsec -e dhrystone "$W/b.out") \
		> /dev/null; then
		echo "ok    $tag"
	else
		echo "BROKEN $tag"
	fi
}

try none    THUMB_NOREGC=1
try all     IGNORE=0
for f in main Proc_1 Proc_2 Proc_6 Proc_8 Func_2; do
	try "$f" THUMB_REGCFN=$f
done
