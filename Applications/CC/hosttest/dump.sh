#!/bin/bash
# Compile one file through the chain and disassemble it.
#   bash dump.sh /tmp/foo.c
CC=$(cd "$(dirname "$0")/.." && pwd)
BIN=$CC/host-armm0
W=/tmp/dump
rm -rf "$W"; mkdir -p "$W"; cd "$W" || exit 1
gcc -E -P -nostdinc -I "$CC/hosttest/ctest-include" "$1" > a.pp || exit 1
"$BIN/cc0" < a.pp > a.tok || exit 1
rm -f a.ir; "$BIN/cc1" < a.tok 1<> a.ir || exit 1
rm -f a.bc; "$BIN/cc2" .symtmp armm0 0 < a.ir 1<> a.bc || exit 1
"$BIN/bcdump" a.bc
