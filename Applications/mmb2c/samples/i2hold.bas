' The two things the ioctl could not carry before: the OPEN timeout and
' the option bit that HOLDS the bus for a repeated START.
'
' The BMP180 answers either way - it keeps its register pointer across a
' STOP - so a wrong hold would read RIGHT and prove nothing.  What is
' tested here is that each form is accepted, does not wedge the bus, and
' returns the same chip id (&H55), and that a hold left dangling by an
' error does not strand the bus for the transfer after it.
DIM i2cin$
DIM INTEGER e

SETPIN 38, 39, I2C2
I2C2 OPEN 400, 1000
PRINT "open ok, timeout 1000 ms"

' 1. plain: write with STOP, then read
I2C2 WRITE &H77, 0, 1, &HD0
I2C2 READ &H77, 0, 1, i2cin$
PRINT "no hold   chip id = &H"; HEX$(ASC(i2cin$))

' 2. combined: write holding the bus, read as a repeated START
I2C2 WRITE &H77, 1, 1, &HD0
I2C2 READ &H77, 0, 1, i2cin$
PRINT "with hold chip id = &H"; HEX$(ASC(i2cin$))

' 3. a hold that is never completed, then an ordinary transfer.  If the
'    dangling hold stranded the bus this one fails.
I2C2 WRITE &H77, 1, 1, &HD0
I2C2 WRITE &H77, 0, 1, &HD0
I2C2 READ &H77, 0, 1, i2cin$
PRINT "after dangling hold = &H"; HEX$(ASC(i2cin$))

' 4. a device that is not there still reports, and does not wedge
ON ERROR SKIP 1
I2C2 WRITE &H55, 0, 1, 0
e = MM.ERRNO
PRINT "absent device: errno "; e; "  "; MM.ERRMSG$
' the register pointer must be set again - a bare READ returns whatever
' the device auto-incremented to, which is a property of the BMP180 and
' not of the bus
I2C2 WRITE &H77, 0, 1, &HD0
I2C2 READ &H77, 0, 1, i2cin$
PRINT "bus still good = &H"; HEX$(ASC(i2cin$))

I2C2 CLOSE
PRINT "done"
