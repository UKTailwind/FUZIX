#!/bin/bash
# Print the dialect tags of every failing conformance test, so a
# rejection that is really "this is not C89" can be told from a
# rejection that is a gap in our C89.
SUITE=${SUITE:-/home/peter/src/c-testsuite/tests/single-exec}
W=${W:-/tmp/ctest}
while read -r b kind; do
	[ -n "$b" ] || continue
	printf '%s %-6s %s\n' "$b" "$kind" \
		"$(tr '\n' ' ' < "$SUITE/$b.c.tags" 2>/dev/null)"
done < "$W/failures.txt"
