#!/bin/sh
# Compile and link fat for the board's own toolchain and libc.
# fattest.sh proves the logic against glibc; this proves that what it
# proved is also what the board will run.
set -e
cd "$(dirname "$0")/../../../Applications/util"
rm -f fat fat.o
make -f Makefile.armm0 fat
ls -l fat
arm-none-eabi-size fat 2>/dev/null || true
