' PLAY SOUND / PLAY TONE under the gates: argument validation runs
' before any daemon business, so the error paths gate on the host; the
' one legitimate call lands on the host's "needs the native runtime"
' refusal, which is itself the expected text.  Sound comes from the
' board (samples/playdemo.bas).
ON ERROR SKIP 1
PLAY SOUND 5, B, S, 440
PRINT "1: " MM.ERRMSG$
ON ERROR SKIP 1
PLAY SOUND 1, B, S, 99999
PRINT "2: " MM.ERRMSG$
ON ERROR SKIP 1
PLAY SOUND 1, B, S, 440, 99
PRINT "3: " MM.ERRMSG$
ON ERROR SKIP 1
PLAY TONE 500, 500, -5
PRINT "4: " MM.ERRMSG$
ON ERROR SKIP 1
PLAY TONE 500, 500, 100
PRINT "5: " MM.ERRMSG$
PLAY STOP
PRINT "play surface ok"
