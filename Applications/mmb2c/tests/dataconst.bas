' DATA items that name a CONST.  MMBasic's READ evaluates a numeric
' item's text when it runs (cmd_read: getinteger/getnumber on the
' item), so a bare CONST name reads as its value - wherever in the
' program the CONST is declared - while a string target gets the
' item's own text.  Both translators evaluate every DATA statement
' after the whole declaration pass for exactly this; before, a bare
' name was text and read as 0, and a CONST declared below its DATA
' line was an implied variable in a static initializer.
CONST SOLID = 3
CONST LADDER = 4
CONST HALF! = 0.5
CONST NAME$ = "abc"

READ a%, b%, c%, d!
PRINT "values: " a% b% c% d!
READ e$, f$, g$
PRINT "text: " e$ " " f$ " " g$
READ h%, i%, j!
PRINT "later: " h% i% j!
READ k%, l$
PRINT "unknown: " k% " " l$
RESTORE second
READ m%
PRINT "restore: " m%
PRINT "dataconst ok"

DATA SOLID, LADDER, SOLID OR LADDER, HALF
DATA SOLID, NAME$, HALF
second:
DATA LATE, LATE * 2, LATE / 2
DATA nothing, nothing
CONST LATE = 7
