' moddemo - PLAY MODFILE and PLAY MODSAMPLE on hardware.  The music
' starts, two gunshot-style sample triggers land over it while it
' keeps playing, then PLAY STOP.  The second act arms the completion
' interrupt: the song plays once through and the handler fires when
' the player exits.
PLAY MODFILE "test.mod"
PAUSE 2500
PLAY MODSAMPLE 1, 1, 64
PAUSE 800
PLAY MODSAMPLE 1, 2, 48
PAUSE 1200
PLAY STOP
PRINT "stopped ok"
PAUSE 300

done% = 0
PLAY MODFILE "test.mod", fin
DO WHILE done% = 0
  PAUSE 100
LOOP
PRINT "mod ended, interrupt fired"
PRINT "moddemo done"
END

SUB fin
  done% = 1
END SUB
