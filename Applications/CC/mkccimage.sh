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
for f in cc0 cc1 cc2 ccbc bcrun bcdump cpp mmbc saveimage loadimage mmedit playmp3; do
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
	# what mmbc-generated C includes: the runtime's interface and the
	# FCC-view headers (math.h etc. map to bcrun natives)
	echo "get $CC/mmb_runtime.h mmb_runtime.h"
	# the geometry primitives, included only by a program that draws:
	# static functions, so cc1 drops the ones it does not call
	echo "get $CC/mmb_gfx.h mmb_gfx.h"
	# SETPIN and PIN, on the same terms - and easy to forget: a card
	# without this compiles every program that draws and fails only on
	# the ones that touch a pin, long after the change that added it.
	echo "get $CC/mmb_gpio.h mmb_gpio.h"
	for f in "$CC"/hosttest/fcc-include/*.h; do
		echo "get $f $(basename "$f")"
	done
	echo "cd /usr/bin"
	echo "bget $CC/hwtest/ccbc.s cc"
	echo "chmod 755 cc"
	# mmbc: the MMBasic -> C translator (mmb2c.py rewritten in C,
	# byte-identical by that repo's gates) - BASIC self-hosts:
	#   mmbc prog.bas ; cc prog.c ; ./prog.bc
	# saveimage and loadimage are what SAVE IMAGE and LOAD IMAGE run:
	# whole operations, so they are programs rather than runtime, and
	# they cost a BASIC program nothing.  Useful from the shell too.
	# mmedit is the MMBasic editor ported to Fuzix (Applications/mmedit):
	# the machine edits its own BASIC as well as translating it.
	# playmp3 is what PLAY MP3 runs, on the same bargain as the image
	# pair - and it was left OUT of this list for a whole release: the
	# file existed, stageall.sh staged it, the check above insisted on
	# it, and nothing installed it.  PLAY MP3 on a fresh card reported
	# "no such program".  Any name added here must be added to
	# hwtest/verifyimage.sh too, which agreed with the omission.
	for f in cpp bcrun bcdump mmbc saveimage loadimage mmedit playmp3; do
		echo "bget $CC/hwtest/$f.s $f"
		echo "chmod 755 $f"
	done
	echo "cd /root"
	echo "mkdir cc"
	echo "cd cc"
	for f in "$CC"/hosttest/samples/*.c; do
		echo "get $f $(basename "$f")"
	done
	# BASIC samples for mmbc (synced from the mmb2c repo)
	for f in "$CC"/hwtest/*.bas "$CC"/hwtest/*.in; do
		[ -r "$f" ] && echo "get $f $(basename "$f")"
	done
	# The board-side sample runner, so the C suite can be re-run on the
	# machine itself without sending anything: sh rs.sh > out.txt.
	# It deletes each object before compiling, because a cc that fails
	# otherwise leaves the previous one in place and the suite reports
	# a pass for a build that never happened.
	echo "get $CC/hwtest/runsamples.sh rs.sh"
	# rc with the swap size matching the arena split; it MUST be
	# executable or init cannot run it, and a boot without rc has a
	# read-only root whose strangest symptom is "cannot make pipe"
	echo "cd /etc"
	echo "get $R/Kernel/platform/platform-rpipico/rc rc"
	echo "chmod 755 rc"
	echo "df"
	echo "exit"
} > "$W/cmds"

# Not "set -e" territory: see the note above about ucp's exit status
set +e
"$R/Standalone/ucp" "$P2" < "$W/cmds" 2>&1 | tee "$W/ucp.log"
set -e

# ucp does NOT stop on a failed command. A "cd" into a directory that
# does not exist prints "cd: error number 2" and carries on, dropping
# every following file into whatever directory it was already in - which
# still fsck's clean and still gzips, so the first sign is a card that
# cannot find cc. That happened when /usr/bin did not exist. Refuse the
# image instead.
if grep -q "error number" "$W/ucp.log"; then
	echo "ucp reported an error - the image is wrong:" >&2
	grep -n "error number" "$W/ucp.log" >&2
	exit 1
fi

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
