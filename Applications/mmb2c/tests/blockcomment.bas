' A /* ... */ block is raw text to the tokeniser, so a SUB in one is not
' there at all, and the line after a */ statement goes on as BASIC
' (FUZIX issue #19).  /* opens a block only where a statement starts.
Print "before"
/*
Sub S
  Print "commented S"
End Sub
Print "inside"
*/
Print "after"
x = 1 : /* a block from the middle of a line
Print "still inside" */ ignored
  */ the rest of this statement is ignored : Print "after the second"
Print "a /* in a string", x
S
Sub S
  Print "real S"
End Sub
