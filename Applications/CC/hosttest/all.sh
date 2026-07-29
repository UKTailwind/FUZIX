#!/bin/bash
#
# Run every sample through optest.sh, plus the literal encoder test.
# One line per sample, so a regression is obvious at a glance.
#
# Three samples cannot use gcc as an oracle and say so rather than
# being quietly dropped:
#
#   bench  calls time_us/adval, which only exist on the board
#   min3   contains a deliberately unterminated string
#   ll     prints sizeof(long), which is 4 for us and 8 on an x86-64
#          host, so the oracle disagrees by construction
#
# Everything else must PASS.

cd "$(dirname "$0")" || exit 1
fail=0
for f in samples/*.c; do
	b=$(basename "$f" .c)
	printf '%-14s ' "$b"
	case "$b" in
	bench|min3)
		echo "n/a   not an oracle test"
		continue ;;
	ll)
		echo "n/a   host sizeof(long) is 8, ours is 4"
		continue ;;
	dbl)
		# Delete this case as the first act of step 3
		echo "todo  waiting on the float and double opcodes"
		continue ;;
	esac
	out=$(W=/tmp/fcc-all bash optest.sh "$f" 2>&1)
	line=$(echo "$out" | grep -E '^(PASS|FAIL)' | head -1)
	echo "${line:-NO RESULT}"
	case "$line" in PASS*) ;; *) fail=$((fail + 1)) ;; esac
done
printf '%-14s ' literals
# Not piped into head: that closes the pipe early and littest.sh dies
# of SIGPIPE, which then looks like a failure
out=$(bash littest.sh); rc=$?
echo "$out" | sed -n 1p
[ "$rc" = 0 ] || fail=$((fail + 1))
exit $((fail != 0))
