' An array ELEMENT passed to a SUB is passed BY REFERENCE.
'
' MMBasic's findvar() hands a sub a pointer to the element itself
' (MMBasic.c:2230, "set argvalue to point to the variable's data"), so a
' sub that assigns to its parameter changes the caller's array.
'
' This was translated as an expression - a copy into a temporary - so
' every write was thrown away.  Nothing failed: the program ran and
' quietly computed on the copy.  brownian.bas found it because its whole
' animation is "vector i, direction(i), 1, x(i), y(i)" updating x() and
' y() through the parameters; every atom stayed exactly where it started
' and nothing else looked wrong.
'
' The suite could not have caught it: by-ref SCALARS were covered, and
' the two translators shared the bug so they agreed with each other.

Option explicit
Option default none

Dim integer a(4), i
Dim float f(4)
Dim string s$(4) length 32
Dim integer m(2,2)

' ---- integer element
For i = 1 To 4 : a(i) = i * 10 : Next i
bump a(2)
Print "int:";
For i = 1 To 4 : Print " "; Str$(a(i)); : Next i
Print

' ---- float element
For i = 1 To 4 : f(i) = i : Next i
scale f(3), 2.5
Print "float: "; Str$(f(3), 0, 2)

' ---- string element
s$(1) = "hello"
shout s$(1)
Print "string: "; s$(1)

' ---- two dimensions
m(1,1) = 7
bump m(1,1)
Print "2d: "; Str$(m(1,1))

' ---- an expression is still a copy: a(1)+0 must NOT write back
For i = 1 To 4 : a(i) = i : Next i
bump2 (a(1) + 0)
Print "expr: "; Str$(a(1))

' ---- the element must be re-read after the call, not cached
For i = 1 To 4 : a(i) = 0 : Next i
setto a(4), 99
Print "after: "; Str$(a(4))

Print "BYREFELEM DONE"
End

Sub bump(v As integer)
  v = v + 1
End Sub

Sub bump2(v As integer)
  v = v + 1
End Sub

Sub scale(v As float, by As float)
  v = v * by
End Sub

Sub shout(t As string)
  t = UCase$(t)
End Sub

Sub setto(v As integer, n As integer)
  v = n
End Sub
