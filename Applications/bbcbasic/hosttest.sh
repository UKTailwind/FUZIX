#!/bin/sh
# Build the FUZIX personality of BBC BASIC natively on Linux for
# debugging: same bbccon.c code paths (text loader, polled timer,
# kbwait1 input, sbrk probe) under glibc and gdb.  Needs nasm and a
# BBCSDL checkout (for the x86-64 data segment and assembler).
#
#   BBCSDL=/path/to/bbcsdl sh hosttest.sh
#
# Then:  ./bbcfuzix-host  (pipe a session in, or run interactively)
set -e
B=$(cd "$(dirname "$0")" && pwd)
S=${BBCSDL:-/tmp/bbcsdl}/src
test -f "$S/bbdata_x86_64.nas" || {
    echo "BBCSDL sources not found at $S (set BBCSDL=)" >&2
    exit 1
}
FLAGS="-Wall -I $B -DFUZIX -U__linux__ -DPC3_HOST_TEST -fsigned-char"
gcc $FLAGS -c -O2 -ffast-math -fno-finite-math-only \
    "$B/bbmain.c" "$B/bbexec.c" "$B/bbeval.c"
gcc $FLAGS -c -Os "$S/bbasmb_x86_64.c" -o bbasmb.o
gcc $FLAGS -Wno-array-bounds -Wno-unused-result -c -Os \
    "$B/bbccos.c" "$B/bbccon.c"
gcc $FLAGS -fno-builtin -c -Os "$B/bbcstdio.c"
nasm -f elf64 -s "$S/bbdata_x86_64.nas" -o bbdata.o
gcc -o bbcfuzix-host bbmain.o bbexec.o bbeval.o bbasmb.o bbdata.o \
    bbccos.o bbccon.o bbcstdio.o -lm -lrt
echo "built: ./bbcfuzix-host"
