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
# Partition 2 of the image is the Fuzix root; its geometry is read
# from the image's own MBR (p2geom.sh - mkcard.sh is the only place
# the layout is stated).  It is extracted, written into with ucp,
# fsck'd and put back.
#
# Two things about ucp that will otherwise waste an afternoon: "get"
# creates the destination with basename(), i.e. in ucp's own current
# directory, so this drives one session with cd and bare names rather
# than one invocation per file with a full path; and ucp exits with the
# status of its last command, so "set -e" will stop the script on a
# perfectly successful run.

R=$(cd "$(dirname "$0")/../.." && pwd)
CC=$R/Applications/CC
# The MMBasic translator, the mm runtime headers and the BASIC corpus.
MMB=$R/Applications/mmb2c
SRC=$R/Images/rpipico/pc3-sd.img
OUT=$R/Images/rpipico/pc3-sd-cc.img
W=${W:-/tmp/mkccimage}
P2=$W/p2.img

set -e
[ -r "$SRC" ] || { echo "no base image at $SRC" >&2; exit 1; }

. "$R/Kernel/platform/platform-rpipico/p2geom.sh"
p2geom "$SRC"
START=$P2_START
COUNT=$P2_COUNT
for f in cc0 cc1 cc2 ccbc bcrun bcdump cpp mmbc saveimage loadimage loadjpg loadpng mmedit \
		playmp3 playmod; do
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
	# EVERY mmb_*.h, by glob: a glob cannot forget.  It used to be a
	# hand-written list here AND a second one in the sync script, and
	# between them they were wrong twice.  mmb_spi.h was missing at
	# v0.11, so a fresh card compiled everything except a program that
	# opened SPI; then at v0.15 mmb_play.h, mmb_blit.h, mmb_sprite.h
	# and mmb_flash.h were all missing at once - which is to say the
	# release whose theme was games shipped a card that could not
	# compile a program using PLAY, BLIT or SPRITE.  Found by building
	# a four-line program ON THE BOARD; every host gate passed,
	# because the host had the headers.  There is one copy now.
	#
	# What the list used to record, and is worth keeping: these are the
	# geometry and peripheral primitives, static functions with one
	# header per primitive so a program carries exactly what it names
	# (cc1's dead-static rule counts names, not reachability, so the
	# include is the granularity).  mmb_gfx.h is the umbrella kept for
	# hand-written C.  mmb_wait.h must be included LAST by generated
	# code and finds the others by their own include guards - that is
	# the emitter's business, not this script's; staging order does not
	# matter.
	for f in "$MMB"/mmb_*.h; do
		echo "get $f $(basename "$f")"
	done
	# The pin and ADC REGISTERS, which mmb_gpio.h reaches for directly
	# now that pin work is not a syscall.  Flat, because that is the
	# only shape this include directory has; a native program gets the
	# same file from the C library as <sys/pc3io.h>.  One source, staged
	# twice, so the two cannot drift.
	echo "get $R/Library/include/sys/pc3io.h pc3io.h"
	# By name, not by glob: fcc/include also holds a stdlib.h, and the
	# card wants ctest-include's, staged above.
	for f in math.h ctype.h stdint.h time.h; do
		echo "get $MMB/fcc/include/$f $f"
	done
	echo "cd /usr/bin"
	echo "bget $CC/hwtest/ccbc.s cc"
	echo "chmod 755 cc"
	# mmbc: the MMBasic -> C translator (mmb2c.py rewritten in C,
	# byte-identical to it by mmb2c/mmbc/cgate.sh) - BASIC self-hosts:
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
	# EVERY staged program, by glob - cc0/cc1/cc2 went to /usr/lib/cc
	# above and ccbc is installed as "cc" just now, so those four are
	# the only exceptions.  This was a hand-written list and it missed
	# playmp3 for a whole release; then it missed PLAYMOD for the
	# release that added PLAY MODFILE and PLAY MODSAMPLE, so the
	# statement raised "Could not play the MOD file" on a card whose
	# every gate was green.  The binary was cross-built and staged both
	# times; only the installing was forgotten.
	# Exactly what stageall.sh staged, from the list it writes - not a
	# hand-kept list here, which is how playmp3 missed a whole release
	# and playmod missed the release that ADDED PLAY MODFILE and PLAY
	# MODSAMPLE: cross-built, staged, installed nowhere, and the
	# statement answered "Could not play the MOD file" on a card whose
	# every gate was green.
	#
	# cc0/cc1/cc2 went to /usr/lib/cc above and ccbc is installed as
	# "cc" just now.  playsnd is DEPRECATED: its synthesiser moved into
	# the kernel's DAC interrupt at v0.15 and the client only reaches
	# for the daemon on a kernel too old to have the ioctl, which is
	# not the kernel shipped beside this card.
	while read -r b; do
		case $b in cc0 | cc1 | cc2 | ccbc | playsnd) continue ;; esac
		echo "bget $CC/hwtest/$b.s $b"
		echo "chmod 755 $b"
	done < "$CC/hwtest/staged.list"
	echo "cd /root"
	echo "mkdir cc"
	echo "cd cc"
	for f in "$CC"/hosttest/samples/*.c; do
		echo "get $f $(basename "$f")"
	done
	# The BASIC corpus, from Applications/mmb2c.  samples/ goes whole -
	# those programs need a screen, a keyboard or a device, so the host
	# gates cannot run them and the board is the only place they mean
	# anything.  From tests/ only the names in tests/card.list, because
	# most of that directory exists for the gates.
	#
	# tests FIRST, then samples: port, pulse and settick exist in both
	# and are DIFFERENT programs - the sample is the board demo and is
	# the one wanted here.  Staging order decides that, so it is said
	# out loud below rather than left to whoever edits these two loops.
	while read -r b; do
		case $b in ''|\#*) continue ;; esac
		[ -r "$MMB/tests/$b" ] || {
			echo "card.list names $b, which tests/ does not have" >&2
			exit 1; }
		echo "get $MMB/tests/$b $b"
	done < "$MMB/tests/card.list"
	for f in "$MMB"/samples/*.bas; do
		b=$(basename "$f")
		[ -r "$MMB/tests/$b" ] && echo "note: samples/$b shadows tests/$b" >&2
		echo "get $f $b"
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
