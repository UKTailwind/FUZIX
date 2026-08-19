' DS3231 ALARM on GP32.
'
' ============ READ THIS BEFORE TOUCHING REGISTER &H0F ============
'
' The first version of this test cleared the alarm flag the obvious way:
'
'     RTC SETREG &H0F, 0
'
' That writes the WHOLE status register, and bit 3 of it is EN32kHz -
' the 32 kHz square wave on GP27, which is HOW THE KERNEL KNOWS WHICH
' MACHINE IT IS.  With it off the next boot decides it is a PC2, where
' GP32 is the SD card's MISO, so the card is looked for on the wrong pin
' and the machine comes up saying "SD drive 0: no card found".  The
' DS3231 is battery-backed, so it survives the power cycle, and the
' program cannot undo it because the machine will not boot.  It bricked
' a board, and MMBasic on that board would not recognise a PC3 either.
'
' The flags here are write-0-to-clear, so clearing ONE of them means
' reading the register and putting the other bits back.  The kernel now
' forces EN32kHz on as well - belt and braces - but a program that goes
' near this register should still be written properly.
'
' ================================================================
'
' The other thing this test exists for: telling an ALARM from the 1 Hz
' SQUARE WAVE.  An earlier version "passed" while measuring the square
' wave, because a bug made every RTC SETREG write 0, which clears INTCN
' and switches the pin to that wave.  Eight edges a second apart look
' exactly like eight alarms.  So:
'
'   an ALARM latches   - low once, and STAYS low until A1F is cleared
'   a SQUARE WAVE      - toggles for ever, and no register write stops it
'
' Steps 2, 5 and 6 are the ones that tell them apart.
'
' DS3231: &H0E control (bit0 A1IE, bit2 INTCN), &H0F status (bit0 A1F,
' bit3 EN32kHz, bit7 OSF), &H07-&H0A alarm 1 with a mask bit 7 each.
' All BCD.
OPTION EXPLICIT
OPTION DEFAULT INTEGER

DIM ctrl, stat, sec, target, t, lows
DIM secs_now

SETPIN 32, DIN, PULLUP           ' the alarm line is open drain
RTC GETREG &H0E, ctrl
RTC GETREG &H0F, stat
PRINT "control = &H"; HEX$(ctrl); "   status = &H"; HEX$(stat)
PRINT "  EN32kHz = "; (stat >> 3) AND 1; "  (must stay 1 - board detection)"

' ---- 1. select the alarm output, alarm disabled -------------------
' INTCN = 1, A1IE = 0.  The pin must now sit HIGH and stay there.
RTC SETREG &H0E, &H04
clear_a1f()
PAUSE 100

' ---- 2. it must NOT toggle ----------------------------------------
lows = 0
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
clear_a1f()
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
clear_a1f()
PAUSE 100
PRINT "after clearing A1F, PIN(32) = "; PIN(32); " (want 1)"

' ---- tidy up ------------------------------------------------------
RTC SETREG &H0E, &H04            ' alarm output, disabled
clear_a1f()
RTC GETREG &H0F, stat
PRINT "EN32kHz still "; (stat >> 3) AND 1; " (MUST be 1)"
PRINT "done"
END

' Clear ONLY the alarm-1 flag.  Read, drop bit 0, put everything else
' back - above all EN32kHz.  See the note at the top.
SUB clear_a1f
  LOCAL INTEGER s
  RTC GETREG &H0F, s
  RTC SETREG &H0F, (s AND &HF6) OR &H08
END SUB
