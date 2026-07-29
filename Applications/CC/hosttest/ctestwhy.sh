#!/bin/bash
# Summarise why cc1 rejected the c-testsuite failures: first error per
# test, counted. This is the list that says which dialect gaps to close
# and in what order.
W=${W:-/tmp/ctest}
cd "$W" || exit 1
for b in $(grep ' CC1$' failures.txt | cut -d' ' -f1); do
	grep ' - ' "$b.cc1.err" 2>/dev/null | head -1 | sed 's/^.* - //'
done | sort | uniq -c | sort -rn
echo
echo "--- tests per message"
for b in $(grep ' CC1$' failures.txt | cut -d' ' -f1); do
	m=$(grep ' - ' "$b.cc1.err" 2>/dev/null | head -1 | sed 's/^.* - //')
	echo "$m|$b"
done | sort | awk -F'|' '{ if ($1!=p) { if (p!="") printf "\n"; printf "%-28s", $1; p=$1 } printf " %s", $2 } END { printf "\n" }'
