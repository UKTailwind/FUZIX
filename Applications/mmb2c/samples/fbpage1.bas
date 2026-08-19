' fbpage.bas plotting one point at a time - the comparison, and the
' shape the program had before PIXEL grew its array form.
'
' Identical in every other respect: same maths, same density, same
' palette, same double buffering, and the same silence while it draws.
' The only difference is one syscall per point against one per row.

Option Explicit
Option Default Float

Const rows = 150
Const pts  = 100

Dim r, t, u, v, x, TAU
Dim Integer i, j, hw, hh
Dim Integer pal(15)
Dim frames, ms

Mode 2

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
      Pixel hw + u * hw * 0.4, hh + v * hh * 0.4, pal(1 + ((i + j) Mod 15))
    Next j
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
