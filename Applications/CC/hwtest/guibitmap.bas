' guibitmap.bas - GUI BITMAP, and above all its BIT ORDER.
'
' Drawn into the off-screen framebuffer and read back with PIXEL, so
' this is a pixel-exact check rather than a "it did not crash".  The
' patterns are chosen so a wrong bit order cannot come out right:
' asymmetric horizontally, vertically, and between the two bytes of a
' row.

Option Explicit
' A graphics mode, or there is nothing to draw into and PIXEL reads the
' text console's own buffer back through its palette.  MODE 2 is
' 320x240 in sixteen colours - the mode these sprites were drawn for.
Mode 2
' ... and in a graphics mode PRINT goes to the screen, so the answers
' would be drawn over the picture instead of reaching the serial line.
Option Console Serial

Const FG = RGB(White), BG = RGB(Black), NONE = -1

' 16 wide x 8 high = 128 bits = 16 bytes, two bytes per row.
' Row 0 is &h80,&h01 - the leftmost and the rightmost pixel only, which
' tells bit 7 first from bit 0 first AND tells the two bytes apart.
Dim b$ = Chr$(&h80) + Chr$(&h01)
Cat b$, Chr$(&h40) + Chr$(&h02)
Cat b$, Chr$(&h20) + Chr$(&h04)
Cat b$, Chr$(&h10) + Chr$(&h08)
Cat b$, Chr$(&h08) + Chr$(&h10)
Cat b$, Chr$(&h04) + Chr$(&h20)
Cat b$, Chr$(&h02) + Chr$(&h40)
Cat b$, Chr$(&h01) + Chr$(&h80)

Dim x%, y%, row$

Cls RGB(Black)
GUI BITMAP 0, 0, b$, 16, 8, 1, FG, BG

Print "-- 16x8, a cross: bit 7 of byte 0 is the top-left pixel"
For y% = 0 To 7
  row$ = ""
  For x% = 0 To 15
    If Pixel(x%, y%) = FG Then Cat row$, "#" Else Cat row$, "."
  Next x%
  Print row$
Next y%

' The same data at scale 2: every pixel becomes a 2x2 block.
Cls RGB(Black)
GUI BITMAP 0, 0, b$, 16, 8, 2, FG, BG
Print "-- scale 2: the top-left block"
For y% = 0 To 3
  row$ = ""
  For x% = 0 To 3
    If Pixel(x%, y%) = FG Then Cat row$, "#" Else Cat row$, "."
  Next x%
  Print row$
Next y%
Print "corner far  "; Pixel(31, 15) = FG; " ";  Pixel(30, 14) = FG

' Transparent background: what was underneath must survive.
Cls RGB(Black)
Box 0, 0, 16, 8, 0, RGB(Red), RGB(Red)
GUI BITMAP 0, 0, b$, 16, 8, 1, FG, NONE
Print "-- bc = -1 leaves the background alone"
Print "set pixel   "; Pixel(0, 0) = FG
Print "clear pixel "; Pixel(1, 0) = RGB(Red)

' The default size is 8x8 and the default scale is 1 - NOT the FONT
' scale, whatever the manual says; cmd_guiMX170 sets scale = 1 and
' never reads the font.
Cls RGB(Black)
Font 1, 3
Dim s$ = Chr$(&hFF) + Chr$(&h81) + Chr$(&h81) + Chr$(&h81)
Cat s$, Chr$(&h81) + Chr$(&h81) + Chr$(&h81) + Chr$(&hFF)
GUI BITMAP 0, 0, s$, , , , FG, BG
Font 1, 1
Print "-- defaults 8x8 scale 1"
For y% = 0 To 7
  row$ = ""
  For x% = 0 To 7
    If Pixel(x%, y%) = FG Then Cat row$, "#" Else Cat row$, "."
  Next x%
  Print row$
Next y%
Print "past 8x8    "; Pixel(8, 0) = FG; " "; Pixel(0, 8) = FG

' The INTEGER form is LITTLE-ENDIAN: MMBasic passes &i64 as bytes, so
' the LOW byte is the top line.
Cls RGB(Black)
GUI BITMAP 0, 0, &hFF, 8, 8, 1, FG, BG
Print "-- integer &hFF is the TOP line, not the bottom"
Print "top row     "; Pixel(0, 0) = FG; Pixel(7, 0) = FG
Print "bottom row  "; Pixel(0, 7) = FG

Cls RGB(Black)
GUI BITMAP 0, 0, &hFF00000000000000, 8, 8, 1, FG, BG
Print "-- ... and the high byte is the bottom line"
Print "top row     "; Pixel(0, 0) = FG
Print "bottom row  "; Pixel(0, 7) = FG; Pixel(7, 7) = FG

' GUI BITMAP draws through the FRAMEBUFFER write target like every
' other graphics command - it goes across on the same two crossings
' (mm_plot / mm_fill) as BOX and LINE, and the kernel points those at
' the caller's own target before any drawing ioctl runs.  So: draw into
' the off-screen buffer and the screen must be untouched.
FrameBuffer Create
Cls RGB(Black)
FrameBuffer Write F
Cls RGB(Black)
GUI BITMAP 0, 0, b$, 16, 8, 1, FG, BG
Print "-- FRAMEBUFFER WRITE F"
Print "in the buffer  "; Pixel(0, 0) = FG
FrameBuffer Write N
Print "screen clean   "; Pixel(0, 0) = RGB(Black)
FrameBuffer Copy F, N
Print "after copy     "; Pixel(0, 0) = FG
FrameBuffer Close
