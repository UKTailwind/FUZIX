' The SPRITE engine under the gates: LIFO bookkeeping, the AABB
' collision engine (pure coordinates, so it runs headless), the
' functions, and the error paths.  Pixels are the board's business.
MODE 2
DIM a%(63)
FOR i% = 0 TO 63 : a%(i%) = RGB(255,255,255) : NEXT i%

SPRITE LOADARRAY #1, 8, 8, a%()
SPRITE COPY #1, #2, 2
SPRITE SHOW #1, 10, 10, 1
SPRITE SHOW #2, 100, 100, 1
PRINT "n: "; SPRITE(N)
PRINT "x1: "; SPRITE(X, #1)

' move #2 onto #1: the moved sprite records the collision, both carry
' the mask, S names the mover
SPRITE SHOW #2, 12, 12, 1
PRINT "c2: "; SPRITE(C, #2); " "; SPRITE(C, #2, 1)
PRINT "s: "; SPRITE(S)
' the argument form: fun_sprite parses up to five arguments
' for every selector and the S arm never looks at them, so
' this is the same answer (Sprite.c:2462)
PRINT "s2: "; SPRITE(S, 1)
PRINT "t1: "; SPRITE(T, #1)

' a copy off the left edge reports the edge, once
SPRITE SHOW #3, -2, 50, 1
PRINT "e3: "; SPRITE(E, #3)

PRINT "d: "; SPRITE(D, #1, #2)

' static objects: SAFE show onto one
SPRITE STATIC 1, 200, 200, 20, 20
SPRITE SHOW SAFE #3, 205, 205, 1
PRINT "st: "; SPRITE(ST, COLLISION); " "; SPRITE(ST, OBJECT)
SPRITE STATIC 1, OFF

' NEXT is applied by MOVE
SPRITE NEXT #1, 50, 50
SPRITE MOVE
PRINT "x1b: "; SPRITE(X, #1)

' hide twice is the reference's error
SPRITE HIDE #2
ON ERROR SKIP 1
SPRITE HIDE #2
PRINT "1: " MM.ERRMSG$

SPRITE HIDE SAFE #1
SPRITE HIDE #3

' a master with copies open refuses to close
ON ERROR SKIP 1
SPRITE CLOSE #1
PRINT "2: " MM.ERRMSG$
SPRITE CLOSE #2
SPRITE CLOSE #3
SPRITE CLOSE #1
PRINT "n2: "; SPRITE(N)

' SCROLL: layer-0 sprites and statics wrap with the background
' (the pixels are the board's business; the positions are checkable
' here).  Centre of #5 is (12,12): x +8 -> 20, y -4 -> 8, so the
' corner lands at (18,6).  The static's centre (305,205) moves the
' same way.
SPRITE LOADARRAY #5, 4, 4, a%()
SPRITE SHOW #5, 10, 10, 0
SPRITE STATIC 2, 300, 200, 10, 10
SPRITE SCROLL 8, 4
PRINT "sc5: "; SPRITE(X, #5); " "; SPRITE(Y, #5)
PRINT "st2: "; SPRITE(ST, 2, X); " "; SPRITE(ST, 2, Y)
SPRITE STATIC 2, OFF
SPRITE HIDE #5
SPRITE CLOSE #5

' hide all / restore round trip
SPRITE LOADARRAY #4, 4, 4, a%()
SPRITE SHOW #4, 20, 20, 2
SPRITE HIDE ALL
ON ERROR SKIP 1
SPRITE SHOW #4, 30, 30, 2
PRINT "3: " MM.ERRMSG$
SPRITE RESTORE
PRINT "x4: "; SPRITE(X, #4)
SPRITE CLOSE ALL
PRINT "sprite surface ok"
