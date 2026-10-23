' REFUSE: which changes the member itself
' The same through a structure array: which element is not known when
' translating, so a write to any element's member counts, and so does
' STRUCT CLEAR of the whole structure.
Type T
  s As String
End Type
Dim a(2) As T, g As T
a(1).s = "c"
Sub Show(x$)
  Struct Clear g
  a(2).s = "d"
  Print x$
End Sub
Show a(1).s
