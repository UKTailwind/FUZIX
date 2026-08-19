' DefineFont - a program's own font.  The block sits at the BOTTOM and
' the font is selected at the TOP, as MMBasic allows and picofrog
' relies on.
PRINT "start"
FONT 10
TEXT 0, 0, "012", "LT", 10
FONT 1
PRINT "done"
END

' Three 8x8 glyphs from "0".  Each group is a 32-bit LITTLE-endian
' word, so the bytes are the reverse of what is written:
'
'   03300808   -> 08 08 30 03   8 wide, 8 high, first '0', 3 characters
'   FFFFFFFF*2 -> a solid block
'   55AA55AA*2 -> AA 55 AA 55 ...  a checkerboard
'   18181818 00180018 -> 18 18 18 18 18 00 18 00   an exclamation mark,
'                        the byte-order vector taken from picofrog
DefineFont 10
  03300808
  FFFFFFFF FFFFFFFF
  55AA55AA 55AA55AA
  18181818 00180018
End DefineFont
