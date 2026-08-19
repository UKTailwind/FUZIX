#!/bin/sh
# Regenerate tests/type.expected and tests/structtest.expected from
# host-armm0 bcrun runs of the already-built objects.
M=$(cd "$(dirname "$0")/.." && pwd)
FCC=${FCC:-/home/peter/src/FUZIX/Applications/CC}
"$FCC/host-armm0/bcrun" /tmp/fccbuild/type.bc \
	> "$M/tests/type.expected" 2>&1
cd /tmp && "$FCC/host-armm0/bcrun" /tmp/fccbuild/structtest.bc \
	> "$M/tests/structtest.expected" 2>&1
grep -c 'PASS' "$M/tests/structtest.expected"
grep -c 'FAIL' "$M/tests/structtest.expected"
wc -l "$M/tests/type.expected" "$M/tests/structtest.expected"
