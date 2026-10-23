' A name with a '(' that is neither an array nor a function the
' translator has may be one of the firmware's - DEVICE here, as robots
' uses it for a Wii controller - so the line is one we did not
' translate, listed and commented out, not a run-time "not declared".
' In a SELECT CASE that refused the whole program (2026-10-23).
Print "a"
If 0 Then bits = Device(Wii b)
Print "b"

Sub Stick
  Select Case Device(Wii RY)
    Case < 50 : s$ = "down"
  End Select
End Sub
