' SPI - the other half of tests/i2c2.bas.
'
' There was no SPI test at all until the shared data layer went in,
' which is part of why SPI's copy of the data forms had drifted from
' I2C's: nothing compared them.  MMBasic has one implementation
' (GetCommsTxData / GetCommsRxDest / PutCommsRxData) reached from
' I2C.c, Onewire.c and SPI.c, and so does this now - so the forms here
' are deliberately the same as the ones in i2c2.bas.
'
' There is no controller under the gates, so OPEN fails and each
' transfer says so rather than pretending. What this proves is that
' both translators agree on the C, that it compiles, and that every
' form parses.
DIM INTEGER b(8), i
DIM FLOAT g(8)
DIM s$
DIM v1, v2, v3

SETPIN 2, 3, 4, SPI
PRINT "pins assigned"

ON ERROR SKIP 1
SPI OPEN 1000000, 0
PRINT "open attempted"

' --- every TX form ---
ON ERROR SKIP 1
SPI WRITE 3, 1, 2, 3                    ' a list of expressions
ON ERROR SKIP 1
SPI WRITE 3, b()                        ' a whole integer array
ON ERROR SKIP 1
SPI WRITE 3, g()                        ' a whole float array
s$ = "abcd"
ON ERROR SKIP 1
SPI WRITE 4, s$                         ' a string, no copy
PRINT "tx forms attempted"

' --- every RX form ---
ON ERROR SKIP 1
SPI READ 3, b()                         ' into an array
ON ERROR SKIP 1
SPI READ 4, s$                          ' into a string
ON ERROR SKIP 1
SPI READ 3, v1, v2, v3                  ' into a list of lvalues
ON ERROR SKIP 1
SPI READ 1, v1                          ' into one scalar
PRINT "rx forms attempted, b(0) = ";b(0);" len = ";LEN(s$)

' the same two refusals i2c2.bas checks, because they are the same code
ON ERROR SKIP 2
SPI WRITE 3, 1, 2
PRINT "short list:      ";MM.ERRMSG$
ON ERROR SKIP 2
SPI READ 40, b()
PRINT "small array:     ";MM.ERRMSG$

' the SPI() function is unaffected by any of this
ON ERROR SKIP 1
i = SPI(&HAA)
PRINT "SPI() still there"

SPI CLOSE
PRINT "done"
