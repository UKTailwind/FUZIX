' REFUSE: Duplicate label
' The reference refuses a program with a label defined twice before it
' runs a line (825ade6, PrepareProgram's NameAdd).  Labels are one name
' space, so a second one inside a SUB counts too.
here:
Print 1
Sub S
here:
  Print 2
End Sub
