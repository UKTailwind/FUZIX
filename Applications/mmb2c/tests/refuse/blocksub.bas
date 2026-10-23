' REFUSE: multi-line comments cannot be used inside subroutines and functions
' The manual's rule (FUZIX #33): a /* ... */ block stays outside every SUB
' and FUNCTION.
S
Sub S
  /*
  a comment
  */
  Print "s"
End Sub
