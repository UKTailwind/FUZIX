' ON ERROR SKIP in a program with NO ON ERROR IGNORE anywhere: the
' compiler emits the checked forms for the skip windows alone, so this
' gates the window machinery specifically.  Every line has a counterpart
' on a real PicoMite: run it there and the output must be identical.
Option Explicit
Option Default None

Dim Float f
Dim Integer i

' the assignment whose expression fails must NOT happen: f keeps 99
f = 99
On Error Skip
f = 1 / 0
Print "skip1: "; f; " errno "; MM.Errno

' SKIP n covers n statements, and the count is exact: the statement
' after the window runs unchecked and untrapped
i = 7
On Error Skip 2
i = 1 \ 0
i = 2 \ 0
Print "skip2: "; i; " "; MM.Errno

' a runtime command error under SKIP is trapped exactly as before
On Error Skip
i = Str2Bin(int16, "xxx")   ' wrong length: the runtime raises
Print "cmd: "; i; " "; MM.Errno

' the domain checks fire inside a window
f = 5
On Error Skip
f = Sqr(-1)
Print "sqr: "; f; " "; MM.Errno

' a window ends where it says: this print is ordinary compiled code
On Error Clear
Print "done: "; MM.Errno
