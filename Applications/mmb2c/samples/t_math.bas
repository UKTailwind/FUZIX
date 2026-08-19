' TEST 1 of the shared-libm protocol: does the kernel's copy compute the
' same answers as bcrun's own?
'
' Every function in the PICOIOC_LIBM table, over a spread of arguments
' including the awkward ones (range reduction past 2*pi, values near the
' domain edges, negatives).  The host build calls bcrun's local libm and
' the board build calls the kernel's, so the two outputs must match
' character for character - that IS the test, and it is why everything
' prints through the same format.

Option default float
Dim integer i
Dim x

Print "-- one argument --"
For i = 0 To 12
  x = -3 + i * 0.5
  Print Str$(x, 2, 2); " ";
  Print Str$(Sin(x), 2, 6); " ";
  Print Str$(Cos(x), 2, 6); " ";
  Print Str$(Atn(x), 2, 6)
Next

Print "-- range reduction --"
For i = 1 To 6
  x = i * 100
  Print Str$(x, 5, 0); " "; Str$(Sin(x), 2, 6); " "; Str$(Cos(x), 2, 6)
Next

Print "-- domain edges --"
For i = 0 To 8
  x = -1 + i * 0.25
  Print Str$(x, 2, 2); " "; Str$(Asin(x), 2, 6); " "; Str$(Acos(x), 2, 6)
Next

Print "-- exp/log/sqrt --"
For i = 1 To 8
  x = i * 1.7
  Print Str$(x, 3, 2); " ";
  Print Str$(Exp(x), 6, 4); " ";
  Print Str$(Log(x), 2, 6); " ";
  Print Str$(Log(x) / Log(10), 2, 6); " ";
  Print Str$(Sqr(x), 2, 6)
Next

Print "-- two argument --"
For i = 1 To 6
  Print Str$(i, 2, 0); " ";
  Print Str$(i ^ 2.5, 8, 4); " ";
  Print Str$(Atan2(i, 3), 2, 6); " ";
  Print Str$(i * 7 Mod 3.5, 2, 4)
Next

Print "-- rounding --"
For i = 0 To 6
  x = -2.75 + i * 0.9
  Print Str$(x, 2, 2); " ";
  Print Str$(Int(x), 3, 0); " ";
  Print Str$(Fix(x), 3, 0); " ";
  Print Str$(Abs(x), 2, 4)
Next

Print "done"
