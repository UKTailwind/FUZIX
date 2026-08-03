#!/bin/sh
# Compare a board transcript captured over serial against the gcc
# references.  Serial capture carries CRs and the command echo, so trim
# to the marked span and strip them before diffing.
#   sh cmpboard.sh /tmp/boardout.txt
raw=$1
# The capture arrives double spaced: python writes \r\n, PowerShell's
# Out-File turns that into \r\r\n, and stripping the CRs leaves a blank
# line after every real one.  Keep the odd lines rather than squeezing
# blanks, so a genuinely blank line in a sample's output survives.
sed -n '/^=== autoinit/,/^=== END/p' "$raw" | tr -d '\r' \
	| awk 'NR % 2 == 1' > /tmp/board.clean
diff /tmp/hostrefs.txt /tmp/board.clean > /tmp/board.diff 2>&1
if [ -s /tmp/board.diff ]; then
	echo "DIFFERENCES:"
	cat /tmp/board.diff
else
	echo "IDENTICAL to the gcc references ($(wc -l < /tmp/board.clean) lines)"
fi
