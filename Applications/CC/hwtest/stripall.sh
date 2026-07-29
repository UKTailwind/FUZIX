#!/bin/bash
# Strip the cross built passes ready for the serial transfer. Unstripped
# they are 75-190K and the only way onto the board is the console.
cd "$(dirname "$0")/.." || exit 1
for f in cc0 cc1 cc2 bcrun bcdump ccbc; do
	[ -f "$f" ] || continue
	arm-none-eabi-strip "$f" -o "hwtest/$f.stripped" || exit 1
done
ls -l hwtest/*.stripped
