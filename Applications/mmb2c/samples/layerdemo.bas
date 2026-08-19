' FRAMEBUFFER LAYER and MERGE.
'
' A layer is another off-screen framebuffer.  What makes it a layer is
' MERGE: it goes OVER the F buffer on the way to the screen, and
' wherever it holds the transparent colour, F shows through.  NEITHER
' SOURCE IS CHANGED, so the background is drawn once and only the thing
' on top is redrawn - every frame is one MERGE, not a full repaint.
'
' Both depths are exercised, because the merge keys differently in
' each and MMBasic has both:
'
'   MODE 2  320x240, 16 colours, four bits a pixel - keyed per NIBBLE,
'           transparent index 0 to 15
'   MODE 1  640x480 one bit a pixel - a pixel is a bit, so the merge is
'           a boolean: transparent 0 is an OR, transparent 1 an AND
'
' NOTHING IS PRINTED UNTIL THE END.  In a graphics mode PRINT draws the
' characters onto the screen - into whichever buffer is selected - so
' printing results while measuring would both hide them and scribble on
' the thing being measured.  MODE 1 is the console, so the report goes
' out there.
Const T = 0                             ' the transparent index
Dim integer i, x
Dim float m4, m1, td, tf, tc

' ---------- MODE 2: 320x240, four bits a pixel ----------
Mode 2
FrameBuffer Create
FrameBuffer Layer

' the background, drawn ONCE into F
FrameBuffer Write F
Cls RGB(Blue)
For i = 0 To 220 Step 20
  Line 0, i, 319, i, 1, RGB(Cyan)
Next i
Box 20, 20, 120, 80, 2, RGB(White), RGB(Red)

' something on top, in the layer
FrameBuffer Write L
Cls T
Circle 160, 120, 40, 2, 1, RGB(Yellow), RGB(Yellow)

' one merge, timed
FrameBuffer Write N
m4 = Timer
FrameBuffer Merge T
m4 = Timer - m4
Pause 1200

' the drawing alone, so the merge's share is known rather than guessed
td = Timer
For x = 40 To 280 Step 24
  FrameBuffer Write L
  Cls T
  Circle x, 120, 30, 2, 1, RGB(Yellow), RGB(Yellow)
Next x
td = Timer - td

' and the same loop with the merge in it
tf = Timer
For x = 40 To 280 Step 24
  FrameBuffer Write L
  Cls T
  Circle x, 120, 30, 2, 1, RGB(Yellow), RGB(Yellow)
  FrameBuffer Write N
  FrameBuffer Merge T
Next x
tf = Timer - tf

' a plain copy of the same 38,400 bytes, which is pass one of a merge
' and does not wait for blanking
FrameBuffer Write N
tc = Timer
For i = 1 To 11
  FrameBuffer Copy F, N
Next i
tc = Timer - tc

FrameBuffer Close L
FrameBuffer Close F

' ---------- MODE 1: 640x480, one bit a pixel ----------
Mode 1
FrameBuffer Create
FrameBuffer Layer
FrameBuffer Write F
Cls
Box 40, 40, 600, 400, 1, 1, 1
FrameBuffer Write L
Cls 0
Circle 320, 240, 120, 3, 1, 1, 1
FrameBuffer Write N
m1 = Timer
FrameBuffer Merge 0
m1 = Timer - m1
Pause 1200
FrameBuffer Close L
FrameBuffer Close F
Cls

' ---------- the report, on the console ----------
Print "FRAMEBUFFER LAYER and MERGE"
Print
Print "MODE 2  320x240 4bpp, keyed per nibble"
Print "  one merge          "; Str$(m4, 0, 2); " ms  (blanking wait + composite)"
Print "  drawing alone      "; Str$(td / 11, 0, 2); " ms a frame"
Print "  drawing + merge    "; Str$(tf / 11, 0, 2); " ms a frame"
Print "  so the merge costs "; Str$((tf - td) / 11, 0, 2); " ms a frame"
Print "  COPY F,N           "; Str$(tc / 11, 0, 2); " ms  (no wait; pass one of a merge)"
Print
Print "MODE 1  640x480 1bpp, a boolean per word"
Print "  one merge          "; Str$(m1, 0, 2); " ms"
Print
Print "both framebuffers are 38,400 bytes"
Print "done"
