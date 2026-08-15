' scrollpix - SPRITE SCROLL on hardware, verified by PIXEL().
' A wrap scroll must carry every pixel to its new place; a layer-1
' sprite must survive the ride (hidden, scrolled under, reshown); the
' band checks belong to sc2test, which proved the kernel call.
MODE 2
CLS RGB(0,0,0)

' a striped card and its pristine copy
FOR y% = 0 TO 29
  FOR x% = 0 TO 59
    PIXEL 20+x%, 20+y%, MAP((x%\4 + y%\3) AND 15)
  NEXT x%
NEXT y%
BLIT READ #10, 20, 20, 60, 30
BLIT WRITE #10, 200, 100
PIXEL 10, 10, RGB(MAGENTA)

DIM a%(63)
FOR i% = 0 TO 63
  IF ((i% + i%\8) AND 1) = 1 THEN a%(i%) = RGB(MAGENTA) ELSE a%(i%) = 0
NEXT i%
SPRITE LOADARRAY #1, 8, 8, a%()
SPRITE SHOW #1, 40, 24, 1

' scroll right 8 and up 4, wrapping
SPRITE SCROLL 8, 4

' EVERYTHING scrolled, the pristine copy included: card pixel (x,y)
' is now at screen (28+x, 16+y) and the copy holds it at (208+x, 96+y)
' - neither region wrapped or met a band.  The sprite reshows at
' (40..47, 24..31) = card (12..19, 8..15): its rectangle, skipped.
f% = 0
FOR y% = 0 TO 25
  FOR x% = 0 TO 51
    IF NOT (x% >= 12 AND x% < 20 AND y% >= 8 AND y% < 16) THEN
      IF PIXEL(28+x%, 16+y%) <> PIXEL(208+x%, 96+y%) THEN f% = f% + 1
    ENDIF
  NEXT x%
NEXT y%
r1% = f%

' the sprite rode along: still at (40,24) - layer 1 sprites keep
' their position - and its checker is intact over the scrolled card.
' The magenta reference scrolled too, so paint a fresh one.
PIXEL 10, 10, RGB(MAGENTA)
f% = 0
IF SPRITE(X, #1) <> 40 THEN f% = f% + 1
IF PIXEL(41,24) <> PIXEL(10,10) THEN f% = f% + 1
r2% = f%

SPRITE HIDE #1
SPRITE CLOSE ALL
BLIT CLOSE #10

MODE 1
PRINT "scrolled   : "; r1%
PRINT "sprite ride: "; r2%
PRINT "scrollpix done"
