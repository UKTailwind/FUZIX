' A count a FUNCTION sets goes on in its caller (PicoMite issue 34).
' g's ON ERROR SKIP 5 is 6: its own end leaves 5, g = ... 4, END
' FUNCTION 3, the statement that called g 2 and the PRINT 1 - so the
' first ERROR after them is skipped and the second is not.
Function g(a)
  On Error Skip 5
  g = a * 10
End Function

y = g(1)
Print "y ="; y
Error "one"
Print "after one: "; MM.ErrMsg$
Error "two"
Print "not reached"
