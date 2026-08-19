' ON ERROR SKIP / IGNORE, MM.ERRNO and MM.ERRMSG$.  Every line has a
' counterpart on a real PicoMite: run it there and the output must be
' identical, ERROR MESSAGE INCLUDED - the last case deliberately runs
' the skip count out and stops the program, as it does there.
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

On Error Clear
Print "cleared: "; MM.Errno; " ["; MM.Errmsg$; "]"

' SKIP n covers n statements
i = 7
On Error Skip 2
i = 1 \ 0
i = 2 \ 0
Print "skip2: "; i; " "; MM.Errno

' IGNORE stays on until CLEAR or ABORT
On Error Ignore
f = Sqr(-1)
f = Log(0)
s = "still here"
Print "ignore: "; s; " ["; MM.Errmsg$; "]"
On Error Clear
Print "afterclear: "; MM.Errno

' A PRINT that fails part way prints NOTHING, not the items before the
' failure: the interpreter builds the whole line and the error takes the
' buffer with it.  So "part: " must not appear.
On Error Skip
Print "part: "; 1 / 0; " never"
Print "next: still running"

' an error inside a SUB resumes at the SUB's next statement, and the
' caller carries on
Sub Risky(n As Integer)
  Local Integer r
  r = 10 \ n
  Print "  sub: r="; r
  Print "  sub: reached the end"
End Sub

On Error Ignore
Risky 0
Print "aftersub: "; MM.Errno

' a function whose result expression fails returns without the
' assignment, so the caller gets the unset default
Function Half(n As Float) As Float
  Half = n / 0
End Function

f = Half(8)
Print "func: "; f
On Error Abort
Print "done"

' LAST, because it stops the program.  Entering a SUB costs skip count -
' the SUB line and the LOCAL are statements the interpreter executes and
' counts - so SKIP 2 does not reach the third statement inside it and
' the error is real.  Proven on a real PicoMite, which stops here.
On Error Skip 2
Risky 0
Print "not reached"
