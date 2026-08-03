#!/bin/sh
#
# Relink every userland tree the SD image draws from.
#
# Needed whenever the C library changes: the binaries on the card are
# statically linked, so a libc fix reaches them only by rebuilding them.
# The fread/fseek desync (UPSTREAM-fread-fseek-report.md) is exactly
# that kind of fix - it silently returns wrong data to anything that
# freads and fseeks a small file, which is most of the compiler.
#
#   sh relink-userland.sh
#
# Objects are deleted first.  A stale .o linked against the old libc
# would look like a successful rebuild and carry the bug onto the card.

set -e
R=$(cd "$(dirname "$0")/../../.." && pwd)
MK="FUZIX_ROOT=$R USERCPU=armm0"

for d in Applications/util Applications/V7/cmd Applications/V7/games \
         Applications/MWC/cmd Applications/cave Applications/cursesgames \
         Applications/games Applications/levee Applications/bbcbasic \
         Applications/mmedit Applications/cpp Applications/CC
do
	[ -d "$R/$d" ] || continue
	[ -r "$R/$d/Makefile.armm0" ] || continue
	echo "=== $d"
	( cd "$R/$d" && rm -f *.o && make -f Makefile.armm0 $MK ) \
		> /tmp/relink.$$.log 2>&1 || {
			echo "FAILED - tail of the log:" >&2
			tail -20 /tmp/relink.$$.log >&2
			exit 1
		}
done
rm -f /tmp/relink.$$.log

echo "=== platform utils"
( cd "$R/Kernel/platform/platform-rpipico/utils" \
  && rm -f *.o && make FUZIX_ROOT=$R )

echo "done"
