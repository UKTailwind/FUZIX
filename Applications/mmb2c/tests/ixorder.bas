' findvar checks each subscript as it is evaluated, left to right - a
' float through FloatToInt32 ("Number too large"), an integer against 32
' bits ("Index out of bounds"), then the base ("Dimensions") - and raises
' at the first that fails, the tops only after all of them (FUZIX issue
' #35).  LINE INPUT finds its variable before it reads the line, and a
' FUNCTION called in a subscript keeps its own checks to itself.
Dim r%(3, 3), m%(3, 3), a%(3), s$(3)
r%(1, 3) = 7
On Error Skip : r%(-1, -5000000000) = 1 : Print MM.ErrMsg$
On Error Skip : r%(-5000000000, -1) = 1 : Print MM.ErrMsg$
On Error Skip : r%(9, -1) = 1 : Print MM.ErrMsg$
On Error Skip : x = a%(-5E9) : Print MM.ErrMsg$
On Error Skip : x = r%(1, -5E9) : Print MM.ErrMsg$
On Error Skip : x = r%(-5E9, -1) : Print MM.ErrMsg$
x = r%(1.4, 2.6) : Print x
On Error Skip : Line Input s$(5) : Print MM.ErrMsg$
Line Input s$(0)
Print s$(0)
On Error Ignore
x = m%(f(), 9)
Print y; " "; MM.ErrMsg$
End
Function f()
  y = r%(1, 1) + 5
  f = 1
End Function
