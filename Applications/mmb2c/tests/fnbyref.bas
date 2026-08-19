' fnbyref.bas - a FUNCTION passing its OWN NAME to a routine by
' reference.
'
' Inside a FUNCTION its name is a variable - the return value - and
' MMBasic passes it by reference like any other.  The argument
' classifier used to skip every name that was a routine, so this one
' went by VALUE through a temporary: the callee wrote into the
' temporary and the function returned whatever it had before.  It
' compiled and it ran, which is what made it bad.
'
' Found in Pico-Vaders, whose controller layer is built on it:
'     Function twait%(duration%, mask%)
'       Call ctrl$, twait%
' so every button read came back zero and the game could not be
' started.
'
' Assigning to the return variable, reading it and returning it were
' never broken; only this.  Both are here so a regression in either is
' visible.

Option Explicit

Sub fill_i(x%)
  If x% < 0 Then Exit Sub
  x% = 42
End Sub

Sub fill_f(x!)
  x! = 1.5
End Sub

Sub fill_s(x$)
  x$ = "filled"
End Sub

Sub add_to(x%, n%)
  Inc x%, n%
End Sub

Function by_ret%()
  by_ret% = 0
  fill_i(by_ret%)
End Function

Function by_local%()
  Local t%
  t% = 0
  fill_i(t%)
  by_local% = t%
End Function

' The value already in it must reach the callee, not just come back.
Function accumulate%()
  accumulate% = 7
  add_to(accumulate%, 3)
  add_to(accumulate%, 10)
End Function

Function ret_f!()
  ret_f! = 0
  fill_f(ret_f!)
End Function

Function ret_s$()
  ret_s$ = ""
  fill_s(ret_s$)
End Function

' The negative-argument guard the Game*Mite drivers use: the callee
' returns without writing, so the value set before the call stands.
Function untouched%()
  untouched% = -5
  fill_i(untouched%)
End Function

Print "by return var "; by_ret%()
Print "by a local    "; by_local%()
Print "accumulated   "; accumulate%()
Print "float         "; ret_f!()
Print "string        " + ret_s$()
Print "not written   "; untouched%()
