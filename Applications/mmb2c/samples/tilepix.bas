' tilepix - TILEMAP on hardware: a tileset built from BASIC (PicoMite
' packing: low nibble is the left pixel), a map out of DATA, drawn and
' verified by PIXEL readback against the same picture painted by hand
' with PIXEL - readback against readback, as flashpix does, so the
' palette is out of the question.  Zeros are a pass.  Covers TILEMAP
' DRAW plain and transparent, the sub-tile offset, tile 0 as empty,
' the clip at the screen edge, and TILEMAP SPRITE DRAW.
MODE 2
CLS RGB(0,0,0)

' the tileset file: 12x4, three 4x4 tiles in a row, 8-byte header.
' tile t pixel (x,y) = (t-1)*4 + x + y (AND 15), so no two tiles look
' alike and colour 0 appears only in tile 1 at (0,0) - the
' transparency test.
OPEN "timg.bin" FOR OUTPUT AS #1
PRINT #1, CHR$(12); CHR$(0); CHR$(0); CHR$(0);
PRINT #1, CHR$(4); CHR$(0); CHR$(0); CHR$(0);
FOR y% = 0 TO 3
  FOR x% = 0 TO 11 STEP 2
    b% = TilePx(x%, y%) + TilePx(x% + 1, y%) * 16
    PRINT #1, CHR$(b%);
  NEXT x%
NEXT y%
CLOSE #1

FLASH ERASE 1
FLASH DISK LOAD 1, "timg.bin", O
KILL "timg.bin"

' a 4x3 map; 0 is empty and must leave the background alone
TILEMAP CREATE mapdata, 1, 1, 4, 4, 3, 4, 3

' the drawn picture goes at (sx, sy); the hand-painted one at
' (sx, sy + 60), both over a blue ground
' ---- 1: plain draw, whole map ----
BOX 40, 40, 16, 12, 0, RGB(0,0,255), RGB(0,0,255)
TILEMAP DRAW 1, N, 0, 0, 40, 40, 16, 12
r1% = Compare(40, 40, 0, 0, -1)

' ---- 2: transparent 0: tile 1's (0,0) shows the ground ----
BOX 80, 40, 16, 12, 0, RGB(0,0,255), RGB(0,0,255)
TILEMAP DRAW 1, N, 0, 0, 80, 40, 16, 12, 0
r2% = Compare(80, 40, 0, 0, 0)

' ---- 3: a viewport 2,1 into the world: sub-tile offset ----
BOX 118, 38, 20, 16, 0, RGB(0,0,255), RGB(0,0,255)
TILEMAP DRAW 1, N, 2, 1, 120, 40, 14, 11
r3% = Compare(120, 40, 2, 1, -1)

' ---- 4: the clip: draw off the bottom-right corner ----
BOX 304, 226, 16, 14, 0, RGB(0,0,255), RGB(0,0,255)
TILEMAP DRAW 1, N, 0, 0, 310, 232, 16, 12
r4% = Compare(310, 232, 0, 0, -1)

' ---- 5: sprites: tile 2 at (200,100), tile 3 half off the left ----
BOX 198, 98, 8, 8, 0, RGB(0,0,255), RGB(0,0,255)
BOX 0, 198, 8, 8, 0, RGB(0,0,255), RGB(0,0,255)
TILEMAP SPRITE CREATE 1, 1, 2, 200, 100
TILEMAP SPRITE CREATE 2, 1, 3, -2, 200
TILEMAP SPRITE DRAW N, -1
' the same two by hand at (200,160) and (0,140)
BOX 198, 158, 8, 8, 0, RGB(0,0,255), RGB(0,0,255)
BOX 0, 138, 8, 8, 0, RGB(0,0,255), RGB(0,0,255)
FOR y% = 0 TO 3
  FOR x% = 0 TO 3
    PIXEL 200+x%, 160+y%, MAP(TilePx(4+x%, y%))
  NEXT x%
  FOR x% = 2 TO 3
    PIXEL x%-2, 140+y%, MAP(TilePx(8+x%, y%))
  NEXT x%
NEXT y%
f% = 0
FOR y% = 0 TO 3
  FOR x% = 0 TO 3
    IF PIXEL(200+x%, 100+y%) <> PIXEL(200+x%, 160+y%) THEN f% = f% + 1
  NEXT x%
  FOR x% = 0 TO 1
    IF PIXEL(x%, 200+y%) <> PIXEL(x%, 140+y%) THEN f% = f% + 1
  NEXT x%
NEXT y%
r5% = f%

TILEMAP CLOSE
MODE 1
PRINT "draw plain   : "; r1%
PRINT "draw transp  : "; r2%
PRINT "draw offset  : "; r3%
PRINT "draw clipped : "; r4%
PRINT "sprites      : "; r5%
PRINT "tilepix done"
END

' the tileset's own pixel: tile column x\4, pixel (x AND 3, y)
FUNCTION TilePx(x%, y%)
  TilePx = ((x% \ 4) * 4 + (x% AND 3) + y%) AND 15
END FUNCTION

' Paint by hand what TILEMAP DRAW should have drawn from world offset
' (vx,vy) at (sx,sy) - 16x12 pixels over blue ground, clipped to the
' screen - at (sx, sy+60), and count the pixels that differ.  t < 0:
' opaque.  A pixel the draw could not reach (off screen) is skipped.
FUNCTION Compare(sx%, sy%, vx%, vy%, t%)
  LOCAL x%, y%, wx%, wy%, c%, r%, tile%, f%, want%, rx%, ry%
  rx% = sx% : ry% = sy% + 60
  IF ry% + 12 > MM.VRES THEN ry% = sy% - 60
  IF rx% + 16 > MM.HRES THEN rx% = sx% - 60
  BOX rx%, ry%, 16, 12, 0, RGB(0,0,255), RGB(0,0,255)
  FOR y% = 0 TO 11
    FOR x% = 0 TO 15
      wx% = vx% + x% : wy% = vy% + y%
      c% = wx% \ 4 : r% = wy% \ 4
      want% = -1
      IF c% < 4 AND r% < 3 THEN
        tile% = MapAt(c%, r%)
        IF tile% > 0 THEN
          want% = TilePx((tile%-1)*4 + (wx% AND 3), wy% AND 3)
          IF want% = t% THEN want% = -1
        END IF
      END IF
      IF want% >= 0 THEN PIXEL rx%+x%, ry%+y%, MAP(want%)
    NEXT x%
  NEXT y%
  f% = 0
  FOR y% = 0 TO 11
    FOR x% = 0 TO 15
      IF sx%+x% >= MM.HRES OR sy%+y% >= MM.VRES THEN CONTINUE FOR
      IF PIXEL(sx%+x%, sy%+y%) <> PIXEL(rx%+x%, ry%+y%) THEN f% = f% + 1
    NEXT x%
  NEXT y%
  Compare = f%
END FUNCTION

FUNCTION MapAt(c%, r%)
  LOCAL i%, v%
  RESTORE mapdata
  FOR i% = 0 TO c% + r% * 4
    READ v%
  NEXT i%
  MapAt = v%
END FUNCTION

mapdata:
DATA 1, 2, 3, 0
DATA 0, 3, 1, 2
DATA 2, 0, 0, 1
