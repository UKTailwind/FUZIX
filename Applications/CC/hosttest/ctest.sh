#!/bin/bash
#
# Run the c-testsuite single-exec conformance tests through our chain.
#
#   bash ctest.sh              # every c89 test
#   bash ctest.sh -a           # every test, whatever the dialect tag
#   bash ctest.sh -v 00042     # one test, showing what each pass said
#
# https://github.com/c-testsuite/c-testsuite - each test is NNNNN.c with
# NNNNN.c.expected holding the expected stdout and NNNNN.c.tags naming
# the C version it needs. A test passes if the program exits 0 and its
# output matches.
#
# We only claim C89, so c99 and c11 tests are reported as out of scope
# rather than as failures. That is a measurement, not an excuse: the
# count is printed either way and -a runs the lot.
#
# Failures are classified by which pass rejected the program, because
# "47 failures" tells you nothing and "31 of them are cc1 rejecting a
# construct" tells you where the work is.

CC=$(cd "$(dirname "$0")/.." && pwd)
SUITE=${SUITE:-/home/peter/src/c-testsuite/tests/single-exec}
W=${W:-/tmp/ctest}
BIN=$CC/host-armm0
INC=$CC/hosttest/ctest-include

all=0
verbose=""
case "$1" in
-a) all=1; shift ;;
-v) verbose=1; shift ;;
esac

[ -d "$SUITE" ] || { echo "no suite at $SUITE" >&2; exit 1; }
rm -rf "$W"; mkdir -p "$W"

pass=0; skip=0
declare -A failkind
fails=""

run_one() {
	local src=$1 b=$2 exp=$3
	local rc

	rm -f "$W/$b".*

	# -nostdinc and our own headers: preprocessing against the host's
	# glibc pulls in GNU extensions no C89 compiler can parse, and every
	# needs-libc test then fails in cc1 for a reason that has nothing to
	# do with the test. See ctest-include/README.md.
	if ! gcc -E -P -nostdinc -I "$INC" -I "$SUITE" "$src" \
			> "$W/$b.pp" 2> "$W/$b.cpp.err"; then
		echo "CPP"; return
	fi
	"$BIN/cc0" < "$W/$b.pp" > "$W/$b.tok" 2> "$W/$b.cc0.err"
	if [ $? != 0 ] || grep -q ' - ' "$W/$b.cc0.err"; then
		echo "CC0"; return
	fi
	rm -f "$W/$b.ir"
	"$BIN/cc1" < "$W/$b.tok" 1<> "$W/$b.ir" 2> "$W/$b.cc1.err"
	if [ $? != 0 ]; then
		echo "CC1"; return
	fi
	rm -f "$W/$b.bc"
	"$BIN/cc2" .symtmp armm0 0 < "$W/$b.ir" 1<> "$W/$b.bc" 2> "$W/$b.cc2.err"
	if [ $? != 0 ] || [ -s "$W/$b.cc2.err" ]; then
		echo "CC2"; return
	fi

	timeout 10 "$BIN/bcrun" "$W/$b.bc" > "$W/$b.out" 2> "$W/$b.run.err"
	rc=$?
	if grep -q 'no runtime function' "$W/$b.run.err"; then
		echo "LIBC"; return
	fi
	if [ $rc != 0 ]; then
		echo "EXIT$rc"; return
	fi
	if ! diff -q "$exp" "$W/$b.out" > /dev/null 2>&1; then
		echo "OUTPUT"; return
	fi
	echo "PASS"
}

cd "$W" || exit 1

for src in "$SUITE"/*.c; do
	b=$(basename "$src" .c)
	tags="$SUITE/$b.c.tags"
	exp="$SUITE/$b.c.expected"
	[ -f "$exp" ] || exp=/dev/null

	if [ -n "$verbose" ] && [ "$verbose" != 1 ]; then :; fi
	if [ "$all" = 0 ] && [ -f "$tags" ] && grep -qE '^c(99|11)$' "$tags"; then
		skip=$((skip + 1)); continue
	fi

	kind=$(run_one "$src" "$b" "$exp")
	if [ "$kind" = PASS ]; then
		pass=$((pass + 1))
	else
		failkind[$kind]=$(( ${failkind[$kind]:-0} + 1 ))
		fails="$fails$b $kind"$'\n'
	fi
done

total=$((pass + $(echo -n "$fails" | grep -c . )))
echo
echo "c-testsuite single-exec, C89 subset"
echo "  ran     $total"
echo "  passed  $pass"
[ "$skip" != 0 ] && echo "  skipped $skip   (tagged c99/c11 - we only claim C89)"
echo
if [ -n "$fails" ]; then
	echo "failures by pass:"
	for k in "${!failkind[@]}"; do
		printf '  %-8s %s\n' "$k" "${failkind[$k]}"
	done | sort -k2 -rn
	echo
	echo "  CPP    the preprocessor rejected it"
	echo "  CC0    tokenizer error"
	echo "  CC1    front end rejected the program - the dialect gaps"
	echo "  CC2    code generator refused"
	echo "  LIBC   needs a runtime function bcrun does not provide"
	echo "  EXITn  ran but exited non-zero"
	echo "  OUTPUT ran and exited 0 but printed the wrong thing"
	echo
	echo "$fails" | sed '/^$/d' | sort -k2 > "$W/failures.txt"
	echo "full list: $W/failures.txt"
fi
