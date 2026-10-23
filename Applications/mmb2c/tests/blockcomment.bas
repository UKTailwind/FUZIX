' A /* ... */ block is raw text to the tokeniser, so a SUB in one is not
' there at all (FUZIX issue #19).  By the manual's rule /* and */ are the
' first thing on their lines, with a space or the end of the line after
' them, and anything after a */ is comment too (FUZIX #33) - RC5 still
' runs a statement after "*/ :", which the manual does not allow.
Print "before"
/* the rest of this line is comment, and a */ in it ends nothing
Sub S
  Print "commented S"
End Sub
Print "inside"
*/ the rest of this line is comment : Print "not run"
Print "after"
*/ a */ with no block open does nothing
Print "a /* in a string"
S
Sub S
  Print "real S"
End Sub
/*
a block between routines is outside them both
*/
Function F()
  F = 1
End Function
Print F()
