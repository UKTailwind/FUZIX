' REFUSE: structure member p.x is not translated here
' A member reached from a place that takes only plain variables - here a
' FOR's control variable - was made an implied FLOAT called "p.x", and
' the member never changed.
Type pt
  x As Integer
End Type
Dim p As pt
For p.x = 1 To 3
Next
Print p.x
