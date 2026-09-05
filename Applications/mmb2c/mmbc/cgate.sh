#!/bin/bash
#
# The burn-down gate: generated C from mmbc vs mmb2c.py, plain and
# --fcc, over every tests/*.bas.  The diff line count per test is the
# progress meter; the port is done when every count is 0 and stdout
# matches too.
#
#   bash mmbc/cgate.sh

M=$(cd "$(dirname "$0")/.." && pwd)
W=${W:-/tmp/mmbcgate}
mkdir -p "$W"

# MMBC overrides the translator under test (a build tree elsewhere);
# without it the gate builds and uses this tree's own.
if [ -z "$MMBC" ]; then
	make -s -C "$M/mmbc" || exit 1
	MMBC=$M/mmbc/mmbc
fi

total=0
# samples/ as well as tests/: the samples are the only programs in the
# tree that exercise GUI BITMAP, BEZIER and the ported applications
# (robots, picofrog, vaders, websrv), so a translator change that broke
# a shape only they use had nothing to fail against.  They need a
# screen to RUN, which is why make check cannot run them - but the two
# emitters can still be compared on them, which is what this gate does.
for src in "$M"/tests/*.bas "$M"/samples/*.bas; do
	case $src in
	*/samples/*) b=s_$(basename "$src" .bas) ;;
	*) b=$(basename "$src" .bas) ;;
	esac
	for mode in plain fcc; do
		if [ $mode = fcc ]; then flag=--fcc; else flag=; fi
		python3 "$M/mmb2c.py" $flag "$src" -o "$W/$b.$mode.py.c" \
			> "$W/$b.$mode.py.out" 2>&1
		"$MMBC" $flag "$src" -o "$W/$b.$mode.c.c" \
			> "$W/$b.$mode.c.out" 2>&1
		d=$(diff "$W/$b.$mode.py.c" "$W/$b.$mode.c.c" 2>/dev/null \
			| grep -c '^[<>]')
		# the two runs are handed different -o paths by this very
		# script, so the 'wrote <path>' line differs by construction;
		# normalise the path before comparing stdout
		o=$(diff <(sed "s|$W/$b.$mode.py.c\$|OUT|" \
				"$W/$b.$mode.py.out") \
			 <(sed "s|$W/$b.$mode.c.c\$|OUT|" \
				"$W/$b.$mode.c.out") \
			2>/dev/null | grep -c '^[<>]')
		total=$((total + d + o))
		printf '%-16s %-5s  C diff %5d   stdout diff %3d\n' \
			"$b" "$mode" "$d" "$o"
	done
done
echo "cgate: total diff lines $total (0 = byte-identical suite)"
[ $total = 0 ]
