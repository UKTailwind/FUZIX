REM modfrog - PLAY MODSAMPLE exercised properly, over a real song.
REM
REM frogger-main.mod plays on THREE channels, which leaves channel 4
REM free: a sample mixed onto a channel the music is using fights it
REM for that channel, so a game puts its effects on the spare one.
REM
REM The samples this file carries, and what each one is for in the
REM game it came from:
REM
REM   4 Coin in - Start    7 Die Road      10 Pickup Mate
REM   5 Hop                8 Die Water
REM   6 Landing Safe       9 Free Life
REM
REM Comments are REM rather than the usual quote so the whole file can
REM be typed to the board through a shell without quoting trouble.

Option explicit
Option default none

Const MODPATH = "/root/frogger-main.mod"
Const SFX = 4                  REM the channel the music leaves free
Const FIRST = 4
Const LAST = 10

Dim string names(10)
Dim integer i, k, v
Dim string a

names(4) = "Coin in - Start"
names(5) = "Hop"
names(6) = "Landing Safe"
names(7) = "Die Road"
names(8) = "Die Water"
names(9) = "Free Life"
names(10) = "Pickup Mate"

Print "PLAY MODSAMPLE test - " + MODPATH
Print

REM ---- the music has to be running: a sample is a request to the
REM player that is already going, and PLAY MODSAMPLE raises
REM "Samples play over MOD file" if nothing is.
Print "starting the music"
Play MODFILE MODPATH
Pause 2500

Print
Print "each sample once, on channel " + Str$(SFX)
For i = FIRST To LAST
  Print "  " + Str$(i, 2, 0) + "  " + names(i)
  Play MODSAMPLE i, SFX
  Pause 1400
Next i

REM ---- volume is 1 to 64 and is per trigger, so the same sample can
REM be near or far.  Hop is the shortest one, which makes the
REM difference easiest to hear.
Print
Print "one sample at four volumes"
For v = 16 To 64 Step 16
  Print "  Hop at volume " + Str$(v)
  Play MODSAMPLE 5, SFX, v
  Pause 700
Next v

REM ---- two triggers close together, which is what a game actually
REM does: the second must cut in rather than be dropped.
Print
Print "hop hop hop, fast"
For i = 1 To 3
  Play MODSAMPLE 5, SFX
  Pause 180
Next i
Pause 800

REM ---- and the same sample on a channel the MUSIC is using, to hear
REM why the spare channel is worth having.
Print
Print "the same sample on channel 1, under the music"
Play MODSAMPLE 7, 1
Pause 1500

Print
Print "keys 1 to 7 fire samples 4 to 10, Q quits"
Do
  a = Inkey$
  If a <> "" Then
    k = Asc(a)
    If k >= 49 And k <= 55 Then
      i = FIRST + k - 49
      Print "  " + names(i)
      Play MODSAMPLE i, SFX
    End If
  Else
    k = 0
  End If
  Pause 20
Loop Until k = 81 Or k = 113

Play STOP
Print
Print "modfrog done"
End
