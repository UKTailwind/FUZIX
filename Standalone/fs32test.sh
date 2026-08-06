#!/bin/sh
#
# FS32 host gates 1-3 (FS32-FORMAT.md "Verification gates").
#
#   sh fs32test.sh        run in Standalone/ after make mkfs fsck ucp
#
# Gate 1: a fresh mkfs image fscks clean.
# Gate 2: ucp populates a tree and a 40 MB file; everything reads back
#         byte-identical.  40 MB is chosen with intent: it exceeds the
#         classic 32 MB FILE limit, needs the triple indirect tree
#         (> 8.4 MB), and on a 200000-block filesystem its data blocks
#         cross 65536 - so 32-bit block numbers demonstrably reach disk.
# Gate 3: three deliberate corruptions are detected: a wild inode
#         pointer, a wrong free-inode count, and a classic-format magic
#         (which must be refused by name, not "fixed").
#
# fsck -y answers yes to everything, so a CLEAN run still says yes to
# "Continue?" and "Rebuild free list?".  Cleanliness is judged by the
# absence of any repair prompt, not by the exit code.
#
# ucp's bput takes ONE name: it copies that fuzix file to the same name
# in the host working directory - hence the work dir dance below.

set -e

SD=$(cd "$(dirname "$0")" && pwd)
W=/tmp/fs32work
IMG=$W/fs32test.img
BIG=$W/fs32big.bin
LOG=$W/fsck.log

FSIZE=200000		# blocks: ~98 MB, comfortably past both 16-bit walls
INODES=2048

rm -rf "$W"
mkdir -p "$W"
cd "$W"

fail() {
	echo "FAIL: $1" >&2
	exit 1
}

# A clean fsck run prompts only "Continue?" and "Rebuild free list?".
# Anything else is a finding.
fsck_clean() {
	"$SD/fsck" -y "$IMG" > "$LOG" 2>&1 || true
	if grep -Eq "Zap|Fix|multiply|out of range|detached|wrong place|improper|corrupt|invalid|classic" "$LOG"; then
		echo "--- fsck was not clean: ---" >&2
		cat "$LOG" >&2
		fail "$1"
	fi
}

echo "--- gate 1: fresh filesystem fscks clean"
"$SD/mkfs" "$IMG" $INODES $FSIZE > /dev/null
fsck_clean "fresh mkfs image"

echo "--- gate 2: populate, 40MB file, read back byte-identical"
head -c 40000000 /dev/urandom > "$BIG"
"$SD/ucp" "$IMG" > /dev/null << EOF
cd /
mkdir bin
mkdir etc
mkdir usr
chmod 0755 bin
chmod 0755 etc
chmod 0755 usr
cd /usr
mkdir bin
cd /bin
bget $SD/ucp ucp-copy
chmod 0755 ucp-copy
mknod tty 20666 512
mknod hda 60660 0
cd /
bget $BIG big.bin
exit
EOF
fsck_clean "populated image"

mkdir -p "$W/out"
cd "$W/out"
"$SD/ucp" "$IMG" > /dev/null << EOF
bput big.bin
cd /bin
bput ucp-copy
exit
EOF
cd "$W"
cmp "$BIG" "$W/out/big.bin" || fail "40MB file did not read back byte-identical"
cmp "$SD/ucp" "$W/out/ucp-copy" || fail "binary did not read back byte-identical"

echo "--- gate 2b: delete the big file, fsck clean"
"$SD/ucp" "$IMG" > /dev/null << EOF
rm big.bin
exit
EOF
fsck_clean "after deleting the 40MB file"

echo "--- gate 3a: wild pointer in an inode is detected"
# Fresh image with exactly one file, then poke that file's i_addr[0]
# high byte -> enormous block.  (Not the root DIRECTORY's pointer:
# zapping that legitimately leaves fsck unable to walk /, which panics
# - same as classic.)  The file's inode number is NOT predictable
# (i_alloc pops the highest cached free inode), so read it out of the
# root directory entry: root dir data is block ISIZE, "victim" is the
# third 32-byte entry, d_ino is its first uint16.
rm -f "$IMG"
"$SD/mkfs" "$IMG" $INODES $FSIZE > /dev/null
printf 'bget %s victim\nexit\n' "$SD/fs32test.sh" | "$SD/ucp" "$IMG" > /dev/null
ISIZE=$((2 + (INODES + 1) / 2))
VINO=$(od -An -tu2 -j $((ISIZE*512 + 2*32)) -N2 "$IMG" | tr -d ' ')
[ -n "$VINO" ] && [ "$VINO" -ge 2 ] || fail "could not read victim inode number"
# inode VINO lives in block 2 + VINO/2, slot VINO%2; i_addr[0] at +28
VOFF=$(( (2 + VINO/2)*512 + (VINO%2)*256 + 28 + 3 ))
printf '\377' | dd of="$IMG" bs=1 seek=$VOFF conv=notrunc status=none
"$SD/fsck" -y "$IMG" > "$LOG" 2>&1 || true
grep -q "out of range" "$LOG" || { cat "$LOG" >&2; fail "wild pointer not detected"; }
fsck_clean "after repairing wild pointer"
# The file keeps its size but block 0 becomes a hole - correct repair;
# the point here is detection and a clean second pass.

echo "--- gate 3b: wrong free-inode count is detected"
rm -f "$IMG"
"$SD/mkfs" "$IMG" $INODES $FSIZE > /dev/null
# s_tinode is at offset 18 in the superblock (block 1)
printf '\001\001' | dd of="$IMG" bs=1 seek=$((512 + 18)) conv=notrunc status=none
"$SD/fsck" -y "$IMG" > "$LOG" 2>&1 || true
grep -q "Free inode count" "$LOG" || { cat "$LOG" >&2; fail "bad s_tinode not detected"; }
fsck_clean "after repairing s_tinode"

echo "--- gate 3c: classic magic is refused by name"
# 12742 little-endian at the start of block 1
printf '\306\061' | dd of="$IMG" bs=1 seek=512 conv=notrunc status=none
"$SD/fsck" -y "$IMG" > "$LOG" 2>&1 || true
grep -q "classic" "$LOG" || { cat "$LOG" >&2; fail "classic magic not refused by name"; }

cd /
rm -rf "$W"
echo "fs32test: all gates pass"
