' PLAY VOLUME's forms through mmb_play.h's mmp_volume, which a program
' with a SOUND or TONE gets - the synthesiser hears left and right apart.
' The TONE never runs: nothing is heard in the gates, and this keeps the
' translation byte-identical between mmb2c.py and mmbc.  playvol.bas is
' the inline form.  The left level is never left out: a command's first
' parameter cannot be empty.
PLAY VOLUME 25, 75
PLAY VOLUME 90
IF 0 THEN PLAY TONE 440, 440
PRINT "volume forms"
