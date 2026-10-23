' REFUSE: multi-line comments cannot be used inside subroutines and functions
' The same in a FUNCTION, with line numbers (FUZIX #33).
10 Print F()
20 Function F()
30 /*
40 a comment
50 */
60 F = 1
70 End Function
