#!/bin/bash
#
# Which failing tests are actually C89?
#
# The suite's own tags are not reliable for this - several tests tagged
# c89 use VLAs, statement expressions, __attribute__ or _Generic. So ask
# gcc in strict mode instead: -std=c89 -pedantic-errors. If gcc refuses
# it too then it is not C89 and our refusing it is correct behaviour,
# not a gap. If gcc accepts it, it is our gap and it is on the list.
#
SUITE=${SUITE:-/home/peter/src/c-testsuite/tests/single-exec}
W=${W:-/tmp/ctest}
INC=$(cd "$(dirname "$0")" && pwd)/ctest-include

ours=0; theirs=0
while read -r b kind; do
	[ -n "$b" ] || continue
	# No -w here: it inhibits the pedantic diagnostics before
	# -pedantic-errors can promote them, and everything then looks
	# like valid C89.
	if gcc -std=c89 -pedantic-errors -fsyntax-only \
	       -I "$SUITE" "$SUITE/$b.c" > /dev/null 2>&1; then
		printf '%s %-6s C89   OUR GAP\n' "$b" "$kind"
		ours=$((ours + 1))
	else
		why=$(gcc -std=c89 -pedantic-errors -fsyntax-only \
			-I "$SUITE" "$SUITE/$b.c" 2>&1 |
			grep -m1 -oE 'ISO C90 (does not support|forbids)[^;]*')
		printf '%s %-6s not C89   %s\n' "$b" "$kind" "${why:-see gcc}"
		theirs=$((theirs + 1))
	fi
done < "$W/failures.txt"
echo
echo "our gaps: $ours    not C89 at all: $theirs"
