' REFUSE: which changes the member itself
' The same through a structure array: which element a variable index
' names is not known when translating, so a write to any element's
' member counts (two literal indices that differ are two elements, FUZIX
' #38), and so does STRUCT CLEAR of the whole structure.
Type T
  s As String
End Type
Dim a(2) As T, g As T, k
a(1).s = "c"
k = 1
Sub Show(x$)
  Struct Clear g
  a(k).s = "d"
  Print x$
End Sub
Show a(1).s
