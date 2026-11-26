' PLAY MODSAMPLE as V8.00.00b2 has it (264ccde, 9424dcf): a rate, a pan,
' LOOP, CHANGE and STOP, any argument after the channel left empty.
' With no MOD file playing STOP does nothing and says nothing, and the
' others say so.  What the player is asked to do is pc3host's e2e test
' (tests/e2e/modsample.bas).

PLAY MODSAMPLE STOP
PLAY MODSAMPLE STOP 3
Print "stopped"
On Error Skip
PLAY MODSAMPLE CHANGE 1, , , -50
Print MM.ErrMsg$
On Error Skip
PLAY MODSAMPLE CHANGE 2, 32.4, 11025.5, -99.5
Print MM.ErrMsg$
On Error Skip
PLAY MODSAMPLE 10, 1, 40, 16000, -100, LOOP
Print MM.ErrMsg$
On Error Skip
PLAY MODSAMPLE 11, 3, , , LOOP
Print MM.ErrMsg$
On Error Skip
PLAY MODSAMPLE 11, 4, 64, 22050
Print MM.ErrMsg$
On Error Skip
PLAY MODSAMPLE 1, 2
Print MM.ErrMsg$
