#!/bin/sh
#
# The master copies of the mm runtime live in this repo.  bcrun's
# hosted build (FUZIX Applications/CC, bcrun_mm.c) compiles verbatim
# copies of them; re-run this after editing mmb_runtime.c or
# mmb_runtime.h so the two trees stay identical.

M=$(cd "$(dirname "$0")/.." && pwd)
FCC=${FCC:-/home/peter/src/FUZIX/Applications/CC}

cp "$M/mmb_runtime.c" "$M/mmb_runtime.h" \
   "$M/mmb_gfx.h" "$M/mmb_gfx_pts.h" "$M/mmb_gfx_circle.h" \
   "$M/mmb_gfx_text.h" "$M/mmb_gfx_map.h" "$M/mmb_gpio.h" "$FCC/"
echo "synced mmb_runtime.[ch] mmb_gfx*.h mmb_gpio.h -> $FCC"
