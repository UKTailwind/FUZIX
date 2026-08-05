' PLAY MP3 and PLAY VOLUME.
'
' The volume is remembered between statements, so the second PLAY
' below is quieter than the first without being told so - which is the
' whole point of PLAY VOLUME being a statement of its own rather than
' an argument.
'
' PLAY MP3 does not wait: the music carries on while the program does.
' Nothing here can check that from the gates, because the gates have no
' sound card - this exists so the TRANSLATION is covered and stays
' byte-identical between mmb2c.py and mmbc.
PRINT "play test"
PLAY VOLUME 80
PLAY MP3 "/root/mp3/whiter.mp3"
PAUSE 2000
PLAY VOLUME 40
PLAY MP3 "/root/mp3/hmp3.mp3"
' out of range is clamped, not an error
PLAY VOLUME 250
PLAY VOLUME -3
PRINT "done"
