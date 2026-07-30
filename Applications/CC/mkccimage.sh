#!/bin/bash
#
# Build an SD card image with the on-target compiler installed.
#
#   bash mkccimage.sh            -> Images/rpipico/pc3-sd-cc.img(.gz)
#
# Cross build the passes first:
#
#   make -f Makefile.armm0 FUZIX_ROOT=$PWD/../.. USERCPU=armm0 \
#        cc0 cc1 cc2 ccbc bcrun bcdump
#   (cd ../cpp && make -f Makefile.armm0 FUZIX_ROOT=... USERCPU=armm0)
#   then strip each into hwtest/<name>.s
#
# Partition 2 of the image is the Fuzix root: sector 133120, 65536
# sectors of 512 bytes. It is extracted, written into with ucp, fsck'd
# and put back.
#
# Two things about ucp that will otherwise waste an afternoon: "get"
# creates the destination with basename(), i.e. in ucp's own current
# directory, so this drives one session with cd and bare names rather
# than one invocation per file with a full path; and ucp exits with the
# status of its last command, so "set -e" will stop the script on a
# perfectly successful run.

R=$(cd "$(dirname "$0")/../.." && pwd)
CC=$R/Applications/CC
SRC=$R/Images/rpipico/pc3-sd.img
OUT=$R/Images/rpipico/pc3-sd-cc.img
W=${W:-/tmp/mkccimage}
P2=$W/p2.img

START=133120
COUNT=65536

set -e
[ -r "$SRC" ] || { echo "no base image at $SRC" >&2; exit 1; }
for f in cc0 cc1 cc2 ccbc bcrun bcdump cpp; do
	[ -r "$CC/hwtest/$f.s" ] || {
		echo "missing $CC/hwtest/$f.s - cross build and strip first" >&2
		exit 1; }
done

rm -rf "$W"; mkdir -p "$W"

echo "--- copying base image"
cp "$SRC" "$OUT"
dd if="$OUT" of="$P2" bs=512 skip=$START count=$COUNT status=none

echo "--- baseline fsck"
"$R/Standalone/fsck" -a "$P2" || true

echo "--- installing"
{
	echo "cd /usr/lib"
	echo "mkdir cc"
	echo "cd cc"
	for f in cc0 cc1 cc2; do
		echo "bget $CC/hwtest/$f.s $f"
		echo "chmod 755 $f"
	done
	# The headers cc looks for: ccbc.c passes -I<libpath>include, and
	# cpp has no built-in search path at all, so without these
	# "#include <stdio.h>" fails. These describe what bcrun provides -
	# deliberately not /usr/include, which describes the Fuzix C
	# library that native binaries link against.
	echo "mkdir include"
	echo "cd include"
	for f in "$CC"/hosttest/ctest-include/*.h; do
		echo "get $f $(basename "$f")"
	done
	echo "cd /usr/bin"
	echo "bget $CC/hwtest/ccbc.s cc"
	echo "chmod 755 cc"
	for f in cpp bcrun bcdump; do
		echo "bget $CC/hwtest/$f.s $f"
		echo "chmod 755 $f"
	done
	echo "cd /root"
	echo "mkdir cc"
	echo "cd cc"
	for f in "$CC"/hosttest/samples/*.c; do
		echo "get $f $(basename "$f")"
	done
	echo "df"
	echo "exit"
} > "$W/cmds"

# Not "set -e" territory: see the note above about ucp's exit status
set +e
"$R/Standalone/ucp" "$P2" < "$W/cmds"
set -e

echo "--- fsck after"
"$R/Standalone/fsck" -a "$P2"

echo "--- writing the partition back and verifying"
dd if="$P2" of="$OUT" bs=512 seek=$START count=$COUNT conv=notrunc status=none
dd if="$OUT" of="$W/verify.img" bs=512 skip=$START count=$COUNT status=none
cmp "$P2" "$W/verify.img" || { echo "write back mismatch" >&2; exit 1; }
"$R/Standalone/fsck" -a "$W/verify.img"

gzip -c "$OUT" > "$OUT.gz"
ls -l "$OUT" "$OUT.gz"
echo "done"
