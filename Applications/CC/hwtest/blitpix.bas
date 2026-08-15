' blitpix - BLIT against the real framebuffer, verified by PIXEL().
' Every check prints a mismatch count: all zeros is a pass.  The last
' line is a timing figure, not a gate.
MODE 2
CLS RGB(0,0,0)

' an 8x8 card using all 16 colours at (10,20)
FOR y% = 0 TO 7
  FOR x% = 0 TO 7
    PIXEL 10+x%, 20+y%, MAP((x% + y%*3) MOD 16)
  NEXT x%
NEXT y%
BLIT READ #1, 10, 20, 8, 8

' plain write at an odd destination: the unaligned path
f% = 0
BLIT WRITE #1, 101, 60
FOR y% = 0 TO 7
  FOR x% = 0 TO 7
    IF PIXEL(101+x%, 60+y%) <> PIXEL(10+x%, 20+y%) THEN f% = f% + 1
  NEXT x%
NEXT y%
r1% = f%

' mirrored both ways
f% = 0
BLIT WRITE #1, 120, 60, 3
FOR y% = 0 TO 7
  FOR x% = 0 TO 7
    IF PIXEL(120+x%, 60+y%) <> PIXEL(10+(7-x%), 20+(7-y%)) THEN f% = f% + 1
  NEXT x%
NEXT y%
r2% = f%

' don't-copy-black over a painted base: colour-0 pixels keep the base.
' Compare pixel against PIXEL, never against MAP(): the kernel expands
' its RGB332 palette entry where MAP() speaks RGB121, and the two agree
' only in the high bits - found on the board, the exact class of trap
' PC3-SIDE-BY-SIDE exists for.  A painted 2x2 patch supplies the base
' colour as the kernel renders it.
BOX 140, 60, 8, 8, 1, MAP(9), MAP(9)
BOX 154, 60, 2, 2, 1, MAP(9), MAP(9)
BLIT WRITE #1, 140, 60, 4
f% = 0
FOR y% = 0 TO 7
  FOR x% = 0 TO 7
    c% = (x% + y%*3) MOD 16
    IF c% = 0 THEN
      IF PIXEL(140+x%, 60+y%) <> PIXEL(154, 60) THEN f% = f% + 1
    ELSE
      IF PIXEL(140+x%, 60+y%) <> PIXEL(10+x%, 20+y%) THEN f% = f% + 1
    ENDIF
  NEXT x%
NEXT y%
r3% = f%

' screen-to-screen with overlap, on a copy so the pristine card at
' (10,20) stays the reference: place a copy, slide it right+down over
' itself, compare against the card.
BLIT WRITE #1, 200, 60
BLIT 200, 60, 203, 62, 8, 8
f% = 0
FOR y% = 0 TO 7
  FOR x% = 0 TO 7
    IF PIXEL(203+x%, 62+y%) <> PIXEL(10+x%, 20+y%) THEN f% = f% + 1
  NEXT x%
NEXT y%
r4% = f%
BLIT CLOSE #1

' timing: 100 writes of a 32x16 buffer
CLS RGB(0,0,0)
FOR y% = 0 TO 15
  FOR x% = 0 TO 31
    PIXEL x%, y%, MAP((x% + y%) MOD 16)
  NEXT x%
NEXT y%
BLIT READ #2, 0, 0, 32, 16
t! = TIMER
FOR i% = 1 TO 100
  BLIT WRITE #2, 50 + (i% MOD 100), 50 + (i% MOD 80)
NEXT i%
t1% = INT(TIMER - t!)
BLIT CLOSE #2

' MODE 1: bit-level round trip (colour is the tiles' business there)
MODE 1
f% = 0
FOR x% = 0 TO 15
  PIXEL 8+x%, 8, RGB(255,255,255)
  PIXEL 8+x%, 9, 0
NEXT x%
BLIT READ #1, 8, 8, 16, 2
BLIT WRITE #1, 40, 100
FOR x% = 0 TO 15
  IF (PIXEL(40+x%,100) <> 0) <> (PIXEL(8+x%,8) <> 0) THEN f% = f% + 1
  IF (PIXEL(40+x%,101) <> 0) <> (PIXEL(8+x%,9) <> 0) THEN f% = f% + 1
NEXT x%
r5% = f%
BLIT CLOSE #1
' back on the console: everything above printed to the HDMI screen
PRINT "write      : "; r1%
PRINT "mirror     : "; r2%
PRINT "keep black : "; r3%
PRINT "overlap    : "; r4%
PRINT "mode1      : "; r5%
PRINT "100 writes 32x16: "; t1%; " ms"
PRINT "blitpix done"
