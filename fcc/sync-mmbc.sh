#!/bin/sh
#
# The master copies of the mmbc translator (the C rewrite of mmb2c.py)
# live in this repo under mmbc/.  The FUZIX board build (Applications/
# CC, Makefile.armm0) compiles verbatim copies; re-run this after any
# mmbc change so the two trees stay identical.  The byte-identity
# harness (mmbc/cgate.sh + fcctests with MMB2C=) runs HERE, on the
# masters, before syncing.

M=$(cd "$(dirname "$0")/.." && pwd)
FCC=${FCC:-/home/peter/src/FUZIX/Applications/CC}

cp "$M"/mmbc/mmbc_*.c "$M"/mmbc/mmbc.h "$M"/mmbc/mmbc_expr.h "$FCC/"
# BASIC samples for the SD image (/root/cc): the acceptance program
# and a small one
cp "$M/tests/solar_eclipse.bas" "$M/tests/solar_eclipse.in" \
   "$M/tests/t1.bas" "$M/tests/bench.bas" "$FCC/hwtest/"
# The FCC-view headers the generated C needs (math.h maps to bcrun
# natives); installed into /usr/lib/cc/include by mkccimage.sh
# alongside mmb_runtime.h (synced by sync-runtime.sh)
mkdir -p "$FCC/hosttest/fcc-include"
cp "$M"/fcc/include/math.h "$M"/fcc/include/ctype.h \
   "$M"/fcc/include/stdint.h "$M"/fcc/include/time.h \
   "$FCC/hosttest/fcc-include/"
echo "synced mmbc sources + basic samples + fcc headers -> $FCC"
