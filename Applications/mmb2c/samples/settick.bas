' SETTICK torture - no graphics, no sprites, no sound.
'
' picofrog arms a tick to run the second half of a move (FJump does
' "SetTick 20,jump,1" on EVERY move) and the handler switches itself
' off again from inside itself ("If JP%>1500 Then SetTick 0,0,1").
' Those two together are what this exercises, and nothing else does:
' brownian drives sprites and collisions just as hard and has no ticks
' at all.
'
' Each phase announces itself before it runs, so the last line printed
' names the one that did not come back.

Option console serial

Dim JP%, FIRES%, DONE%, ROUND%, N%, TOT%
Dim F4%, D4%

Print "settick: start"

' ---- phase 1: arm, let the handler switch itself off, re-arm --------
Print "phase 1: self-disarm, 200 rounds"
For ROUND% = 1 To 200
  JP% = 600 : FIRES% = 0 : DONE% = 0 : N% = 0
  SetTick 20, jump, 1
  Do
    Inc N%, 1
    Pause 1
  Loop Until DONE% = 1 Or N% > 2000
  Inc TOT%, FIRES%
  If DONE% = 0 Then Print "  ROUND " + Str$(ROUND%) + " NEVER FIRED"
  If ROUND% Mod 50 = 0 Then Print "  round " + Str$(ROUND%) + " fires " + Str$(TOT%)
Next ROUND%
Print "phase 1 ok, total fires " + Str$(TOT%)

' ---- phase 2: re-arm an ALREADY ARMED tick, as FJump does -----------
Print "phase 2: re-arm while armed, 500 times"
JP% = 0 : FIRES% = 0 : DONE% = 0
For ROUND% = 1 To 500
  SetTick 20, jump2, 1
  Pause 3
Next ROUND%
SetTick 0, 0, 1
Print "phase 2 ok, fires " + Str$(FIRES%)

' ---- phase 3: handler disarms on its FIRST firing -------------------
Print "phase 3: disarm on first firing, 200 rounds"
For ROUND% = 1 To 200
  DONE% = 0 : N% = 0
  SetTick 20, once, 1
  Do
    Inc N%, 1
    Pause 1
  Loop Until DONE% = 1 Or N% > 2000
  If DONE% = 0 Then Print "  ROUND " + Str$(ROUND%) + " NEVER FIRED"
Next ROUND%
Print "phase 3 ok"

' ---- phase 4: handler RE-ARMS itself with a new period --------------
Print "phase 4: re-arm from inside the handler, 200 firings"
FIRES% = 0
SetTick 20, again, 1
Do
  Pause 1
Loop Until FIRES% > 200
SetTick 0, 0, 1
Print "phase 4 ok, fires " + Str$(FIRES%)

' ---- phase 5: two ticks at once, one self-disarming -----------------
Print "phase 5: ticks 1 and 4 together, 100 rounds"
For ROUND% = 1 To 100
  JP% = 600 : DONE% = 0 : N% = 0 : F4% = 0 : D4% = 0
  SetTick 7, other, 4
  SetTick 20, jump, 1
  Do
    Inc N%, 1
    Pause 1
  Loop Until DONE% = 1 Or N% > 2000
  SetTick 0, 0, 4
  If DONE% = 0 Then Print "  ROUND " + Str$(ROUND%) + " NEVER FIRED"
Next ROUND%
Print "phase 5 ok"

Print "settick: ALL PHASES PASSED"
End

' picofrog's own handler, verbatim in shape
Sub jump
  Inc FIRES%, 1
  Inc JP%, 100
  If JP% > 1500 Then SetTick 0, 0, 1 : JP% = 600 : DONE% = 1
End Sub

Sub jump2
  Inc FIRES%, 1
End Sub

Sub once
  SetTick 0, 0, 1
  DONE% = 1
End Sub

Sub again
  Inc FIRES%, 1
  SetTick 20, again, 1
End Sub

Sub other
  Inc F4%, 1
End Sub
