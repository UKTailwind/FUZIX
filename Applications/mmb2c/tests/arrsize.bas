' ARRAY ADD and MATH SCALE check that the two arrays are the same
' length, and SPRITE LOADARRAY takes a one- OR two-dimensional array.
'
' The two size-mismatch wordings differ and that is not a slip.  Asked
' on a real MMBasic 6.03.02 under ON ERROR SKIP: ARRAY ADD raises
' "Array size mismatch" (StandardError(16), in array_add) and MATH SCALE
' raises "Size mismatch" (a bare error(), in cmd_math).  The same probe
' answered a third question nobody had asked: `ARRAY SCALE` is "Unknown
' command" there, because SCALE lives only in cmd_math.  We refuse it in
' our own words rather than emit code MMBasic would not run.
'
' Until 2026-08-24 none of these checked anything: a destination shorter
' than its source was written past the end, in silence.
'
' SPRITE LOADARRAY's second form is ours, not MMBasic's - the reference
' says "Argument 4 must be a 1D numerical array".  It costs no code: the
' first BASIC subscript is the adjacent one, so DIM s(w-1,h-1) walked
' flat is the raster row by row.  The 1-D form is MMBasic's own, read as
' w*h pixels in sequence.

Option Base 0

Dim Integer a(3), b(4), c(3)
Dim Float f(3), g(4)
Dim String s(3), t(4)

On Error Skip 1
Array Add a(), 1, b()
Print "add int  :"; MM.ErrMsg$
On Error Skip 1
Array Add f(), 1, g()
Print "add flt  :"; MM.ErrMsg$
On Error Skip 1
Array Add s(), "x", t()
Print "add str  :"; MM.ErrMsg$
On Error Skip 1
Math Scale a(), 2, b()
Print "scale    :"; MM.ErrMsg$

' and the matching lengths still work
a(0) = 1 : a(1) = 2 : a(2) = 3 : a(3) = 4
Array Add a(), 10, c()
Print "add ok   :"; c(0); c(1); c(2); c(3)
Math Scale a(), 3, c()
Print "scale ok :"; c(0); c(1); c(2); c(3)
s(0) = "a" : s(1) = "b" : s(2) = "c" : s(3) = "d"
Dim String u(3)
Array Add s(), "!", u()
Print "adds ok  : "; u(0); " "; u(1); " "; u(2); " "; u(3)
