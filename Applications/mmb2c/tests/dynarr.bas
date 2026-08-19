' dynarr.bas - DIM with a bound worked out while the program runs, and
' REDIM.
'
' An array whose bounds are not known at translate time cannot be a C
' array - the bounds would have to be in its type - so it becomes what
' an array PARAMETER already is here: a flat pointer plus a bounds
' table.  Indexing, BOUND() and whole-array operations then need no new
' code at all; the only new machinery is the allocation.

Option Explicit
Option Base 0

Dim n% = 4, m% = 2
Dim a%(n%)                     ' 0..4
Dim g(n%, m%)                  ' two dimensions, both run-time
Dim s$(n%)
Dim k%, j%, t%

Print "-- one dimension"
For k% = 0 To n%
  a%(k%) = k% * k%
Next k%
Print "bound     "; Bound(a%(), 1)
Print "values    ";
For k% = 0 To Bound(a%(), 1) : Print a%(k%); : Next k%
Print

Print "-- two dimensions, row-major like every other array"
For k% = 0 To n%
  For j% = 0 To m%
    g(k%, j%) = k% * 10 + j%
  Next j%
Next k%
Print "bounds    "; Bound(g(), 1); Bound(g(), 2)
Print "g(3,2)    "; g(3, 2)
Print "g(0,0)    "; g(0, 0)
Print "g(4,2)    "; g(4, 2)

Print "-- strings"
For k% = 0 To n%
  s$(k%) = "item" + Str$(k%)
Next k%
Print "s$(2)     " + s$(2)
Print "s$(4)     " + s$(4)

Print "-- a whole array still passes to a SUB"
total(a%(), t%)
Print "sum       "; t%

Print "-- MATH works on it too"
Math Set 7, a%()
Print "after set "; a%(0); a%(4)

Print "-- REDIM discards, REDIM PRESERVE keeps"
Dim p%(3)
For k% = 0 To 3 : p%(k%) = k% + 1 : Next k%
Redim Preserve p%(6)
Print "grown     "; Bound(p%(), 1); " :";
For k% = 0 To 6 : Print p%(k%); : Next k%
Print
Redim Preserve p%(2)
Print "shrunk    "; Bound(p%(), 1); " :";
For k% = 0 To 2 : Print p%(k%); : Next k%
Print
Redim p%(4)
Print "plain     "; Bound(p%(), 1); " :";
For k% = 0 To 4 : Print p%(k%); : Next k%
Print

Print "-- ERASE really gives a run-time array back"
Erase p%()
Print "erased    "; Bound(p%(), 0)

Print "-- and it works inside a SUB, per invocation"
local_one(3)
local_one(6)

Sub total(v%(), out%)
  Local i%
  out% = 0
  For i% = 0 To Bound(v%(), 1)
    Inc out%, v%(i%)
  Next i%
End Sub

Sub local_one(sz%)
  Local q%(sz%), i%
  For i% = 0 To sz% : q%(i%) = i% : Next i%
  Print "local"; sz%; "  bound"; Bound(q%(), 1); " last"; q%(sz%)
End Sub
