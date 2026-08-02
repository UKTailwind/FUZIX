' Comparing against a unary minus.
'
' The Thumb backend parks the left operand of a fused push/op in r2:r3.
' BC_NEGD and BC_NEG64 both used r2 as a scratch, so a window containing
' one handed the operator a destroyed operand: the comparison was then
' neither equal, less than nor greater than.  `If i = -p Then` silently
' never fired, which took most of the ripple benchmark's picture with it
' and looked like a graphics fault for a whole session.
'
' Every line here is a compare whose right-hand side is a negation.

p = 159.0
i = -p
If i = -p Then Print "d eq" Else Print "d EQ FAILED"
If i <> -p Then Print "d NE WRONG" Else Print "d ne"
If i < -p Then Print "d LT WRONG"
If i > -p Then Print "d GT WRONG"

' the shape it actually broke in: the first pass of a FOR whose start is
' a negation, used to reset per-column state
q = 0.5
n = 0
For x = 0 To 3
  s = x * x
  r = Sqr(159.0 * 159.0 - s)
  For j = -r To r Step 3
    If j = -r Then n = n + 1
  Next j
Next x
Print "d resets "; n

' the same for 64-bit integers, which used r2 the same way
pi% = 159
ii% = -pi%
If ii% = -pi% Then Print "i eq" Else Print "i EQ FAILED"
If ii% <> -pi% Then Print "i NE WRONG" Else Print "i ne"
If ii% < -pi% Then Print "i LT WRONG"
If ii% > -pi% Then Print "i GT WRONG"

k% = 0
For y% = 1 To 4
  m% = y% * 100
  If -m% = -m% Then k% = k% + 1
  If 0 - m% = -m% Then k% = k% + 1
Next y%
Print "i resets "; k%
