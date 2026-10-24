#!/bin/sh
#
# The release number is written down in two places and nothing made
# them agree:
#
#   config.h                   PC3_RELEASE "0.13"   - the boot banner
#   FUZIX-PC3-MANUAL.md        date: "Release v0.13 ..."
#
# There used to be a third: MM_RELEASE in Applications/mmb2c/
# mmb_runtime.h, what a BASIC program's MM.VER answered.  It was set
# once, when MM.VER was added at v0.10, and was still 0.10 three
# releases later - so every program that asked the machine what it was
# running got an answer three releases old, and nothing said so.  Since
# 24 Oct 2026 MM.VER answers the PicoMite version the translator follows
# (MM_VERSION), because PicoMite programs compare it with a firmware
# version, so it is no longer the release and is not checked here.
#
#	sh relcheck.sh		 -> the two, or what disagrees
#
# Run from the gates and at step 1 of the release recipe.  Exit 1 on any
# disagreement.

D=$(dirname "$0")

CFG=$D/config.h
MAN=$D/FUZIX-PC3-MANUAL.md

for f in "$CFG" "$MAN"; do
	[ -r "$f" ] || { echo "cannot read $f" >&2; exit 1; }
done

rel=$(sed -n 's/^#define[ \t]*PC3_RELEASE[ \t]*"\([^"]*\)".*/\1/p' "$CFG")
[ -n "$rel" ] || { echo "no PC3_RELEASE in config.h" >&2; exit 1; }

rc=0
echo "  PC3_RELEASE   $rel      (config.h)"

date=$(sed -n 's/^date:[ \t]*"\(.*\)".*/\1/p' "$MAN" | head -1)
echo "  manual        $date"
case $date in
*"v$rel"*) ;;
*) echo "the manual's date: line does not name v$rel" >&2
   rc=1 ;;
esac

[ $rc -eq 0 ] && echo "release $rel, both agree"
exit $rc
