#!/bin/sh
# Map a user-mode fault offset onto the candidate binaries.
# usage: faultmap.sh 0xa3d6 0xa45c
cd /home/peter/src/FUZIX/Applications/CC || exit 1
for b in bcrun mmbc cc2 cc1 ccbc cc0; do
	echo "== $b =="
	arm-none-eabi-addr2line -f -e "$b" "$@"
done
