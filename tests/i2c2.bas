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
