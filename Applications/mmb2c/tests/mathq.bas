Option Base 0
' The quaternions: Q_CREATE, Q_EULER, Q_VECTOR, Q_INVERT, Q_MULT and
' Q_ROTATE.
'
' A quaternion here is FIVE floats, not four: w, x, y, z, and a
' MAGNITUDE carried alongside.  The first four are always normalised
' and element 4 holds the scale that was taken out, which is why
' Q_VECTOR of (3, 4, 12) gives the unit vector and 13, and why
' Q_ROTATE hands the same 13 back with the rotated direction.  Every
' one of these refuses an array that is not exactly five long.
'
' Two conventions that look like slips and are the reference's:
' Q_EULER NEGATES the yaw, and Q_CREATE halves theta before OPTION
' ANGLE rather than after.  Both are copied deliberately.
'
' Every line is blessed against a real MMBasic 6.03.02 with
' devtools/ab.py - the values and the nine refusals alike.  The
' wordings came off the board rather than out of MATHS.c, which was
' worth doing: cmd_math passes an argument number of 31 for Q_MULT's
' and Q_ROTATE's destination, and it never reaches the message.

Option Angle Degrees
' A quaternion here is FIVE floats: w, x, y, z and a magnitude carried
' alongside, so DIM q(4) under BASE 0.
Dim Float q(4), r(4), n(4), v(4), t(4)

Math Q_Create 90, 0, 0, 1, q()
Print "create"; q(0); q(1); q(2); q(3); q(4)

Math Q_Euler 30, 40, 50, r()
Print "euler "; r(0); r(1); r(2); r(3); r(4)

Math Q_Vector 3, 4, 12, v()
Print "vector"; v(0); v(1); v(2); v(3); v(4)

Math Q_Invert q(), n()
Print "invert"; n(0); n(1); n(2); n(3); n(4)

Math Q_Mult q(), r(), t()
Print "mult  "; t(0); t(1); t(2); t(3); t(4)

Math Q_Rotate q(), v(), n()
Print "rotate"; n(0); n(1); n(2); n(3); n(4)

' the destination may be a source
Math Q_Mult q(), r(), q()
Print "alias "; q(0); q(1); q(2); q(3); q(4)

' the wrong-size refusals, in MMBasic's own words
Dim Float bad(3), ok(4)
On Error Skip 1
Math Q_Invert bad(), ok()
Print "e inv1:"; MM.ErrMsg$
On Error Skip 1
Math Q_Invert ok(), bad()
Print "e inv2:"; MM.ErrMsg$
On Error Skip 1
Math Q_Mult bad(), ok(), ok()
Print "e mul1:"; MM.ErrMsg$
On Error Skip 1
Math Q_Mult ok(), bad(), ok()
Print "e mul2:"; MM.ErrMsg$
On Error Skip 1
Math Q_Mult ok(), ok(), bad()
Print "e mul3:"; MM.ErrMsg$
On Error Skip 1
Math Q_Create 90, 0, 0, 1, bad()
Print "e cre :"; MM.ErrMsg$
On Error Skip 1
Math Q_Vector 1, 2, 3, bad()
Print "e vec :"; MM.ErrMsg$
On Error Skip 1
Math Q_Euler 1, 2, 3, bad()
Print "e eul :"; MM.ErrMsg$
On Error Skip 1
Math Q_Rotate ok(), ok(), bad()
Print "e rot3:"; MM.ErrMsg$
