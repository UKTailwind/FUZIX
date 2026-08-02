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
# 64000 BLOCKS, NOT 65535.  blkno_t is uint16_t, so 65535 is the literal
# ceiling - mkfs casts the size to uint16_t and 65536 wraps to 0.  Worse,
# NULLBLK is ((blkno_t)-1) = 65535, which put the "no such block"
# sentinel exactly on the filesystem's upper bound, and since the
# partition is 65536 sectors, sector 65535 physically exists: anything
# that let a NULLBLK reach the block layer wrote a real sector instead
# of being refused.  64000 puts clear air between the last data block,
# the sentinel and the end of the partition.  The partition is unchanged
# at 65536 sectors, so no partition table edit is needed.

set -e

R=$(cd "$(dirname "$0")/../../.." && pwd)
P=$R/Kernel/platform/platform-rpipico
OUT=$R/Images/rpipico/pc3-sd.img
FS=$R/Images/rpipico/filesys.img

# Card layout (PC3-DEVNOTES.md): p1 = 64M FAT placeholder, p2 = 32M
# Fuzix root (boot "hdb2"), p3 = 4M type 0x7F.
START=133120
COUNT=65536
FSSIZE=64000
ISIZE=256           # 2048 inodes, matching the root this replaces

[ -r "$OUT" ] || { echo "no card image at $OUT to write into" >&2; exit 1; }

echo "--- building a $FSSIZE block root (isize $ISIZE)"
cd "$P"
IMG="$FS" FSSIZE=$FSSIZE ISIZE=$ISIZE sh ./update-flash.sh

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
