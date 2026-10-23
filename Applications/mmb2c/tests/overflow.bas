' A float +, -, *, / or ^ that gives +INF is "Overflow" (op_add, op_mul,
' op_div, op_exp, and op_subtract since adb769f) - only +INF: a -INF
' result passes, and so does an integer to a negative power (op_exp's
' integer path, 0 ^ -1).  Checked where ON ERROR can trap it, as the
' other run-time arithmetic checks are here.  Run on a real PicoMite: the
' output must be identical.
Dim Float x, y, d, z
x = 1e308
d = 0.1
On Error Skip 1
y = x + x
Print "add: "; MM.ErrMsg$
On Error Skip 1
y = x - (-x)
Print "sub: "; MM.ErrMsg$
On Error Skip 1
y = x * 10
Print "mul: "; MM.ErrMsg$
On Error Skip 1
y = x / d
Print "div: "; MM.ErrMsg$
On Error Skip 1
y = x ^ 2
Print "pow: "; MM.ErrMsg$
On Error Skip 1
y = -x - x
Print "neg: "; MM.ErrMsg$; (y < -1e308)
On Error Skip 1
z = 0 ^ -1
Print "int: "; MM.ErrMsg$
