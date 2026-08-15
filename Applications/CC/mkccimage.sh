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
	# static functions, one header per primitive so a program carries
	# exactly the primitives it names (cc1's dead-static rule counts
	# names, not reachability, so the include is the granularity).
	# mmb_gfx.h is the umbrella kept for hand-written C.
	for f in mmb_gfx.h mmb_gfx_pts.h mmb_gfx_circle.h mmb_gfx_box.h \
			mmb_gfx_rbox.h mmb_gfx_triangle.h mmb_gfx_arc.h \
			mmb_gfx_text.h mmb_gfx_map.h mmb_gfx_polygon.h \
			mmb_gfx_bezier.h mmb_gfx_fill.h; do
		echo "get $CC/$f $f"
	done
	# SETPIN and PIN, on the same terms - and easy to forget: a card
	# without this compiles every program that draws and fails only on
	# the ones that touch a pin, long after the change that added it.
	echo "get $CC/mmb_gpio.h mmb_gpio.h"
	# SETPIN's interrupt modes: the poll, the table and the dispatch.
	# Same terms - only a program that arms one names any of it.
	echo "get $CC/mmb_int.h mmb_int.h"
	# PWM: MMBasic's wrap-and-duty arithmetic and the slice mapping.
	echo "get $CC/mmb_pwm.h mmb_pwm.h"
	# I2C2 - the second controller, on header pins.
	echo "get $CC/mmb_i2c.h mmb_i2c.h"
	# SPI0 - likewise.  This one was missing until v0.11: the header
	# was added to sync-runtime.sh and to both front ends, and NOT
	# here, so a freshly built card compiled everything except a
	# program that opened SPI.  That is the failure this list keeps
	# collecting; add a new mmb_*.h in BOTH places or neither.
	echo "get $CC/mmb_spi.h mmb_spi.h"
	# PEEK: reading memory by address, which is what makes
	# MM.INFO(FONT ADDRESS n) usable from BASIC.
	echo "get $CC/mmb_peek.h mmb_peek.h"
	# PORT and PULSE: several pins as one value, and a timed
	# inversion.  Both drive the registers directly, so both are
	# here rather than behind an ioctl.
	# The data arguments I2C, SPI and one-wire share - one copy, as
	# MMBasic has one.  Included ahead of both bus headers.
	echo "get $CC/mmb_comms.h mmb_comms.h"
	# One-wire and the DS18B20, bit-banged in userland because a slot
	# is 60 us and a syscall is 1.5.
	echo "get $CC/mmb_onewire.h mmb_onewire.h"
	echo "get $CC/mmb_port.h mmb_port.h"
	echo "get $CC/mmb_pulse.h mmb_pulse.h"
	# A PAUSE that services what the two above leave running.  It
	# must be the LAST of these included, and finds them by their
	# own include guards.
	echo "get $CC/mmb_wait.h mmb_wait.h"
	# The pin and ADC REGISTERS, which mmb_gpio.h reaches for directly
	# now that pin work is not a syscall.  Flat, because that is the
	# only shape this include directory has; a native program gets the
	# same file from the C library as <sys/pc3io.h>.  One source, staged
	# twice, so the two cannot drift.
	echo "get $R/Library/include/sys/pc3io.h pc3io.h"
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
	# BASIC samples for mmbc (synced from the mmb2c repo).  These are
	# the WHOLE corpus, probes and regressions included, and they are
	# here for the board-side testing that needs them.  The curated
	# subset a user should read lives in /root/MMBasic with a README -
	# mksdimage.sh installs that from devtools/mkexamples.sh.
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
