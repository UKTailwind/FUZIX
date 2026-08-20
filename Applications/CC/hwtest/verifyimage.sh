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
# Exactly what stageall.sh staged, read from the list it writes - the
# same source mkccimage.sh installs from, so this cannot verify a card
# clean while a staged program is missing from it.  That is what it did
# for playmp3 and again for playmod.  The two exception lists must
# match; playsnd is deprecated, see the note in mkccimage.sh.
while read -r b; do
	case $b in cc0 | cc1 | cc2 | ccbc | playsnd) continue ;; esac
	same "/usr/bin/$b" "$S/$b.s"
done < "$S/staged.list"

echo "--- the headers the generated C includes"
# From Applications/mmb2c, which is where mkccimage.sh stages them from.
# It used to say Applications/CC, and mmb2c MOVED - so $want was the
# size of a file that does not exist, i.e. empty, and this reported
# mmb_runtime.h STALE on every card whether or not it was.  A check
# that always fails is a check nobody reads, and this is the one file
# whose staleness makes cc reject generated programs with "type
# mismatch".  The glob below had the same dead path and so matched
# nothing at all: it verified every mmb_*.h by checking none of them.
MMB=$R/Applications/mmb2c
same /usr/lib/cc/include/mmb_runtime.h "$MMB/mmb_runtime.h"
# EVERY mmb_*.h in the tree, by the same glob mkccimage.sh stages them
# with - not a list beside a list.  Two hand-written lists drifted from
# each other twice: mmb_spi.h was in neither at v0.11, and at v0.15
# mmb_play/blit/sprite/flash.h were shipped by neither, so a card that
# verified clean could not compile a program that made a sound.  A
# header present in the tree and absent from the card now FAILS here.
for f in "$MMB"/mmb_*.h; do
	same "/usr/lib/cc/include/$(basename "$f")" "$f"
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
