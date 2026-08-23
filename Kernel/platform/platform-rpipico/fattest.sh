#!/bin/sh
#
# fattest.sh - the gate for Applications/util/fat.c, in particular "put".
#
#   sh fattest.sh
#
# WHY THIS EXISTS: "fat put" writes a filesystem that this machine does
# not own.  The card is carried to a Windows or Mac desktop, and a fault
# in a directory entry or a cluster chain does not show up as a wrong
# answer here - it shows up as that desktop calling the card damaged,
# days later, with the file already believed safe.  So the check is not
# "did the bytes come back": it is fsck.vfat, which is an adversary that
# knows the format far better than the tool does, plus a readback by
# mtools, which is a second independent reader.  Bytes coming back is
# the weakest of the three - a chain can be self-consistently wrong.
#
# fat.c is plain portable C, so it builds and runs here.  That matters:
# every case below would otherwise need a card, a reflash and a desktop.

R=$(cd "$(dirname "$0")/../../.." && pwd)
SRC=$R/Applications/util/fat.c
TMP=${TMPDIR:-/tmp}/fattest.$$
FAT=$TMP/fat

MTOOLS_SKIP_CHECK=1
export MTOOLS_SKIP_CHECK

FAILED=0
NTEST=0

die() {
	echo "fattest: $*" >&2
	rm -rf "$TMP"
	exit 2
}

bad() {
	FAILED=$((FAILED + 1))
	echo "  FAIL: $*"
}

pass() {
	NTEST=$((NTEST + 1))
}

for t in gcc mkfs.vfat fsck.vfat mcopy mdir mmd mdel truncate cmp; do
	command -v "$t" >/dev/null 2>&1 || die "need $t (dosfstools, mtools)"
done

mkdir -p "$TMP" || die "cannot make $TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

echo "=== building fat.c for the host"
gcc -D_GNU_SOURCE -Wall -O2 -o "$FAT" "$SRC" || die "build failed"

# --- helpers --------------------------------------------------------------

# fsck must be silent AND clean.  dosfstools reports some faults without
# a non-zero status, so the output is inspected as well as the code.
fsck_ok() {
	_img=$1
	_what=$2
	_out=$(fsck.vfat -n "$_img" 2>&1)
	if [ $? -ne 0 ]; then
		bad "$_what: fsck.vfat rejects the filesystem"
		echo "$_out" | sed 's/^/        /'
		return 1
	fi
	case "$_out" in
	*Dirty*|*orphan*|*"Free cluster summary"*|*"not in use"*|*Broken*|\
	*"Bad "*|*"bogus"*|*"Cluster chain"*|*mismatch*|*"unknown"*)
		bad "$_what: fsck.vfat complained"
		echo "$_out" | sed 's/^/        /'
		return 1
		;;
	esac
	return 0
}

# Every copy of the FAT must be identical.  A desktop chkdsk compares
# them and calls a card that disagrees with itself damaged, so this is
# checked directly rather than left to fsck to mention.
byte() {	# one byte of a file, in decimal
	od -An -tu1 -j"$2" -N1 "$1" | tr -d ' \n'
}

fats_agree() {
	_img=$1
	_what=$2
	_ssz=$(( $(byte "$_img" 11) + $(byte "$_img" 12) * 256 ))
	_rsvd=$(( $(byte "$_img" 14) + $(byte "$_img" 15) * 256 ))
	_nfat=$(byte "$_img" 16)
	_fsz=$(( $(byte "$_img" 22) + $(byte "$_img" 23) * 256 ))
	if [ "$_fsz" = 0 ]; then
		_fsz=$(( $(byte "$_img" 36) + $(byte "$_img" 37) * 256 +
			 $(byte "$_img" 38) * 65536 ))
	fi
	[ "$_nfat" -ge 2 ] || return 0
	_i=1
	while [ "$_i" -lt "$_nfat" ]; do
		dd if="$_img" bs="$_ssz" skip="$_rsvd" count="$_fsz" \
		   of="$TMP/fat0" status=none 2>/dev/null
		dd if="$_img" bs="$_ssz" skip=$((_rsvd + _i * _fsz)) count="$_fsz" \
		   of="$TMP/fatn" status=none 2>/dev/null
		cmp -s "$TMP/fat0" "$TMP/fatn" ||
			bad "$_what: FAT copy $_i does not match the first"
		_i=$((_i + 1))
	done
	rm -f "$TMP/fat0" "$TMP/fatn"
	return 0
}

# The file must come back byte for byte through mtools (an independent
# reader) and through fat's own get (the complement being tested).
readback_ok() {
	_img=$1
	_name=$2
	_orig=$3
	_what=$4
	_rc=0

	if ! mcopy -i "$_img" -n "::$_name" "$TMP/m.out" >/dev/null 2>&1; then
		bad "$_what: mtools cannot read back '$_name'"
		return 1
	fi
	cmp -s "$_orig" "$TMP/m.out" || { bad "$_what: mtools readback differs"; _rc=1; }

	if ! "$FAT" -d "$_img" get "$_name" "$TMP/g.out" >/dev/null 2>&1; then
		bad "$_what: fat get cannot read back '$_name'"
		return 1
	fi
	cmp -s "$_orig" "$TMP/g.out" || { bad "$_what: fat get readback differs"; _rc=1; }
	rm -f "$TMP/m.out" "$TMP/g.out"
	return $_rc
}

# The whole of one case: put it, fsck it, read it back both ways.
put_ok() {
	_img=$1
	_src=$2
	_name=$3
	_what=$4
	shift 4
	if ! "$FAT" -d "$_img" put "$_src" "$@" >"$TMP/err" 2>&1; then
		bad "$_what: put failed"
		sed 's/^/        /' "$TMP/err"
		return 1
	fi
	fsck_ok "$_img" "$_what" || return 1
	readback_ok "$_img" "$_name" "$_src" "$_what" || return 1
	pass
	return 0
}

mkfile() {
	head -c "$2" /dev/urandom > "$1" || die "cannot make $1"
}

# --- the matrix, run against every geometry -------------------------------

matrix() {
	IMG=$1
	LABEL=$2

	echo "--- $LABEL"

	# sizes, including the boundaries: nothing, inside one sector, exactly
	# one sector, a partial last sector, and several clusters
	: > "$TMP/empty"
	put_ok "$IMG" "$TMP/empty" empty "$LABEL empty file"
	# 2048 and 4096 fill a cluster exactly on the larger geometries, and
	# 2049/4097 spill one byte into the next one
	for n in 1 100 511 512 513 2048 2049 4096 4097 5000 100000; do
		mkfile "$TMP/sz$n" "$n"
		put_ok "$IMG" "$TMP/sz$n" "sz$n" "$LABEL $n bytes"
	done
	fats_agree "$IMG" "$LABEL"

	# a plain lower case 8.3 name: no long name entry, the NT case flags
	# carry it, and it must not come back shouting
	mkfile "$TMP/myprog.bas" 300
	put_ok "$IMG" "$TMP/myprog.bas" myprog.bas "$LABEL lower case 8.3"
	if ! mdir -i "$IMG" -b "::myprog.bas" 2>/dev/null | grep -q myprog.bas; then
		bad "$LABEL: mtools does not see myprog.bas as lower case"
	fi
	if ! "$FAT" -d "$IMG" ls | grep -q "myprog.bas"; then
		bad "$LABEL: fat ls does not show myprog.bas as lower case"
	fi

	# names that need a long name entry
	mkfile "$TMP/src" 2000
	put_ok "$IMG" "$TMP/src" "A Long File Name.txt" "$LABEL long name" \
		"A Long File Name.txt"
	put_ok "$IMG" "$TMP/src" "MixedCase.Txt" "$LABEL mixed case" \
		"MixedCase.Txt"
	put_ok "$IMG" "$TMP/src" "no-dot-at-all-here-and-quite-long" \
		"$LABEL no extension" "no-dot-at-all-here-and-quite-long"

	# exactly the longest name the reader can hold (64)
	L64=$(printf 'x%.0s' $(seq 1 60)).txt
	put_ok "$IMG" "$TMP/src" "$L64" "$LABEL 64-char name" "$L64"

	# two long names that mangle to the same basis: the numeric tail has
	# to keep them apart, and both must still answer to their real names
	put_ok "$IMG" "$TMP/src" "Collide Me Please One.txt" \
		"$LABEL tail collision 1" "Collide Me Please One.txt"
	mkfile "$TMP/src2" 3000
	put_ok "$IMG" "$TMP/src2" "Collide Me Please Two.txt" \
		"$LABEL tail collision 2" "Collide Me Please Two.txt"
	# ... and the first must not have been disturbed by the second
	readback_ok "$IMG" "Collide Me Please One.txt" "$TMP/src" \
		"$LABEL tail collision recheck"

	# overwrite, both directions across a cluster boundary
	mkfile "$TMP/big" 60000
	mkfile "$TMP/small" 40
	put_ok "$IMG" "$TMP/big" over.dat "$LABEL overwrite: create" over.dat
	put_ok "$IMG" "$TMP/small" over.dat "$LABEL overwrite: shrink" over.dat
	put_ok "$IMG" "$TMP/big" over.dat "$LABEL overwrite: grow" over.dat
	put_ok "$IMG" "$TMP/empty" over.dat "$LABEL overwrite: to empty" over.dat
	put_ok "$IMG" "$TMP/big" over.dat "$LABEL overwrite: from empty" over.dat
	# overwriting through the long name must patch the entry, not add one
	put_ok "$IMG" "$TMP/src" "A Long File Name.txt" \
		"$LABEL overwrite a long name" "A Long File Name.txt"
	_n=$(mdir -i "$IMG" -b 2>/dev/null | grep -c "A Long File Name.txt")
	[ "$_n" = "1" ] || bad "$LABEL: overwrite left $_n copies of the long name"

	# subdirectories: named explicitly, and named as the destination
	mmd -i "$IMG" ::sub >/dev/null 2>&1 || bad "$LABEL: cannot make ::sub"
	put_ok "$IMG" "$TMP/src" "sub/inside.txt" "$LABEL into a subdir" \
		"sub/inside.txt"
	put_ok "$IMG" "$TMP/myprog.bas" "sub/myprog.bas" \
		"$LABEL dest is a directory" sub
	put_ok "$IMG" "$TMP/src" "sub/A Long One Inside.txt" \
		"$LABEL long name in a subdir" "sub/A Long One Inside.txt"

	# a directory that has to grow past its first cluster, with long
	# names so the slots run out sooner
	mmd -i "$IMG" ::grow >/dev/null 2>&1 || bad "$LABEL: cannot make ::grow"
	_i=0
	while [ $_i -lt 60 ]; do
		"$FAT" -d "$IMG" put "$TMP/small" "grow/A Growing Name $_i.txt" \
			>/dev/null 2>&1 || { bad "$LABEL: grow put $_i failed"; break; }
		_i=$((_i + 1))
	done
	fsck_ok "$IMG" "$LABEL directory growth"
	readback_ok "$IMG" "grow/A Growing Name 59.txt" "$TMP/small" \
		"$LABEL directory growth"
	_n=$(mdir -i "$IMG" -b ::grow 2>/dev/null | grep -c "A Growing Name")
	[ "$_n" = "60" ] || bad "$LABEL: grew to $_n entries, wanted 60"
	pass

	# deleted slots must be reused, and reusing them must not corrupt the
	# entries on either side
	mdel -i "$IMG" "::grow/A Growing Name 10.txt" >/dev/null 2>&1
	mdel -i "$IMG" "::grow/A Growing Name 11.txt" >/dev/null 2>&1
	put_ok "$IMG" "$TMP/src" "grow/Refilled Slot Here.txt" \
		"$LABEL reuse deleted slots" "grow/Refilled Slot Here.txt"
	readback_ok "$IMG" "grow/A Growing Name 12.txt" "$TMP/small" \
		"$LABEL neighbour after reuse"
	readback_ok "$IMG" "grow/A Growing Name 9.txt" "$TMP/small" \
		"$LABEL neighbour before reuse"

	# things that must be refused, cleanly
	"$FAT" -d "$IMG" put "$TMP/src" "bad:name.txt" >/dev/null 2>&1 &&
		bad "$LABEL: accepted a name with a colon"
	"$FAT" -d "$IMG" put "$TMP/src" "$(printf 'y%.0s' $(seq 1 70))" \
		>/dev/null 2>&1 && bad "$LABEL: accepted a 70-character name"
	"$FAT" -d "$IMG" put "$TMP/src" "nosuchdir/x.txt" >/dev/null 2>&1 &&
		bad "$LABEL: accepted a directory that does not exist"
	"$FAT" -d "$IMG" put "$TMP/nonexistent" x.txt >/dev/null 2>&1 &&
		bad "$LABEL: accepted a source that does not exist"
	"$FAT" -d "$IMG" put "$TMP/src" "sub/.." >/dev/null 2>&1 ||
		bad "$LABEL: could not put through .."
	fsck_ok "$IMG" "$LABEL after the refusals"
	pass

	# ".." is a real entry in a FAT subdirectory, and in a first level
	# one it holds 0 meaning "the root" - which on FAT32 is not the
	# root's cluster number.  Taken at face value it addresses two
	# clusters below the data area, in the middle of the FATs.
	put_ok "$IMG" "$TMP/src" "dotdot.txt" "$LABEL a name reached through .." \
		"sub/../dotdot.txt"
	"$FAT" -d "$IMG" ls "sub/.." >/dev/null 2>&1 ||
		bad "$LABEL: ls through .. failed"
	pass

	# the reads that already worked must go on working
	"$FAT" -d "$IMG" ls >/dev/null 2>&1 || bad "$LABEL: ls broke"
	"$FAT" -d "$IMG" ls sub >/dev/null 2>&1 || bad "$LABEL: ls sub broke"
	"$FAT" -d "$IMG" ls "sz*" >/dev/null 2>&1 || bad "$LABEL: ls pattern broke"
	"$FAT" -d "$IMG" info >/dev/null 2>&1 || bad "$LABEL: info broke"
	pass

	# Everything written at the start must still be exactly itself after
	# everything written since.  Checking only the file just put would
	# miss the failure that matters most here - a put that quietly
	# damages a file written earlier.
	for n in 1 100 511 512 513 2048 2049 4096 4097 5000 100000; do
		readback_ok "$IMG" "sz$n" "$TMP/sz$n" "$LABEL $n bytes, at the end"
	done
	readback_ok "$IMG" "A Long File Name.txt" "$TMP/src" \
		"$LABEL long name, at the end"
	readback_ok "$IMG" "sub/inside.txt" "$TMP/src" "$LABEL subdir, at the end"
	fats_agree "$IMG" "$LABEL at the end"
	fsck_ok "$IMG" "$LABEL at the end" && pass
}

# --- geometries -----------------------------------------------------------
#
# FAT16 and FAT32, each with a one-sector cluster and a multi-sector one:
# the sector-within-cluster arithmetic is where an off-by-one hides, and
# a 512-byte cluster makes directories grow after only 16 entries.

echo "=== making filesystems"
mkfs.vfat -F 16 -S 512 -s 1 -C "$TMP/a.img" 16384 >/dev/null || die "mkfs a"
mkfs.vfat -F 16 -S 512 -s 4 -C "$TMP/b.img" 65536 >/dev/null || die "mkfs b"
mkfs.vfat -F 32 -S 512 -s 1 -C "$TMP/c.img" 131072 >/dev/null || die "mkfs c"
truncate -s 512M "$TMP/d.img" || die "truncate d"
mkfs.vfat -F 32 -S 512 -s 8 "$TMP/d.img" >/dev/null || die "mkfs d"
# A FAT32 volume with FEWER clusters than the threshold that is supposed
# to define FAT32.  mkfs.vfat makes one on request and warns; fsck warns
# too ("may lead to problems on some systems").  It matters because the
# FAT width decides how wide an entry is WRITTEN: reading such a volume
# as FAT16 mostly still finds the data, writing it as FAT16 puts half an
# entry into every slot of a 32-bit FAT.
mkfs.vfat -F 32 -S 512 -s 8 -C "$TMP/e.img" 131072 >/dev/null 2>&1 ||
	die "mkfs e"

matrix "$TMP/a.img" "FAT16 512B clusters"
matrix "$TMP/b.img" "FAT16 2K clusters"
matrix "$TMP/c.img" "FAT32 512B clusters"
matrix "$TMP/d.img" "FAT32 4K clusters"
matrix "$TMP/e.img" "FAT32 under the cluster threshold"

# --- the two ways to run out ----------------------------------------------
#
# Both must fail with the filesystem still intact: that is the whole
# claim this tool makes about losing halfway through.

echo "--- running out of room"
mkfs.vfat -F 16 -S 512 -s 1 -C "$TMP/full.img" 4096 >/dev/null || die "mkfs full"
mkfile "$TMP/toobig" 8000000
if "$FAT" -d "$TMP/full.img" put "$TMP/toobig" big.dat >/dev/null 2>&1; then
	bad "a file larger than the filesystem was accepted"
else
	pass
fi
fsck_ok "$TMP/full.img" "after a full disk" && pass
if "$FAT" -d "$TMP/full.img" ls 2>/dev/null | grep -q big.dat; then
	bad "the file that did not fit was left in the directory"
else
	pass
fi
# and the space it tried to use must still be usable afterwards
mkfile "$TMP/fits" 20000
put_ok "$TMP/full.img" "$TMP/fits" fits.dat "after a full disk, a file that fits" \
	fits.dat

echo "--- running out of root directory"
mkfs.vfat -F 16 -S 512 -s 1 -r 32 -C "$TMP/rootfull.img" 8192 >/dev/null ||
	die "mkfs rootfull"
mkfile "$TMP/tiny" 10
_i=0
_stopped=0
while [ $_i -lt 60 ]; do
	if ! "$FAT" -d "$TMP/rootfull.img" put "$TMP/tiny" "f$_i.dat" \
	     >/dev/null 2>&1; then
		_stopped=1
		break
	fi
	_i=$((_i + 1))
done
if [ $_stopped = 1 ]; then
	pass
else
	bad "the fixed FAT16 root never reported itself full"
fi
fsck_ok "$TMP/rootfull.img" "after a full root directory" && pass
readback_ok "$TMP/rootfull.img" "f0.dat" "$TMP/tiny" "first file after root full"

# --- verdict --------------------------------------------------------------

echo
if [ $FAILED = 0 ]; then
	echo "fattest: PASS ($NTEST checks)"
	exit 0
fi
echo "fattest: FAIL ($FAILED problems in $((NTEST + FAILED)) checks)"
exit 1
