' spritepix - the sprite engine on hardware, verified by PIXEL().
' Every comparison is pixel-to-pixel (a painted reference patch or a
' BLIT copy of the pristine background) - never against MAP() or RGB(),
' whose expansions differ from the kernel's readback.  Zeros pass.
MODE 2
CLS RGB(0,0,0)

' the background card, and a pristine copy of it at (150,80)
FOR y% = 0 TO 39
  FOR x% = 0 TO 79
    PIXEL 20+x%, 20+y%, MAP((x%+y%) AND 15)
  NEXT x%
NEXT y%
BLIT READ #10, 20, 20, 80, 40
BLIT WRITE #10, 150, 80

' a magenta reference pixel
PIXEL 10, 10, RGB(MAGENTA)

' an 8x8 sprite: magenta checker, transparent (0) elsewhere
DIM a%(63)
FOR i% = 0 TO 63
  IF ((i% + i%\8) AND 1) = 1 THEN a%(i%) = RGB(MAGENTA) ELSE a%(i%) = 0
NEXT i%
SPRITE LOADARRAY #1, 8, 8, a%()

' show over the card: checker cells magenta, the rest untouched card
SPRITE SHOW #1, 30, 30, 1
f% = 0
FOR y% = 0 TO 7
  FOR x% = 0 TO 7
    i% = y%*8 + x%
    IF ((i% + i%\8) AND 1) = 1 THEN
      IF PIXEL(30+x%,30+y%) <> PIXEL(10,10) THEN f% = f% + 1
    ELSE
      IF PIXEL(30+x%,30+y%) <> PIXEL(160+x%,90+y%) THEN f% = f% + 1
    ENDIF
  NEXT x%
NEXT y%
r1% = f%

' hide restores the card exactly, everywhere
SPRITE HIDE #1
f% = 0
FOR y% = 0 TO 39
  FOR x% = 0 TO 79
    IF PIXEL(20+x%,20+y%) <> PIXEL(150+x%,80+y%) THEN f% = f% + 1
  NEXT x%
NEXT y%
r2% = f%

' two overlapping sprites; hiding the bottom one SAFEly leaves the top
SPRITE COPY #1, #2, 1
SPRITE SHOW #1, 50, 30, 1
SPRITE SHOW #2, 54, 34, 1
SPRITE HIDE SAFE #1
f% = 0
IF PIXEL(55,34) <> PIXEL(10,10) THEN f% = f% + 1
SPRITE HIDE #2
r3% = f%

' NEXT + MOVE: old spot restored, sprite appears at the new one
SPRITE SHOW #1, 30, 30, 1
SPRITE NEXT #1, 60, 40
SPRITE MOVE
f% = 0
FOR y% = 0 TO 7
  FOR x% = 0 TO 7
    IF PIXEL(30+x%,30+y%) <> PIXEL(160+x%,90+y%) THEN f% = f% + 1
  NEXT x%
NEXT y%
IF PIXEL(61,40) <> PIXEL(10,10) THEN f% = f% + 1
SPRITE HIDE #1
r4% = f%

' the card is pristine after everything
f% = 0
FOR y% = 0 TO 39
  FOR x% = 0 TO 79
    IF PIXEL(20+x%,20+y%) <> PIXEL(150+x%,80+y%) THEN f% = f% + 1
  NEXT x%
NEXT y%
r5% = f%
SPRITE CLOSE ALL
BLIT CLOSE #10

MODE 1
PRINT "show       : "; r1%
PRINT "hide       : "; r2%
PRINT "safe stack : "; r3%
PRINT "move       : "; r4%
PRINT "pristine   : "; r5%
PRINT "spritepix done"
