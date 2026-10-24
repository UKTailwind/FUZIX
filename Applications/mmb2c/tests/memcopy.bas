' MEMORY COPY and MEMORY SET - Memory.c cmd_memory - on arrays: INTEGERs
' straight and stepped, an overlap that has to run backwards, bytes in a
' string, and SET in its widths.  Elite saves and restores its dashboard
' with MEMORY COPY INTEGER between the framebuffer and an array.
DIM INTEGER s(7), d(7), i
FOR i = 0 TO 7 : s(i) = i + 1 : NEXT i
MEMORY COPY INTEGER PEEK(VARADDR s()), PEEK(VARADDR d()), 8
PRINT d(0); d(7)
' every other element of s() into the first four of d()
MEMORY COPY INTEGER PEEK(VARADDR s()), PEEK(VARADDR d()), 4, 2, 1
PRINT d(0); d(1); d(2); d(3)
' up one element over itself: it copies from the top down
FOR i = 0 TO 7 : d(i) = i + 1 : NEXT i
MEMORY COPY INTEGER PEEK(VARADDR d()), PEEK(VARADDR d()) + 8, 7
PRINT d(0); d(1); d(7)
DIM b$ = "abcdef"
MEMORY COPY PEEK(VARADDR b$) + 1, PEEK(VARADDR b$) + 3, 2
PRINT b$
MEMORY SET BYTE PEEK(VARADDR d()), 255, 8
PRINT HEX$(d(0))
MEMORY SET INTEGER PEEK(VARADDR d()), 9, 4, 2
PRINT d(0); d(2); d(4); d(6); d(1)
DIM FLOAT f(3)
MEMORY SET FLOAT PEEK(VARADDR f()), 1.5, 4
PRINT f(0); f(3)
ON ERROR SKIP 1
MEMORY COPY INTEGER PEEK(VARADDR d()) + 1, PEEK(VARADDR s()), 1
PRINT MM.ERRMSG$
