#!/bin/bash
#
# The translate-time refusals.  Every tests/refuse/*.bas is a program the
# reference will not run at all - a duplicate label, say, is refused
# before the first statement - and both translators must refuse it too:
# a non-zero exit, an ERROR line carrying the text the program's first
# line names, and the same ERROR lines from both (cgate's rule, for the
# programs that produce no C).
#
#   ' REFUSE: Duplicate label
#
# The other gates glob tests/*.bas, so nothing in tests/refuse reaches
# them - which is the point: these programs have no output to compare.
#
#   MMBC=<mmbc> W=<work dir> bash fcc/refusetests.sh
#
# MMBC defaults to the host build beside this tree's Makefile.host.

M=$(cd "$(dirname "$0")/.." && pwd)
MMBC=${MMBC:-$M/mmbc/mmbc}
W=${W:-/tmp/refusetests}
mkdir -p "$W"
pass=0
fail=0
for src in "$M"/tests/refuse/*.bas; do
	b=$(basename "$src" .bas)
	want=$(sed -n "1s/^' REFUSE: //p" "$src")
	if [ -z "$want" ]; then
		echo "FAIL  $b: no ' REFUSE: line"; fail=$((fail + 1)); continue
	fi
	cp "$src" "$W/$b.bas"
	(cd "$W" && python3 -B "$M/mmb2c.py" "$b.bas" > "$b.py.out" 2>&1); prc=$?
	(cd "$W" && "$MMBC" "$b.bas" > "$b.c.out" 2>&1); crc=$?
	grep '^ERROR' "$W/$b.py.out" > "$W/$b.py.err"
	grep '^ERROR' "$W/$b.c.out" > "$W/$b.c.err"
	why=""
	[ $prc != 0 ] || why="$why mmb2c.py accepted it;"
	[ $crc != 0 ] || why="$why mmbc accepted it;"
	grep -qF -- "$want" "$W/$b.py.err" || why="$why mmb2c.py did not say \"$want\";"
	grep -qF -- "$want" "$W/$b.c.err" || why="$why mmbc did not say \"$want\";"
	cmp -s "$W/$b.py.err" "$W/$b.c.err" || why="$why the two disagree;"
	if [ -z "$why" ]; then
		pass=$((pass + 1))
	else
		echo "FAIL  $b:$why"
		sed 's/^/    py:  /' "$W/$b.py.err" | head -n 3
		sed 's/^/    c:   /' "$W/$b.c.err" | head -n 3
		fail=$((fail + 1))
	fi
done
echo "refusals: $pass refused as they should be, $fail not"
[ $fail = 0 ]
