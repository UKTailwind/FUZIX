#!/bin/bash
#
# Native-execution gate for MMBasic-shaped code.
#
#   bash fcc/qemutests.sh              the whole test set
#   bash fcc/qemutests.sh t1           one program
#
# fcctests.sh proves the BYTECODE path only: the x86 bcrun never
# executes a byte of the translated Thumb, so a broken peephole sails
# through it.  This is its native twin.  Every tests/*.bas is REBUILT
# with the current cc2 and run under qemu-arm, three ways:
#
#   native      the mixed object, native spans executing
#   bytecode    the same object under BCRUN_BYTECODE=1 (the referee -
#               same object, interpreter only, so a disagreement is
#               the translator's and nothing else's)
#   reclaimed   a THUMB_RECLAIM=1 build: dead bytecode overwritten,
#               no alias to fall back to, board-shaped
#
# all compared against tests/*.expected.
#
# Why it exists: the 10b statement-seam rewrite shipped wrong code
# because no gate ran mmb-generated shapes natively, and the harness
# that came closest (hosttest/qemudiff.sh) compared objects built
# before the change.  So: nothing here is reused.  The compilers are
# rebuilt, then every object is rebuilt, then they run.  Set
# SKIPBUILD=1 only when iterating on the runtime, never to gate a
# backend change.
#
# MMB2C=... overrides the translator (mmbc/mmbc) exactly as in
# fcctests.sh; THUMB_* knobs pass through to cc2.

M=$(cd "$(dirname "$0")/.." && pwd)
FCC=${FCC:-/home/peter/src/FUZIX/Applications/CC}
BIN=$FCC/host-armm0
QBC="qemu-arm $FCC/qemu-armm0/bcrun"
W=${W:-/tmp/fccqemu}
WR=$W/reclaim
mkdir -p "$W" "$WR"

if [ "${SKIPBUILD:-0}" != 1 ]; then
	echo "== rebuilding compilers (host-armm0, qemu-armm0)"
	make -s -C "$FCC" -f Makefile.host > "$W/make.host.log" 2>&1 || {
		echo "FAIL  make -f Makefile.host"; tail -20 "$W/make.host.log"; exit 1; }
	make -s -C "$FCC" -f Makefile.qemu > "$W/make.qemu.log" 2>&1 || {
		echo "FAIL  make -f Makefile.qemu"; tail -20 "$W/make.qemu.log"; exit 1; }
fi
[ -x "$FCC/qemu-armm0/bcrun" ] || { echo "no qemu-armm0/bcrun"; exit 1; }

# run $1=bc $2=input-or-empty $3=logfile ; env prefix in $RUNENV
run_one() {
	if [ -n "$2" ] && [ -f "$2" ]; then
		env $RUNENV timeout 600 $QBC "$1" < "$2" > "$3" 2>&1
	else
		env $RUNENV timeout 600 $QBC "$1" > "$3" 2>&1
	fi
}

# $1 = raw output, $2 = filtered copy (never beside the source tree)
filt() { grep -v -e 'Time taken' -e ' ms ' "$1" > "$2"; }

pass=0; fail=0
for src in "$M"/tests/*.bas; do
	b=$(basename "$src" .bas)
	exp=$M/tests/$b.expected
	inp=$M/tests/$b.in
	[ -f "$exp" ] || continue
	[ -n "$1" ] && [ "$1" != "$b" ] && continue

	# ---- rebuild, plain
	if ! W=$W bash "$M/fcc/fccbuild.sh" "$src" > "$W/$b.build.log" 2>&1; then
		echo "FAIL  $b (build)"; tail -3 "$W/$b.build.log"
		fail=$((fail + 1)); continue
	fi
	# ---- rebuild, reclaimed
	if ! W=$WR THUMB_RECLAIM=1 bash "$M/fcc/fccbuild.sh" "$src" \
			> "$WR/$b.build.log" 2>&1; then
		echo "FAIL  $b (reclaim build)"; tail -3 "$WR/$b.build.log"
		fail=$((fail + 1)); continue
	fi

	ref=$W/$b.expected.f
	filt "$exp" "$ref"

	bad=
	RUNENV= run_one "$W/$b.bc" "$inp" "$W/$b.nat"
	[ $? = 0 ] || bad="$bad native-exit"
	RUNENV=BCRUN_BYTECODE=1 run_one "$W/$b.bc" "$inp" "$W/$b.bcode"
	[ $? = 0 ] || bad="$bad bytecode-exit"
	RUNENV= run_one "$WR/$b.bc" "$inp" "$W/$b.rec"
	[ $? = 0 ] || bad="$bad reclaim-exit"

	for w in nat bcode rec; do
		filt "$W/$b.$w" "$W/$b.$w.f"
		cmp -s "$ref" "$W/$b.$w.f" || bad="$bad $w"
	done

	if [ -z "$bad" ]; then
		n=$(python3 "$FCC/hosttest/nat/natlist.py" "$W/$b.bc" 2>/dev/null | tail -1)
		echo "pass  $b   ($n)"
		pass=$((pass + 1))
	else
		echo "FAIL  $b  [$bad ]"
		for w in nat bcode rec; do
			cmp -s "$ref" "$W/$b.$w.f" || {
				echo "  --- $w"; diff "$ref" "$W/$b.$w.f" | head -4; }
		done
		fail=$((fail + 1))
	fi
done

# ---- Dhrystone: not an MMBasic shape, but the other subject of record,
# and the only one here with struct copies and library string calls.
# No canned output - the three ways referee each other, and gcc's own
# run supplies the final-values block.
if [ -z "$1" ] || [ "$1" = dhry ]; then
	D=$FCC/hosttest/dhry
	sed '/#include "dhry.h"/d' "$D/dhry_2.c" > "$W/dhry_2_body.c"
	cat "$D/dhry_1.c" "$W/dhry_2_body.c" > "$W/dhry_one.c"
	gcc -E -P -nostdinc -DTIME_US -DDHRY_RUNS=20000 \
		-I "$FCC/hosttest/ctest-include" -I "$D" "$W/dhry_one.c" > "$W/dhry.pp"
	"$BIN/cc0" < "$W/dhry.pp" > "$W/dhry.tok" 2> "$W/dhry.cc0.err"
	rm -f "$W/dhry.ir"
	"$BIN/cc1" < "$W/dhry.tok" 1<> "$W/dhry.ir" 2> "$W/dhry.cc1.err"
	rm -f "$W/dhry.bc" "$WR/dhry.bc"
	"$BIN/cc2" .symtmp armm0 0 < "$W/dhry.ir" 1<> "$W/dhry.bc" 2> "$W/dhry.cc2.err"
	THUMB_RECLAIM=1 "$BIN/cc2" .symtmp armm0 0 < "$W/dhry.ir" 1<> "$WR/dhry.bc" \
		2> "$WR/dhry.cc2.err"
	gcc -O2 -w -std=gnu89 -DTIME_US -DDHRY_RUNS=20000 -o "$W/dhry.native" \
		"$D/dhry_1.c" "$D/dhry_2.c" "$D/dhry_shim.c" 2> "$W/dhry.gcc.err"
	"$W/dhry.native" > "$W/dhry.ref" 2>&1
	RUNENV= run_one "$W/dhry.bc" "" "$W/dhry.nat"
	RUNENV=BCRUN_BYTECODE=1 run_one "$W/dhry.bc" "" "$W/dhry.bcode"
	RUNENV= run_one "$WR/dhry.bc" "" "$W/dhry.rec"
	# keep only the final-values block: everything above it is timing,
	# and Ptr_Comp is a raw pointer - a VM offset here, a host address
	# there - which Dhrystone itself calls implementation-dependent
	for w in ref nat bcode rec; do
		sed -n '/Final values/,$p' "$W/dhry.$w" | grep -v 'Ptr_Comp: ' \
			> "$W/dhry.$w.f"
	done
	bad=
	[ -s "$W/dhry.ref.f" ] || bad="$bad no-reference"
	for w in nat bcode rec; do
		cmp -s "$W/dhry.ref.f" "$W/dhry.$w.f" || bad="$bad $w"
	done
	if [ -z "$bad" ]; then
		n=$(python3 "$FCC/hosttest/nat/natlist.py" "$W/dhry.bc" 2>/dev/null | tail -1)
		echo "pass  dhry   ($n)"
		pass=$((pass + 1))
	else
		echo "FAIL  dhry  [$bad ]"
		for w in nat bcode rec; do
			cmp -s "$W/dhry.ref.f" "$W/dhry.$w.f" || {
				echo "  --- $w"; diff "$W/dhry.ref.f" "$W/dhry.$w.f" | head -4; }
		done
		fail=$((fail + 1))
	fi
fi

echo
echo "qemu native sweep: $pass passed, $fail failed"
[ $fail = 0 ]
