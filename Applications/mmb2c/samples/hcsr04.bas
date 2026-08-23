' Distance( against a real HC-SR04 - trigger on GP1, echo on GP7.
'
' Distance( configures both pins itself, exactly as MMBasic's
' fun_distance does (echo to input with a pull-up, trigger low then
' output), so no SETPIN is needed first.
'
' What comes back is centimetres, or -1 when no echo returned inside
' 38 ms (nothing in range), or -2 when the sensor never answered at all
' (not wired, not powered, or the echo pin is not one this machine can
' capture on - GP4 to GP7).
Option Explicit

Dim integer k
Dim float d

Print "ten readings, one every 200 ms:"
For k = 1 To 10
  d = Distance(GP1, GP7)
  Print Str$(d, 0, 1); " cm"
  Pause 200
Next k

' the same measurement with the machine busy, which is the whole point
' of measuring it from the kernel's edge timestamps
Print "five more:"
For k = 1 To 5
  Print Str$(Distance(GP1, GP7), 0, 1); " cm"
  Pause 100
Next k
