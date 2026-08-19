' SETTICK - the four periodic timers.
'
' The clock is real under the gates (the runtime's microsecond clock, not
' a stub), so this actually counts ticks rather than merely compiling.
' The tolerances are loose because a host under load is not a real-time
' machine; what is being checked is that ticks fire, that they fire at
' roughly the right ratio to each other, that PAUSE stops them and
' RESUME starts them again, and that turning one off is final.
'
' Note the names: BASIC is case-insensitive, so a SUB called Fast and a
' variable called fast are the SAME NAME.  They are kept apart here.
DIM INTEGER na = 0, nb = 0, i = 0, mark = 0

SUB TickA
  na = na + 1
END SUB

SUB TickB
  nb = nb + 1
END SUB

SETTICK 10, TickA, 1
SETTICK 50, TickB, 2

' Spin until the 10ms timer has fired plenty of times.
DO
  i = i + 1
LOOP UNTIL na >= 40

PRINT "a fired ";
IF na >= 40 THEN PRINT "yes" ELSE PRINT "no"
PRINT "b fired ";
IF nb >= 4 THEN PRINT "yes" ELSE PRINT "no"
PRINT "ratio sane ";
IF nb > 0 AND na > nb * 2 THEN PRINT "yes" ELSE PRINT "no"

' PAUSE must stop it dead.
SETTICK PAUSE, 1
mark = na
FOR i = 1 TO 2000000
NEXT i
PRINT "paused ";
IF na = mark THEN PRINT "yes" ELSE PRINT "no"

' RESUME must start it again.
SETTICK RESUME, 1
DO
  i = i + 1
LOOP UNTIL na > mark
PRINT "resumed yes"

' Off is off.
SETTICK 0, 0, 1
SETTICK 0, 0, 2
mark = na
FOR i = 1 TO 2000000
NEXT i
PRINT "stopped ";
IF na = mark THEN PRINT "yes" ELSE PRINT "no"
PRINT "done"
