#!/bin/sh
# A fault address from the board resolves against whichever binary you
# pick, because every Fuzix process loads at the same PROGLOAD.  So ask
# all the candidates and let the plausible one identify itself.
#   sh whichbin.sh 0x4e48
cd "$(dirname "$0")/.." || exit 1
off=$1
for b in bcrun cc0 cc1 cc2 mmbc ccbc bcdump; do
	[ -f "$b" ] || continue
	printf '%-8s ' "$b"
	arm-none-eabi-addr2line -f -C -e "$b" "$off" 2>/dev/null | tr '\n' ' '
	echo
done
