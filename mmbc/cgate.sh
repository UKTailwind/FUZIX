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

make -s -C "$M/mmbc" || exit 1

total=0
for src in "$M"/tests/*.bas; do
	b=$(basename "$src" .bas)
	for mode in plain fcc; do
		if [ $mode = fcc ]; then flag=--fcc; else flag=; fi
		python3 "$M/mmb2c.py" $flag "$src" -o "$W/$b.$mode.py.c" \
			> "$W/$b.$mode.py.out" 2>&1
		"$M/mmbc/mmbc" $flag "$src" -o "$W/$b.$mode.c.c" \
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
