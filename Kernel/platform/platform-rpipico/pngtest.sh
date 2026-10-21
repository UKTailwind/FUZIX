#!/bin/sh
#
# pngtest.sh - the gate for loadpng's inflate (utils/upng.c).
#
#   sh pngtest.sh
#
# WHY THIS EXISTS: the decoder had two memory bugs that no picture on
# hand ever showed.  A code length of 15 - legal deflate, and what zlib
# writes for a skewed enough image - incremented blcount[15] in a
# 15-entry array, and a back-reference before the start of the data read
# behind the output buffer.  On the PC the first decoded correctly by the
# luck of the stack layout and the second returned a picture; PicoMite
# found both in its copy (4d91e6c).  So the decoder is built with
# AddressSanitizer and fed images made to hit exactly those two paths.
#
# See pngtest.py for how the images are made and what is checked.

R=$(cd "$(dirname "$0")/../../.." && pwd)
D=$(cd "$(dirname "$0")" && pwd)
U=$R/Kernel/platform/platform-rpipico/utils
TMP=${TMPDIR:-/tmp}/pngtest.$$

die() {
	echo "pngtest: $*" >&2
	rm -rf "$TMP"
	exit 2
}

for t in gcc python3; do
	command -v $t >/dev/null 2>&1 || die "no $t"
done
[ -r "$U/upng.c" ] || die "no $U/upng.c"

mkdir -p "$TMP" || die "cannot make $TMP"
# The sanitizer IS the test for the blcount write: without it that bug
# decodes correctly.  FreeMemorySafe never frees, so leaks are expected.
gcc -w -O1 -g -fsanitize=address -fno-omit-frame-pointer -DUPNG_NO_FILE \
	-I"$U" -o "$TMP/pngtest" "$D/pngtest.c" "$U/upng.c" ||
	die "build failed (does this gcc have -fsanitize=address?)"

ASAN_OPTIONS=detect_leaks=0:exitcode=99 python3 "$D/pngtest.py" "$TMP/pngtest" "$TMP"
rc=$?
rm -rf "$TMP"
exit $rc
