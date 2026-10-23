' A line number may come twice: NameLabels checks only labels, and GOTO
' finds the line where the number comes first (FUZIX issue #14).
10 Print "first"
10 Print "second"
20 Print "DONE"
30 n = n + 1 : If n < 2 Then GoTo 10
