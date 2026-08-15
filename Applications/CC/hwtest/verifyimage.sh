#!/bin/sh
#
# Check that the things this session depends on actually landed on the
# card image.  mkccimage.sh already refuses on a ucp error and fsck's
# the result, but neither notices a file that is present and STALE, and
# a stale /usr/lib/cc/include/mmb_runtime.h is exactly what made cc
# reject the generated prologue with "type mismatch".
#
# The quieter version of that is worse and has also happened: a header
# too OLD to declare a runtime function does not fail the compile at
# all.  cc falls back to an implicit declaration, so a bare literal
# argument goes into ONE 32-bit slot where the native reads two, every
# argument after it shifts by one, and the program runs and is wrong.
# LMID(a(), 1) = "AB" - the form that leaves the count out - reported
# "Selection exceeds length of string" for a selection that was well
# inside the string.  Size-compare every header, not just the ones a
# change obviously touched.
#
#   sh verifyimage.sh
#
# It compares each file on the card against the one that was staged for
# it, by size, and says so when they disagree.  Size is not a checksum,
# but a stale binary is a DIFFERENT build, and two builds of the same
# program agreeing to the byte is not something that happens by
# accident.
#
# ucp has no "echo", so every heading here comes from the shell and each
# listing is its own ucp run.  The first version of this script put the
# headings inside the ucp script and filtered on them - which meant ucp
# rejected every one, nothing matched the filter, and the check printed
# nothing at all while exiting 0.

set -e
R=$(cd "$(dirname "$0")/../../.." && pwd)
S=$(cd "$(dirname "$0")" && pwd)
IMG=$R/Images/rpipico/pc3-sd-cc.img
UCP=$R/Standalone/ucp
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

[ -r "$IMG" ] || { echo "no $IMG - build it first" >&2; exit 1; }
[ -x "$UCP" ] || { echo "no $UCP - make -C Standalone" >&2; exit 1; }

# Geometry from the image's own MBR - never a copy of mkcard.sh's numbers
. "$R/Kernel/platform/platform-rpipico/p2geom.sh"
p2geom "$IMG"

dd if="$IMG" of="$W/p2.img" bs=512 skip=$P2_START count=$P2_COUNT status=none

# ls one directory of the image
list() {
	printf 'cd %s\nls\nexit\n' "$1" | "$UCP" "$W/p2.img" 2>&1
}

# size of one file on the image, or empty if it is not there
size_on_card() {
	list "$(dirname "$1")" |
	awk -v n="$(basename "$1")" '$NF == n { print $(NF - 5) }'
}

fail=0

# $1 = path on the card, $2 = the file it was staged from
same() {
	got=$(size_on_card "$1")
	want=$(wc -c < "$2" | tr -d ' ')
	if [ -z "$got" ]; then
		echo "  MISSING  $1"
		fail=1
	elif [ "$got" != "$want" ]; then
		echo "  STALE    $1 ($got on the card, $want staged)"
		fail=1
	else
		echo "  ok       $1 ($got)"
	fi
}

# The passes live in /usr/lib/cc; everything a user types lives in
# /usr/bin, where "cc" is the driver (ccbc).
echo "--- the compiler passes"
for f in cc0 cc1 cc2; do
	same "/usr/lib/cc/$f" "$S/$f.s"
done

echo "--- the programs"
same /usr/bin/cc "$S/ccbc.s"
for f in cpp bcrun bcdump mmbc mmedit saveimage loadimage playmp3; do
	same "/usr/bin/$f" "$S/$f.s"
done

echo "--- the headers the generated C includes"
same /usr/lib/cc/include/mmb_runtime.h "$R/Applications/CC/mmb_runtime.h"
# Every mmb_*.h mkccimage.sh stages, not just the drawing ones: this
# list had drifted behind that one, so the peripheral headers were
# shipped and never checked - and mmb_spi.h was not shipped at all.
for f in mmb_gfx.h mmb_gfx_pts.h mmb_gfx_circle.h mmb_gfx_box.h \
		mmb_gfx_rbox.h mmb_gfx_triangle.h mmb_gfx_arc.h \
		mmb_gfx_text.h mmb_gfx_map.h mmb_gpio.h \
		mmb_gfx_polygon.h mmb_gfx_bezier.h mmb_gfx_fill.h \
		mmb_int.h mmb_pwm.h mmb_i2c.h mmb_spi.h mmb_peek.h \
		mmb_comms.h mmb_onewire.h mmb_port.h mmb_pulse.h \
		mmb_wait.h; do
	same "/usr/lib/cc/include/$f" "$R/Applications/CC/$f"
done

echo "--- /root/cc"
list /root/cc | sed -n '3,$p' | awk '{ printf "  %-20s %s\n", $NF, $(NF-5) }'

# The curated examples, which come from the BASE image (mksdimage.sh)
# rather than from this script.  Listed for the same reason /root/cc is:
# a ucp "cd" into a directory that does not exist carries on and drops
# every following file somewhere else, and an empty listing here is what
# that looks like - so an examples directory that silently failed to
# reach the card would otherwise be found by the user, not by us.
echo "--- /root/MMBasic"
list /root/MMBasic | sed -n '3,$p' | awk '{ printf "  %-20s %s\n", $NF, $(NF-5) }'

echo
if [ $fail = 0 ]; then
	echo "image matches what was staged"
else
	echo "IMAGE DOES NOT MATCH - re-run stageall.sh and rebuild it" >&2
	exit 1
fi
