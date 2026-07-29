#!/bin/bash
#
# Capture the gcc reference output for samples, for hwbuild.py to diff
# what the board produces against. Same flags as optest.sh, which are
# both needed: plain char is unsigned on the target, and the sources
# are C89 where "int printf();" means an unspecified argument list.
#
#   bash mkref.sh struct2 struct3 struct4
#
cd "$(dirname "$0")" || exit 1
S=../hosttest/samples
for n in "$@"; do
	gcc -std=gnu89 -funsigned-char -w -o "/tmp/$n.ref" "$S/$n.c" || exit 1
	"/tmp/$n.ref" > "$n.ref.out" 2>&1
	printf '%-10s %s lines\n' "$n" "$(wc -l < "$n.ref.out")"
done
