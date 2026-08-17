' CALL by name: the statement form runs a SUB named in a string, the
' function form Call() runs a FUNCTION and yields its value.  Names
' match case-insensitively, with or without the type suffix, and
' trailing arguments may be omitted exactly as in a direct call.  Run
' on a real PicoMite: the output must be identical.
Option Explicit
Option Default Integer

Dim which$

Sub greet(n)
  Print "greet "; n
End Sub

Sub shout(n)
  Print "SHOUT "; n * 2
End Sub

Function twice$(n)
  twice$ = Str$(n * 2)
End Function

Function thrice$(n)
  thrice$ = Str$(n * 3)
End Function

Function label$(n, m)
  label$ = "name" + Str$(n) + Str$(m)
End Function

Function optarg$(init)
  If init Then optarg$ = "setup" Else optarg$ = "polled"
End Function

which$ = "greet"
Call which$, 5
Call "shout", 5
Print Call("twice$", 7)
which$ = "THRICE$"
Print Call(which$, 7)
Print Call("label$", 4, 2)
Print Call("label$", 4)
which$ = "optarg$"
Print Call(which$)
Print Call(which$, 1)
