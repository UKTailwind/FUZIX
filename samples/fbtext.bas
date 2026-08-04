' PRINT @(x, y) - text on graphics, in place, with nothing scrolling.
'
' The thing this is here to show: the counter is redrawn every frame at
' the same spot, over a moving picture, and the picture does not move
' underneath it.  Before, a PRINT in MODE 2 went to the console, the
' console drew into the SCREEN framebuffer, and a wrapped line scrolled
' the whole display - so a counter like this was unusable.
'
' Now PRINT draws glyphs through whatever is being drawn on, as MMBasic
' does, so the text goes into the off-screen buffer with everything else
' and arrives on the screen with the frame.

Option Explicit
Option Default Float

Dim Integer i, frames
Dim t, ms

Mode 2
FrameBuffer Create
FrameBuffer Write F

Colour Rgb(WHITE), Rgb(BLACK)

frames = 0
Timer = 0
Do
  Cls
  ' something moving, so it is obvious the text is not scrolling it
  For i = 0 To 15
    Circle 160 + 90 * Cos(i / 2.4 + frames / 20.0), 120 + 70 * Sin(i / 2.4 + frames / 20.0), 6, , , Rgb(255, (i * 17) And 255, 0)
  Next i

  t = Timer
  Print @(0, 0) "frame "; frames
  Print @(0, 14) "ms    "; Int(t)
  Print @(0, 28) "per fr"; Int(t / (frames + 1))
  ' mode 2 = swapped ink and paper, so this one is a label in reverse
  Print @(160, 226, 2) " PRINT @ "

  FrameBuffer Copy F, N
  frames = frames + 1
Loop Until Inkey$ <> "" Or frames = 600

FrameBuffer Close F
Mode 1
Print frames; " frames in "; Int(t); " ms"
