#!/bin/bash
# Regcache A/B on dhrystone under qemu: size + wall time, plus the
# 400k-run object pair for the board ladder.
D=$(cd "$(dirname "$0")" && pwd)
CC=$(cd "$D/../.." && pwd)
W=/tmp/abregc
Q="qemu-arm $CC/qemu-armm0/bcrun"
mkdir -p "$W"
sed '/#include "dhry.h"/d' "$D/dhry_2.c" > "$W/d2.c"
cat "$D/dhry_1.c" "$W/d2.c" > "$W/one.c"
gcc -E -P -nostdinc -DTIME_US -DDHRY_RUNS=2000000 \
	-I "$CC/hosttest/ctest-include" -I "$D" "$W/one.c" > "$W/d.pp"
"$CC/host-armm0/cc0" < "$W/d.pp" > "$W/d.tok"
rm -f "$W/d.ir"
"$CC/host-armm0/cc1" < "$W/d.tok" 1<> "$W/d.ir" 2>/dev/null
for v in off on; do
	rm -f "$W/$v.raw"
	[ $v = off ] && export THUMB_NOREGC=1
	THUMB_RECLAIM=1 "$CC/host-armm0/cc2" .symtmp armm0 0 \
		< "$W/d.ir" 1<> "$W/$v.raw" 2>/dev/null
	unset THUMB_NOREGC
	echo "$v: $(stat -c %s "$W/$v.raw") bytes"
done
$Q "$W/off.raw" > "$W/off.out" 2>&1
$Q "$W/on.raw" > "$W/on.out" 2>&1
diff <(grep -v -i -e microsec -e dhrystone "$W/off.out") \
     <(grep -v -i -e microsec -e dhrystone "$W/on.out") \
	&& echo IDENTICAL
for v in off on; do
	for i in 1 2 3; do
		/usr/bin/time -f "$v  %es" qemu-arm "$CC/qemu-armm0/bcrun" \
			"$W/$v.raw" > /dev/null 2> "$W/t"
		cat "$W/t"
	done
done
