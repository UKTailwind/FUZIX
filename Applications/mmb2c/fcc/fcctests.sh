#!/bin/bash
#
# Run every tests/*.bas through mmb2c --fcc -> FCC -> bcrun and compare
# with the .expected output, exactly as `make run` does with gcc.
#
#   bash fcc/fcctests.sh

M=$(cd "$(dirname "$0")/.." && pwd)
FCC=${FCC:-$(cd "$(dirname "$0")/../../CC" && pwd)}
BIN=$FCC/host-armm0
W=${W:-/tmp/fccbuild}
mkdir -p "$W"

pass=0; fail=0
for src in "$M"/tests/*.bas; do
	b=$(basename "$src" .bas)
	exp=$M/tests/$b.expected
	inp=$M/tests/$b.in
	[ -f "$exp" ] || continue

	if ! bash "$M/fcc/fccbuild.sh" "$src" > "$W/$b.build.log" 2>&1; then
		echo "FAIL  $b (build)"; tail -3 "$W/$b.build.log"
		fail=$((fail + 1)); continue
	fi
	# A test may be about something the runtime must REFUSE, and
	# mm_error prints and exits(1) - so "ran and stopped" is the pass
	# for it, and its message is part of the expected output.  Those
	# run with stderr merged; without a tests/<name>.rc it is 0 and
	# the two streams stay apart, as every test used to be.
	exprc=0
	[ -f "$M/tests/$b.rc" ] && exprc=$(cat "$M/tests/$b.rc")
	if [ -f "$inp" ]; then
		timeout 120 "$BIN/bcrun" "$W/$b.bc" < "$inp" > "$W/$b.out" 2> "$W/$b.err"
	else
		timeout 120 "$BIN/bcrun" "$W/$b.bc" > "$W/$b.out" 2> "$W/$b.err"
	fi
	rc=$?
	[ "$exprc" != 0 ] && cat "$W/$b.err" >> "$W/$b.out"
	if [ $rc != "$exprc" ]; then
		echo "FAIL  $b (exit $rc, expected $exprc)"; tail -2 "$W/$b.err"
		fail=$((fail + 1)); continue
	fi
	# elapsed-time lines can never match a canned file; the gcc
	# harness filters them the same way
	grep -v -e 'Time taken' "$W/$b.out" > "$W/$b.out.f"
	grep -v -e 'Time taken' "$exp" > "$W/$b.exp.f"
	if diff -q "$W/$b.exp.f" "$W/$b.out.f" > /dev/null; then
		echo "pass  $b"
		pass=$((pass + 1))
	else
		echo "FAIL  $b (output)   diff $exp $W/$b.out"
		diff "$W/$b.exp.f" "$W/$b.out.f" | head -6
		fail=$((fail + 1))
	fi
done
echo
echo "fcc pipeline: $pass passed, $fail failed"
