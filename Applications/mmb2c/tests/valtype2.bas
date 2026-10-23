' VAL keeps StrVal's type through STR$'s other arguments, a unary minus
' and brackets (FUZIX issue #28).  Each line has VAL beside the literal
' it reads, and the two halves must agree.
Print "[" + Str$(Val("2000000"), 10) + "]"
a$ = "2000000"
Print "[" + Str$(Val(a$), 10, 2) + "] [" + Str$(2000000, 10, 2) + "]"
Print "[" + Str$(Val(a$), 12, 0, "*") + "] [" + Str$(2000000, 12, 0, "*") + "]"
Print "[" + Str$(-Val(a$)) + "] [" + Str$(-2000000) + "]"
Print "[" + Str$((Val(a$)), 9) + "] [" + Str$(2000000, 9) + "]"
Print "[" + Str$(-Val("1.5"), 8, 3) + "] [" + Str$(-1.5, 8, 3) + "]"
Print -Val(a$); " "; -2000000
Print (Val(a$)); " "; (2000000)
Print -(Val(a$)); " "; -(-Val(a$))
Print -Val("-3"); " "; -Val("2.5e6")
x% = (Val("9007199254740993")) : Print x%
