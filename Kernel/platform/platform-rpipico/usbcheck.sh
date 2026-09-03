#!/bin/sh
#
# TinyUSB must be the version this port is written against.
#
#   sh usbcheck.sh <tinyusb-path | pico-sdk-path>
#
# The version is now PINNED, as the `tinyusb/` git submodule of this
# platform (hathach 0.21.0), and the build points PICO_TINYUSB_PATH at
# it - so `git submodule update --init` alone builds our version, and
# this check is belt-and-braces: it catches a PICO_TINYUSB_PATH aimed
# elsewhere (e.g. left at the SDK's own bundled copy) and prints the fix
# rather than failing deep in the patch step.
#
# It used to be that the version could NOT be pinned: TinyUSB came from
# the SDK's own lib/tinyusb, a submodule of a submodule whose remote we
# cannot push to, so it was a working-tree checkout no repo recorded -
# and BUILDING-PC3.md said "0.20" while the kernel built against 0.18 for
# months, every gate green, because the upgrade command named a path that
# did not exist with a local SDK.  The published v0.17 kernel was an 0.18
# build.  Owning the submodule is what fixed that; this assert remains as
# the cheap guard it always should have been alongside it.
#
# 0.21 matters for USB HOST: its RP2040/RP2350 host controller driver was
# rewritten (EPX switching on the RP2350's STOP_EPX_ON_NAK, a 300us NAK
# poll while a transfer is pending, no panic on a disconnected root port),
# and that is what keeps this machine's keyboard on the bus.  0.20 - which
# MMBasic still uses - retried a NAKing device every 16us and lost
# keyboards at boot, "panic: Invalid speed" (PC3-IRQ-REVIEW.md,
# 2026-09-01).  The kernel and MMBasic no longer share a host-stack
# version, deliberately.

WANT_MAJOR=0
WANT_MINOR=21

TU=$1
[ -n "$TU" ] || { echo "usage: usbcheck.sh <tinyusb-path | pico-sdk-path>" >&2; exit 1; }

# Accept either a TinyUSB tree (our submodule: $TU/src/tusb_option.h) or a
# pico-sdk path (the old caller: $TU/lib/tinyusb/src/tusb_option.h).
if [ -r "$TU/src/tusb_option.h" ]; then
	H=$TU/src/tusb_option.h
elif [ -r "$TU/lib/tinyusb/src/tusb_option.h" ]; then
	H=$TU/lib/tinyusb/src/tusb_option.h
else
	echo "usbcheck: no tusb_option.h under $TU" >&2
	echo "  Not a TinyUSB tree or an SDK with TinyUSB in it.  If this is the" >&2
	echo "  submodule, run: git submodule update --init" >&2
	echo "    Kernel/platform/platform-rpipico/tinyusb" >&2
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
	echo "usbcheck: TinyUSB $MAJOR.$MINOR.$REV at $H"
	exit 0
fi

echo "usbcheck: TinyUSB $MAJOR.$MINOR.$REV, wanted $WANT_MAJOR.$WANT_MINOR" >&2
echo "  at $H" >&2
echo "" >&2
echo "  This port is written against $WANT_MAJOR.$WANT_MINOR, whose USB HOST" >&2
echo "  fixes are the ones the keyboard and hub depend on.  It is pinned as" >&2
echo "  the tinyusb/ submodule; if PICO_TINYUSB_PATH is pointing somewhere" >&2
echo "  else, or the submodule is not initialised, fix it:" >&2
echo "" >&2
echo "    git submodule update --init Kernel/platform/platform-rpipico/tinyusb" >&2
exit 1
