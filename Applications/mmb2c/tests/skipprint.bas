' cmd_print writes its whole line when the PRINT ends, or nothing if it
' fails (FUZIX issue #30): a FUNCTION's PRINT that fails under a SKIP
' window prints nothing of itself, and the FUNCTION goes on after it, as
' the routines a window can run are guarded as the window is.  A SUB's
' PRINT is written when it ends, not when the caller's statement does.
On Error Skip
y = f()
Print "after"
On Error Skip
S
Print "end"
End
Function f()
  Print "p1"
  Print "part "; Eof(5)
  Print "p2"
  f = 1
End Function
Sub S
  Print "s1"
  Print "s2"
End Sub
