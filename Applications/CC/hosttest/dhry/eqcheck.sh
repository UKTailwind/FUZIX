#!/bin/bash
# eqtort 3-way under qemu: native vs forced-bytecode vs reclaim, at
# every regcache setting.  The eqop audit's executable half.
CC=$(cd "$(dirname "$0")/../.." && pwd)
W=/tmp/eqcheck
Q="qemu-arm $CC/qemu-armm0/bcrun"
mkdir -p "$W"
gcc -E -P -nostdinc -I "$CC/hosttest/ctest-include" \
	"$CC/hosttest/samples/eqtort.c" > "$W/e.pp" || exit 1
"$CC/host-armm0/cc0" < "$W/e.pp" > "$W/e.tok" || exit 1
rm -f "$W/e.ir"
"$CC/host-armm0/cc1" < "$W/e.tok" 1<> "$W/e.ir" 2>/dev/null || exit 1

for mode in "THUMB_NOREGC=1" "IGNORE=0" "THUMB_REGC8=1"; do
	rm -f "$W/a.raw" "$W/r.raw"
	env $mode "$CC/host-armm0/cc2" .symtmp armm0 0 \
		< "$W/e.ir" 1<> "$W/a.raw" 2>/dev/null
	env $mode THUMB_RECLAIM=1 "$CC/host-armm0/cc2" .symtmp armm0 0 \
		< "$W/e.ir" 1<> "$W/r.raw" 2>/dev/null
	$Q "$W/a.raw" > "$W/n.out" 2>&1
	BCRUN_BYTECODE=1 $Q "$W/a.raw" > "$W/b.out" 2>&1
	$Q "$W/r.raw" > "$W/rc.out" 2>&1
	if cmp -s "$W/n.out" "$W/b.out" && cmp -s "$W/n.out" "$W/rc.out"
	then
		echo "ok    $mode"
	else
		echo "BROKEN $mode"
		diff "$W/b.out" "$W/n.out" | head -5
	fi
done
