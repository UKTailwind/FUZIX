' FRAMEBUFFER on the PC3: what it costs, and what it is for.
'
' Needs a screen, so it is not one of the gated tests - framebuf.bas
' and fbwrite.bas check the rules on the host.  This one answers the
' question the design could not: the off-screen buffer is in PSRAM,
' which is about a quarter the speed of SRAM and does not fit the
' 16K XIP cache, so drawing into it should be measurably slower than
' drawing on the screen, and the COPY back is 38,400 bytes.  Whether
' that adds up to a usable frame rate is a measurement, not an
' argument.

Mode 2

Dim Integer i, y, c
Dim Float t0, t1, t2

Print "MODE 2: "; MM.HRes; " x "; MM.VRes

' ---- 1. drawing on the screen, and off it ---------------------------
' A full-screen fill drawn as one span per line: 240 spans, 38,400
' bytes, one ioctl each - the kernel fills whole bytes at a time, so
' this measures memory and not syscalls.

Print
Print "20 full-screen fills:"

Timer = 0
For i = 1 To 20
  c = RGB(255, 255, 255)
  If i And 1 Then c = RGB(0, 0, 128)
  For y = 0 To MM.VRes - 1
    Line 0, y, MM.HRes - 1, y, , c
  Next y
Next i
t0 = Timer

FrameBuffer Create
FrameBuffer Write F

Timer = 0
For i = 1 To 20
  c = RGB(255, 255, 255)
  If i And 1 Then c = RGB(0, 0, 128)
  For y = 0 To MM.VRes - 1
    Line 0, y, MM.HRes - 1, y, , c
  Next y
Next i
t1 = Timer

FrameBuffer Write N

Print "  screen, SRAM  "; t0; " ms total"
Print "  buffer, PSRAM "; t1; " ms total"

' ---- 2. the copy back ------------------------------------------------
' 38,400 bytes, PSRAM to SRAM.  At the 12 MB/s measured for PSRAM this
' should be around 3.2 ms, which would put a redraw-and-show frame
' somewhere near 5 ms before any drawing at all.

Print
Print "100 copies of the buffer to the screen:"
Timer = 0
For i = 1 To 100
  FrameBuffer Copy F, N
Next i
t2 = Timer
Print "  "; t2; " ms total"

' ---- 3. what it is for ----------------------------------------------
' The same animation twice.  On the screen the eye sees the erase; in
' the buffer it never does, because nothing reaches the screen until
' the whole frame is built and copied in one go - and ",B" starts that
' copy at the top of the frame.

Print
Print "200 frames on the screen, then 200 through the buffer"

FrameBuffer Write N
Timer = 0
For i = 0 To 199
  Bounce(i, 0)
Next i
t0 = Timer

FrameBuffer Write F
Timer = 0
For i = 0 To 199
  Bounce(i, 1)
Next i
t1 = Timer
FrameBuffer Write N

Print "  direct   "; t0; " ms"
Print "  buffered "; t1; " ms"

FrameBuffer Close F
Print
Print "done"
End

' One frame: erase, then a ball on a diagonal path.  Erasing the whole
' screen every frame is the point - it is what makes the direct version
' flicker.
Sub Bounce(n As Integer, buffered As Integer)
  Local Integer x, py, r
  Local Integer w, h
  w = MM.HRes
  h = MM.VRes
  r = 20
  x = r + ((n * 3) Mod (w - 2 * r))
  py = r + ((n * 2) Mod (h - 2 * r))
  For y = 0 To h - 1
    Line 0, y, w - 1, y, , RGB(0, 0, 0)
  Next y
  Circle x, py, r, 1, 1, RGB(255, 255, 0), RGB(255, 255, 0)
  If buffered Then FrameBuffer Copy F, N, B
End Sub
