' A FOR ... NEXT or a DO ... LOOP opened and closed inside a single-line IF
' (a user's Life program, 10 Nov 2026: refused at the FOR, before the NEXT
' that closes it, and the block left open made the last SUB "unterminated")
Dim integer n, k, s
k = 1
If k = 1 Then For n = 1 To 3 : Print n; : Next n : Print
If k = 2 Then For n = 1 To 3 : Print "never" : Next n
If k = 2 Then Print "no" Else For n = 4 To 6 : Print n; : Next : Print
s = 0
If k = 1 Then Do : Inc s : Loop Until s = 5
Print "s ="; s
show
End

Sub show
  Print "the SUB after"
End Sub
