' OPTION ANGLE DEGREES - MMBasic's optionangle, folded at translation.
' SIN/COS/TAN take degrees in, ATN/ATAN2/ASIN/ACOS give degrees out,
' and DEG( / RAD( are untouched by it (they are the explicit converters).
Option Angle Degrees
Option Explicit

Dim float x

Print Sin(90)
Print Sin(30)
Print Cos(0)
Print Cos(60)
Print Tan(45)

Print Atn(1)
Print Asin(1)
Print Acos(0)
Print Atan2(1, 1)
Print Atan2(0, -1)

' the round trip: degrees in, degrees out
x = Asin(Sin(37))
Print Str$(x, 0, 6)

' DEG( and RAD( ignore OPTION ANGLE - they say what they convert
Print Str$(Deg(Rad(45)), 0, 6)
Print Str$(Rad(180), 0, 6)

' and it reaches inside a SUB, because it is a whole-program setting
Sub Show(a As Float)
  Print Str$(Sin(a), 0, 6)
End Sub

Show(90)
Show(270)
