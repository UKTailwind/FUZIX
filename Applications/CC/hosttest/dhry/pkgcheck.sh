#!/bin/bash
# qemu sanity for the board package: outputs must be identical
# base-vs-new (timing lines excluded), and non-empty.
CC=$(cd "$(dirname "$0")/../.." && pwd)
W=/tmp/ccperf-board
Q="qemu-arm $CC/qemu-armm0/bcrun"
cd "$W" || exit 1
SEIN=/home/peter/src/mmb2c/tests/solar_eclipse.in
for f in dhry-base.bc dhry-r4.bc dhry-new.bc; do
	$Q "$f" > "$f.out" 2>&1 || { echo "RUN FAIL $f"; exit 1; }
	[ -s "$f.out" ] || { echo "EMPTY $f"; exit 1; }
done
for f in se-base.bc se-new.bc; do
	$Q "$f" < "$SEIN" > "$f.out" 2>&1 || { echo "RUN FAIL $f"; exit 1; }
	[ -s "$f.out" ] || { echo "EMPTY $f"; exit 1; }
done
grep -v -i -e microsec -e dhrystones -e 'begins\|ends' dhry-base.bc.out > a
grep -v -i -e microsec -e dhrystones -e 'begins\|ends' dhry-r4.bc.out > b
grep -v -i -e microsec -e dhrystones -e 'begins\|ends' dhry-new.bc.out > c
diff a b && diff a c && echo "DHRY IDENTICAL ($(wc -l < a) lines)"
grep -v -i -e 'time' se-base.bc.out > d
grep -v -i -e 'time' se-new.bc.out > e
diff d e && echo "ECLIPSE IDENTICAL ($(wc -l < d) lines)"
