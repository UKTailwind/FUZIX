' t5.bas - the built-in functions, checked against the manual's examples
OPTION DEFAULT FLOAT

PRINT "-- rounding --"
PRINT INT(9.89); INT(-2.11); FIX(9.89); FIX(-2.11)
PRINT CINT(45.47); CINT(45.57); CINT(-34.45); CINT(-34.55)
PRINT ABS(-7); SGN(-3); SGN(0); SGN(4)

PRINT "-- trig --"
PRINT SIN(0); COS(0); TAN(0)
PRINT ASIN(1); ACOS(1); ATN(1)
PRINT ATAN2(1, 1)
PRINT DEG(PI); RAD(180)
PRINT SQR(144); LOG(EXP(2)); EXP(0)
PRINT MATH(COSH 0); MATH(SINH 0); MATH(TANH 0); MATH(LOG10 1000)
PRINT MATH(ATAN3 -1, -1)

PRINT "-- integers and bits --"
DIM INTEGER x, y
x = &HFFFF0000FFFF0044
y = &H800FFFFFFFFFFFFF
x = x AND y
PRINT HEX$(x, 16)
PRINT (3 AND 6); (3 OR 6); (3 XOR 6); 1 << 4; 256 >> 4
PRINT BIT(&B1010, 1); BIT(&B1010, 2)
PRINT 4 >= 5; 3 > 2
PRINT MAX(3, 9, 2); MIN(3, 9, 2)

PRINT "-- STR$ (manual examples) --"
PRINT "["; STR$(123.456); "]"
PRINT "["; STR$(123.456, 1); "]"
PRINT "["; STR$(123.456, -1); "]"
PRINT "["; STR$(123.456, 6); "]"
PRINT "["; STR$(123.456, -6); "]"
PRINT "["; STR$(-123.456, 6); "]"
PRINT "["; STR$(-123.456, 6, 5); "]"
PRINT "["; STR$(-123.456, 6, -5); "]"
PRINT "["; STR$(53, 6); "]"
PRINT "["; STR$(53, 6, 2); "]"
PRINT "["; STR$(53, 6, 2, "*"); "]"

PRINT "-- FORMAT$ --"
PRINT "["; FORMAT$(45); "]"
PRINT "["; FORMAT$(45, "%g"); "]"
PRINT "["; FORMAT$(3.14159, "%8.3f"); "]"
PRINT "["; FORMAT$(3.14159, "%-8.3f"); "]"
PRINT "["; FORMAT$(42, "value = %06.2f units"); "]"
PRINT "["; FORMAT$(1234.5, "%e"); "]"
PRINT "["; FORMAT$(-7.5, "%+.1f"); "]"

PRINT "-- strings --"
DIM s$ = "Hello World"
PRINT LEN(s$); ASC(s$); BYTE(s$, 2)
PRINT LEFT$(s$, 5); "|"; RIGHT$(s$, 5); "|"; MID$(s$, 7, 3)
PRINT UCASE$(s$); "|"; LCASE$(s$)
PRINT "["; SPACE$(3); "]"; "["; STRING$(4, "-"); "]"; "["; STRING$(3, 65); "]"
PRINT INSTR(s$, "World"); INSTR(s$, "zzz"); INSTR(4, s$, "o")
PRINT VAL("  3.25"); VAL("&HFF"); VAL("junk")
PRINT CHR$(65) + CHR$(66)
PRINT "["; TRIM$("   xx   "); "]"
PRINT "["; TRIM$("   xx   ", " ", "R"); "]"
PRINT "["; TRIM$("   xx   ", " ", "B"); "]"
PRINT "["; TRIM$("**42**", "*", B); "]"
PRINT "["; FIELD$("foo, boo, zoo, doo", 2, ","); "]"
PRINT "["; FIELD$("foo, 'boo, zoo', doo", 2, ",", "'"); "]"

PRINT "-- CHOICE --"
PRINT CHOICE(1, "hello", "bye"); "|"; CHOICE(0, "hello", "bye")
DIM a = 1, b = 1
PRINT CHOICE(a = b, 4, 5); CHOICE(a <> b, 4, 5)

PRINT "-- BOUND --"
DIM myarray(44, 45)
DIM one(9)
PRINT BOUND(myarray(), 1); BOUND(myarray(), 2); BOUND(myarray(), 0)
PRINT BOUND(one())
ShowBound one()

PRINT "-- date and time --"
PRINT EPOCH("01-01-2000 00:00:00")
PRINT "["; DATETIME$(946684800); "]"
PRINT "["; DAY$("01-01-2000"); "]"; "["; DAY$("2000-01-01"); "]"
PRINT "["; DAY$("25-12-2025"); "]"
PRINT LEN(DATE$()); LEN(TIME$())
PRINT CHOICE(TIMER >= 0, "timer ok", "timer bad")

PRINT "-- binary conversion --"
PRINT LEN(BIN2STR$(INT32, 1000)); LEN(BIN2STR$(DOUBLE, 1.5))
PRINT STR2BIN(INT32, BIN2STR$(INT32, -12345))
PRINT STR2BIN(UINT16, BIN2STR$(UINT16, 65535))
PRINT STR2BIN(DOUBLE, BIN2STR$(DOUBLE, 3.25))
PRINT STR2BIN(INT16, BIN2STR$(INT16, -2, BIG), BIG)

PRINT "-- RGB --"
PRINT HEX$(RGB(255, 165, 0), 6); "|"; HEX$(RGB(orange), 6); "|"; HEX$(RGB(white), 6)

PRINT "-- TAB --"
PRINT "col1"; TAB(20); "col20"; TAB(30); "col30"
PRINT "done"
END

SUB ShowBound arr()
  PRINT "bound inside a sub ="; BOUND(arr())
END SUB
