' TILEMAP under the gates: the host has no display, so DRAW is silent
' (the same silence as every drawing primitive) and what this checks is
' everything else - the map and attribute tables read out of DATA with
' the program's own READ position left alone, every query, the viewport
' and its clamp, the sprites, and every error path.  The pixels are the
' C harness's (blitharness) and the board's business (samples/tilepix).
MODE 2

' a tileset for the slot: 32x8, four 8x8 tiles in a row, in the
' PicoMite's packing (low nibble the left pixel).  Its pixels do not
' matter here; its header does.
OPEN "tiles.bin" FOR OUTPUT AS #1
PRINT #1, CHR$(32); CHR$(0); CHR$(0); CHR$(0);
PRINT #1, CHR$(8); CHR$(0); CHR$(0); CHR$(0);
FOR i% = 1 TO 16 * 8
  PRINT #1, CHR$(&H21);
NEXT i%
CLOSE #1
FLASH ERASE 1
FLASH DISK LOAD 1, "tiles.bin"
KILL "tiles.bin"

' a 5x3 map and four tile attributes, both from labels
TILEMAP CREATE mapdata, 1, 1, 8, 8, 4, 5, 3
TILEMAP ATTR attrs, 1, 4
PRINT "size: " TILEMAP(COLS 1) TILEMAP(ROWS 1)
PRINT "tile: " TILEMAP(TILE 1, 0, 0) TILEMAP(TILE 1, 12, 9) TILEMAP(TILE 1, 39, 23)
PRINT "off map: " TILEMAP(TILE 1, 40, 0) TILEMAP(TILE 1, 0, 24)
PRINT "attr: " TILEMAP(ATTR 1, 1) TILEMAP(ATTR 1, 2) TILEMAP(ATTR 1, 3) TILEMAP(ATTR 1, 4) TILEMAP(ATTR 1, 5)
PRINT "collision: " TILEMAP(COLLISION 1, 0, 0, 40, 24) TILEMAP(COLLISION 1, 0, 0, 40, 24, 2) TILEMAP(COLLISION 1, 8, 16, 24, 8)
PRINT "corner: " TILEMAP(COLLISION 1, 30, 20, 8, 8) TILEMAP(COLLISION 1, 30, 20, 8, 8, 1)

' SET changes the map under the queries
TILEMAP SET 1, 0, 0, 3
PRINT "set: " TILEMAP(TILE 1, 3, 3) TILEMAP(COLLISION 1, 0, 0, 8, 8)

' the program's own READ starts where it always did
READ a%, b%
PRINT "read: " a% b%

' VIEW is absolute; SCROLL is relative and clamps at 0, and at the
' world's far edge only when the world is bigger than the screen (this
' one is 40x24, so there is no far edge to hit)
TILEMAP VIEW 1, 5, 6
PRINT "view: " TILEMAP(VIEWX 1) TILEMAP(VIEWY 1)
TILEMAP SCROLL 1, -100, -100
PRINT "floor: " TILEMAP(VIEWX 1) TILEMAP(VIEWY 1)
TILEMAP SCROLL 1, 1000, 1000
PRINT "no ceiling: " TILEMAP(VIEWX 1) TILEMAP(VIEWY 1)

' DRAW records the viewport whether or not there is a screen to draw on
TILEMAP DRAW 1, N, 3, 4, 0, 0, 320, 240
PRINT "drawn: " TILEMAP(VIEWX 1) TILEMAP(VIEWY 1)
FRAMEBUFFER CREATE
TILEMAP DRAW 1, F, 0, 0, 0, 0, 40, 24, 0
TILEMAP DRAW 1, F, -7, -3, 10, 10, 40, 24

' sprites borrow the tileset
TILEMAP SPRITE CREATE 1, 1, 2, 10, 20
TILEMAP SPRITE CREATE 2, 1, 3, 15, 25
PRINT "sprite: " TILEMAP(SPRITE X 1) TILEMAP(SPRITE Y 1) TILEMAP(SPRITE TILE 1) TILEMAP(SPRITE W 1) TILEMAP(SPRITE H 1)
PRINT "hit: " TILEMAP(SPRITE HIT 1, 2)
TILEMAP SPRITE MOVE 2, 100, 100
PRINT "miss: " TILEMAP(SPRITE HIT 1, 2) TILEMAP(SPRITE X 2)
TILEMAP SPRITE SET 1, 4
PRINT "sprite set: " TILEMAP(SPRITE TILE 1)
TILEMAP SPRITE DRAW F, -1
TILEMAP SPRITE DRAW N, 0
TILEMAP SPRITE DESTROY 2
ON ERROR SKIP 1
a% = TILEMAP(SPRITE X 2)
PRINT "1: " MM.ERRMSG$

' the errors, in the reference's words
ON ERROR SKIP 1
a% = TILEMAP(TILE 2, 0, 0)
PRINT "2: " MM.ERRMSG$
ON ERROR SKIP 1
TILEMAP SET 1, 5, 0, 1
PRINT "3: " MM.ERRMSG$
ON ERROR SKIP 1
TILEMAP CREATE mapdata, 2, 1, 8, 8, 4, 100, 100
PRINT "4: " MM.ERRMSG$
ON ERROR SKIP 1
TILEMAP CREATE mapdata, 5, 1, 8, 8, 4, 5, 3
PRINT "5: " MM.ERRMSG$
ON ERROR SKIP 1
TILEMAP DRAW 1, N, 0, 0, 0, 0, 40, 24, 16
PRINT "6: " MM.ERRMSG$
ON ERROR SKIP 1
TILEMAP SPRITE CREATE 3, 2, 1, 0, 0
PRINT "7: " MM.ERRMSG$
ON ERROR SKIP 1
a% = TILEMAP(SPRITE HIT 1, 2)
PRINT "8: " MM.ERRMSG$
ON ERROR SKIP 1
TILEMAP SPRITE SET 1, 0
PRINT "9: " MM.ERRMSG$

' destroyed is not created; an erased slot is not an image
TILEMAP DESTROY 1
ON ERROR SKIP 1
a% = TILEMAP(COLS 1)
PRINT "10: " MM.ERRMSG$
FLASH ERASE 1
ON ERROR SKIP 1
TILEMAP CREATE mapdata, 1, 1, 8, 8, 4, 5, 3
PRINT "11: " MM.ERRMSG$
TILEMAP CLOSE
FRAMEBUFFER CLOSE F
PRINT "tilemap surface ok"

DATA 7, 8
mapdata:
DATA 0, 1, 2, 3, 4
DATA 1, 2, 0, 0, 0
DATA 0, 0, 0, 0, 4
attrs:
DATA 1, 2, 3, 0
