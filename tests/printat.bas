' PRINT @(x, y [, mode]) - MMBasic's fun_at.
'
' On a screen this puts the text at a pixel position and draws it into
' whatever is being drawn on.  The gates have no screen, so what is
' checked here is the half that does not need one: that it parses in
' every position a PRINT item can occupy, that it is a FUNCTION
' returning "" rather than a statement - so the text either side of it
' still comes out, in order, with the semicolons and commas doing what
' they always did - and that it leaves ordinary PRINT alone.

Option Explicit
Dim Integer t

t = 42

Print "-- plain PRINT is unchanged --"
Print "a"; "b"; "c"
Print 1, 2

Print "-- @ before the text --"
Print @(0, 0) "at the origin"

Print "-- @ with a mode --"
Print @(8, 16, 1) "over the top"
Print @(8, 32, 2) "reversed"

Print "-- @ mixed with items and separators --"
Print @(0, 48) "t="; t; " done"
Print @(0, 60) "x", "y"

Print "-- @ twice in one statement --"
Print @(0, 72) "left" @(100, 72) "right"

Print "-- @ with expressions for the position --"
Print @(t * 2, t + 6) "computed"

Print "-- and a trailing semicolon still flushes --"
Print @(0, 90) "partial";
Print
Print "-- done --"
