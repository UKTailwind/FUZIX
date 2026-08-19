' playdemo - PLAY SOUND and PLAY TONE on the board's speaker.
' A rising four-note scale on the synth, a two-tone chord, a second of
' noise, then silence; ends with PLAY STOP so nothing lingers.
PLAY VOLUME 70
PLAY SOUND 1, B, S, 262
PAUSE 300
PLAY SOUND 1, B, S, 330
PAUSE 300
PLAY SOUND 1, B, S, 392
PAUSE 300
PLAY SOUND 1, B, S, 523
PAUSE 300
PLAY SOUND 1, B, O
PAUSE 200
PLAY TONE 440, 554, 600
PAUSE 800
PLAY SOUND 2, B, N, 500, 15
PAUSE 700
PLAY SOUND 2, B, O
PAUSE 300
PLAY STOP
PRINT "playdemo done"
