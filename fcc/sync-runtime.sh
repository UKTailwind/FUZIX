#!/bin/sh
#
# The master copies of the mm runtime live in this repo.  bcrun's
# hosted build (FUZIX Applications/CC, bcrun_mm.c) compiles verbatim
# copies of them; re-run this after editing mmb_runtime.c or
# mmb_runtime.h so the two trees stay identical.

M=$(cd "$(dirname "$0")/.." && pwd)
FCC=${FCC:-/home/peter/src/FUZIX/Applications/CC}

# Every header the on-board cc is given must be here.  mmb_int.h,
# mmb_pwm.h and mmb_i2c.h were each added to the card image and to
# hwbuild without being added HERE, so the two trees held different
# copies and an edit in this repo simply did not reach the board.
cp "$M/mmb_runtime.c" "$M/mmb_runtime.h" \
   "$M/mmb_gfx.h" "$M/mmb_gfx_pts.h" "$M/mmb_gfx_circle.h" \
   "$M/mmb_gfx_polygon.h" "$M/mmb_gfx_bezier.h" "$M/mmb_gfx_fill.h" \
   "$M/mmb_gfx_box.h" "$M/mmb_gfx_rbox.h" "$M/mmb_gfx_triangle.h" \
   "$M/mmb_gfx_arc.h" \
   "$M/mmb_gfx_text.h" "$M/mmb_gfx_map.h" "$M/mmb_gpio.h" \
   "$M/mmb_int.h" "$M/mmb_pwm.h" "$M/mmb_comms.h" "$M/mmb_i2c.h" \
   "$M/mmb_spi.h" \
   "$M/mmb_peek.h" "$M/mmb_port.h" "$M/mmb_pulse.h" "$M/mmb_wait.h" \
   "$FCC/"
echo "synced mmb_runtime.[ch] mmb_gfx*.h mmb_gpio.h mmb_int.h mmb_pwm.h mmb_comms.h mmb_i2c.h mmb_spi.h mmb_peek.h mmb_port.h mmb_pulse.h mmb_wait.h -> $FCC"
