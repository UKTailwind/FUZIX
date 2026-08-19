' Multi-dimensional DIM initialiser lists fill in MMBasic's storage
' order: the FIRST subscript varies fastest (cmd_dim fills linear
' array memory, Commands.c:8658).  So the first four values here are
' dpm(0,0)..dpm(3,0) and the next four dpm(0,1)..dpm(3,1).  Run this
' on a real PicoMite: the output must be identical.
Option Explicit
Option Default Integer

Dim dpm(3,1) = (82,92,93,94, 68,71,75,79)
Dim i
For i = 0 To 3
  Print i; " "; dpm(i,0); " "; dpm(i,1)
Next i

' three dimensions, same rule
Dim Float t(1,1,1) = (1,2,3,4,5,6,7,8)
Print t(0,0,0); t(1,0,0); t(0,1,0); t(1,1,0)
Print t(0,0,1); t(1,0,1); t(0,1,1); t(1,1,1)

' a CONST bound folds like a literal one
Const n = 2
Dim g(n,1) = (10,11,12, 20,21,22)
Print g(0,0); g(1,0); g(2,0); g(0,1); g(1,1); g(2,1)
