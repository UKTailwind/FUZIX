' ON ERROR SKIP / IGNORE, MM.ERRNO and MM.ERRMSG$.  Every line here has
' a counterpart on a real PicoMite: run it there and the output must be
' identical.
Option Explicit
Option Default None

Dim Float f
Dim Integer i
Dim String s

' the assignment whose expression fails must NOT happen: f keeps 99
f = 99
On Error Skip
f = 1 / 0
Print "skip1: "; f; " errno "; MM.Errno; " ["; MM.Errmsg$; "]"

' and the protection lasts exactly one statement
On Error Clear
Print "cleared: "; MM.Errno; " ["; MM.Errmsg$; "]"

' SKIP n covers n statements
i = 7
On Error Skip 2
i = 1 \ 0
i = 2 \ 0
Print "skip2: "; i; " "; MM.Errno

' IGNORE stays on until ABORT
On Error Ignore
f = Sqr(-1)
f = Log(0)
s = "still here"
Print "ignore: "; s; " ["; MM.Errmsg$; "]"
On Error Clear
Print "afterclear: "; MM.Errno

' the rest of a failed statement is skipped - nothing after the failure
' prints, exactly as the interpreter jumps away
On Error Skip
Print "part: "; 1 / 0; " never"
Print "next: still running"

' an error inside a SUB resumes at the SUB's next statement, and the
' caller carries on normally
Sub Risky(n As Integer)
  Local Integer r
  r = 10 \ n
  Print "  sub: r="; r
  Print "  sub: reached the end"
End Sub

On Error Skip 2
Risky 0
Print "aftersub: "; MM.Errno

' a function whose expression fails returns without the assignment
Function Half(n As Float) As Float
  Half = n / 0
End Function

On Error Skip 2
f = Half(8)
Print "func: "; f

On Error Abort
Print "done"
