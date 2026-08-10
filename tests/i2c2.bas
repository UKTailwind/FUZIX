' I2C2 - the second controller, on header pins.
'
' There is no second controller under the gates, so OPEN reports failure
' and every transfer says so rather than pretending it worked.  What
' this proves is that both translators agree on the generated C, that
' SETPIN's pin-PAIR form parses beside its ordinary modes, and that the
' write list and the read array both compile.
DIM INTEGER b(8), i

SETPIN 38, 39, I2C2
PRINT "pins assigned"

' Ordinary SETPIN still works after it - the pair form must not have
' eaten the mode words.
SETPIN 2, DIN, PULLUP
PRINT "din still parses ";PIN(2)

ON ERROR SKIP 4
I2C2 OPEN 400, 1000
I2C2 WRITE 118, 0, 1, 208
I2C2 READ 118, 0, 1, b()
I2C2 CLOSE
PRINT "attempted, b(0) = ";b(0)
PRINT "done"
