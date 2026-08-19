' SETPIN and PIN - digital and both analogue modes.
'
' There is no hardware under the gates, but an OUTPUT now reads back
' what it was set to - the host keeps a shadow latch, so PIN(2) after
' PIN(2)=1 answers 1 here exactly as it does on the board, where MMBasic
' and we both read an output pin's own drive.  An input still answers 0;
' nothing is connected to it in either place.  What this proves is that
' the two
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
