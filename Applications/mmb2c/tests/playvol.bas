' PLAY VOLUME's forms - cmd_play's VOLUME arm: left [, right], and a
' level not given stays as it was, so the one-number form is the left
' channel alone.  The left level is never left out: a command's first
' parameter cannot be empty.  Nothing is heard in the gates; this
' keeps the inline translation - a program with no SOUND or TONE, whose
' players take one level, the larger - byte-identical between mmb2c.py
' and mmbc.  playvold.bas is the same through mmb_play.h.
PLAY VOLUME 30, 70
PLAY VOLUME 40
IF 0 THEN PLAY MP3 "tune.mp3"
PRINT "volume forms"
