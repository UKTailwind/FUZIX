' A whole structure assigned into a nested member, and a nested member
' assigned whole (PicoMite 9749c83, 10-21 ADD): each is copied at the
' member's own TYPE, and the members around it are untouched.  Both
' were refused, because the firmware used to copy the outer TYPE's size.
Type inner
  a As Integer
  s As String Length 5
  f As Float
End Type
Type outer
  n As Integer
  in As inner
  ins(2) As inner
  z As Integer
End Type
Dim q As inner, o As outer, p As outer, r As inner
q.a = 7 : q.s = "hello" : q.f = 1.5
o.n = 1 : o.z = 99
o.in = q
Print o.n; o.in.a; " "; o.in.s; o.in.f; o.z
q.a = 8 : q.s = "x"
o.ins(1) = q
Print o.ins(1).a; " "; o.ins(1).s; o.ins(0).a; o.ins(2).a; o.z
r = o.in
Print r.a; " "; r.s; r.f
r = o.ins(1)
Print r.a; " "; r.s
p.in = o.ins(1)
Print p.in.a; " "; p.in.s; p.n; p.z
t o
Print o.in.a; " "; o.in.s
Sub t(x As outer)
  Local y As inner
  y.a = 3 : y.s = "sub"
  x.in = y
End Sub
