#!/bin/sh
#
# The SDK's TinyUSB must be the version this port is written against.
#
#   sh usbcheck.sh <pico-sdk-path>
#
# WHY THIS CANNOT BE A GIT COMMIT, which is the obvious thing to reach
# for and does not work here:
#
#   lib/tinyusb is a submodule of pico-sdk, which is itself a submodule
#   of the micropython tree, and pico-sdk's only remote is
#   raspberrypi/pico-sdk - upstream, which we cannot push to.  Committing
#   the moved pointer inside pico-sdk makes a commit no other machine can
#   fetch, and recording THAT in the superproject leaves a fresh clone
#   unable to check the submodule out at all.  It would make
#   reproducibility worse while looking like it had fixed it.
#
# So the version is not recorded as a pointer.  It is ASSERTED, here, at
# configure time, in a file this repository does own - and the fix is
# printed rather than described.
#
# The cost of not having this: BUILDING-PC3.md said "TinyUSB must be
# 0.20" from the day it was written and the kernel was built against
# 0.18 for months, because the upgrade command it gave named a
# FetchContent path that does not exist with a local SDK.  Every gate
# was green.  The published v0.17 kernel is an 0.18 build.
#
# 0.21 matters for USB HOST: its RP2040/RP2350 host controller driver
# was rewritten (EPX switching on the RP2350's STOP_EPX_ON_NAK, a 300us
# NAK poll while a transfer is pending, no panic on a disconnected root
# port), and that is what keeps this machine's keyboard on the bus.
# 0.20 - which MMBasic still uses - retried a NAKing device every 16us
# and lost keyboards at boot, saying "panic: Invalid speed" for it
# (PC3-IRQ-REVIEW.md, 2026-09-01).  The kernel and MMBasic no longer
# share a host-stack version, deliberately.

WANT_MAJOR=0
WANT_MINOR=21

SDK=$1
[ -n "$SDK" ] || { echo "usage: usbcheck.sh <pico-sdk-path>" >&2; exit 1; }

H=$SDK/lib/tinyusb/src/tusb_option.h
if [ ! -r "$H" ]; then
	echo "usbcheck: no $H" >&2
	echo "  PICO_SDK_PATH does not look like an SDK with TinyUSB in it." >&2
	exit 1
fi

get() {
	sed -n "s/^#define TUSB_VERSION_$1[ \t]*\([0-9][0-9]*\).*/\1/p" "$H" |
		head -1
}
MAJOR=$(get MAJOR)
MINOR=$(get MINOR)
REV=$(get REVISION)

if [ "$MAJOR" = "$WANT_MAJOR" ] && [ "$MINOR" = "$WANT_MINOR" ]; then
	echo "usbcheck: TinyUSB $MAJOR.$MINOR.$REV in $SDK"
	exit 0
fi

echo "usbcheck: TinyUSB $MAJOR.$MINOR.$REV, wanted $WANT_MAJOR.$WANT_MINOR" >&2
echo "  in $SDK/lib/tinyusb" >&2
echo "" >&2
echo "  The SDK bundles $MAJOR.$MINOR; this port is written against" >&2
echo "  $WANT_MAJOR.$WANT_MINOR, whose USB HOST fixes are the ones the" >&2
echo "  keyboard and hub depend on.  Upgrade the SDK's copy in place:" >&2
echo "" >&2
echo "    git -C $SDK/lib/tinyusb fetch --depth 1 origin tag $WANT_MAJOR.$WANT_MINOR.0" >&2
echo "    git -C $SDK/lib/tinyusb checkout -f $WANT_MAJOR.$WANT_MINOR.0" >&2
echo "" >&2
echo "  It is a working-tree checkout, not something any repository here" >&2
echo "  records, so it has to be redone on a new machine or after the" >&2
echo "  SDK submodule is reset.  That is what this check is for." >&2
exit 1
