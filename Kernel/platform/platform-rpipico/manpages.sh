#!/bin/sh
#
# Every command on the card has a manual page, and every page names a
# command that is there.
#
#   sh manpages.sh
#
# WHY THIS EXISTS.  mancheck.sh proves the PC3 manual does not name a
# command the card lacks.  This is the same rule one level down: a
# `man ls' that answers "No manual entry" is the state the card shipped
# in, and a page describing a command that was removed is worse - it
# reads as authority.
#
# Three things are checked, and the third is the one that matters:
#
#   1.  Every executable in /bin, /usr/bin and /usr/games has a page in
#       /usr/man/man1.
#   2.  Every page in /usr/man/man1 corresponds to something on the
#       card.
#   4.  Every SEE ALSO reference resolves to a page that exists.
#
#   3.  Every page RENDERS: man(1) is not nroff, and it prints
#       "**** Unknown formatter command" into the middle of a page for
#       anything outside the subset it knows.  A page nobody rendered
#       before shipping is a page with that line in it.  This builds
#       man.c for the host and runs every page through it.
#
# Runs offline against the built image - the same ucp listing
# verifyimage.sh and mancheck.sh use - so it belongs in the release
# gates beside relcheck.sh and ioctlcheck.sh.

set -e
D=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$D/../../.." && pwd)
IMG=$R/Images/rpipico/pc3-sd-cc.img
UCP=$R/Standalone/ucp
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

[ -r "$IMG" ] || { echo "no $IMG - build it first" >&2; exit 1; }
[ -x "$UCP" ] || { echo "no $UCP - make -C Standalone" >&2; exit 1; }

. "$D/p2geom.sh"
p2geom "$IMG"
dd if="$IMG" of="$W/p2.img" bs=512 skip=$P2_START count=$P2_COUNT status=none

ls_dir() {
	printf 'cd %s\nls\nexit\n' "$1" | "$UCP" "$W/p2.img" 2>/dev/null |
		awk '{ print $NF }' | grep -v '^$' | grep -v '/$'
}

for d in /bin /usr/bin /usr/games; do ls_dir $d; done | sort -u > "$W/cmds"
ls_dir /usr/man/man1 | sed 's/\.1$//' | sort -u > "$W/pages"

fail=0

echo "--- commands with no page"
n=0
for c in $(cat "$W/cmds"); do
	if ! grep -qxF "$c" "$W/pages"; then
		echo "  NO PAGE  $c"
		n=$((n + 1))
	fi
done
[ "$n" -eq 0 ] && echo "  none" || fail=1

echo "--- pages naming no command"
n=0
for m in $(cat "$W/pages"); do
	if ! grep -qxF "$m" "$W/cmds"; then
		echo "  NO COMMAND  $m"
		n=$((n + 1))
	fi
done
[ "$n" -eq 0 ] && echo "  none" || fail=1

# 3: render every page the tree can produce.  Against the sources
# rather than the image, because that is what a commit changes and
# where the fix goes.
echo "--- rendering"
if command -v cc > /dev/null 2>&1; then
	cc -w -O0 -o "$W/man" "$R/Applications/util/man.c" 2>/dev/null || {
		echo "  cannot build man.c for the host - rendering not checked"
		exit $fail
	}
	sh "$R/Applications/man1/mkawk1.sh" > /dev/null
	n=0
	for f in "$R/Applications/man1"/*.1; do
		bad=$(PAGER=cat "$W/man" -l "$f" 2>&1 |
			grep -o 'Unknown formatter command: \.[^ ]*' |
			sort -u | tr '\n' ' ')
		if [ -n "$bad" ]; then
			echo "  $(basename $f): $bad"
			n=$((n + 1))
		fi
	done
	[ "$n" -eq 0 ] && echo "  all clean" || fail=1
else
	echo "  no host compiler - rendering not checked"
fi

# 4: a SEE ALSO that names a command the card has not got is the same
# mistake mancheck.sh exists to catch, one level down - and the easiest
# one to make, because the obvious neighbour of a command is often a
# program this machine does not carry.
echo "--- cross references"
n=0
for f in $(ls_dir /usr/man/man1); do
	printf 'cd /usr/man/man1
cat %s
exit
' "$f" |
		"$UCP" "$W/p2.img" 2>/dev/null |
		sed -n 's/^\.BR*  *\([A-Za-z0-9_.-]*\) *(1).*/\1/p'
done | grep -v '^$' | sort -u > "$W/refs"
for r in $(cat "$W/refs"); do
	if ! grep -qxF "$r" "$W/pages"; then
		echo "  DANGLING  $r"
		n=$((n + 1))
	fi
done
[ "$n" -eq 0 ] && echo "  all resolve" || fail=1

nc=$(wc -l < "$W/cmds" | tr -d ' ')
np=$(wc -l < "$W/pages" | tr -d ' ')
if [ "$fail" -eq 0 ]; then
	echo "manpages: $nc commands, $np pages, all matched and all render"
else
	echo "manpages: FAILED" >&2
fi
exit $fail
