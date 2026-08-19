#!/bin/sh
#
# Every command the manual names must exist on the card.
#
#   sh mancheck.sh
#
# The manual's own words, before this check existed:
#
#   "This list is checked against the build recipe rather than
#    remembered: it used to promise less, hd, factor, units, m4, make
#    and mail, none of which were ever installed.  A manual that names a
#    command the card does not have is worse than one that stays quiet."
#
# It was not checked against anything, and by v0.17 it promised `ue`,
# `picogpio`, `gpiotool`, `gfxtest`, `flashrom`, `doswrite`, `as09`,
# `ld09`, a /usr/lib/bbc that does not exist, and a games collection of
# some thirty titles of which the card carries two.  Naming the danger
# is not the same as checking for it, which is what this file is for.
#
# What is checked: THE FIRST WORD OF EVERY LINE A TRANSCRIPT SHOWS YOU
# TYPING - a line beginning "# " inside a fenced block.  That is a
# property of the document rather than a second list beside it, so a
# command added to the chapter is checked from then on.
#
# Backticked words were tried first and were far too greedy: the manual
# uses backticks for argument names (`cutoff`, `transparent`), C
# functions (`printf`, `scanf`), device nodes (`hdb2`), file names
# (`prog.bas`) and - the one that gives the game away - the list of
# things it says are NOT installed.  Thirty-two false alarms out of
# seventy-five names.  A check nobody can trust gets switched off, so
# it matters that this one only ever reports something real.
#
# Runs offline, against the built image rather than a running board -
# same ucp listing verifyimage.sh uses - so it belongs in the release
# gates beside relcheck.sh and ioctlcheck.sh.

set -e
D=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$D/../../.." && pwd)
IMG=$R/Images/rpipico/pc3-sd-cc.img
UCP=$R/Standalone/ucp
MAN=$D/FUZIX-PC3-MANUAL.md
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

[ -r "$IMG" ] || { echo "no $IMG - build it first" >&2; exit 1; }
[ -x "$UCP" ] || { echo "no $UCP - make -C Standalone" >&2; exit 1; }
[ -r "$MAN" ] || { echo "no $MAN" >&2; exit 1; }

. "$D/p2geom.sh"
p2geom "$IMG"
dd if="$IMG" of="$W/p2.img" bs=512 skip=$P2_START count=$P2_COUNT status=none

# What the card actually carries, from the image itself.
for d in /bin /usr/bin /usr/games; do
	printf 'cd %s\nls\nexit\n' "$d" | "$UCP" "$W/p2.img" 2>/dev/null |
		awk '{ print $NF }'
done | grep -v '^$' | sort -u > "$W/have"

# What the manual shows you typing, from the command chapters only.
awk '
	/^# Commands at the `#` prompt/           { on = 1 }
	/^# The filesystem and included software/ { on = 1 }
	/^# Text processing/                      { on = 0 }
	on && /^```/                              { fence = !fence; next }
	on && fence && /^# [a-z]/ {
		w = $2
		if (w !~ /[\/.]/)          # not ./prog.bc, not a file name
			print w
	}
' "$MAN" | sort -u > "$W/named"

# Shell built-ins, which are real things to type and are not files.
cat > "$W/skip" <<'EOF'
cd
exit
EOF

miss=0
for n in $(cat "$W/named"); do
	grep -qx "$n" "$W/skip" && continue
	if ! grep -qx "$n" "$W/have"; then
		echo "  NOT ON THE CARD  $n"
		miss=$((miss + 1))
	fi
done

n_named=$(wc -l < "$W/named" | tr -d ' ')
n_have=$(wc -l < "$W/have" | tr -d ' ')
if [ "$miss" -eq 0 ]; then
	echo "mancheck: $n_named names in the manual, all present ($n_have on the card)"
else
	echo "mancheck: $miss of $n_named names are not on the card" >&2
	exit 1
fi
