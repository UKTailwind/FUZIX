#!/bin/sh
#
# The release number is written down in three places and nothing made
# them agree:
#
#   config.h                   PC3_RELEASE "0.13"   - the boot banner
#   Applications/CC/           MM_RELEASE 0.13      - what a BASIC
#     mmb_runtime.h                                   program's MM.VER
#                                                     answers
#   FUZIX-PC3-MANUAL.md        date: "Release v0.13 ..."
#
# MM_RELEASE was set once, when MM.VER was added at v0.10, and was
# still 0.10 three releases later - so every program that asked the
# machine what it was running got an answer three releases old, and
# nothing anywhere said so.  The comment on it claimed the release
# recipe covered it.  The recipe did not.
#
# MM.VER's format is MMBasic's: major.mmpp, so the fields compare as one
# number and 0.10 reads higher than 0.09.  That means the minor part is
# PADDED TO TWO DIGITS - "0.9" in config.h is 0.09 here, not 0.9 - which
# is the other half of why this drifted: the two are not the same string
# and cannot be diffed by eye.
#
#	sh relcheck.sh		 -> the three, or what disagrees
#
# Run from the gates and at step 3 of the release recipe.  Exit 1 on any
# disagreement.
#
# mmb_runtime.h lives in Applications/mmb2c, and there is one of it -
# this used to check a synced copy in Applications/CC, so a fix could be
# made in the right place and still not reach the card.

D=$(dirname "$0")
R=$(cd "$D/../../.." && pwd)

CFG=$D/config.h
RT=$R/Applications/mmb2c/mmb_runtime.h
MAN=$D/FUZIX-PC3-MANUAL.md

for f in "$CFG" "$RT" "$MAN"; do
	[ -r "$f" ] || { echo "cannot read $f" >&2; exit 1; }
done

rel=$(sed -n 's/^#define[ \t]*PC3_RELEASE[ \t]*"\([^"]*\)".*/\1/p' "$CFG")
[ -n "$rel" ] || { echo "no PC3_RELEASE in config.h" >&2; exit 1; }

# "0.13" -> 0.13, "0.9" -> 0.09, "1.0" -> 1.00
maj=${rel%%.*}
min=${rel#*.}
case ${#min} in
1) min=0$min ;;
2) ;;
*) echo "PC3_RELEASE \"$rel\": minor part is not one or two digits" >&2
   exit 1 ;;
esac
want=$maj.$min

mmv=$(sed -n 's/^#define[ \t]*MM_RELEASE[ \t]*\([0-9.]*\).*/\1/p' "$RT")
[ -n "$mmv" ] || { echo "no MM_RELEASE in $RT" >&2; exit 1; }

rc=0
echo "  PC3_RELEASE   $rel      (config.h)"
echo "  MM_RELEASE    $mmv      (mmb_runtime.h, MM.VER)"

if [ "$mmv" != "$want" ]; then
	echo "MM_RELEASE is $mmv, should be $want for PC3_RELEASE \"$rel\"" >&2
	echo "  edit Applications/mmb2c/mmb_runtime.h" >&2
	rc=1
fi

date=$(sed -n 's/^date:[ \t]*"\(.*\)".*/\1/p' "$MAN" | head -1)
echo "  manual        $date"
case $date in
*"v$rel"*) ;;
*) echo "the manual's date: line does not name v$rel" >&2
   rc=1 ;;
esac

[ $rc -eq 0 ] && echo "release $rel, all three agree"
exit $rc
