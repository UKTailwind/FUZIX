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
    (MODE 7 teletext: out of scope, as in BBCSDL)

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

MODE 0/3/6 (640 wide) is the awkward one: 960/640 = 1.5 means
alternating 1- and 2-wide pixels - shimmer on exactly the fine text
these modes exist for.  Options:

  (a) pixel-perfect 640x512 centred (x1 h, x2 v): crisp, all borders,
      image is 62% x 67% of the screen.  RECOMMENDED default.
  (b) 960x768 with the 2:3 pattern: full screen, uneven pixels.
  Offer (b) later as a *command if wanted.  (The 80-column console
  already covers most text use anyway.)

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
mode 0: 640x256/8 = 20480; modes 1/2: 320x256/2 = 40960.  The console
allocation (38400 fb + 6400 tiles = 44800 bytes) is a union with
this, so no extra SRAM is needed and the expander only ever handles
two formats.

Line budget: 1024x768@58 = 45kHz lines = ~7000 cycles: ~6.8/pixel on
the M33 with word writes and LUTs - tight but comparable to what the
tile renderer does today, and PicoMite proves wider modes on the same
core.

## Kernel/userland interface

/dev/gfx (new char device):

    ioctl GFXIOC_SETMODE  (uint8_t)  0..6 BBC mode; 0xFF back to console
    ioctl GFXIOC_SETPAL   (uint16_t) logical<<8 | rgb332
    lseek/write                      raw BBC-res framebuffer bytes

The app owns a shadow framebuffer in its workspace (<=20K of the
~110K), renders everything there (lines, fills, text-at-graphics-
cursor with the bbcfont), and pushes dirty byte ranges with plain
lseek+write.  A full-frame push is 20K = well under a millisecond;
PLOT-heavy programs push only the rows they touched.  No mmap needed,
no shared state, swap-safe by construction.

While a graphics mode is active the kernel console suspends screen
rendering (kernel messages go to the serial mirror only); SETMODE
0xFF or process exit (device close) restores the text console intact.

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

1. Kernel: mode-switch scaffolding in display.c (timing tables, HSTX
   clock divider, 1024x768 command lists), console suspend/resume.
2. Kernel: BBC framebuffer + core1 scanline expanders (start MODE 1:
   320x4colours x3x3 - the easiest and the most-used games mode),
   /dev/gfx device + ioctls.
3. App: VDU/PLOT engine rendering to the shadow fb + dirty-range
   pushes.  First light: MODE 1 : GCOL 0,1 : PLOT 85 triangles.
4. Remaining modes (2/5 then 0/3/6 with option (a)), palette/VDU19,
   flood fill, VDU5 text.
5. Test with classic listings; measure; then decide if the (b)
   stretch option and flashing colours are worth it.
