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
