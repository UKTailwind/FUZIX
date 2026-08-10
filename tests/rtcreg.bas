' RTC GETREG / SETREG - MMBasic's own pair, and the only way to arm a
' DS3231 alarm, because MMBasic has no alarm command either.
'
' Under the gates there is no clock, so every read answers -1 and every
' write is a no-op.  What this proves is that both forms translate, that
' the value comes back into an ordinary variable, and that a program
' using them runs where there is no RTC rather than failing.
DIM INTEGER c, s, i

RTC GETREG 14, c
PRINT "control ";c
RTC GETREG 15, s
PRINT "status  ";s

' Arming an alarm, the PicoMite way: match on seconds only (A1M2..A1M4
' set), then INTCN | A1IE into the control register.
RTC SETREG 7, 0
RTC SETREG 8, 128
RTC SETREG 9, 128
RTC SETREG 10, 128
RTC SETREG 14, 5
PRINT "armed"

' And clearing the alarm flag afterwards.
RTC SETREG 15, 0
PRINT "cleared"
PRINT "done"
