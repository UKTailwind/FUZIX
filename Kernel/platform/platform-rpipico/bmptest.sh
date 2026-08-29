#!/bin/sh
#
# bmptest.sh - the gate for loadimage's SPRITE mode.
#
#   sh bmptest.sh
#
# WHY THIS EXISTS: SPRITE LOADBMP produces DATA, not a picture.  A wrong
# colour on the screen is visible; a wrong index in a sprite buffer is
# not - it is blitted about, compared for collisions and written back,
# and the first sign of trouble is a game that looks slightly wrong in
# a way nobody can point at.  So the conversion is checked against an
# independent implementation of the reference's own expression, on
# pictures whose colours sit exactly on its boundaries.
#
# It runs on the host because sprite mode never opens the display:
# `loadimage -s` writes to stdout and touches no ioctl, so the whole
# decoder - bit depths, palette, row order, the window - can be tested
# without a board.
#
# See bmptest.py for what is checked and why those colours.

R=$(cd "$(dirname "$0")/../../.." && pwd)
D=$(cd "$(dirname "$0")" && pwd)
SRC=$R/Kernel/platform/platform-rpipico/utils/loadimage.c
TMP=${TMPDIR:-/tmp}/bmptest.$$

die() {
	echo "bmptest: $*" >&2
	rm -rf "$TMP"
	exit 2
}

for t in gcc python3; do
	command -v $t >/dev/null 2>&1 || die "no $t"
done
[ -r "$SRC" ] || die "no $SRC"

mkdir -p "$TMP" || die "cannot make $TMP"
gcc -w -O2 -o "$TMP/loadimage" "$SRC" || die "build failed"

python3 "$D/bmptest.py" "$TMP/loadimage" "$TMP"
rc=$?
rm -rf "$TMP"
exit $rc
