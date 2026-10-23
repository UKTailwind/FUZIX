' A structure member written in place: READ, INC and CAT into a member
' and into an element's member, and a numeric member passed by
' reference, which findvar hands over as a pointer with the member's own
' type (MMBasic.c:3390-3412).  A string member goes to a by-reference
' parameter its routine only reads as a copy.  These all took "p.x" for a variable of its
' own - an implied FLOAT - and the member never changed.  Run on a real
' PicoMite: the output must be identical.
Type pt
  x As Integer
  f As Float
  s As String Length 6
End Type
Dim p As pt, a(2) As pt
Sub Bump(v%)
  v% = v% + 1
End Sub
Sub Half(v!)
  v! = v! / 2
End Sub
Sub Show(BYVAL t$)
  Print "show "; t$
End Sub
Sub Relay(u$)
  Shout u$
End Sub
Sub Shout(t$)
  Print "shout "; UCase$(t$)
End Sub
Read p.x, p.f, p.s
Print p.x; p.f; " "; p.s
Read a(1).x, a(2).s
Print a(1).x; " "; a(2).s
Inc p.x, 10
Inc p.f
Cat p.s, "!"
Print p.x; p.f; " "; p.s
Bump p.x
Bump a(1).x
Half p.f
Print p.x; a(1).x; p.f
Show p.s
Shout p.s
Mid$(p.s, 2, 2) = "XY"
Print "mid "; p.s
Relay p.s
ON ERROR SKIP 1
Cat p.s, "toolong"
Print "cat: " MM.ERRMSG$; " "; p.s
Data 5, 2.5, "abc", 7, "xyz"
