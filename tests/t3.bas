' t3.bas - every shape of IF, and colon separated statements
DIM INTEGER a, b, c
DIM r$

' ---- colons, no spaces, several statements per line
a=1:b=2:c=3
PRINT "colons:";a;b;c
a = 4 : b = 5 : PRINT "spaced:"; a; b
:: PRINT "empty statements survive" ::

' ---- single line IF, no ELSE
IF a = 4 THEN PRINT "1 then-only, taken"
IF a = 99 THEN PRINT "2 then-only, NOT taken"

' ---- single line IF with ELSE
IF a = 4 THEN PRINT "3 then branch" ELSE PRINT "3 else branch"
IF a = 99 THEN PRINT "4 then branch" ELSE PRINT "4 else branch"

' ---- multiple statements after THEN: all are conditional
IF a = 4 THEN b = 11 : c = 12 : PRINT "5 all three ran"
PRINT "5 b,c ="; b; c
b = 0 : c = 0
IF a = 99 THEN b = 11 : c = 12 : PRINT "6 must not appear"
PRINT "6 b,c ="; b; c

' ---- multiple statements either side of ELSE
IF a = 4 THEN b = 21 : PRINT "7 then" ELSE b = 22 : PRINT "7 else"
PRINT "7 b ="; b
IF a = 99 THEN b = 31 : PRINT "8 then" ELSE b = 32 : PRINT "8 else"
PRINT "8 b ="; b

' ---- IF ... GOTO, with and without THEN
IF a = 4 THEN GOTO skip1
PRINT "9 must not appear"
skip1: PRINT "9 THEN GOTO worked"
IF a = 4 GOTO skip2
PRINT "10 must not appear"
skip2: PRINT "10 bare GOTO worked"

' ---- numeric line numbers as labels
IF a = 4 THEN 200
PRINT "11 must not appear"
200 PRINT "11 line number target worked"

' ---- nested single line IF
IF a = 4 THEN IF b = 32 THEN PRINT "12 nested single line"
IF a = 4 THEN IF b = 99 THEN PRINT "13 must not appear" ELSE PRINT "13 inner else"

' ---- an IF as the tail of a colon list
a = 4 : IF a = 4 THEN PRINT "14 IF after a colon"
IF a = 4 THEN PRINT "15a" : IF b = 32 THEN PRINT "15b"

' ---- block IF, both terminators
IF a = 4 THEN
  PRINT "16 block then"
ENDIF
IF a = 99 THEN
  PRINT "17 must not appear"
END IF

IF a = 1 THEN
  PRINT "18 wrong"
ELSEIF a = 4 THEN
  PRINT "18 elseif taken"
ELSE
  PRINT "18 else"
ENDIF

IF a = 1 THEN
  PRINT "19 wrong"
ELSE
  PRINT "19 else taken"
ENDIF

' ---- nested block IF, and a single line IF inside a block IF
IF a = 4 THEN
  PRINT "20 outer"
  IF b = 32 THEN
    PRINT "20 inner"
  ELSE
    PRINT "20 inner else"
  ENDIF
  IF b = 32 THEN PRINT "20 single line inside block" ELSE PRINT "20 no"
ENDIF

' ---- compound conditions and strings
r$ = "abc"
IF a = 4 AND b = 32 THEN PRINT "21 AND ok"
IF a = 99 OR b = 32 THEN PRINT "22 OR ok"
IF NOT (a = 99) THEN PRINT "23 NOT ok"
IF r$ = "abc" THEN PRINT "24 string compare ok" ELSE PRINT "24 wrong"
IF r$ <> "abc" THEN PRINT "25 wrong" ELSE PRINT "25 string else ok"

' ---- IF inside loops, and EXIT from a single line IF
FOR i% = 1 TO 5
  IF i% = 3 THEN PRINT "26 hit three" : c = i%
  IF i% = 4 THEN EXIT FOR
NEXT i%
PRINT "26 c ="; c; " i ="; i%

i% = 0
DO
  i% = i% + 1
  IF i% = 2 THEN PRINT "27 two"
LOOP UNTIL i% >= 3
PRINT "27 i ="; i%

Early 1 : Early 9
PRINT "28 done"
Report 4 : Report 40 : Report 400
END

SUB Early n
  IF n > 5 THEN PRINT "29 exiting early for"; n : EXIT SUB
  PRINT "29 carried on for"; n
END SUB

SUB Report v
  IF v < 10 THEN
    PRINT "30 small"; v
  ELSEIF v < 100 THEN
    PRINT "30 medium"; v
  ELSE
    PRINT "30 large"; v
  ENDIF
END SUB
