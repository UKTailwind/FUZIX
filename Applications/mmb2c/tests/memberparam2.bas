' A string member passed by reference where the routine writes another
' element of the same array - through a structure parameter given it, or
' under the array's own name: the write reaches that element, not the
' member, so the copy is exact and it translates (FUZIX issue #38) - two
' literal subscripts that differ are two elements.
Type T
  s As String
End Type
Dim ga(3) As T
ga(1).s = "a" : ga(2).s = "z"
ShowP ga(1).s, ga(2)
Print ga(1).s; " "; ga(2).s
Show ga(1).s
Print ga(3).s
Sub ShowP(x$, p As T)
  p.s = "y"
  Print x$; " "; p.s
End Sub
Sub Show(x$)
  ga(3).s = "w"
  Print x$
End Sub
