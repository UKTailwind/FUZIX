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
' The MATH(RAND) values agree with the reference since 4861a1a, which
' corrected its seeder to Knuth's 69069 (it had the typo 6069, and these
' were checked against an independent implementation until then) and
' divides by 2^32, so a draw is below 1.0.  Seed 0 seeds as 4357, and a
' seed is 0 to 4294967295.

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
Math Randomize 0
Print "seed0 "; Math(RAND); Math(RAND)
On Error Skip 1
Math Randomize -1
Print "neg   : "; MM.ErrMsg$

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
