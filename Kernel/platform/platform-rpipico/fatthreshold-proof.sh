#!/bin/sh
# Prove the FAT32-detection guard is load-bearing: build a variant with
# the old cluster-count-only test and show it corrupts the very volume
# the new geometry in fattest.sh covers.  A guard whose test cannot fail
# is not evidence of anything.
R=$(cd "$(dirname "$0")/../../.." && pwd)
T=${TMPDIR:-/tmp}/fatproof.$$
mkdir -p "$T"
trap 'rm -rf "$T"' EXIT
export MTOOLS_SKIP_CHECK=1

sed 's/^\tfat32 = (nclus >= 65525) .*$/\tfat32 = (nclus >= 65525);/' \
	"$R/Applications/util/fat.c" > "$T/old.c"
if cmp -s "$T/old.c" "$R/Applications/util/fat.c"; then
	echo "PROOF INCONCLUSIVE: the substitution changed nothing"
	exit 2
fi
gcc -D_GNU_SOURCE -w -O2 -o "$T/fatold" "$T/old.c" || exit 2
gcc -D_GNU_SOURCE -w -O2 -o "$T/fatnew" "$R/Applications/util/fat.c" || exit 2

head -c 5000 /dev/urandom > "$T/src"

for v in old new; do
	mkfs.vfat -F 32 -S 512 -s 8 -C "$T/$v.img" 131072 >/dev/null 2>&1
	"$T/fat$v" -d "$T/$v.img" put "$T/src" probe.dat >/dev/null 2>&1
	printf '%-4s put: ' "$v"
	if fsck.vfat -n "$T/$v.img" >"$T/$v.fsck" 2>&1; then
		if mcopy -i "$T/$v.img" -n ::probe.dat "$T/$v.back" 2>/dev/null &&
		   cmp -s "$T/src" "$T/$v.back"; then
			echo "filesystem clean, file intact"
		else
			echo "fsck clean but the file did not come back"
		fi
	else
		echo "FSCK REJECTS THE FILESYSTEM"
		sed -n '2,6p' "$T/$v.fsck" | sed 's/^/        /'
	fi
done
