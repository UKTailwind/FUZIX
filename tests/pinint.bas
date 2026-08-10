' SETPIN INTH/INTL/INTB - pin interrupts.
'
' A handler is a SUB and ends with END SUB.  There is no IRETURN to
' write: MMBasic builds one itself as the return address of the GOSUB it
' fakes (MM_Misc.c:10205), so END SUB performs the interrupt return.
'
' There is no hardware under the gates - the stub pins read 0 and never
' change - so nothing fires here and the count stays zero.  What this
' proves is what the gates CAN prove: that both translators agree on the
' generated C, that the poll site compiles in every statement position
' (inside IF, inside FOR, inside a SUB), and that a program which arms
' an interrupt still runs.
'
' The edge behaviour itself is board-only, against a driven pin.
DIM INTEGER hits = 0

SUB OnEdge
  hits = hits + 1
END SUB

SUB OnAny
  hits = hits + 100
END SUB

SETPIN 34, INTH, OnEdge
SETPIN 35, INTL, OnEdge
SETPIN 36, INTB, OnAny
PRINT "armed"

FOR i = 1 TO 3
  IF hits > 0 THEN PRINT "fired"
NEXT i

PRINT "hits ";hits
PRINT "pin34 ";PIN(34)
SETPIN 34, OFF
PRINT "off"
PRINT "done"
