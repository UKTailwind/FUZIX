' A float becomes an integer as the reference's FloatToInt64 makes it
' (a40112b): rounded, but NaN or anything outside the 64-bit range is
' "Number too large" - a C cast of those is undefined, and the board and
' a PC gave different numbers - and above 2^52, where a double has no
' fraction left, the value is taken as it stands.  INT and FIX go through
' it too (FnInt and FnFix).  Values are set before each ON ERROR SKIP 1,
' so the skip is spent on the statement under test.
Dim f, i%, big

Print CInt(2.5); CInt(-2.5); Int(-2.5); Fix(-2.5); Int(2.5); Fix(2.5)

f = 2^52 + 1
i = CInt(f)
Print "2^52+1: "; i
f = -(2^52 + 1)
i = Int(f)
Print "-(2^52+1): "; i

' 2.0^63, a FLOAT: 2^63 of two integers is op_exp's integer power,
' which wraps to -2^63 and fits
big = 2.0^63
ON ERROR SKIP 1
i = CInt(big)
Print "CINT(2^63): " MM.ERRMSG$
big = 1e30
ON ERROR SKIP 1
i = Int(big)
Print "INT(1e30): " MM.ERRMSG$
big = -1e300
ON ERROR SKIP 1
i = Fix(big)
Print "FIX(-1e300): " MM.ERRMSG$
big = 1e19
ON ERROR SKIP 1
i = big
Print "i% = 1e19: " MM.ERRMSG$
big = 1e308 * 10
ON ERROR SKIP 1
i = big
Print "i% = INF: " MM.ERRMSG$
big = 2^62
i = big
Print "2^62 fits: "; i
