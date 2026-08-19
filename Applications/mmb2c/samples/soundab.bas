' soundab - one 440 Hz sine through PLAY SOUND, for A/B against the
' kernel synth's reference tone (snd 1).  Same pitch, same duration.
PLAY VOLUME 70
PLAY SOUND 1, B, S, 440, 25
PAUSE 2000
PLAY SOUND 1, B, O
PAUSE 200
PLAY STOP
PRINT "soundab done"
