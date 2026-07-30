#!/bin/bash
# Build and run bench.c both ways on the host: FCC bytecode under
# bcrun, and gcc -O2 native.  The checksums must agree line for line;
# the ratio is the dispatch overhead the Thumb backend exists to
# remove.  Send /tmp/ccbench/bench.bc to the board for the third
# column.
CC=$(cd "$(dirname "$0")/.." && pwd)
INC=$CC/hosttest/ctest-include
BIN=$CC/host-armm0
W=/tmp/ccbench
mkdir -p "$W"; cd "$W" || exit 1

gcc -E -P -nostdinc -DMM_FCC -I "$INC" "$CC/hosttest/bench.c" > bench.pp || exit 1
"$BIN/cc0" < bench.pp > bench.tok 2> cc0.err || { cat cc0.err; exit 1; }
rm -f bench.ir
"$BIN/cc1" < bench.tok 1<> bench.ir 2> cc1.err || { cat cc1.err; exit 1; }
rm -f bench.bc
"$BIN/cc2" .symtmp armm0 0 < bench.ir 1<> bench.bc 2> cc2.err
[ -s cc2.err ] && { cat cc2.err; exit 1; }

gcc -O2 -o bench.native "$CC/hosttest/bench.c" || exit 1

echo "== gcc -O2 native =="
./bench.native
echo "== FCC bytecode under bcrun ($(stat -c %s bench.bc) bytes) =="
"$BIN/bcrun" bench.bc
