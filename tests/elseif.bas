' ELSEIF, both ways MMBasic spells it.
'
' AllCommands.h has "Else If" AND "ElseIf" as separate rows binding to
' the same handler, so they are one keyword with two spellings - not an
' ELSE with an IF after it.  Taken as two words the second spelling
' opened a nested block that wanted its own ENDIF, and a program
' written the way the manual writes it died with "unterminated if
' block" on the line after it.
Dim integer n, seen

For n = 1 To 4
  If n = 1 Then
    Print "one   (IF)"
  ElseIf n = 2 Then
    Print "two   (ElseIf)"
  Else If n = 3 Then
    Print "three (Else If)"
  Else
    Print "four  (ELSE)"
  EndIf
Next n

' the two spellings must reach the same branch, so a chain that mixes
' them counts once per value and never falls through twice
seen = 0
For n = 1 To 4
  If n = 1 Then
    seen = seen + 1
  Else If n = 2 Then
    seen = seen + 10
  ElseIf n = 3 Then
    seen = seen + 100
  Else If n = 4 Then
    seen = seen + 1000
  EndIf
Next n
Print "chain ="; seen

' a single line IF is not a block, so ELSE IF there is a nested IF and
' always was - it must still work, and mean the same thing
For n = 1 To 3
  If n = 1 Then Print "line one"; Else If n = 2 Then Print "line two"; Else Print "line three";
  Print ""
Next n

' ELSE IF inside a multi-line block, with the branch body on the same
' line after THEN
n = 2
If n = 1 Then
  Print "not this"
Else If n = 2 Then Print "same line body"
EndIf

Print "done"
