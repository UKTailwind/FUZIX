Option Base 0
' The three random generators, and which is which.
'
'   RND / RND(n)     rand(), reseeded every hundred calls.  Hardware-
'                    random on a PicoMite, so NOT reproducible and NOT
'                    seedable, and its values cannot be compared
'                    between machines - only its contract can.
'   RANDOMIZE        a NO-OP.  An RP2040 statement; the RP2350 has
'                    nothing for it to do.  It must NOT make RND repeat.
'   MATH RANDOMIZE   seeds the Mersenne Twister, and only that.
'   MATH(RAND)       draws from it.  Seeded, it IS reproducible.
'
' This translator had the first three wrong until 2026-08-24: MATH
' RANDOMIZE seeded the generator RND drew from, so a seeded program was
' reproducible here and hardware-random there, and RANDOMIZE was not a
' no-op.
'
' THE MATH(RAND) VALUES ARE NOT BLESSED AGAINST THE INTERPRETER, and
' that is deliberate.  The reference's Mersenne seeder multiplies by
' 6069 where the published algorithm - Knuth TAOCP Vol 2 p.102 Table 1
' line 25, which the reference cites by name - uses 69069.  On the
' author's ruling this uses the correct constant and MMBasic is to be
' corrected to match, so these numbers differ from an unfixed PicoMite
' and will agree with a fixed one.  They were checked against an
' independent implementation of the same algorithm rather than against
' nothing: see the note in mmb_mt.h.

' MATH RANDOMIZE and MATH(RAND) are a pair on a Mersenne Twister, and
' a seeded sequence is reproducible - so it can be compared.
Math Randomize 42
Print "seed42"; Math(RAND); Math(RAND); Math(RAND)
Math Randomize 42
Print "again "; Math(RAND); Math(RAND); Math(RAND)
Math Randomize 1
Print "seed1 "; Math(RAND); Math(RAND); Math(RAND)
Math Randomize 4357
Print "s4357 "; Math(RAND); Math(RAND)

' RND is NOT that generator, and RANDOMIZE does not touch it.  Its
' values cannot be compared - they are hardware-random on a PicoMite -
' so only the contract is checked here.
Dim Float r
Dim Integer i, inrange
inrange = 1
For i = 1 To 500
  r = Rnd
  If r < 0 Or r >= 1 Then inrange = 0
Next i
Print "rndrng"; inrange

' RANDOMIZE is a no-op: it must not make RND repeat.
Randomize 7
r = Rnd
Randomize 7
Print "rndsam"; (r = Rnd)

' and the argument is optional
Randomize
Print "bare  "; 1
