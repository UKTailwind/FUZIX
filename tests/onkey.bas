' ON KEY - both forms, and turning them off.
'
' Under the gates stdin is not a terminal, so no key ever arrives and no
' handler fires.  That is the documented answer for a program run from a
' pipe or a file rather than a fault, and it is what this checks: all
' four forms translate, the poll compiles everywhere, and a program that
' arms ON KEY still runs to the end with the counts at zero.
'
' The two forms differ in what happens to the KEY - the any-key form
' leaves it for INKEY$, the specific form eats it - and that can only be
' shown at a real console, so it is a board test.
DIM INTEGER anyn = 0, seln = 0
DIM k$

SUB AnyKey
  k$ = INKEY$
  anyn = anyn + 1
END SUB

SUB SelKey
  seln = seln + 1
END SUB

ON KEY AnyKey
ON KEY 65, SelKey
PRINT "armed"

DIM INTEGER i
FOR i = 1 TO 200000
NEXT i

PRINT "any ";anyn
PRINT "sel ";seln
ON KEY 65, 0
ON KEY 0
PRINT "off"
PRINT "done"
