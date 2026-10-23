' REFUSE: which changes it through 'p'
' The same through the whole structure: STRUCT CLEAR p empties g.s, which
' x$ is on a PicoMite (FUZIX issue #29).  The member's structure is an
' element here, and comes first.
Type T
  s As String
End Type
Dim g(2) As T
g(1).s = "a"
ShowW g(1), g(1).s
End
Sub ShowW(p As T, x$)
  Struct Clear p
  Print "["; x$; "]"
End Sub
