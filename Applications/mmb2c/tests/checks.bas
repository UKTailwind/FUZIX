' The checks the interpreter makes before it does the arithmetic - the
' ones ON ERROR SKIP exists to catch.  Every value here is chosen so the
' answer is exact, and every line has a counterpart on a real PicoMite:
' run this there and the output must be identical.
Option Explicit
Option Default None

Dim Float f
Dim Integer i
Dim String s

' division that does NOT error: the ordinary path still works
f = 10 / 4
Print "div: "; f; " "; 10 \ 4; " "; 10 Mod 4
f = -7 / 2
Print "negdiv: "; f; " "; -7 \ 2; " "; -7 Mod 2

' the domain edges the C library would have answered with nan/inf
Print "sqr: "; Sqr(0); " "; Sqr(2)
Print "log: "; Log(1); " "; Log(2.718281828459045)
Print "asin: "; Asin(-1); " "; Asin(0); " "; Asin(1)
Print "acos: "; Acos(-1); " "; Acos(0); " "; Acos(1)

' ASC of an empty string is 0 in the firmware, not an error
s = ""
Print "asc: "; Asc(s); " "; Asc("A")

' string concatenation right up to the 255-byte limit is legal
Dim String a250 = String$(250, "x")
s = a250 + "12345"
Print "cat: "; Len(s)

' BIN2STR$/STR2BIN round trip, and the exact-length rule
s = Bin2Str$(int16, -1234)
Print "bin2str: "; Len(s); " "; Str2Bin(int16, s)

Print "done"
