' LOCAL arrays and strings live in a block taken per invocation, so
' this is where recursion has to be right: each call must see its own
' copy, and must still see it after a deeper call has come and gone.
'
' Every number here is checkable by hand, which is the point - the
' expected output was not taken from a run.

Print "-- LOCAL scalars, arrays and strings --"

Sub simple
  Local Integer a(3)
  Local s$
  Local Integer i
  For i = 0 To 3
    a(i) = i * 11
  Next i
  s$ = "abc" + "def"
  Print "  simple: a(0)="; a(0); " a(3)="; a(3); " s$=["; s$; "]"
End Sub

simple

' Recursion: each level fills its own array with its own depth, then
' calls deeper, then re-reads.  If the levels shared a block the
' re-read would come back with the innermost level's value.
Sub level(d As Integer)
  Local Integer mine(4)
  Local tag$
  Local Integer i
  For i = 0 To 4
    mine(i) = d * 100 + i
  Next i
  tag$ = "d" + Str$(d)
  If d < 3 Then
    level(d + 1)
  End If
  Print "  level "; d; " after: mine(0)="; mine(0); " mine(4)="; mine(4);
  Print " tag$=["; tag$; "]"
End Sub

Print "-- recursion --"
level 1

' A STATIC local must NOT move into the per-invocation block: it is
' meant to survive between calls.
Sub counter
  Static Integer n = 0
  Local Integer scratch(2)
  scratch(0) = 7
  n = n + 1
  Print "  counter n="; n; " scratch(0)="; scratch(0)
End Sub

Print "-- STATIC survives, LOCAL does not --"
counter
counter
counter

' A function with a local string array, returning a string.
Function pick$(k As Integer)
  Local w$(3)
  w$(0) = "zero"
  w$(1) = "one"
  w$(2) = "two"
  w$(3) = "three"
  pick$ = w$(k)
End Function

Print "-- local string array in a function --"
Print "  pick(0)=["; pick$(0); "] pick(3)=["; pick$(3); "]"

' A leaked block would be invisible in any of the above: they each run
' a handful of times.  1608 bytes a call, 2000 calls, is 3.2MB - more
' than any heap here - so this finishes only if every exit really does
' give the block back.
Sub churn
  Local Integer big(200)
  big(0) = 1
End Sub

Print "-- no leak over many calls --"
For j = 1 To 2000
  churn
Next j
Print "  2000 calls survived"

Print "-- done --"
