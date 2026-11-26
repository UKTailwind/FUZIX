' The PicoMite's sprite fixes of 20-21 October (issues #64, #88 to #91,
' #93), headless: what they change in the bookkeeping and SPRITE().
' Pixels are pc3host's e2e test (tests/e2e/spritefix.bas).
MODE 2
Dim integer a(63), i
For i = 0 To 63 : a(i) = RGB(255, 255, 255) : Next

' #91: each edge of the screen is an entry of its own (0xF1 left, 0xF2
' top); it was one entry, 0xF3
SPRITE LOADARRAY #1, 8, 8, a()
SPRITE SHOW #1, -5, -5, 1
Print "edges:"; SPRITE(C, #1); SPRITE(C, #1, 1); SPRITE(C, #1, 2)
SPRITE HIDE #1

' #88: after MOVE a layer-0 sprite meets the layer-1 sprite it is on,
' and each lists the other; #0 lists both
SPRITE LOADARRAY #4, 8, 8, a()
SPRITE LOADARRAY #5, 8, 8, a()
SPRITE SHOW #4, 100, 100, 1
SPRITE SHOW #5, 200, 50, 0
SPRITE NEXT #5, 102, 102
SPRITE MOVE
Print "move:"; SPRITE(C, #0); SPRITE(C, #0, 1); SPRITE(C, #0, 2); SPRITE(C, #4, 1); SPRITE(C, #5, 1); SPRITE(S)
SPRITE HIDE #4
SPRITE HIDE #5

' #91: COPY's count is bounded by the first copy's number, and the
' master counts its copies (64 was a shift too far)
On Error Skip
SPRITE COPY #1, #60, 10
Print "copy:"; MM.ErrNo <> 0
SPRITE COPY #1, #63, 2
On Error Skip
SPRITE CLOSE #1
Print MM.ErrMsg$
SPRITE CLOSE #63
On Error Skip
SPRITE CLOSE #1
Print MM.ErrMsg$
SPRITE CLOSE #64
SPRITE CLOSE #1
Print "closed"

' #89: SWAP leaves each sprite its own copies
SPRITE LOADARRAY #6, 8, 8, a()
SPRITE COPY #6, #7, 2
SPRITE LOADARRAY #9, 8, 8, a()
SPRITE SHOW #6, 50, 150, 1
SPRITE SWAP #6, #9
SPRITE CLOSE #9
On Error Skip
SPRITE CLOSE #6
Print "swap: "; MM.ErrMsg$

' #90: SHOW SAFE of a sprite showing takes its new layer, keeps its
' place, and meets the sprites of that layer
SPRITE LOADARRAY #10, 8, 8, a()
SPRITE LOADARRAY #11, 8, 8, a()
SPRITE SHOW SAFE #10, 200, 150, 1
SPRITE SHOW SAFE #11, 202, 152, 2
SPRITE SHOW SAFE #10, 200, 150, 2
Print "safe:"; SPRITE(L, #10); SPRITE(N, 1); SPRITE(N, 2); SPRITE(C, #10); SPRITE(C, #10, 1)

' #64: SPRITE LOAD may load sprite 64, and refuses a copy and a master
' with copies, as SPRITE READ does
Open "t.spr" For Output As #1
Print #1, "2,1"
Print #1, "12"
Print #1, "34"
Close #1
SPRITE LOAD "t.spr", 64
Print "load:"; SPRITE(W, #64); SPRITE(H, #64)
SPRITE COPY #64, #12, 1
On Error Skip
SPRITE LOAD "t.spr", 12
Print MM.ErrMsg$
On Error Skip
SPRITE LOAD "t.spr", 64
Print MM.ErrMsg$
