' A LONG STRING as the data for a bus - the 255-byte wall coming down.
'
' A BASIC string holds 255 bytes.  A 240-pixel row of RGB565 is 480, so
' a display row took two writes and a whole frame could not be built in
' BASIC at all.  The kernel never had that limit; only the shape of the
' data argument did.
'
' It has to be SPELLED OUT, because a long string IS an integer array:
'
'     SPI WRITE n, LONGSTRING a()    the bytes
'     SPI WRITE n, a()               the elements, one byte per cell
'
' The second is MMBasic's behaviour and stays.  Saying LONGSTRING is how
' a program says which it meant - and the old code had no way to say the
' first at all.
DIM INTEGER ls(200)                     ' 1600 bytes of payload
DIM INTEGER i

LONGSTRING CLEAR ls()
FOR i = 1 TO 480
  LONGSTRING APPEND ls(), CHR$(i AND 255)
NEXT i
PRINT "long string holds ";LLEN(ls());" bytes"

SETPIN 2, 3, 4, SPI
ON ERROR SKIP 1
SPI OPEN 1000000, 0

' a whole 480-byte row in ONE call - the thing that was impossible
ON ERROR SKIP 1
SPI WRITE 480, LONGSTRING ls()
PRINT "480-byte write attempted in one call"

' and back into one
ON ERROR SKIP 1
SPI READ 480, LONGSTRING ls()
PRINT "480-byte read attempted, LLEN now ";LLEN(ls())

' asking for more than it holds is refused, as a short string is
LONGSTRING CLEAR ls()
LONGSTRING APPEND ls(), "abc"
ON ERROR SKIP 2
SPI WRITE 10, LONGSTRING ls()
PRINT "short source:    ";MM.ERRMSG$

' and a destination that cannot hold the answer
DIM INTEGER small(2)
LONGSTRING CLEAR small()
ON ERROR SKIP 2
SPI READ 400, LONGSTRING small()
PRINT "small dest:      ";MM.ERRMSG$

SPI CLOSE

' the same form on I2C, which takes it even though its driver caps the
' length - the forms are shared and a program should not have to
' remember which bus allows what
SETPIN 38, 39, I2C2
ON ERROR SKIP 1
I2C2 OPEN 400, 1000
LONGSTRING CLEAR ls()
LONGSTRING APPEND ls(), "ab"
ON ERROR SKIP 1
I2C2 WRITE &H77, 0, 2, LONGSTRING ls()
ON ERROR SKIP 1
I2C2 READ &H77, 0, 2, LONGSTRING ls()
PRINT "i2c long string form accepted"
ON ERROR SKIP 1
I2C2 CLOSE
PRINT "done"
