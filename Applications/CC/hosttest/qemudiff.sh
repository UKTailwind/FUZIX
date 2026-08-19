#!/bin/bash
# Stage 1 harness: the same .bc through the host bcrun and the
# Linux-ARM bcrun under qemu-arm, stdout byte-compared.  Lines
# containing "Time taken" or "ms " are timing and excluded.
#
#   bash qemudiff.sh prog.bc [< input]     one program
#   bash qemudiff.sh                       the standard set

CC=$(cd "$(dirname "$0")/.." && pwd)
HOST=$CC/host-armm0/bcrun
QEMU="qemu-arm $CC/qemu-armm0/bcrun"

one() {
	bc=$1; inp=$2
	if [ -n "$inp" ] && [ -f "$inp" ]; then
		"$HOST" "$bc" < "$inp" > /tmp/qd.host 2>&1
		$QEMU  "$bc" < "$inp" > /tmp/qd.qemu 2>&1
	else
		"$HOST" "$bc" > /tmp/qd.host 2>&1
		$QEMU  "$bc" > /tmp/qd.qemu 2>&1
	fi
	grep -v -e 'Time taken' -e ' ms ' /tmp/qd.host > /tmp/qd.host.f
	grep -v -e 'Time taken' -e ' ms ' /tmp/qd.qemu > /tmp/qd.qemu.f
	if diff -q /tmp/qd.host.f /tmp/qd.qemu.f > /dev/null; then
		echo "pass  $(basename "$bc")"
	else
		echo "FAIL  $(basename "$bc")"
		diff /tmp/qd.host.f /tmp/qd.qemu.f | head -6
		return 1
	fi
}

if [ -n "$1" ]; then
	one "$1" "$2"
	exit $?
fi

fail=0
for bc in /tmp/fccbuild/t[0-9].bc; do
	one "$bc" || fail=1
done
one /tmp/fccbuild/solar_eclipse.bc "$(cd "$(dirname "$0")/../../mmb2c" && pwd)"/tests/solar_eclipse.in || fail=1
one /tmp/ccbench/bench.bc || fail=1
exit $fail
