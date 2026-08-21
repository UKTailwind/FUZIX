' MATH C_* - component-wise array operations
Dim integer ai(4), bi(4), ci(4)
Dim float af(4), bf(4), cf(4)
Dim integer k

For k = 0 To 4
  ai(k) = k + 1
  bi(k) = 3
  af(k) = k + 1
  bf(k) = 2.0
Next k

Math C_ADD ai(), bi(), ci()
Print "add "; ci(0); " "; ci(4)
Math C_SUB ai(), bi(), ci()
Print "sub "; ci(0); " "; ci(4)
Math C_MUL ai(), bi(), ci()
Print "mul "; ci(0); " "; ci(4)
Math C_DIV ai(), bi(), ci()
Print "div "; ci(0); " "; ci(4)
Math C_AND ai(), bi(), ci()
Print "and "; ci(0); " "; ci(4)
Math C_OR ai(), bi(), ci()
Print "or "; ci(0); " "; ci(4)
Math C_XOR ai(), bi(), ci()
Print "xor "; ci(0); " "; ci(4)

Math C_ADD af(), bf(), cf()
Print "fadd "; cf(0); " "; cf(4)
Math C_DIV af(), bf(), cf()
Print "fdiv "; cf(0); " "; cf(4)
Math C_XOR af(), bf(), cf()
Print "fxor "; cf(0); " "; cf(4)
Math C_MULT af(), bf(), cf()
Print "fmult "; cf(0); " "; cf(4)
