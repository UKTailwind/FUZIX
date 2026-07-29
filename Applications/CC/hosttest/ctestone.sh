#!/bin/bash
# Run a single conformance test through the chain, showing each step.
#   bash ctestone.sh 00112
CC=$(cd "$(dirname "$0")/.." && pwd)
SUITE=${SUITE:-/home/peter/src/c-testsuite/tests/single-exec}
INC=$CC/hosttest/ctest-include
BIN=$CC/host-armm0
W=/tmp/ctestone
b=$1
rm -rf "$W"; mkdir -p "$W"; cd "$W" || exit 1

gcc -E -P -nostdinc -I "$INC" -I "$SUITE" "$SUITE/$b.c" > "$b.pp" || exit 1
"$BIN/cc0" < "$b.pp" > "$b.tok" 2> "$b.cc0.err"; echo "cc0 rc=$?"
rm -f "$b.ir"; "$BIN/cc1" < "$b.tok" 1<> "$b.ir" 2> "$b.cc1.err"; echo "cc1 rc=$?"
grep ' - ' "$b.cc1.err" | head -5
rm -f "$b.bc"; "$BIN/cc2" .symtmp armm0 0 < "$b.ir" 1<> "$b.bc" 2> "$b.cc2.err"; echo "cc2 rc=$?"
cat "$b.cc2.err"
"$BIN/bcrun" "$b.bc" > "$b.out" 2> "$b.run.err"; echo "bcrun rc=$?"
cat "$b.run.err"
echo "--- output:"; cat "$b.out"
echo "--- expected:"; cat "$SUITE/$b.c.expected" 2>/dev/null
