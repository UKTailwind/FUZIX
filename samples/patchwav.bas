' Write robots_w.bas: PETSCII Robots with the splash music played as a
' WAV instead of a MOD, and nothing else changed.
'
' playmod feeds its stream by polling - top up, usleep, look again.
' playwav, playflac and playmp3 do not poll at all.  So if the splash
' music is clean as a WAV under exactly the same game, the fault is in
' playmod's feed loop; if it pulses too, the game is starving whatever
' plays and the loop is innocent.
'
' Only show_intro's "Play Modfile" is touched (capital M - select_music
' uses lowercase and is not reached from the splash).  Do not press a
' key while testing: PLAY MODSAMPLE needs a MOD loaded and will raise
' with a WAV playing, which would end the program rather than the test.

Dim a$, n

Open "robots.bas" For input As #1
Open "robots_w.bas" For output As #2
Do While Not Eof(#1)
  Line Input #1, a$
  If Instr(a$, "Play Modfile path$(") > 0 Then
    a$ = " Play wav " + Chr$(34) + "/root/Hotel.wav" + Chr$(34)
    Inc n
  EndIf
  Print #2, a$
Loop
Close #1
Close #2
Print "patched "; n; " line(s) -> robots_w.bas"
