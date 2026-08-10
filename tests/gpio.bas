' SETPIN and PIN - digital and both analogue modes.
'
' There is no hardware under the gates, so every read answers 0 and the
' numbers here are not the point.  What this proves is that the two
' translators agree on the generated C, that the C compiles and runs
' under gcc, under fcc and on the board's own cc, and that PIN() is one
' type in every mode - which is the thing that changed when AIN arrived.
SETPIN 2, DOUT
PIN(2) = 1
PRINT "dout ";PIN(2)
PIN(2) = 0
PRINT "dout ";PIN(2)
SETPIN 3, DIN
PRINT "din ";PIN(3)
SETPIN 41, ARAW
PRINT "araw ";PIN(41)
SETPIN 41, AIN
PRINT "ain ";PIN(41)
SETPIN 41, OFF
PRINT "done"
