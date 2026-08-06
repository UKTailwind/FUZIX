#!/bin/bash
# Dump the regcache classification for one function of dhrystone.
#   bash regdbg.sh [funcname]
D=$(cd "$(dirname "$0")" && pwd)
CC=$(cd "$D/../.." && pwd)
W=/tmp/regdbg
FN=${1:-main}
mkdir -p "$W"
sed '/#include "dhry.h"/d' "$D/dhry_2.c" > "$W/d2.c"
cat "$D/dhry_1.c" "$W/d2.c" > "$W/one.c"
gcc -E -P -nostdinc -DTIME_US -DDHRY_RUNS=400000 \
	-I "$CC/hosttest/ctest-include" -I "$D" "$W/one.c" > "$W/d.pp"
"$CC/host-armm0/cc0" < "$W/d.pp" > "$W/d.tok"
rm -f "$W/d.ir"
"$CC/host-armm0/cc1" < "$W/d.tok" 1<> "$W/d.ir" 2>/dev/null
rm -f "$W/d.raw"
THUMB_REGCDBG=2 "$CC/host-armm0/cc2" .symtmp armm0 0 \
	< "$W/d.ir" 1<> "$W/d.raw" 2> "$W/dbg.txt"
awk -v fn="$FN" '$0 ~ "^regc\\? "fn" " {p=1} p && /^regc\?/ && $0 !~ "^regc\\? "fn" " && n++ {exit} p {print}' "$W/dbg.txt" | head -40
grep "^regcache:" "$W/dbg.txt"
