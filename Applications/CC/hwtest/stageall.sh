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

stage() {
	[ -r "$2" ] || { echo "missing $2" >&2; exit 1; }
	arm-none-eabi-strip -o "$S/$1.s" "$2"
}

for f in cc0 cc1 cc2 ccbc bcrun bcdump mmbc; do
	stage "$f" "$CC/$f"
done
stage cpp       "$R/Applications/cpp/cpp"
stage mmedit    "$R/Applications/mmedit/mmedit"
stage saveimage "$R/Kernel/platform/platform-rpipico/utils/saveimage"
stage loadimage "$R/Kernel/platform/platform-rpipico/utils/loadimage"
# playmp3 is what PLAY MP3 runs, on the same terms as the image pair.
# It is the ONE program on the card built with the hardware FPU
# (utils/Makefile says why), so a rebuild that quietly dropped those
# flags would still stage, still install, and stutter - see
# PC3-MP3-PLAN.md.
stage playmp3   "$R/Kernel/platform/platform-rpipico/utils/playmp3"

ls -l "$S"/cc0.s "$S"/cc1.s "$S"/cc2.s "$S"/ccbc.s "$S"/bcrun.s \
      "$S"/bcdump.s "$S"/mmbc.s "$S"/cpp.s "$S"/mmedit.s \
      "$S"/saveimage.s "$S"/loadimage.s "$S"/playmp3.s
