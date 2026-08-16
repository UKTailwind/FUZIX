' playfile - the three file players: PLAY MP3, PLAY WAV, PLAY FLAC.
' Each spawns a one-shot player and does not wait, so this only checks
' that all three translate and that the file name may be worked out at
' run time.  Nothing here is played: the gates have no audio device,
' and a spawn of a program that is not there simply fails.
Option explicit
Option default none

Dim string f

Print "play file forms"

PLAY VOLUME 70

f = "tune.mp3"
If 1 = 0 Then PLAY MP3 f
If 1 = 0 Then PLAY MP3 "fixed.mp3"

f = "tune.wav"
If 1 = 0 Then PLAY WAV f
If 1 = 0 Then PLAY WAV "fixed.wav"

f = "tune.flac"
If 1 = 0 Then PLAY FLAC f
If 1 = 0 Then PLAY FLAC "fixed.flac"

PLAY STOP

Print "translated"
