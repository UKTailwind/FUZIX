' Write robots2.bas: robots with the MUSIC suppressed, nothing else.
'
' select_music() is a Select Case over 0..3, so passing 9 falls through
' and plays nothing; show_intro's own "Play Modfile" is commented out.
' PLAY MODSAMPLE stays, and does nothing without a MOD loaded.
'
' The point is an A/B on speed: if robots is quick without the MOD
' daemon alive and slow with it, the two processes do not fit in the
' pool together and the machine is swapping one of them to the SD card.

Dim a$, n

Open "robots.bas" For input As #1
Open "robots2.bas" For output As #2
Do While Not Eof(#1)
  Line Input #1, a$
  If Instr(a$, "select_music(map_nr mod 3)") > 0 Then
    a$ = "  select_music(9)"
    Inc n
  EndIf
  If Instr(a$, "Play Modfile path$(") > 0 Then
    a$ = "'" + a$
    Inc n
  EndIf
  Print #2, a$
Loop
Close #1
Close #2
Print "patched "; n; " line(s)"
