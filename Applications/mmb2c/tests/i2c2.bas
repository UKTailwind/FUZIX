' I2C2 - the second controller, on header pins.
'
' There is no second controller under the gates, so OPEN reports failure
' and every transfer says so rather than pretending it worked.  What
' this proves is that both translators agree on the generated C, that
' SETPIN's pin-PAIR form parses beside its ordinary modes, and that
' every form of the data argument compiles.
'
' The FIRST HALF DELIBERATELY HAS NO "ON ERROR".  mmbc emitted the
' __mmi2c_sda/__mmi2c_scl declaration inside the ON ERROR block, so a
' program that used I2C2 without trapping errors generated C that would
' not compile at all - and this file could not show it, because every
' I2C2 line it had was under ON ERROR SKIP.  Keep it that way.
DIM INTEGER b(8), i
DIM s$

SETPIN 38, 39, I2C2
PRINT "pins assigned"

' Ordinary SETPIN still works after it - the pair form must not have
' eaten the mode words.
SETPIN 2, DIN, PULLUP
PRINT "din still parses ";PIN(2)

' No ON ERROR anywhere near this one: it must translate and compile.
SETPIN 42, 43, I2C2
PRINT "second pair parses"

ON ERROR SKIP 8
I2C2 OPEN 400, 1000
' every data form MMBasic takes
I2C2 WRITE 118, 0, 1, 208               ' a list of byte expressions
I2C2 WRITE 118, 1, 2, 208, 209          ' ...with the bus HELD (option 1)
I2C2 WRITE 118, 0, 3, b()               ' a whole numeric array
I2C2 READ 118, 0, 1, b()                ' into an array
I2C2 READ 118, 1, 4, s$                 ' into a string, holding
I2C2 CLOSE
PRINT "attempted, b(0) = ";b(0);" len = ";LEN(s$)
PRINT "done"

' --- the forms that came with the shared layer -------------------------
'
' These were written out twice before, once here and once in SPI, and
' the two copies had drifted.  MMBasic has ONE implementation
' (GetCommsTxData / GetCommsRxDest / PutCommsRxData) and three callers;
' so does this now, which is why the same forms have to work on both
' buses.  tests/spi.bas is the other half of this test.
DIM v1, v2, v3
DIM FLOAT g(8)

' a list of lvalues, one per value received - MMBasic's COMMS_RXD_LIST,
' which did not exist here at all
ON ERROR SKIP 1
I2C2 READ 118, 0, 3, v1, v2, v3
PRINT "list of lvalues: ";v1;v2;v3

' a single scalar, which is that form with one element
ON ERROR SKIP 1
I2C2 READ 118, 0, 1, v1
PRINT "single scalar:   ";v1

' a float array, both directions
ON ERROR SKIP 1
I2C2 WRITE 118, 0, 3, g()
ON ERROR SKIP 1
I2C2 READ 118, 0, 3, g()
PRINT "float array:     ";g(0)

' THE COUNT MUST MATCH THE LIST.  Three asked for and two given:
' MMBasic raises "Argument count", where the old code built a two-byte
' buffer, told the driver three, and the third came off the stack.
ON ERROR SKIP 2
I2C2 WRITE 118, 0, 3, 1, 2
PRINT "short list:      ";MM.ERRMSG$

' and a destination too small for what was asked
ON ERROR SKIP 2
I2C2 READ 118, 0, 40, b()
PRINT "small array:     ";MM.ERRMSG$
PRINT "shared forms done"
