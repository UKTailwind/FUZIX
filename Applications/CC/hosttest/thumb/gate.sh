#!/bin/bash
# The Thumb emitter's checkpoint gate: build a probe four ways and
# demand agreement - gcc -O2 native, host bcrun (interprets via the
# bytecode alias), qemu bcrun (native path), qemu with BCRUN_BYTECODE=1
# (forced interpretation of the same mixed object).
#
#   bash gate.sh cpd.c          one probe
#   bash gate.sh                all checkpoints
CC=$(cd "$(dirname "$0")/../.." && pwd)
BIN=$CC/host-armm0
INC=$CC/hosttest/ctest-include
D=$(cd "$(dirname "$0")" && pwd)
W=/tmp/thumbgate
mkdir -p "$W"

one() {
	src=$1
	b=$(basename "$src" .c)
	cd "$W" || exit 1
	gcc -E -P -nostdinc -I "$INC" "$D/$src" > p.pp || return 1
	"$BIN/cc0" < p.pp > p.tok 2>/dev/null || return 1
	rm -f p.ir; "$BIN/cc1" < p.tok 1<> p.ir 2>/dev/null || return 1
	rm -f "$b.bc"
	"$BIN/cc2" .symtmp armm0 0 < p.ir 1<> "$b.bc" 2> cc2.err
	[ -s cc2.err ] && { cat cc2.err; return 1; }
	gcc -O2 -w -std=gnu89 -o "$b.native" "$D/$src" || return 1
	./"$b.native" > out.native
	"$BIN/bcrun" "$b.bc" > out.host
	qemu-arm "$CC/qemu-armm0/bcrun" "$b.bc" > out.qemu
	BCRUN_BYTECODE=1 qemu-arm "$CC/qemu-armm0/bcrun" "$b.bc" > out.qbc
	if cmp -s out.native out.host && cmp -s out.native out.qemu \
	   && cmp -s out.native out.qbc; then
		n=$(python3 "$CC/hosttest/nat/natlist.py" "$b.bc" | tail -1)
		echo "pass  $b   ($n)"
	else
		echo "FAIL  $b"
		diff out.native out.qemu | head -4
		return 1
	fi
}

if [ -n "$1" ]; then
	one "$1"; exit $?
fi
fail=0
for p in cpa.c cpb.c cpc.c cpd.c cpe.c cpf.c cpg.c cph.c; do
	one "$p" || fail=1
done
exit $fail
