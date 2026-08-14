' TEXT's justification: a bare word, a quoted one, or a string the
' program works out.  MMBasic tries the argument's RAW TEXT first
' (Draw.c:2148), which is what makes the unquoted form work - picofrog
' and most PicoMite code write it that way.
a$ = "LT"
ON ERROR SKIP 1
TEXT 10, 10, "x", CM
ON ERROR SKIP 1
TEXT 10, 20, "y", C
ON ERROR SKIP 1
TEXT 10, 30, "z", "RB"
ON ERROR SKIP 1
TEXT 10, 40, "w", a$
ON ERROR SKIP 1
TEXT 10, 50, "v"
ON ERROR SKIP 1
TEXT 10, 60, "u", LTV
ON ERROR SKIP 1
TEXT 10, 70, "t", "ZZ"
PRINT "1: " MM.ERRMSG$
PRINT "justarg ok"
