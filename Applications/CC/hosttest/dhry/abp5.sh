#!/bin/bash
# P5 A/B: sizes and qemu wall time, THUMB_NORSKIP=1 = pair kept.
CC=$(cd "$(dirname "$0")/../.." && pwd)
M=/home/peter/src/mmb2c
W=/tmp/ccp5
Q="qemu-arm $CC/qemu-armm0/bcrun"
SEIN=$M/tests/solar_eclipse.in
mkdir -p "$W"
python3 "$M/mmb2c.py" "$M/tests/solar_eclipse.bas" --fcc -o "$W/se.c" \
	> /dev/null 2>&1 || exit 1
gcc -E -P -nostdinc -U__LP64__ -U__LLP64__ -D__ILP32__ -DMM_FCC \
	-I "$M/fcc/include" -I "$CC/hosttest/ctest-include" -I "$M" \
	"$W/se.c" > "$W/se.pp" || exit 1
"$CC/host-armm0/cc0" < "$W/se.pp" > "$W/se.tok" || exit 1
rm -f "$W/se.ir"
"$CC/host-armm0/cc1" < "$W/se.tok" 1<> "$W/se.ir" 2>/dev/null || exit 1
for v in new old; do
	rm -f "$W/se-$v.bc"
	[ $v = old ] && export THUMB_NORSKIP=1
	THUMB_RECLAIM=1 "$CC/host-armm0/cc2" .symtmp armm0 0 \
		< "$W/se.ir" 1<> "$W/se-$v.bc" 2> "$W/$v.err" || exit 1
	unset THUMB_NORSKIP
	[ -s "$W/$v.err" ] && { cat "$W/$v.err"; exit 1; }
	echo "se-$v.bc: $(stat -c %s "$W/se-$v.bc") bytes"
done
$Q "$W/se-old.bc" < "$SEIN" > "$W/o.out" 2>&1
$Q "$W/se-new.bc" < "$SEIN" > "$W/n.out" 2>&1
diff <(grep -v -i time "$W/o.out") <(grep -v -i time "$W/n.out") \
	&& echo "ECLIPSE IDENTICAL"
for v in old new; do
	for i in 1 2 3; do
		/usr/bin/time -f "$v  %es" qemu-arm "$CC/qemu-armm0/bcrun" \
			"$W/se-$v.bc" < "$SEIN" > /dev/null 2> "$W/t"
		cat "$W/t"
	done
done
