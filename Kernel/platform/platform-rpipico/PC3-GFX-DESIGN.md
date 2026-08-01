# PC3 Fuzix Phase 5: BBC graphics modes

Design for MODE/PLOT/GCOL support in BBC BASIC, and the kernel
framebuffer interface it drives.  Companion to PC3-DEVNOTES.md.

## Targets: the original modes

    MODE  res      colours  fb
    0     640x256  2        20K     80-col text / hi-res
    1     320x256  4        20K     the games mode
    2     160x256  16       20K     full colour
    3     640x256  2        10K     (text-only rows variant: treat as 0)
    4     320x256  4        10K     as 1, half memory
    5     160x256  16       10K     as 2, half memory
    6     160x256  2        5K      (treat as 4-family)

Plus one mode that is not a BBC mode at all:

    7     320x240  16       37.5K   MMBasic's geometry

Teletext is out of scope (as in BBCSDL), so the number 7 is free and
is reused for the 320x240 4bpp mode MMBasic wants.  Documented as a
deliberate departure: anyone expecting SAA5050 will not find it.

## Two rasters, and what a mode switch costs

The monitor only ever sees the RASTER - the pixel clock, the sync
command lists and the DMA chain.  The MODE is just how core1 expands
each line into that raster, so a change of mode inside one raster
costs nothing the monitor can detect.  Two rasters, therefore:

    640x480   clk_hstx = clk_sys/3   the text console, and MODE 7
    1024x768  clk_hstx = clk_sys     BBC modes 0-5

Switching within either group is done live, during vertical blanking,
with HSTX and core1 still running: palette, LUT and framebuffer are
all made ready first and `gfx_exp` is the handover.  The monitor keeps
lock and only the picture changes.  Only 640x480 <-> 1024x768 stops
and rebuilds the scanout, and that is the only switch that resyncs.

This is MMBasic's split: `setmode()` changes DISPLAY_TYPE after a
bounded wait for the frame boundary and touches no hardware, while
`restartHDMI()` - the teardown, HSTX reset and rebuild - runs only for
a change of `Option.Resolution`.  MODE 7 was put on the console's
raster deliberately so that entering and leaving graphics from BASIC,
which is the common case, never makes the screen blink.

Blanking is ~670us at XGA and ~1.4ms at VGA; the swap needs ~80us at
worst (clear the framebuffer, rebuild the LUT), so it always lands
inside one interval.  The wait is bounded at 50ms regardless: if core1
ever stops, a mode change must not hang the caller.

## Display timing: 1024x768

256 lines x3 = 768: vertical is integer-perfect, full height.

Clocking (DECIDED, rev 2): clk_sys = 375 MHz from boot - MMBasic's
FreqXGA, the PC3-proven XGA clock (VCO 1500/4) - no dynamic clock
switching.  Graphics modes use MMBasic's exact XGA line: HSTX at
clk_sys (750 Mb/s/lane), pixel clock 75 MHz, 1328x806 frame =
1024x768 at 70.07 Hz.  The text console runs clk_hstx = clk_sys/3:
25 MHz pixel = 640x480 at 59.5 Hz (the same /3 scheme PicoMite uses
for VGA at 378).  Flash QMI 62.5 MHz (div 6), PSRAM 125 MHz (div 3),
UART/SD divisors all derive at runtime.  A mode switch tears the
scanout down in the order proven in MMBasic HDMI.c (break DMA chains,
abort together, bounded waits, THEN stop HSTX) and fully resets the
HSTX peripheral so every rebuild starts from cold-boot state.

## Horizontal scaling: use a 960-wide window

1024 divides badly (640->1.6x, 320->3.2x, 160->6.4x).  A 960-wide
centred window (32px borders) divides perfectly for the colour modes:

    320 x3 = 960    MODE 1/4: 3x3 blocks, full screen
    160 x6 = 960    MODE 2/5: 6x3 blocks, full screen

960x768 is 5:4 - within 7% of the authentic 4:3 frame.  Nobody will
see the difference; every pixel is a clean rectangle.

MODE 0/3 (640 wide) - DECIDED rev 2: full-width 5:8 upscale to 1024
exploiting the exact ratio: the source line is walked in 5-bit
groups through a 32-entry LUT that emits 8 output pixels per group.
The three fractional output pixels in each group blend the two
source colours by linear coverage (weights in fifths, applied in
RGB332 when the LUT is rebuilt on palette changes) - anti-aliased
80-column text at zero per-pixel cost.  GFX_MODE0_NEAREST selects
hard nearest-neighbour pixels at compile time instead.  With x3
vertical this is full-screen 1024x768; pixel aspect 1:1.875, close
to the authentic MODE 0 shape.

## Memory: no native framebuffer exists

A 1024x768 native framebuffer at 4bpp is 384K - impossible next to
USERMEM 320K in 520K of SRAM.  Instead the kernel keeps the BBC-RES
framebuffer (max 20K, SMALLER than the console's 45K text+tile
buffers - the two share the same allocation as a union) and core1
expands each scanline on the fly during HSTX scanout, exactly as the
text console already renders tiles:

  - output line y reads BBC line y/3 (shift/multiply, no divide)
  - a per-mode lookup table expands source bytes to RGB332 output:
    4bpp: 256-entry LUT byte -> 2 pixels -> x3 (mode 1) or x6 (mode 2)
    1bpp: byte -> 8 pixels, x1.5/x2 per the mode-0 option above
  - 16-entry logical palette -> RGB332, applied when (re)building the
    LUT, so GCOL/VDU19 palette changes are one LUT rebuild, not a
    framebuffer pass.

DECIDED: no 2bpp path at all - MODE 1/4 store 4bpp with the colour
CHOICE limited to 4, sharing the MODE 2 pipeline.  Framebuffer sizes:
mode 0: 640x256/8 = 20480; modes 1/2: 320x256/2 = 40960; mode 7:
320x240/2 = 38400, which is exactly the console's own framebuffer.
The console allocation (38400 fb + 6400 tiles = 44800 bytes) is a
union with this, so no extra SRAM is needed and the expander only
ever handles two formats.

MODE 7's expander is the cheapest of the lot: 160 source bytes, each
one LUT word, x2 across and x2 down into the 640x480 raster - 640
output pixels exactly, no borders, and no division to find the source
line (`active >> 1`).  Its 16 logical colours default to 16 DISTINCT
physical colours: 0-7 the authentic BBC set, 8-15 a darker companion
set, since a mode advertised as 16-colour should show sixteen without
the program having to set a palette up first.  The BBC modes still
reach only physicals 0-7, as on real hardware.

Line budget: 1024x768@58 = 45kHz lines = ~7000 cycles: ~6.8/pixel on
the M33 with word writes and LUTs - tight but comparable to what the
tile renderer does today, and PicoMite proves wider modes on the same
core.

## Kernel/userland interface

AS BUILT: three ioctls on /dev/sys, not a new device - the platform
already has a control device and a mode switch is not worth a second
one.  See pico_ioctl.h.

    GFXIOC_MODE  (int)  0-5 BBC mode, 7, or 0xFF back to the console
    GFXIOC_PAL   (int)  logical<<8 | physical
    GFXIOC_BLIT  (struct gfx_blit)  offset, len, buf

The app owns a shadow framebuffer in its workspace (<=40K of the
~110K), renders everything there (lines, fills, text-at-graphics-
cursor with the bbcfont), and pushes dirty byte ranges with BLIT.
A full-frame push is 20-38K = well under a millisecond; PLOT-heavy
programs push only the rows they touched.  No mmap needed, no shared
state, swap-safe by construction.

Note gfx_blit's offset and len are uint16_t, which every mode fits
inside today (the largest framebuffer is 40960).  A mode bigger than
64K would have to widen them.

While a graphics mode is active the kernel console suspends screen
rendering (kernel messages go to the serial mirror only); SETMODE
0xFF or process exit (device close) restores the text console intact.

## Drawing primitives: kernel, plus a published framebuffer address

DECIDED, on measurement (utils/syscallbench.c, run on the board):

    direct memory write    15 ns
    getpid()              597 ns
    ioctl(/dev/sys)      1488 ns

A syscall is ~100x a memory write, but the cost is per CALL, not per
pixel.  A 320-pixel line drawn directly costs 4.8us; the same line as
one ioctl costs 1.5us of overhead plus the same 4.8us of work.  So:

  - **Batched primitives are effectively free through an ioctl.**
    Rectangle, bitmap, string, line, fill.  An editor repaint at one
    call per line is 60us, against 260ms of serial mirroring.
  - Single pixels pay the whole 1488ns, so the question is only
    whether that is fast enough.

MEASURE AGAINST THE RIGHT BASELINE.  The first pass of this note
compared a syscall against a bare memory store (15ns) and concluded
the overhead was 30x the work.  That was wrong: the thing to beat is
MMBasic, and MMBasic's PIXEL statement costs **5us** at the same clock
(1000 `Pixel 100,100` statements timed on the board).  The store is
0.3% of that; the rest is interpreter overhead.  So:

    MMBasic PIXEL statement        5000 ns   full screen  384 ms
    mmbc + kernel ioctl           ~1700 ns                131 ms
    mmbc + direct store            ~215 ns                 17 ms

An ioctl per PIXEL is already ~3x faster than MMBasic.  It is not
disqualifying, and the case for handing userland a raw framebuffer
pointer evaporates with it.

So: the primitives live in the KERNEL - DrawRectangle and DrawBitmap
first, in 1-bit and 4-bit variants, imported from MMBasic (Draw.c's
DrawRectangle2/DrawBitmap2 for 1-bit, RGB121.c's DrawPixel16/
DrawBitmap16 for 4-bit).  One implementation, no copy in every
program, bounds-checked where userland cannot be trusted, dispatched
by mode, and bbcbasic can eventually drop bbcgfx.c's private pset/
line/triangle in favour of them.

**No raw framebuffer address is published.**  The remaining 8x for
bulk pixel work is reached instead by the mechanism that already
exists: render into a shadow buffer in the process's own memory
(~215ns/pixel, and it is the program's own RAM so nothing can be
corrupted) and push the dirty region with one GFXIOC_BLIT.  That is
MMBasic's own WriteBuf model, and exactly what bbcgfx.c already does.
One interface, no hole in the kernel, and faster than MMBasic on both
paths.

## Nibble order: high nibble = left pixel

DECIDED.  The scanout expander is INDIFFERENT - it does one load and
one store per source byte through a 256-entry LUT, and the nibble
order only changes how that LUT is filled, once per palette change.
There is no scanout optimisation to be had either way.

So it is decided on interchange instead.  MicroPython's framebuf
GS4_HMSB puts the even/left pixel in the HIGH nibble, and the PC3
MicroPython port unified its C side on that after exactly this
confusion caused a bug (its DEVELOPMENT_NOTES.md note 56).  Fuzix
MODE 7 already matches.  Keeping it means bitmaps, BMPs and assets are
interchangeable between the two PC3 environments.

MMBasic's RGB121 is the odd one out (low nibble = left), so the
imported primitives need a mechanical `x & 1` inversion.  That is a
contained edit in a handful of places, and much cheaper than
desynchronising the two ports.

## BBC BASIC side (Applications/bbcbasic)

The console edition has no graphics layer at all (vtint/widths etc
error out), so this is an addition, not a port: a fuzix graphics
module implementing the VDU stream (16 CLG, 18 GCOL, 19 palette,
22 MODE, 24 graphics window, 25 PLOT, 29 origin, 5/4 text routing)
with Bresenham lines, the BBC PLOT family (points, lines, triangles
- the primitives BBCSDL gets from SDL2_gfxPrimitives), flood fill
(bounded, small stack), and 8x8 font rendering for VDU5 text.
Coordinates: BBC 1280x1024 graphics units mapped to the mode's
pixel grid, origin bottom-left, as authentic.

## Plan

1. DONE  Kernel: mode-switch scaffolding in display.c (timing tables,
   HSTX clock divider, 1024x768 command lists), console
   suspend/resume.
2. DONE  Kernel: framebuffer + core1 scanline expanders, GFXIOC on
   /dev/sys.
3. DONE  App: VDU/PLOT engine rendering to the shadow fb + dirty-range
   pushes.
4. DONE  Modes 0/3, 1/4, 2/5, palette/VDU19.
5. DONE  MODE 7 (320x240x16) on the console's raster, and the split
   between a live mode change and a scanout rebuild.
6. NEXT  A C header exposing the mode switch and the drawing calls, so
   translated MMBasic (mmbc) can use them.  Open question to settle
   first: what a program should do when it selects a graphics mode
   with no HDMI attached - fail, or run blind.
7. Then: flood fill, VDU5 text, VDU24 graphics window; test with
   classic listings; measure.  Flashing colours probably never.
