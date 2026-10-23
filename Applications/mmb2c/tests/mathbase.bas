Option Base 1
' Under OPTION BASE 1 MATH answers in subscripts: MATH(CROSSING) adds the
' base (ec27921), as MATH(MAX/MIN a(), idx%) always did (MATHS.c:3636) -
' both were offsets here.  MATH SCALE of an integer array by a fraction
' rounds each product (MATHS.c:1599); the scale was rounded first, so
' 5 * 0.3 gave 0.  MATH WINDOW of a constant input gives outmin (d4deef9),
' where it divided by zero.  Run on a real PicoMite: the output must be
' identical.
Dim Float a(5)
a(1) = -1 : a(2) = 1 : a(3) = 2 : a(4) = -3 : a(5) = 4
Print "cross"; Math(CROSSING a())
Print "none"; Math(CROSSING a(), 99)
Dim Integer k
Print "max"; Math(MAX a(), k); k
Print "min"; Math(MIN a(), k); k
Dim Integer s(3), d(3)
s(1) = 5 : s(2) = 7 : s(3) = -5
Math Scale s(), 0.3, d()
Print "scale"; d(1); d(2); d(3)
Math Scale s(), 2, d()
Print "scale2"; d(1); d(2); d(3)
Dim Float c(3), oc(3)
c(1) = 4 : c(2) = 4 : c(3) = 4
Math Window c(), 1, 2, oc()
Print "const"; oc(1); oc(2); oc(3)
