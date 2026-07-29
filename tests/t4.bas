' t4.bas - IF and colon edge cases
DIM INTEGER a = 4, b = 0
DIM s$ = "x"

' a sub call followed by a colon must not look like a label
Ping : Ping
Ping : PRINT "1 call-colon-call ok"

' calls, not assignments, inside a single line IF - with and without ELSE
IF a = 4 THEN Show 1, 2
IF a = 4 THEN Show 3, 4 ELSE Show 5, 6
IF a = 99 THEN Show 7, 8 ELSE Show 9, 10
IF a = 4 THEN Show 11, 12 : Show 13, 14 ELSE Show 15, 16

' function calls in the condition
IF Twice(2) = 4 THEN PRINT "2 function in condition ok"
IF Twice(a) > 100 THEN PRINT "3 wrong" ELSE PRINT "3 function else ok"

' string function in a condition, and a THEN that assigns a string
IF LEFT$(s$, 1) = "x" THEN s$ = s$ + "y" : PRINT "4 s$ = " + s$

' comment after THEN still means a block IF
IF a = 4 THEN            ' this is a block
  PRINT "5 comment after THEN ok"
ENDIF

' trailing colon on a line
PRINT "6 trailing colon ok" :
a = 4 :

' label with statements on the same line, and a backward GOTO
b = 0
again: b = b + 1 : IF b < 3 THEN GOTO again
PRINT "7 loop by label, b ="; b

' GOTO out of a block IF and out of a FOR loop
FOR i% = 1 TO 10
  IF i% = 3 THEN
    PRINT "8 jumping out from inside a block IF"
    GOTO escaped
  ENDIF
NEXT i%
PRINT "8 wrong"
escaped:
PRINT "8 escaped at i ="; i%

' EXIT DO from a single line IF
i% = 0
DO
  i% = i% + 1
  IF i% = 3 THEN PRINT "9 exiting do" : EXIT DO
LOOP
PRINT "9 i ="; i%

' single line IF as the whole body of a block IF branch
IF a = 4 THEN
  IF b = 3 THEN PRINT "10 nested ok" ELSE PRINT "10 wrong"
ELSE
  PRINT "10 outer else"
ENDIF

' IF inside SELECT CASE and vice versa
SELECT CASE a
  CASE 4
    IF b = 3 THEN PRINT "11 IF inside CASE ok"
  CASE ELSE
    PRINT "11 wrong"
END SELECT

IF a = 4 THEN
  SELECT CASE b
    CASE 3
      PRINT "12 SELECT inside IF ok"
    CASE ELSE
      PRINT "12 wrong"
  END SELECT
ENDIF

' deeply nested single line IFs
IF a = 4 THEN IF b = 3 THEN IF s$ = "xy" THEN PRINT "13 triple nest ok"

PRINT "14 done"
END

SUB Ping
  STATIC INTEGER n
  n = n + 1
END SUB

SUB Show p, q
  PRINT "show"; p; q
END SUB

FUNCTION Twice(v)
  Twice = v * 2
END FUNCTION
