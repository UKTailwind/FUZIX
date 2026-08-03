#!/bin/sh
# Build one C sample through the armm0 chain and run it under qemu-arm,
# which is the closest thing to the board that does not need the board.
#   sh qemuone.sh samples/sw2.c
CC=$(cd "$(dirname "$0")/.." && pwd)
BIN=$CC/host-armm0
W=/tmp/qemuone
src=$1
b=$(basename "$src" .c)
mkdir -p "$W"
gcc -E -P -nostdinc -U__LP64__ -U__LLP64__ -D__ILP32__ \
    -I "$CC/hosttest/ctest-include" "$src" > "$W/$b.pp" || exit 1
"$BIN/cc0" < "$W/$b.pp" > "$W/$b.tok" || exit 1
rm -f "$W/$b.ir";  "$BIN/cc1" < "$W/$b.tok" 1<> "$W/$b.ir"  || exit 1
rm -f "$W/$b.raw"; "$BIN/cc2" .symtmp armm0 0 < "$W/$b.ir" 1<> "$W/$b.raw" || exit 1
{ printf '#!/usr/bin/bcrun\n'; cat "$W/$b.raw"; } > "$W/$b.bc"
chmod 755 "$W/$b.bc"
echo "--- host bcrun ---"
"$BIN/bcrun" "$W/$b.bc"; echo "rc=$?"
echo "--- qemu-arm bcrun (native spans execute) ---"
qemu-arm "$CC/qemu-armm0/bcrun" "$W/$b.bc"; echo "rc=$?"
