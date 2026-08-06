#!/bin/sh
#
# Build the SD card's Fuzix root filesystem, and the card image around it.
#
#   sh mksdimage.sh          -> Images/rpipico/pc3-sd.img
#   then: bash ../../../Applications/CC/mkccimage.sh   (adds the compiler)
#
# WHY THIS EXISTS: the root filesystem had no build recipe anywhere in
# the tree.  "diskimage:" in the Makefile is an empty rule, and
# PC3-DEVNOTES.md only says that filesys.img is dd'd into pc3-sd.img at
# sector 133120 - nothing built filesys.img.  mkccimage.sh layers the
# compiler onto an existing base, so the base itself was an artefact
# that could not be reproduced.  update-flash.sh IS the recipe, it was
# just wired to the 2547 block flash device; it is now parameterised and
# this drives it.
#
# FS32: the filesystem may fill the partition exactly.  The classic
# format needed 64000 of the 65536 sectors kept clear because 16-bit
# NULLBLK (65535) was a physically real sector; FS32's sentinel is
# 0xFFFFFFFF and unreachable, so that margin - and the corruption mode
# it guarded against - is gone (see FS32-FORMAT.md).

set -e

R=$(cd "$(dirname "$0")/../../.." && pwd)
P=$R/Kernel/platform/platform-rpipico
OUT=$R/Images/rpipico/pc3-sd.img
FS=$R/Images/rpipico/filesys.img

# Card layout comes from the image's own MBR (mkcard.sh wrote it):
# p1 FAT, p2 FS32 Fuzix root (boot "hdb2"), p3 reserved.
[ -r "$OUT" ] || { echo "no card image at $OUT to write into" >&2; exit 1; }

. "$P/p2geom.sh"
p2geom "$OUT"
START=$P2_START
COUNT=$P2_COUNT
FSSIZE=$COUNT       # FS32: may fill the partition exactly, no NULLBLK margin
INODES=${INODES:-2048}  # FS32 mkfs takes an inode count directly

echo "--- building a $FSSIZE block root ($INODES inodes) at sector $START"
cd "$P"
IMG="$FS" FSSIZE=$FSSIZE INODES=$INODES sh ./update-flash.sh

# mkfs sizes the file to the filesystem, which is smaller than the
# partition.  Pad so the dd below cannot leave the tail of the old
# filesystem in place - a stale superblock or inode found beyond the new
# end is exactly the sort of thing that reads as corruption later.
# Things the SD root has that the flash root cannot afford. update-flash.sh
# is sized for a 2547 block device, so BBC BASIC (124K) is not in it - but
# it is a headline feature of this machine and has to be on the card.
# Anything else that belongs on the card and not in flash goes here.
echo "--- SD-only extras"
"$R/Standalone/ucp" "$FS" <<EOF > "$FS.ucp.log" 2>&1
cd /usr/bin
bget $R/Applications/bbcbasic/bbcbasic bbcbasic
chmod 755 bbcbasic
exit
EOF
if grep -q "error number" "$FS.ucp.log"; then
	echo "ucp failed installing the SD extras:" >&2
	cat "$FS.ucp.log" >&2
	exit 1
fi
rm -f "$FS.ucp.log"

echo "--- padding to the partition size"
dd if=/dev/zero of="$FS" bs=512 seek=$FSSIZE count=$((COUNT - FSSIZE)) \
   conv=notrunc status=none

echo "--- writing it into $OUT at sector $START"
dd if="$FS" of="$OUT" bs=512 seek=$START count=$COUNT conv=notrunc status=none

echo "--- verifying what landed on the image"
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
dd if="$OUT" of="$W/p2.img" bs=512 skip=$START count=$COUNT status=none
"$R/Standalone/fsck" -a "$W/p2.img"
ls -l "$OUT"
echo "done - now run Applications/CC/mkccimage.sh to add the compiler"
