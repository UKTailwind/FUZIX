' Which half is wrong: the drawing, the copy, or the colour?
'
' PIXEL() reads back through the SAME target that PIXEL writes to, so
' with WRITE F it reads the off-screen buffer and with WRITE N the
' screen.  That separates three things a blank screen cannot: whether
' the pixels land, whether COPY moves them, and what colour they
' turned into.
'
' Nothing is printed until the end.  In MODE 2 the console draws into
' the SCREEN framebuffer, so a PRINT in the middle would scribble over
' the very pixels the second half reads back.

OPTION EXPLICIT
DIM INTEGER i
DIM INTEGER c(7), gotf(7), gotn(7)
DIM INTEGER px(7), py(7)

c(0) = RGB(255, 255, 255)
c(1) = RGB(255, 0, 0)
c(2) = RGB(0, 255, 0)
c(3) = RGB(0, 0, 255)
c(4) = RGB(255, 255, 0)
c(5) = RGB(200, 200, 99)
c(6) = RGB(128, 128, 99)
c(7) = RGB(0, 0, 99)

MODE 2
FRAMEBUFFER CREATE
FRAMEBUFFER WRITE F

' eight pixels, well apart, one colour each
FOR i = 0 TO 7
  px(i) = 20 + i * 20
  py(i) = 40
  PIXEL px(i), py(i), c(i)
NEXT i

' the same eight colours as one-pixel-high LINEs, which the runtime
' turns into a filled rectangle - a different kernel call entirely
FOR i = 0 TO 7
  LINE 10, 80 + i * 5, 200, 80 + i * 5, , c(i)
NEXT i

' read the buffer back before anything is copied or printed
FOR i = 0 TO 7
  gotf(i) = PIXEL(px(i), py(i))
NEXT i

FRAMEBUFFER COPY F, N
FRAMEBUFFER WRITE N

FOR i = 0 TO 7
  gotn(i) = PIXEL(px(i), py(i))
NEXT i

' and the lines, from the screen
DIM INTEGER gotl(7)
FRAMEBUFFER WRITE F
FOR i = 0 TO 7
  gotl(i) = PIXEL(100, 80 + i * 5)
NEXT i
FRAMEBUFFER WRITE N

MODE 1
PRINT "        asked       pixel in F   after COPY    line in F"
FOR i = 0 TO 7
  PRINT i; "  "; HEX$(c(i), 6); "      "; HEX$(gotf(i), 6);
  PRINT "       "; HEX$(gotn(i), 6); "       "; HEX$(gotl(i), 6)
NEXT i
