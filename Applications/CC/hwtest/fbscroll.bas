' Scrolling in MODE 2 - the same scroll the console uses.
'
' Two halves.  First, plain PRINT past the bottom of the screen: the
' text should march down and then SCROLL, exactly as it does at the
' shell prompt, rather than wrapping to the top or doing nothing.  That
' is the console's own scroll, reached through GFXIOC_SCROLL, so there
' is one implementation for both.
'
' Then the same thing with a framebuffer selected, which is the case
' that used to be wrong: the scroll must move the OFF-SCREEN buffer,
' not the screen everybody can see.

Option Explicit
Option Default Float
Dim Integer i

Mode 2
Colour Rgb(WHITE), Rgb(BLACK)

' 30 lines on a 240-pixel screen at 12 pixels a line: 20 fit, so the
' last 10 have to scroll.
For i = 1 To 30
  Print "on screen, line"; i
Next i

Pause 1500

' Now off-screen.  Nothing should appear until the COPY, and what
' arrives should be the scrolled tail, not the first 20 lines.
FrameBuffer Create
FrameBuffer Write F
Cls
For i = 1 To 30
  Print "buffered, line"; i
Next i
FrameBuffer Copy F, N
FrameBuffer Write N

Pause 1500

FrameBuffer Close F
Mode 1
Print "done - both halves should have ended on line 30"
