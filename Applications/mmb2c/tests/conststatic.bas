' A CONST of a float sizes a STATIC array (FUZIX issue #22).  MIN and MAX
' are floats now, as the firmware has them, so Const K = Min(2, 3) is 2.0,
' and a STATIC array is C storage whose size must be a constant: the bound
' is converted as the program is translated, as mm_toint would convert it.
Const K = Min(2, 3)
Const N = 4.0
Const R = 2.5
S
End
Sub S
  Static z%(K), w(N), q%(R)
  z%(K) = 5
  w(N) = 1.5
  q%(R) = 7
  Print z%(K); w(N); q%(3)
End Sub
