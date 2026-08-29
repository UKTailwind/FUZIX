' Make 12piece.spr from 12piececol.bmp, on the machine itself.
'
' The pieces are cut out with SPRITE LOADBMP - the same window form the
' chess program uses - written opaquely to the screen, and read back as
' RGB121 indices.  That is the .spr format: "width, count, height" and
' then one hex digit per pixel, which is what SPRITE LOAD reads.
'
' SPRITE WRITE with a 0 orientation is the OPAQUE copy (the reference
' sets mode 8 when bit 2 of the argument is clear), so index 0 lands on
' the screen as black rather than being left transparent.
MODE 2
CLS RGB(0,0,0)

DIM INTEGER s, x, y, c, r, g, b, i
DIM STRING l

FOR s = 1 TO 12
  SPRITE LOADBMP #s, "12piececol", (s - 1) * 20, 0, 20, 20
NEXT s

OPEN "12piece.spr" FOR OUTPUT AS #1
PRINT #1, "20,12,20"
FOR s = 1 TO 12
  SPRITE WRITE #s, 0, 0, 0
  FOR y = 0 TO 19
    l = ""
    FOR x = 0 TO 19
      c = PIXEL(x, y)
      r = (c >> 16) AND &HFF
      g = (c >> 8) AND &HFF
      b = c AND &HFF
      i = 0
      IF r >= 128 THEN i = i + 8
      IF g >= &HDA THEN
        i = i + 6
      ELSE
        IF g >= &H7F THEN
          i = i + 4
        ELSE
          IF g >= &H24 THEN i = i + 2
        ENDIF
      ENDIF
      IF b >= 128 THEN i = i + 1
      l = l + HEX$(i, 1)
    NEXT x
    PRINT #1, l
  NEXT y
NEXT s
CLOSE #1

FOR s = 1 TO 12
  SPRITE CLOSE #s
NEXT s
OPTION CONSOLE SERIAL
PRINT "wrote 12piece.spr"
END
