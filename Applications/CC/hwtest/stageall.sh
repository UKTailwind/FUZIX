#!/bin/sh
#
# Strip everything mkccimage.sh installs into hwtest/<name>.s.
#
# stripall.sh does the compiler passes only, and writes .stripped names;
# mkccimage.sh wants .s and wants the whole set, including the pieces
# that live outside this directory (cpp, mmedit, and the two image
# programs from the platform's utils).  Missing one of those is not a
# build error - mkccimage refuses up front - but getting a STALE one is
# worse, so this restages the lot every time.
set -e
CC=$(cd "$(dirname "$0")/.." && pwd)
R=$(cd "$CC/../.." && pwd)
S=$CC/hwtest

# BUILD FIRST.  This script used to strip and nothing else, while its
# own comment claimed that "getting a STALE one is worse, so this
# restages the lot every time" - and restaging a stale binary is exactly
# what it did.  It shipped a v0.18 card whose bcrun had been compiled
# before MM_RELEASE was bumped, so MM.VER answered 0.17 on it: the very
# bug relcheck.sh was written to prevent, through the one door it does
# not cover.  relcheck compares the three SOURCES; the card carries a
# BINARY, and nothing tied the two together.
#
# make is a no-op when everything is current, so this costs nothing on a
# release that did run relink-userland.sh first - and saves the release
# that did not.  Objects are not deleted here: relink-userland.sh does
# that, and does it for the C library too, which is the case make cannot
# see for itself.
make -f Makefile.armm0 -C "$CC" FUZIX_ROOT="$R" USERCPU=armm0 >/dev/null
make -f Makefile.armm0 -C "$R/Applications/cpp" FUZIX_ROOT="$R" \
	USERCPU=armm0 >/dev/null
make -f Makefile.armm0 -C "$R/Applications/mmedit" FUZIX_ROOT="$R" \
	USERCPU=armm0 >/dev/null
make -C "$R/Kernel/platform/platform-rpipico/utils" FUZIX_ROOT="$R" \
	>/dev/null

# Every program staged is recorded in staged.list, and mkccimage.sh
# and verifyimage.sh install and check exactly that.  They used to keep
# hand-written lists of their own, which is how playmp3 missed a whole
# release and playmod missed the release that added PLAY MODFILE: the
# binary was built here, staged here, and installed by nobody.  The
# list is written by the thing that does the staging, so it cannot fall
# behind it.  It is a list rather than a glob over *.s because this
# directory also holds leftovers - a stale cc.s from an older native
# driver is still here, and a glob installed it OVER /usr/bin/cc.
: > "$S/staged.list"

stage() {
	[ -r "$2" ] || { echo "missing $2" >&2; exit 1; }
	arm-none-eabi-strip -o "$S/$1.s" "$2"
	echo "$1" >> "$S/staged.list"
}

for f in cc0 cc1 cc2 ccbc bcrun bcdump mmbc; do
	stage "$f" "$CC/$f"
done
stage cpp       "$R/Applications/cpp/cpp"
stage mmedit    "$R/Applications/mmedit/mmedit"
stage saveimage "$R/Kernel/platform/platform-rpipico/utils/saveimage"
stage loadimage "$R/Kernel/platform/platform-rpipico/utils/loadimage"
# loadjpg is what LOAD JPG runs - MMBasic's own picojpeg, as a
# program, on the same bargain as the image pair: a whole operation,
# so it costs a resident BASIC program nothing.
stage loadjpg   "$R/Kernel/platform/platform-rpipico/utils/loadjpg"
# loadpng is what LOAD PNG runs.  Unlike the others it needs the
# PSRAM arena: upng inflates the whole image before a pixel can be
# drawn, which is why the reference offers LOAD PNG only on rp2350.
stage loadpng   "$R/Kernel/platform/platform-rpipico/utils/loadpng"
# playmp3 is what PLAY MP3 runs, on the same terms as the image pair.
# It is the ONE program on the card built with the hardware FPU
# (utils/Makefile says why), so a rebuild that quietly dropped those
# flags would still stage, still install, and stutter - see
# PC3-MP3-PLAN.md.
stage playmp3   "$R/Kernel/platform/platform-rpipico/utils/playmp3"
# playsnd is PLAY SOUND / PLAY TONE's synthesiser, the same shape.
stage playsnd   "$R/Kernel/platform/platform-rpipico/utils/playsnd"
stage playmod   "$R/Kernel/platform/platform-rpipico/utils/playmod"
# playwav and playflac are playmp3 with a different decoder (pcmplay.h
# holds everything they share).  Soft float, unlike playmp3 - see the
# utils Makefile for why that matters.
stage playwav   "$R/Kernel/platform/platform-rpipico/utils/playwav"
stage playflac  "$R/Kernel/platform/platform-rpipico/utils/playflac"

ls -l "$S"/cc0.s "$S"/cc1.s "$S"/cc2.s "$S"/ccbc.s "$S"/bcrun.s \
      "$S"/bcdump.s "$S"/mmbc.s "$S"/cpp.s "$S"/mmedit.s \
      "$S"/saveimage.s "$S"/loadimage.s "$S"/loadjpg.s "$S"/loadpng.s "$S"/playmp3.s "$S"/playsnd.s "$S"/playmod.s
