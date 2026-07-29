' t1.bas - basic sanity check
CONST MaxItems = 5, Greeting$ = "Hello"

DIM INTEGER count = 3
DIM total                        ' float, explicit DIM
DIM name$ = "World"
DIM INTEGER tbl(MaxItems)

PRINT Greeting$ + ", " + name$ + "!"
PRINT "count ="; count, "total ="; total

' running is never DIMmed anywhere - implied global, float
FOR i = 1 TO MaxItems
  tbl(i) = i * i
  running = running + tbl(i)
NEXT i

PRINT "squares:";
FOR i = 1 TO MaxItems
  PRINT " "; tbl(i);
NEXT i
PRINT
PRINT "running ="; running

' flow control
IF count > 2 THEN
  PRINT "count is big"
ELSEIF count = 2 THEN
  PRINT "count is two"
ELSE
  PRINT "count is small"
ENDIF

IF count = 3 THEN PRINT "one liner true" ELSE PRINT "one liner false"

j% = 0
DO
  j% = j% + 1
LOOP UNTIL j% >= 4
PRINT "j ="; j%

k% = 10
DO WHILE k% > 7
  k% = k% - 1
LOOP
PRINT "k ="; k%

WHILE k% < 9
  k% = k% + 1
WEND
PRINT "k now ="; k%

SELECT CASE count
  CASE 1, 2
    PRINT "one or two"
  CASE 3 TO 5
    PRINT "three to five"
  CASE ELSE
    PRINT "something else"
END SELECT

SELECT CASE name$
  CASE "World"
    PRINT "the world"
  CASE ELSE
    PRINT "elsewhere"
END SELECT

' subs and functions
Swap a, b
PRINT "a ="; a; " b ="; b
a = 3 : b = 4
Swap a, b
PRINT "after swap a ="; a; " b ="; b

PRINT "F(100) ="; Fahrenheit(100)
PRINT "[" + Trim$("   ***42.5**  ", " *") + "]"

Counter
Counter
Counter

GOTO finish
PRINT "never printed"
finish:
PRINT "done"
END

SUB Swap x, y
  LOCAL t
  t = x
  x = y
  y = t
END SUB

FUNCTION Fahrenheit(c) AS FLOAT
  Fahrenheit = c * 1.8 + 32
END FUNCTION

FUNCTION Trim$(s$, ch$)
  Trim$ = RTrimC$(LTrimC$(s$, ch$), ch$)
END FUNCTION

FUNCTION RTrimC$(s$, ch$)
  RTrimC$ = s$
  DO WHILE LEN(RTrimC$) > 0 AND INSTR(ch$, RIGHT$(RTrimC$, 1)) > 0
    RTrimC$ = MID$(RTrimC$, 1, LEN(RTrimC$) - 1)
  LOOP
END FUNCTION

FUNCTION LTrimC$(s$, ch$)
  LTrimC$ = s$
  DO WHILE LEN(LTrimC$) > 0 AND INSTR(ch$, LEFT$(LTrimC$, 1)) > 0
    LTrimC$ = MID$(LTrimC$, 2)
  LOOP
END FUNCTION

SUB Counter
  STATIC n = 5
  PRINT "counter ="; n
  n = n + 1
END SUB
