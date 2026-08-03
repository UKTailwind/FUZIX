#!/bin/sh
#
# Create the empty partitioned card image that mksdimage.sh writes into.
#
#   sh mkcard.sh            -> Images/rpipico/pc3-sd.img
#
# WHY THIS EXISTS: mksdimage.sh refuses to run without an existing
# pc3-sd.img, and nothing in the tree made one.  The root filesystem
# became reproducible in v0.5; the CARD around it did not, so a fresh
# clone could build a kernel but not a bootable card - the layout lived
# only in whatever image happened to be on the disk.  Now both are
# recipes.
#
# The layout is the one the manual documents and the kernel expects.
# Partition 2 starting at sector 133120 is not decorative: mksdimage.sh
# and mkccimage.sh both dd the root filesystem to that sector, and the
# boot device name "hdb2" refers to it.
#
#   p1  0x0C  LBA   2048  131072 sectors   64 MB  FAT, for interchange
#   p2  0x83  LBA 133120   65536 sectors   32 MB  Fuzix root (hdb2)
#   p3  0x7F  LBA 198656    8192 sectors    4 MB  reserved
#
# Total 206848 sectors = 105,906,176 bytes.  Partition 1 is left
# unformatted on purpose: the manual tells the user to format it from
# Windows, because mkfs.vfat here and Windows' own formatter do not
# always agree about what the Pico's FAT reader will accept.
#
# This DESTROYS any existing pc3-sd.img, so it refuses if one is there.

set -e

R=$(cd "$(dirname "$0")/../../.." && pwd)
OUT=$R/Images/rpipico/pc3-sd.img
SECTORS=206848

if [ -e "$OUT" ]; then
	echo "$OUT already exists - remove it first if you mean to rebuild" >&2
	exit 1
fi

mkdir -p "$(dirname "$OUT")"

echo "--- creating $SECTORS sectors"
dd if=/dev/zero of="$OUT" bs=512 count=$SECTORS status=none

echo "--- partition table"
sfdisk --quiet "$OUT" <<'EOF'
label: dos
unit: sectors
start=2048,   size=131072, type=c
start=133120, size=65536,  type=83
start=198656, size=8192,   type=7f
EOF

echo "--- what landed"
sfdisk --list "$OUT" 2>/dev/null | sed -n '/Device/,$p'
echo
echo "done - now run mksdimage.sh to build the root into it,"
echo "then Applications/CC/mkccimage.sh to add the compiler"
