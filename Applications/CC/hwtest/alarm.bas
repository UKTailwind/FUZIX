' DS3231 ALARM on GP32 - the honest version.
'
' The first test of this "passed" and was wrong.  A bug in the runtime
' made every RTC SETREG write 0, and writing 0 to the control register
' clears INTCN, which switches the INT/SQW pin from the alarm output to
' a 1 Hz SQUARE WAVE.  Eight edges a second apart looked exactly like
' eight alarms.
'
' So this test is built to tell those two apart, which the first one
' could not:
'
'   an ALARM latches   - it goes low once and STAYS low until A1F is
'                        cleared, and clearing it releases the pin
'   a SQUARE WAVE      - toggles every second for ever, and no register
'                        write releases it
'
' Steps 2, 5 and 6 below are the ones that would have caught it.
'
' DS3231 registers: 0x0E control (bit0 A1IE, bit2 INTCN), 0x0F status
' (bit0 A1F), 0x07-0x0A alarm 1, mask bit 7 of each.  All BCD.
OPTION EXPLICIT
OPTION DEFAULT INTEGER

DIM ctrl, stat, sec, target, t, edge, lows
DIM secs_now

SETPIN 32, DIN, PULLUP           ' the alarm line is open drain
RTC GETREG &H0E, ctrl
RTC GETREG &H0F, stat
PRINT "control = &H"; HEX$(ctrl); "   status = &H"; HEX$(stat)

' ---- 1. select the alarm output, alarm disabled -------------------
' INTCN = 1, A1IE = 0.  The pin must now sit HIGH and stay there.
RTC SETREG &H0E, &H04
RTC SETREG &H0F, 0               ' clear A1F
PAUSE 100

' ---- 2. it must NOT toggle ----------------------------------------
' This is the step the first test never did.  With INTCN clear the pin
' would be a 1 Hz square wave and this would count edges.
edge = 0 : lows = 0
FOR t = 1 TO 60
  IF PIN(32) = 0 THEN lows = lows + 1
  PAUSE 50
NEXT t
PRINT "idle: "; lows; " lows in 3 s (want 0 - any means a square wave)"

' ---- 3. arm alarm 1 for 5 seconds from now ------------------------
RTC GETREG &H00, secs_now        ' seconds, BCD
sec = (secs_now AND &H0F) + 10 * ((secs_now >> 4) AND &H07)
target = (sec + 5) MOD 60
' match on seconds only: A1M1 = 0, A1M2/3/4 = 1
RTC SETREG &H07, ((target \ 10) << 4) OR (target MOD 10)
RTC SETREG &H08, &H80
RTC SETREG &H09, &H80
RTC SETREG &H0A, &H80
RTC SETREG &H0F, 0               ' clear A1F again
RTC SETREG &H0E, &H05            ' INTCN | A1IE
PRINT "armed for second "; target; " (now "; sec; ")"

' ---- 4. wait for it -----------------------------------------------
t = 0
DO WHILE PIN(32) = 1 AND t < 160
  PAUSE 50
  t = t + 1
LOOP
IF t >= 160 THEN
  PRINT "NO ALARM within 8 s - failed"
  RTC SETREG &H0E, &H04
  END
ENDIF
PRINT "alarm fired after "; t * 50 / 1000; " s"

' ---- 5. it must LATCH ---------------------------------------------
' A square wave would be high again within half a second.
lows = 0
FOR t = 1 TO 40
  IF PIN(32) = 0 THEN lows = lows + 1
  PAUSE 50
NEXT t
PRINT "held low "; lows; " of 40 samples over 2 s (want 40 - a square"
PRINT "  wave would be about half)"

RTC GETREG &H0F, stat
PRINT "status A1F = "; stat AND 1; " (want 1)"

' ---- 6. clearing A1F must release the pin -------------------------
RTC SETREG &H0F, 0
PAUSE 100
PRINT "after clearing A1F, PIN(32) = "; PIN(32); " (want 1)"

' ---- tidy up ------------------------------------------------------
RTC SETREG &H0E, &H04            ' alarm output, disabled
RTC SETREG &H0F, 0
PRINT "done"
