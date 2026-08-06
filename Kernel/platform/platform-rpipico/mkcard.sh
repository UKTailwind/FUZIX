#!/bin/sh
#
# Create the empty partitioned card image that mksdimage.sh writes into.
#
#   sh mkcard.sh                          -> Images/rpipico/pc3-sd.img
#   ROOT_MB=256 sh mkcard.sh              -> the same with a 256MB root
#
# WHY THIS EXISTS: mksdimage.sh refuses to run without an existing
# pc3-sd.img, and nothing in the tree made one.  The root filesystem
# became reproducible in v0.5; the CARD around it did not, so a fresh
# clone could build a kernel but not a bootable card - the layout lived
# only in whatever image happened to be on the disk.  Now both are
# recipes.
#
# FS32: the layout is PARAMETERISED and this file is its only
# authority.  Everything downstream (mksdimage.sh, mkccimage.sh,
# verifyimage.sh) reads partition 2's geometry back out of the image's
# own MBR via p2geom.sh, so there is nothing to keep in step by hand.
# The classic 32MB ceiling is gone; ROOT_MB is bounded by the card and
# by how long a card write takes, nothing else.
#
#   p1  0x0C  FAT, for interchange       FAT_MB   (default 128)
#   p2  0x83  FS32 Fuzix root (hdb2)     ROOT_MB  (default 800)
#   p3  0x7F  reserved                   RES_MB   (default 4)
#
# The defaults total 933MiB = 978,321,408 bytes: sized for a 1GB card
# taking 10^9 bytes as the floor a "1GB" card can be trusted to have,
# with ~22MB spare for cards that run small.
#
# Partition 1 is left unformatted on purpose: the manual tells the user
# to format it from Windows, because mkfs.vfat here and Windows' own
# formatter do not always agree about what the Pico's FAT reader will
# accept.
#
# This DESTROYS any existing pc3-sd.img, so it refuses if one is there.

set -e

R=$(cd "$(dirname "$0")/../../.." && pwd)
OUT=$R/Images/rpipico/pc3-sd.img

FAT_MB=${FAT_MB:-128}
ROOT_MB=${ROOT_MB:-800}
RES_MB=${RES_MB:-4}

P1_START=2048
P1_SIZE=$((FAT_MB * 2048))
P2_START=$((P1_START + P1_SIZE))
P2_SIZE=$((ROOT_MB * 2048))
P3_START=$((P2_START + P2_SIZE))
P3_SIZE=$((RES_MB * 2048))
SECTORS=$((P3_START + P3_SIZE))

if [ -e "$OUT" ]; then
	echo "$OUT already exists - remove it first if you mean to rebuild" >&2
	exit 1
fi

mkdir -p "$(dirname "$OUT")"

echo "--- creating $SECTORS sectors (FAT ${FAT_MB}M, root ${ROOT_MB}M, reserved ${RES_MB}M)"
dd if=/dev/zero of="$OUT" bs=512 count=$SECTORS status=none

echo "--- partition table"
sfdisk --quiet "$OUT" <<EOF
label: dos
unit: sectors
start=$P1_START, size=$P1_SIZE, type=c
start=$P2_START, size=$P2_SIZE, type=83
start=$P3_START, size=$P3_SIZE, type=7f
EOF

echo "--- what landed"
sfdisk --list "$OUT" 2>/dev/null | sed -n '/Device/,$p'
echo
echo "done - now run mksdimage.sh to build the root into it,"
echo "then Applications/CC/mkccimage.sh to add the compiler"
