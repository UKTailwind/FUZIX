#!/bin/sh
#
# Gate for THIS preprocessor.
#
#   sh cpptest.sh
#
# Nothing else tests it: every host-side gate in the tree preprocesses
# with "gcc -E" (Applications/CC/hosttest/ctest.sh, mmb2c fccbuild.sh),
# so cpp is only reached on the board, through ccbc.  Three C89 gaps
# lived here until the c-testsuite was finally run on hardware.
#
# Builds cpp natively and preprocesses pptest.c, in which every failure
# is an #error naming itself.  Silence is a pass.
#
cd "$(dirname "$0")" || exit 1
W=${W:-/tmp/cpptest}
mkdir -p "$W"

if ! gcc -w -O1 -I. -o "$W/cpp" cpp.c hash.c main.c token1.c token2.c \
		2> "$W/build.err"; then
	echo "FAIL: cpp does not build"
	head -20 "$W/build.err"
	exit 1
fi

if "$W/cpp" -E pptest.c -o "$W/pptest.i" > "$W/run.err" 2>&1; then
	echo "PASS: pptest.c preprocesses clean"
else
	echo "FAIL:"
	cat "$W/run.err"
	exit 1
fi

# and the output has to be usable, not just error free: the directive
# lines must vanish rather than being passed through as text, which is
# how a mis-parsed #line first showed itself
if grep -q '^[	 ]*[0-9][0-9]*[	 ]*$' "$W/pptest.i"; then
	echo "FAIL: a directive line was echoed into the output"
	grep -n '^[	 ]*[0-9][0-9]*[	 ]*$' "$W/pptest.i" | head -5
	exit 1
fi
echo "PASS: no directive text left in the output"
