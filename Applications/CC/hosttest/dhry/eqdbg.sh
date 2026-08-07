#!/bin/bash
CC=$(cd "$(dirname "$0")/../.." && pwd)
W=/tmp/eqcheck
cd "$W" || exit 1
rm -f x.raw x2.err
gcc -E -P -nostdinc -I "$CC/hosttest/ctest-include" \
	"$CC/hosttest/samples/eqtort.c" > e.pp || exit 1
"$CC/host-armm0/cc0" < e.pp > e.tok || exit 1
rm -f e.ir
"$CC/host-armm0/cc1" < e.tok 1<> e.ir 2>/dev/null || exit 1
THUMB_REGCDBG=2 THUMB_REGC8=1 "$CC/host-armm0/cc2" .symtmp armm0 0 \
	< e.ir 1<> x.raw 2> x2.err
echo "cc2 rc=$?"
grep '^regcache' x2.err
echo ===
grep -A5 'regc? tint' x2.err
