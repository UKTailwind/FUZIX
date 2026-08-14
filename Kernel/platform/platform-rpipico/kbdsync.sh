#!/bin/sh
# The HID keyboard decoder core is vendored BYTE-IDENTICAL in two trees
# (see kbd_decode.h): this platform, and micropython/ports/rp2.  Platform
# differences live in the backend files (usbkbd.c here, kbd_backend.c
# there), never in these three.  This says whether the copies have
# drifted; run it before tagging a release, like relcheck.sh.
#
#   sh kbdsync.sh              # compares against ~/src/micropython
#   MP=/path/to/rp2 sh kbdsync.sh
here=$(dirname "$0")
MP=${MP:-$HOME/src/micropython/ports/rp2}
rc=0
for f in kbd_decode.c kbd_decode.h keyboard_maps.h; do
    a=$(sha256sum "$here/$f" | cut -d' ' -f1)
    b=$(sha256sum "$MP/$f" | cut -d' ' -f1)
    if [ "$a" = "$b" ]; then
        echo "ok    $f"
    else
        echo "DRIFT $f"
        rc=1
    fi
done
exit $rc
