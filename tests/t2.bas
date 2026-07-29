' t2.bas - scope, implied globals and argument passing
DIM INTEGER data(5)
DIM shared$ = "global string"

' 'total' is never declared - implied global, created here
total = 0

Fill data()
PRINT "sum ="; SumArray(data())

' 'oops' is created INSIDE a sub with no LOCAL - it is global,
' and the main program can see it afterwards
MakeMess
PRINT "oops ="; oops
PRINT "counter ="; hidden.counter

' a LOCAL of the same name hides the global
shadow = 111
Shadower
PRINT "shadow still ="; shadow

' by reference vs BYVAL
p = 1 : q = 1
Bump p
BumpByVal q
PRINT "p ="; p; "  q ="; q

' omitted and missing arguments
Three 7, , 9
Three 7

' string handling
s$ = "abcdefghij"
MID$(s$, 4, 3) = "XYZ"
PRINT s$; " len="; LEN(s$)
PRINT UCASE$(LEFT$(s$, 5)); "|"; RIGHT$(s$, 2); "|"; MID$(s$, 3, 4)
PRINT "instr ="; INSTR(s$, "XYZ")
PRINT "val ="; VAL("  3.25"); " hex="; HEX$(255, 4)
PRINT "concat: " + shared$ + " / " + STR$(count%) + " / " + CHR$(65)

' SELECT with IS
FOR n% = 1 TO 5
  SELECT CASE n%
    CASE IS < 2
      PRINT n%; "small"
    CASE 2 TO 3
      PRINT n%; "middle"
    CASE ELSE
      PRINT n%; "large"
  END SELECT
NEXT n%

PRINT "nested:"; Outer(3)
END

SUB Fill arr%()
  LOCAL INTEGER i
  FOR i = 0 TO 5
    arr%(i) = i * 10
  NEXT i
END SUB

FUNCTION SumArray%(arr%())
  LOCAL INTEGER i, s
  FOR i = 0 TO 5
    s = s + arr%(i)
  NEXT i
  SumArray% = s
END FUNCTION

SUB MakeMess
  oops = 42                ' implied GLOBAL, not local!
  hidden.counter = hidden.counter + 1
END SUB

SUB Shadower
  LOCAL shadow
  shadow = 999
END SUB

SUB Bump n
  n = n + 100
END SUB

SUB BumpByVal BYVAL n
  n = n + 100
END SUB

SUB Three a, b, c
  PRINT "three:"; a; b; c
END SUB

FUNCTION Outer(x)
  Outer = Inner(x) * 2
END FUNCTION

FUNCTION Inner(x)
  Inner = x + 1
END FUNCTION
