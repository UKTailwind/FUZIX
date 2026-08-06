# Sourced, not run.  Sets P2_START and P2_COUNT (sectors) for the
# Fuzix root partition by reading the MBR of the image given as $1.
#
# WHY: mkcard.sh, mksdimage.sh, mkccimage.sh and verifyimage.sh each
# used to carry their own copy of "133120 / 65536".  A list duplicated
# across the scripts that check each other is how v0.8 shipped without
# playmp3 - so the layout now has exactly one authority: the partition
# table inside the image itself, which mkcard.sh wrote.
#
# Partition 2's MBR entry is at offset 446 + 16 = 462; start LBA is 4
# little-endian bytes at +8, sector count at +12.  od keeps this free
# of any sfdisk dependency at consume time.

p2geom() {
	P2_START=$(od -An -tu4 -j 470 -N 4 "$1" | tr -d ' ')
	P2_COUNT=$(od -An -tu4 -j 474 -N 4 "$1" | tr -d ' ')
	if [ -z "$P2_START" ] || [ -z "$P2_COUNT" ] || \
	   [ "$P2_START" -eq 0 ] || [ "$P2_COUNT" -eq 0 ]; then
		echo "p2geom: no partition 2 in $1" >&2
		exit 1
	fi
}
