#!/bin/sh
#
# Build the /root/MMBasic examples directory as a tar, ready to send to
# the board and unpack there.
#
#   sh mkexamples.sh [outdir]      default /tmp/mmbasic
#
# The manifest below is the point of this script: it says which of the
# BASIC programs in the tree are worth a user's time and what each one
# shows.  Everything here runs on the machine as it ships - the few
# that need something plugged in say so, in the README and here.
#
# Deliberately NOT included: the probes and bisection programs
# (fbprobe, imgloop, heaptest, forktest, bshort ...).  They exist to
# answer a question that has been answered, they print things like
# "0 mismatches", and a user reading them learns nothing about MMBasic.

set -e
OUT=${1:-/tmp/mmbasic}
M=/home/peter/src/mmb2c
H=/home/peter/src/FUZIX/Applications/CC/hwtest

rm -rf "$OUT"
mkdir -p "$OUT"

# name:source:one line for the README
LIST="
t1:$H/t1.bas:the language itself - CONST, DIM, types, SUB and FUNCTION
bitbyte:$H/bitbyte.bas:BIT, BYTE, FLAG and LMID assignment
localheap:$H/localheap.bas:LOCAL arrays and strings, and where they live
varroom:$H/varroom.bas:how much variable space a program has
tickpause:$H/tickpause.bas:SETTICK, and whether it runs during PAUSE
pixart:$H/pixart.bas:MODE, COLOUR and PIXEL - start here for graphics
palette:$H/palette.bas:the sixteen colours of MODE 2
box:$H/box.bas:BOX and RBOX, every argument form
circle:$H/circle.bas:CIRCLE, outline and filled
tri:$H/tri.bas:TRIANGLE and ARC
ellipse:$H/ellipse.bas:an ellipse with a stretched aspect
polygon:$M/samples/polygon.bas:POLYGON, checked by reading the pixels back
bezier:$M/samples/bezier.bas:BEZIER curves
fill:$M/samples/fill.bas:FILL in both modes, timed
xorplot:$M/samples/xorplot.bas:the XOR pattern, and what readback says about it
maptest:$H/maptest.bas:MAP - changing what the sixteen colours mean
fbtext:$H/fbtext.bas:PRINT @(x,y) - text on a graphics screen
fbfont:$H/fbfont.bas:TEXT and the nine built-in fonts
fontaddr:$H/fontaddr.bas:MM.INFO(FONT ADDRESS) and PEEK - the glyphs themselves
fbscroll:$H/fbscroll.bas:scrolling a graphics screen
fbpage:$H/fbpage.bas:an attractor, double buffered through FRAMEBUFFER
fborbit:$H/fborbit.bas:an orbit plotted into an off-screen buffer
layerdemo:$H/layerdemo.bas:FRAMEBUFFER LAYER and MERGE
bubble:$H/bubble.bas:Bubble Universe - the demo, in MODE 2
sombrero:$H/sombrero.bas:a plotted sombrero surface
ripple:$H/ripple.bas:the hidden-line ripple surface
circrnd:$H/circrnd.bas:ten thousand random filled circles, timed
spritepix:$M/samples/spritepix.bas:the SPRITE family, verified by PIXEL()
blitpix:$M/samples/blitpix.bas:BLIT against the framebuffer
scrollpix:$M/samples/scrollpix.bas:SPRITE SCROLL
brownian:$M/samples/brownian.bas:sprites colliding - Brownian motion, double buffered
flashpix:$M/samples/flashpix.bas:the flash slots BLIT can read and write
playdemo:$M/samples/playdemo.bas:PLAY SOUND and PLAY TONE
moddemo:$M/samples/moddemo.bas:PLAY MODFILE and MODSAMPLE (wants a .mod file)
saveimg:$H/saveimg.bas:SAVE IMAGE, and running another program with SYSTEM
imgtrip:$H/imgtrip.bas:SAVE IMAGE and LOAD IMAGE, there and back
port:$M/samples/port.bas:PORT - eight pins as one number (I/O header)
switches:$M/samples/switches.bas:check a button array on GP34-GP41 (I/O header)
pulse:$M/samples/pulse.bas:PULSE - a timed inversion on one pin (I/O header)
spi:$H/spi.bas:SPI on the I/O header
tempr:$M/samples/tempr.bas:ONEWIRE and TEMPR - a DS18B20 on GP26
bmp180:$H/bmp180.bas:a BMP180 pressure sensor on I2C2, over the I/O header (SETPIN + I2C2 OPEN)
bmp180q:$M/samples/bmp180q.bas:the same sensor on the QWIIC socket - the fixed bus needs no SETPIN or OPEN
i2cscan:$M/samples/i2cscan.bas:I2C CHECK over the QWIIC bus; finds the DS3231 at &h68 whatever else is fitted
alarm:$H/alarm.bas:the DS3231 real time clock and its alarm
qnh:$M/samples/qnh.bas:a whole application - sensors, graphics and files
bench:$M/tests/bench.bas:KnivD's MMBasic benchmark - the number to compare
eclipse:$H/solar_eclipse.bas:a solar eclipse computed from first principles
picofrog:$M/samples/picofrog.bas:a complete arcade game (keyboard: arrows, space)
vaders:$M/samples/vaders.bas:Pico-Vaders, ported from the Game*Mite (keyboard)
picoman:$M/samples/picoman.bas:PicoMan from the Game*Mite - fully native (see the manual's making-it-fit chapter)
"

# One entry per LINE - the descriptions have spaces in them, so the
# obvious `for e in $LIST' walks words and truncates every one of them.
printf '%s\n' "$LIST" | grep ':' > "$OUT/.manifest"

n=0
while IFS= read -r e; do
    name=$(echo "$e" | cut -d: -f1)
    src=$(echo "$e" | cut -d: -f2)
    if [ ! -f "$src" ]; then
        echo "missing: $src" >&2
        continue
    fi
    cp "$src" "$OUT/$name.bas"
    n=$((n + 1))
done < "$OUT/.manifest"

# The README, built from the same manifest so the two cannot drift.
{
    cat <<'HEAD'
MMBasic examples for the Pico Computer 3
========================================

Each of these is a working MMBasic program.  To run one:

    mmbc pixart.bas -o pixart.c     translate it to C
    cc pixart.c -o pixart.bc        compile that
    bcrun pixart.bc                 run it

The .bc keeps its "#!" line, so once built it also runs as ./pixart.bc
on its own.  A big program takes a while in cc - eclipse and picofrog
are minutes, not seconds.

Most of them draw on the screen rather than printing, so watch the
display and not the serial console.  A few want hardware plugged in
and say so below; they fail politely if it is not there.

HEAD
    while IFS= read -r e; do
        name=$(echo "$e" | cut -d: -f1)
        desc=$(echo "$e" | cut -d: -f3-)
        [ -f "$OUT/$name.bas" ] || continue
        printf '  %-14s %s\n' "$name.bas" "$desc"
    done < "$OUT/.manifest"
    cat <<'TAIL'

Where to start
--------------
  t1.bas        if you want the language
  pixart.bas    if you want the screen
  playdemo.bas  if you want noise
  picofrog.bas  if you want to play something

picofrog is a port of Martin Herhaus's PicoMite game; the header of
the file lists every change the port made and why.
TAIL
} > "$OUT/README"

rm -f "$OUT/.manifest"
cd "$(dirname "$OUT")"
tar cf "$(basename "$OUT").tar" "$(basename "$OUT")"
echo "$n programs -> $OUT"
echo "tar: $(dirname "$OUT")/$(basename "$OUT").tar ($(stat -c %s "$(dirname "$OUT")/$(basename "$OUT").tar") bytes)"
