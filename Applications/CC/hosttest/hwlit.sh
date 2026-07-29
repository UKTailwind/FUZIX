#!/bin/bash
#
# Prepare the on-target version of the literal test.
#
#   bash hwlit.sh [count] [seed]        # writes into ../hwtest/
#
# littest.sh proves the encoder against gcc on the development host,
# where unsigned long is 64 bits. The board is ILP32 with a 32-bit
# unsigned long and its own libc, and the encoder is built entirely out
# of 64-bit arithmetic, so "it matches gcc on the host" does not settle
# what it does on the machine it is for.
#
# The check is that the board's cc0 emits byte for byte the same token
# stream as the host's cc0, which littest.sh has already shown matches
# gcc. So the host produces the reference and the board runs cmp - only
# a pass or fail has to come back over the wire.
#
# Then, from Kernel/platform/platform-rpipico/devtools on the Windows
# side (see the README there):
#
#   make -f Makefile.armm0 FUZIX_ROOT=$PWD/../.. USERCPU=armm0 cc0
#   arm-none-eabi-strip cc0 -o hwtest/cc0.stripped
#   python uusend.py <path>/hwtest/cc0.stripped cc0 0
#   python uusend.py <path>/hwtest/lit.c   lit.c   0
#   python uusend.py <path>/hwtest/lit.ref lit.ref 0
#   python fzsh.py 25 "chmod +x cc0" "./cc0 < lit.c > lit.tok" \
#                     "cmp lit.tok lit.ref && echo IDENTICAL"

CC=$(cd "$(dirname "$0")/.." && pwd)
O=$CC/hwtest
n=${1:-200}
seed=${2:-11}

rm -rf "$O"; mkdir -p "$O" || exit 1

grep -v '^[[:space:]]*#' "$CC/hosttest/samples/literals.txt" |
	grep -v '^[[:space:]]*$' > "$O/lits"

awk -v n="$n" -v seed="$seed" 'BEGIN {
	srand(seed);
	for (i = 0; i < n; i++) {
		nd = int(rand() * 20) + 1;
		s = "";
		for (j = 0; j < nd; j++)
			s = s int(rand() * 10);
		dp = int(rand() * (nd + 1));
		printf "%s.%se%d%s\n", substr(s, 1, dp), substr(s, dp + 1),
			int(rand() * 640) - 330, (rand() < 0.25 ? "f" : "");
	}
}' >> "$O/lits"

# Wide integers too - the same 64-bit accumulation carries them, and
# that is the part most likely to behave differently on a 32-bit machine
cat >> "$O/lits" <<'EOF'
5000000000
18446744073709551615
0xFFFFFFFFFFFFFFFF
1234567890123456789
EOF

sed 's/$/;/' "$O/lits" > "$O/lit.c"
"$CC/host-armm0/cc0" < "$O/lit.c" > "$O/lit.ref" 2> "$O/lit.err"

echo "$(wc -l < "$O/lits") literals -> $O/lit.c, reference $O/lit.ref"
