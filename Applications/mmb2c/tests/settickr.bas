' SETTICK takes the reference's getints, in its order (ed4ddbc): the tick
' number 1..4, then a period 0..2147483647.  A negative period used to be
' taken as "off" here, silently, and a bad tick number had a wording of
' its own.
ON ERROR SKIP 1
SETTICK -1, T, 1
Print "period -1: " MM.ERRMSG$
ON ERROR SKIP 1
SETTICK 10, T, 5
Print "tick 5: " MM.ERRMSG$
' PAUSE and RESUME take a target, never evaluated, and read the tick
' number only when there are five arguments: PAUSE, 0 is timer 1
ON ERROR SKIP 1
SETTICK PAUSE, T, 0
Print "pause tick 0: " MM.ERRMSG$
ON ERROR SKIP 1
SETTICK PAUSE, 0
Print "pause target 0: " MM.ERRMSG$
ON ERROR SKIP 1
SETTICK RESUME, T, 3
Print "resume tick 3: " MM.ERRMSG$
ON ERROR SKIP 1
SETTICK PAUSE
Print "pause alone: " MM.ERRMSG$
SETTICK 0, T, 1
Print "off ok"
END

Sub T
End Sub
