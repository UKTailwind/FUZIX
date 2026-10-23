' A name of 32 characters or more fails only when its line runs (FUZIX
' issue #16): findvar's "Variable name too long", for a STATIC's hidden
' name too, and findlabel's "Label too long".  A long name with its ':'
' is no label to the tokeniser, so that line is "Unknown command".
Sub s234567890123456
  Static v234567890123456
  Print "after the static"
End Sub
Print "hi"
On Error Ignore
abcdefghijklmnopqrstuvwxyz012345% = 1
Print "1 "; MM.ErrMsg$
On Error Clear
x = abcdefghijklmnopqrstuvwxyz0123456 + 1
Print "2 "; MM.ErrMsg$; x
On Error Clear
s234567890123456
Print "3 "; MM.ErrMsg$
On Error Clear
GoTo abcdefghijklmnopqrstuvwxyz012345
Print "4 "; MM.ErrMsg$
On Error Clear
abcdefghijklmnopqrstuvwxyz012345:
Print "5 "; MM.ErrMsg$
On Error Clear
If 1 Then abcdefghijklmnopqrstuvwxyz012345 = 2 Else Print "else"
Print "6 "; MM.ErrMsg$
On Error Clear
If 0 Then abcdefghijklmnopqrstuvwxyz012345 = 2 Else Print "else"
Print "7 "; MM.ErrMsg$
On Error Clear
Dim abcdefghijklmnopqrstuvwxyz012345(3)
Print "8 "; MM.ErrMsg$
On Error Clear
Const abcdefghijklmnopqrstuvwxyz012345 = 7
Print "9 "; MM.ErrMsg$
End
abcdefghijklmnopqrstuvwxyzabcdef = 1
