#!/bin/sh
#
# keywords.c is GENERATED, from MMBasic's AllCommands.h and mmb2c's own
# dispatch - and being generated is worth nothing if nobody runs the
# generator.  WS2812 and Bitstream shipped on 2026-08-22 and the editor
# went on painting them "the interpreter knows this and mmbc does not"
# until this was noticed a day later, because regenerating was a step
# someone had to remember.
#
#   sh kwcheck.sh
#
# Exits non-zero if keywords.c is not what genkw.py produces today, and
# prints the difference so the answer is obvious: either regenerate, or
# find out why the generator disagrees.

set -e
cd "$(dirname "$0")"

python3 genkw.py > /tmp/kwcheck.$$.c 2>/tmp/kwcheck.$$.err || {
	echo "kwcheck: genkw.py failed" >&2
	cat /tmp/kwcheck.$$.err >&2
	rm -f /tmp/kwcheck.$$.c /tmp/kwcheck.$$.err
	exit 1
}
cat /tmp/kwcheck.$$.err

if diff -q keywords.c /tmp/kwcheck.$$.c > /dev/null; then
	echo "kwcheck: keywords.c is current"
	rm -f /tmp/kwcheck.$$.c /tmp/kwcheck.$$.err
	exit 0
fi

echo "kwcheck: keywords.c is STALE - run 'python3 genkw.py > keywords.c'" >&2
diff keywords.c /tmp/kwcheck.$$.c >&2 || true
rm -f /tmp/kwcheck.$$.c /tmp/kwcheck.$$.err
exit 1
