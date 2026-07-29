#!/bin/bash
#
# Differential test for the floating point literal encoder in cc0.
#
#   ./littest.sh [samples/literals.txt]
#   ./littest.sh -r 5000 [seed]     also throw random literals at it
#
# gcc is the oracle. Each literal is compiled by gcc into a double (or
# a float, if it has an f suffix) and the bits are printed; the same
# text is run through cc0 and dumptokens and the bits compared. There
# is no judgement call here - the encoder either produces the same 64
# bits gcc does or it does not.
#
# This is separate from optest.sh because it tests the encoder alone.
# It needs no code generator, so it works before the float opcodes
# exist.

CC=$(cd "$(dirname "$0")/.." && pwd)
W=${W:-/tmp/fcc-littest}
rand=0
seed=1
if [ "$1" = "-r" ]; then
	rand=$2
	[ -n "$3" ] && seed=$3
	shift 3 2>/dev/null || shift $#
fi
list=${1:-$(dirname "$0")/samples/literals.txt}
case "$list" in /*) ;; *) list="$PWD/$list" ;; esac

[ -r "$list" ] || { echo "cannot read $list" >&2; exit 1; }

rm -rf "$W"; mkdir -p "$W" || exit 1

# The literals, comments and blanks stripped
grep -v '^[[:space:]]*#' "$list" | grep -v '^[[:space:]]*$' > "$W/lits"

# Random literals: a random run of digits, a random place to put the
# point, and a random exponent that reaches both ends of the format.
# The interesting failures in a decimal to binary conversion are the
# ones nobody thinks to write down.
if [ "$rand" -gt 0 ] 2>/dev/null; then
	awk -v n="$rand" -v seed="$seed" 'BEGIN {
		srand(seed);
		for (i = 0; i < n; i++) {
			nd = int(rand() * 20) + 1;
			s = "";
			for (j = 0; j < nd; j++)
				s = s int(rand() * 10);
			dp = int(rand() * (nd + 1));
			lit = substr(s, 1, dp) "." substr(s, dp + 1);
			ex = int(rand() * 640) - 330;
			printf "%se%d%s\n", lit, ex, (rand() < 0.25 ? "f" : "");
		}
	}' >> "$W/lits"
fi
n=$(wc -l < "$W/lits")

# --- reference: what gcc makes of the same text ---------------------
{
	echo '#include <stdio.h>'
	echo 'int main(void) {'
	echo '  union { double d; unsigned long long u; } v;'
	echo '  union { float f; unsigned long u; } w;'
	while read -r lit; do
		case "$lit" in
		*[fF])	echo "  w.f = $lit; printf(\"%08lx\\n\", w.u);" ;;
		*)	echo "  v.d = $lit; printf(\"%016llx\\n\", v.u);" ;;
		esac
	done < "$W/lits"
	echo '  return 0; }'
} > "$W/oracle.c"

if ! gcc -w -o "$W/oracle" "$W/oracle.c" 2> "$W/gcc.err"; then
	echo "gcc could not build the oracle:"; sed 's/^/  /' "$W/gcc.err"; exit 1
fi
"$W/oracle" > "$W/ref"

# --- ours: cc0, then read the constants back out of the token stream -
# Semicolons so the tokenizer sees a sequence of constants and not one
# long number.
sed 's/$/;/' "$W/lits" > "$W/lits.c"
"$CC/host-armm0/cc0" < "$W/lits.c" > "$W/tok" 2> "$W/cc0.err"
"$CC/host-armm0/dumptokens" < "$W/tok" > "$W/dump" 2>&1
grep -E '^(float|double) ' "$W/dump" | awk '{print $2}' > "$W/ours"

# --- compare --------------------------------------------------------
fail=0
i=0
while read -r lit; do
	i=$((i + 1))
	want=$(sed -n "${i}p" "$W/ref")
	got=$(sed -n "${i}p" "$W/ours")
	if [ "$want" != "$got" ]; then
		fail=$((fail + 1))
		printf '  %-34s gcc %s  ours %s\n' "$lit" "$want" "${got:-<missing>}"
	fi
done < "$W/lits"

if [ "$fail" = 0 ]; then
	echo "PASS  $n literals encode exactly as gcc does"
else
	echo "FAIL  $fail of $n literals differ (above)"
fi

# The random literals deliberately overshoot both ends of the format,
# so range warnings are expected and only the count is interesting. A
# warning on a literal that still matched gcc is not a fault: gcc warns
# about those too, it just does not say so with -w.
if [ -s "$W/cc0.err" ]; then
	echo "      $(grep -c . "$W/cc0.err") range warnings from cc0"
fi

exit $((fail != 0))
