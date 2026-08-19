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
# The BASIC corpus: samples/ are the board programs, tests/ the
# gate ones.  Both used to be copied flat into Applications/CC/
# hwtest, which the manifest below used to point at.
M=$(cd "$(dirname "$0")/../../../../Applications/mmb2c" && pwd)

rm -rf "$OUT"
mkdir -p "$OUT"

# name:source:one line for the README
LIST="
t1:$M/tests/t1.bas:the language itself - CONST, DIM, types, SUB and FUNCTION
bitbyte:$M/tests/bitbyte.bas:BIT, BYTE, FLAG and LMID assignment
localheap:$M/tests/localheap.bas:LOCAL arrays and strings, and where they live
varroom:$M/samples/varroom.bas:how much variable space a program has
tickpause:$M/samples/tickpause.bas:SETTICK, and whether it runs during PAUSE
pixart:$M/tests/pixart.bas:MODE, COLOUR and PIXEL - start here for graphics
palette:$M/tests/palette.bas:the sixteen colours of MODE 2
box:$M/tests/box.bas:BOX and RBOX, every argument form
circle:$M/tests/circle.bas:CIRCLE, outline and filled
tri:$M/tests/tri.bas:TRIANGLE and ARC
ellipse:$M/samples/ellipse.bas:an ellipse with a stretched aspect
polygon:$M/samples/polygon.bas:POLYGON, checked by reading the pixels back
bezier:$M/samples/bezier.bas:BEZIER curves
fill:$M/samples/fill.bas:FILL in both modes, timed
blitbench:$M/samples/blitbench.bas:the 4bpp pixel engine, path by path, in ns per pixel
xorplot:$M/samples/xorplot.bas:the XOR pattern, and what readback says about it
maptest:$M/samples/maptest.bas:MAP - changing what the sixteen colours mean
fbtext:$M/samples/fbtext.bas:PRINT @(x,y) - text on a graphics screen
fbfont:$M/samples/fbfont.bas:TEXT and the nine built-in fonts
fontaddr:$M/samples/fontaddr.bas:MM.INFO(FONT ADDRESS) and PEEK - the glyphs themselves
fbscroll:$M/samples/fbscroll.bas:scrolling a graphics screen
fbpage:$M/samples/fbpage.bas:an attractor, double buffered through FRAMEBUFFER
fborbit:$M/samples/fborbit.bas:an orbit plotted into an off-screen buffer
layerdemo:$M/samples/layerdemo.bas:FRAMEBUFFER LAYER and MERGE
bubble:$M/samples/bubble.bas:Bubble Universe - the demo, in MODE 2
sombrero:$M/tests/sombrero.bas:a plotted sombrero surface
ripple:$M/tests/ripple.bas:the hidden-line ripple surface
circrnd:$M/tests/circrnd.bas:ten thousand random filled circles, timed
spritepix:$M/samples/spritepix.bas:the SPRITE family, verified by PIXEL()
blitpix:$M/samples/blitpix.bas:BLIT against the framebuffer
scrollpix:$M/samples/scrollpix.bas:SPRITE SCROLL
brownian:$M/samples/brownian.bas:sprites colliding - Brownian motion, double buffered
flashpix:$M/samples/flashpix.bas:the flash slots BLIT can read and write
playdemo:$M/samples/playdemo.bas:PLAY SOUND and PLAY TONE
moddemo:$M/samples/moddemo.bas:PLAY MODFILE and MODSAMPLE (wants a .mod file)
saveimg:$M/tests/saveimg.bas:SAVE IMAGE, and running another program with SYSTEM
imgtrip:$M/tests/imgtrip.bas:SAVE IMAGE and LOAD IMAGE, there and back
port:$M/samples/port.bas:PORT - eight pins as one number (I/O header)
switches:$M/samples/switches.bas:check a button array on GP34-GP41 (I/O header)
pulse:$M/samples/pulse.bas:PULSE - a timed inversion on one pin (I/O header)
spi:$M/tests/spi.bas:SPI on the I/O header
tempr:$M/samples/tempr.bas:ONEWIRE and TEMPR - a DS18B20 on GP26
bmp180:$M/samples/bmp180.bas:a BMP180 pressure sensor on I2C2, over the I/O header (SETPIN + I2C2 OPEN)
bmp180q:$M/samples/bmp180q.bas:the same sensor on the QWIIC socket - the fixed bus needs no SETPIN or OPEN
i2cscan:$M/samples/i2cscan.bas:I2C CHECK over the QWIIC bus; finds the DS3231 at &h68 whatever else is fitted
alarm:$M/samples/alarm.bas:the DS3231 real time clock and its alarm
qnh:$M/samples/qnh.bas:a whole application - sensors, graphics and files
bench:$M/tests/bench.bas:KnivD's MMBasic benchmark - the number to compare
eclipse:$M/tests/solar_eclipse.bas:a solar eclipse computed from first principles
picofrog:$M/samples/picofrog.bas:a complete arcade game (keyboard: arrows, space)
vaders:$M/samples/vaders.bas:Pico-Vaders, ported from the Game*Mite (keyboard)
picoman:$M/samples/picoman.bas:PicoMan from the Game*Mite - fully native (see the manual's making-it-fit chapter)
robots:$M/samples/robots.bas:PETSCII Robots - the big one; needs the resource tree in robots/ (keyboard or switches)
"

# One entry per LINE - the descriptions have spaces in them, so the
# obvious `for e in $LIST' walks words and truncates every one of them.
printf '%s\n' "$LIST" | grep ':' > "$OUT/.manifest"

n=0
gone=""
while IFS= read -r e; do
    name=$(echo "$e" | cut -d: -f1)
    src=$(echo "$e" | cut -d: -f2)
    if [ ! -f "$src" ]; then
        gone="$gone
    $name: $src"
        continue
    fi
    cp "$src" "$OUT/$name.bas"
    n=$((n + 1))
done < "$OUT/.manifest"

# FATAL, and all of them at once.  A warning here scrolled past in a
# long build once already and the card shipped without the program:
# the image then verified clean, because it matched what was staged and
# the staging was what was short.  If an entry is deliberately optional
# it belongs outside the manifest, like the robots assets below.
if [ -n "$gone" ]; then
    echo "mkexamples: manifest entries with no file:$gone" >&2
    echo "mkexamples: fix the path or drop the entry - not building a"\
         "card that is quietly missing a program" >&2
    exit 1
fi

# PETSCII Robots needs its data beside it: levels, tile attributes,
# music, pictures and the sprite library.  A megabyte of game art does
# not belong in the kernel's git tree, so it lives with the other media
# (~/.pc3emu/sd) and is copied in here if present.
#
# LOUD if absent, never silent: a robots.bas with no robots/ directory
# is a game that starts, draws nothing and stops, which looks like a
# broken port rather than a missing asset.
ROBO=${ROBO:-$HOME/.pc3emu/sd/petrobot}
if [ -d "$ROBO" ]; then
    mkdir -p "$OUT/robots"
    for d in data images lib music; do
        if [ -d "$ROBO/$d" ]; then
            cp -r "$ROBO/$d" "$OUT/robots/$d"
        else
            echo "mkexamples: $ROBO/$d missing - robots will not run" >&2
        fi
    done
    echo "robots resources: $(find "$OUT/robots" -type f | wc -l) files"
else
    echo "mkexamples: NO $ROBO - robots.bas is shipped WITHOUT its" >&2
    echo "  resources and will not run.  Put the tree there or drop" >&2
    echo "  robots from the manifest." >&2
fi

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

robots.bas is PETSCII Robots - the largest MMBasic program here, and
the only one with data of its own.  It lives in robots/ together with
its levels, music, pictures and sprite library, and must be run FROM
THAT DIRECTORY, because it looks for them relative to wherever it is
started:

    cd /root/MMBasic/robots
    mmbc robots.bas
    cc robots.c
    ./robots.bc

It takes a few minutes to compile.  Play it with the arrow keys and
space, or with a switch array on GP34-GP41 (the Game*Mite layout).
TAIL
} > "$OUT/README"

# robots.bas belongs INSIDE robots/, because path$() resolves its
# levels, music and pictures against the CURRENT directory - so the
# program and its data have to sit together.  Moved after the README is
# written so the manifest still describes it in the program list.
if [ -d "$OUT/robots" ] && [ -f "$OUT/robots.bas" ]; then
    mv "$OUT/robots.bas" "$OUT/robots/robots.bas"
fi

rm -f "$OUT/.manifest"
cd "$(dirname "$OUT")"
tar cf "$(basename "$OUT").tar" "$(basename "$OUT")"
echo "$n programs -> $OUT"
echo "tar: $(dirname "$OUT")/$(basename "$OUT").tar ($(stat -c %s "$(dirname "$OUT")/$(basename "$OUT").tar") bytes)"
