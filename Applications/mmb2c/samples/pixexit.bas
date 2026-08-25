' The pixel queue at exit - HALF A TEST, run pixseen.bas after it.
'
' Plot ten points and fall off the end of the program: no End, no CLS,
' no PRINT, nothing that would flush the queue on the way out.  Until
' 2026-08-25 all ten were thrown away, because a translated main() ends
' with a plain return and nothing drained the batch.
'
'     ./pixexit.bc ; ./pixseen.bc      ->  10 of 10
'
' It takes two processes because the first one has to be GONE before the
' screen is read: anything the same program did afterwards - including
' reading a pixel back - would flush the queue and hide the bug.
Option Explicit
Option Default Float
Dim Integer i
Mode 2
Cls
For i = 0 To 9
  Pixel 100 + i, 150, Rgb(WHITE)
Next i
