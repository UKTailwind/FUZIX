' PLAY VOLUME's forms - cmd_play's VOLUME arm: left [, right], either
' one left out, and a level not given stays as it was, so the one-number
' form is the left channel alone.  Nothing is heard in the gates; this
' keeps the inline translation - a program with no SOUND or TONE, whose
' players take one level, the larger - byte-identical between mmb2c.py
' and mmbc.  playvold.bas is the same through mmb_play.h.
PLAY VOLUME 30, 70
PLAY VOLUME , 50
PLAY VOLUME 40
IF 0 THEN PLAY MP3 "tune.mp3"
PRINT "volume forms"
