#!/bin/bash
CC=$(cd "$(dirname "$0")/../.." && pwd)
for f in dhry-base dhry-r4 dhry-cf dhry-nos dhry-new se-base se-new; do
	echo -n "$f: "
	"$CC/host-armm0/bcdump" /tmp/ccperf-board/$f.bc 2>/dev/null |
		grep -o 'version [0-9]'
done
