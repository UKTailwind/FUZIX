#!/bin/bash
# For each failing conformance test, print what cc1 said and the line of
# preprocessed source it said it about. That pair is usually enough to
# name the missing feature without opening the test.
W=${W:-/tmp/ctest}
cd "$W" || exit 1
for b in $(cut -d' ' -f1 failures.txt); do
	kind=$(grep "^$b " failures.txt | cut -d' ' -f2)
	printf '===== %s  %s\n' "$b" "$kind"
	if [ -s "$b.cc1.err" ]; then
		grep ' - ' "$b.cc1.err" | head -3 | while read -r l; do
			ln=$(echo "$l" | sed -E 's/^[^:]*:([0-9]+) .*/\1/')
			msg=$(echo "$l" | sed 's/^.* - //')
			printf '  cc1: %-32s line %s: %s\n' "$msg" "$ln" \
				"$(sed -n "${ln}p" "$b.pp" | cut -c1-90)"
		done
	fi
	[ -s "$b.cc2.err" ] && printf '  cc2: %s\n' "$(head -1 "$b.cc2.err")"
	[ -s "$b.run.err" ] && printf '  run: %s\n' "$(head -1 "$b.run.err")"
done
