' A plotted attractor, double buffered, one PIXEL call per row of points.
'
' Three things this is a demonstration of:
'
' COLOUR.  MODE 2 has sixteen colours and they are MMBasic's RGB121 set
' - one bit of red, two of green, one of blue - so R and B are only ever
' 0 or 255 and G has four levels.  Anything else is mapped to the
' nearest of those.  The original of this program asked for
' RGB(r, g, 99), and a blue of 99 is nearer 0 than 255 every time, as is
' a red below 128; a large part of the colour space it thought it was
' using collapsed onto black, which against a black background is
' nothing at all.  It looked fine in MODE 1 because with two colours
' anything not black is white.  So the palette below is built from what
' the mode actually has, and index 0 - the black one - is never used.
'
' SPEED.  Plotting point by point costs a syscall per point, 1.3us
' against 15ns to store the pixel, so nearly all of the time goes on
' crossing into the kernel.  MMBasic's array form of PIXEL takes a whole
' run in one call; here that is one call per row rather than one per
' point.  fbpage1.bas is the same program plotting singly, to compare.
'
' AND NOTHING IS PRINTED WHILE IT DRAWS.  In MODE 2 a program's PRINT
' still goes to the console, and the console draws into the SCREEN
' framebuffer - so a progress line per frame puts a cursor on the
' picture and scrolls the whole display out from under it.  The timing
' is reported at the end, after MODE 1.

Option Explicit
Option Default Float

' The two density knobs.  rows is how many orbits are drawn and pts how
' many points each contributes; 320x240 is 76,800 pixels, so the whole
' picture wants to stay well under that or it fills in solid.
Const rows = 150
Const pts  = 100

Dim r, t, u, v, x, TAU
Dim Integer i, j, hw, hh
Dim Integer px(pts), py(pts), pc(pts)
Dim Integer pal(15)
Dim frames, ms

Mode 2

' The sixteen colours MODE 2 really has: bit 3 red, bits 2-1 green,
' bit 0 blue.  Index 0 is black and is deliberately never plotted.
For i = 0 To 15
  pal(i) = Rgb(((i \ 8) And 1) * 255, ((i \ 2) And 3) * 85, (i And 1) * 255)
Next i

TAU = 6.283185307179586
r = TAU / 300
hw = MM.HRes / 2
hh = MM.VRes / 2

FrameBuffer Create
FrameBuffer Write F

frames = 0
Timer = 0
Do
  Cls
  For i = 0 To rows
    For j = 0 To pts
      u = Sin(i + v) + Sin(r * i + x)
      v = Cos(i + v) + Cos(r * i + x)
      x = u + t
      px(j) = hw + u * hw * 0.4
      py(j) = hh + v * hh * 0.4
      pc(j) = pal(1 + ((i + j) Mod 15))
    Next j
    Pixel px(), py(), pc()
  Next i
  t = t + 0.01
  FrameBuffer Copy F, N
  frames = frames + 1
  ms = Timer
Loop Until Inkey$ <> ""

FrameBuffer Close F
Mode 1
Print frames; " frames, "; ms / frames; " ms each, ";
Print (rows + 1) * (pts + 1); " points per frame"
