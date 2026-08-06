#!/bin/bash
# A/B the direct [r4,#off] frame access on Dhrystone: build the same IR
# twice (THUMB_NOR4=1 = old code shape), compare native span sizes and
# qemu-arm wall time.  Not a board number - qemu flattens memory costs -
# but instruction-count direction shows here.
D=$(cd "$(dirname "$0")" && pwd)
CC=$(cd "$D/../.." && pwd)
W=/tmp/ccdhry-ab
RUNS=${1:-2000000}
mkdir -p "$W"
sed '/#include "dhry.h"/d' "$D/dhry_2.c" > "$W/d2.c"
cat "$D/dhry_1.c" "$W/d2.c" > "$W/one.c"
gcc -E -P -nostdinc -DTIME_US -DDHRY_RUNS=$RUNS \
	-I "$CC/hosttest/ctest-include" -I "$D" "$W/one.c" > "$W/d.pp" || exit 1
"$CC/host-armm0/cc0" < "$W/d.pp" > "$W/d.tok" || exit 1
rm -f "$W/d.ir"
"$CC/host-armm0/cc1" < "$W/d.tok" 1<> "$W/d.ir" 2>/dev/null || exit 1
for v in new old; do
	rm -f "$W/$v.bc"
	[ $v = old ] && export THUMB_NOR4=1 THUMB_NOCFOLD=1
	THUMB_VERBOSE=1 "$CC/host-armm0/cc2" .symtmp armm0 0 \
		< "$W/d.ir" 1<> "$W/$v.bc" 2> "$W/$v.log" || exit 1
	unset THUMB_NOR4 THUMB_NOCFOLD
done
echo "== native span bytes per function (old -> new)"
paste <(grep 'native:' "$W/old.log") <(grep 'native:' "$W/new.log") |
	sed 's/native: //g'
echo "== object size: old $(stat -c %s "$W/old.bc")  new $(stat -c %s "$W/new.bc")"
echo "== output diff (must be identical)"
qemu-arm "$CC/qemu-armm0/bcrun" "$W/old.bc" > "$W/out.old" 2>&1
qemu-arm "$CC/qemu-armm0/bcrun" "$W/new.bc" > "$W/out.new" 2>&1
grep -v -i -e microsec -e dhrystones "$W/out.old" > "$W/out.old.f"
grep -v -i -e microsec -e dhrystones "$W/out.new" > "$W/out.new.f"
diff "$W/out.old.f" "$W/out.new.f" && echo identical
echo "== qemu wall time, 3 runs each"
for v in old new; do
	for i in 1 2 3; do
		/usr/bin/time -f "$v  %es" qemu-arm "$CC/qemu-armm0/bcrun" \
			"$W/$v.bc" > /dev/null 2> "$W/t.$v.$i"
		cat "$W/t.$v.$i"
	done
done
