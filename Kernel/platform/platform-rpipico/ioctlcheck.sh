#!/bin/sh
#
# Every ioctl code in pico_ioctl.h lives in ONE flat number space:
# plt_dev_ioctl dispatches all of them from a single if-chain, so the
# GFXIOC_/PICOIOC_/SNDIOC_/PSRAMIOC_/PLKIOC_ prefixes are naming, not
# namespacing.  Two names on one number means the earlier test in the
# chain wins and the later one is dead code.
#
# That is not hypothetical: PICOIOC_BOARD was allocated 0x0021, which
# SNDIOC_PCMOPEN already had.  PICOIOC_BOARD is tested first, so every
# PCM open returned success after writing a 2 or a 3 over the caller's
# sample rate, and PLAY MP3 played silence with no error reported.
#
#	sh ioctlcheck.sh		 -> the allocation table, or a duplicate
#
# Run from the gates.  Exit 1 on any duplicate.

H=$(dirname "$0")/pico_ioctl.h
[ -r "$H" ] || { echo "no pico_ioctl.h beside this script" >&2; exit 1; }

# name and value of every ioctl-ish define, sorted by value
list=$(sed -n 's/^#define[ \t]*\([A-Z0-9_]*IOC[A-Z0-9_]*\)[ \t]*\(0x[0-9A-Fa-f]*\).*/\2 \1/p' \
	"$H" | sort)

dups=$(echo "$list" | awk '{print $1}' | uniq -d)
if [ -n "$dups" ]; then
	echo "DUPLICATE ioctl codes in pico_ioctl.h:" >&2
	for d in $dups; do
		echo "$list" | awk -v v="$d" '$1 == v { print "  " $1 "  " $2 }' >&2
	done
	exit 1
fi

echo "$list" | awk '{ printf "  %s  %s\n", $1, $2 }'
n=$(echo "$list" | wc -l)
echo "$n codes, no duplicates"
