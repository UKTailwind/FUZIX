' REFUSE: Cannot change a constant
' MATH WINDOW writes the input's range into its last two arguments, and
' findvar refuses a CONST there (d4deef9).
Const LO = 0
Dim Float w(3), o(3), hi
w(0) = 1 : w(1) = 2 : w(2) = 3 : w(3) = 4
Math Window w(), 0, 1, o(), LO, hi
