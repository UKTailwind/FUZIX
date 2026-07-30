#!/bin/bash
# Stage 3 end to end on the development machine:
#   t_nat.c -> .bc (stub add2, +1000000 tell-tale)
#   add2.s  -> Thumb bytes -> patched over the stub (natpatch.py)
#   stub .bc and patched .bc both run under qemu; the patched one must
#   produce the plain sums, the stub the inflated ones, and the host
#   x86 bcrun must refuse the patched object with a clear message.
D=$(cd "$(dirname "$0")" && pwd)
CC=$(cd "$D/../.." && pwd)
BIN=$CC/host-armm0
INC=$CC/hosttest/ctest-include
W=/tmp/ccnat
mkdir -p "$W"

gcc -E -P -nostdinc -I "$INC" "$D/t_nat.c" > "$W/t_nat.pp" || exit 1
"$BIN/cc0" < "$W/t_nat.pp" > "$W/t_nat.tok" || exit 1
rm -f "$W/t_nat.ir"
"$BIN/cc1" < "$W/t_nat.tok" 1<> "$W/t_nat.ir" 2> "$W/cc1.err" || { cat "$W/cc1.err"; exit 1; }
rm -f "$W/t_nat.bc"
"$BIN/cc2" .symtmp armm0 0 < "$W/t_nat.ir" 1<> "$W/t_nat.bc" 2> "$W/cc2.err"
[ -s "$W/cc2.err" ] && { cat "$W/cc2.err"; exit 1; }

arm-none-eabi-as -mthumb -o "$W/add2.o" "$D/add2.s" || exit 1
arm-none-eabi-objcopy -O binary "$W/add2.o" "$W/add2.bin" || exit 1
python3 "$D/natpatch.py" "$W/t_nat.bc" add2 "$W/add2.bin" "$W/t_nat_native.bc" || exit 1

echo "== stub bytecode under qemu (the million shows) =="
qemu-arm "$CC/qemu-armm0/bcrun" "$W/t_nat.bc"
echo "== patched NATIVE under qemu (plain sums prove native ran) =="
qemu-arm "$CC/qemu-armm0/bcrun" "$W/t_nat_native.bc"
echo "== patched object on the x86 host (must refuse cleanly) =="
"$BIN/bcrun" "$W/t_nat_native.bc" || true
echo
echo "board image: $W/t_nat_native.bc"
