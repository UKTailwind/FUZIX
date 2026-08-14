' Letters, quoted letters and strings worked out as the program runs -
' the three forms MMBasic takes for a FRAMEBUFFER buffer and for PLAY
' SOUND's channel and type (cmd_framebuffer, cmd_play: checkstring
' first, getCstring + strcasecmp after).  picofrog writes the quoted
' ones in lower case and keeps its buffer letter in a variable.
'
' A letter written literally is decoded when the program is TRANSLATED,
' so what gates here is the other arm: the decoders that run, and their
' messages, which are the reference's own.
lc$ = "l"
bad$ = "X"
ch$ = "b"
ty$ = "q"

' The forms decided at translation time.  They have to translate and
' run; what they then do needs hardware, so the error each gives here
' (no layer, no sound daemon) is skipped rather than printed.
ON ERROR SKIP 1
FRAMEBUFFER WRITE L
ON ERROR SKIP 1
FRAMEBUFFER WRITE "n"
ON ERROR SKIP 1
FRAMEBUFFER WRITE N
ON ERROR SKIP 1
PLAY SOUND 1, B, S, 440, 15
ON ERROR SKIP 1
PLAY SOUND 2, "l", "q", 220, 10

' and the forms decided as it runs
ON ERROR SKIP 1
FRAMEBUFFER WRITE lc$
ON ERROR SKIP 1
FRAMEBUFFER WRITE bad$
PRINT "1: " MM.ERRMSG$
ON ERROR SKIP 1
PLAY SOUND 1, ch$, ty$, 440, 15
PRINT "2: " MM.ERRMSG$
ON ERROR SKIP 1
PLAY SOUND 1, bad$, ty$, 440, 15
PRINT "3: " MM.ERRMSG$
ON ERROR SKIP 1
PLAY SOUND 1, ch$, bad$, 440, 15
PRINT "4: " MM.ERRMSG$
PRINT "strargs ok"
