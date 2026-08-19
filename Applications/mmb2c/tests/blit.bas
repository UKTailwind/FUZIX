' BLIT under the gates: the host has no display, so the pixel work is
' silent (the same silence as every drawing primitive) and what this
' checks is the command surface - allocation, dimensions, the error
' paths, and that every form translates.  The pixels themselves are
' checked by the C harness (blitharness) and on the board.
MODE 2

' the straight path: read, write plain, write with a mode, close
BLIT READ #1, 10, 10, 32, 16
BLIT WRITE #1, 50, 50
BLIT WRITE #1, 60, 60, 5
BLIT 0, 0, 100, 100, 40, 30

' reading into a buffer that is open is the reference's error
ON ERROR SKIP 1
BLIT READ #1, 0, 0, 8, 8
PRINT "1: " MM.ERRMSG$

' writing from a buffer that was never opened
ON ERROR SKIP 1
BLIT WRITE #2, 0, 0
PRINT "2: " MM.ERRMSG$

' closing it is the same error
ON ERROR SKIP 1
BLIT CLOSE #2
PRINT "3: " MM.ERRMSG$

' close frees: the same buffer opens again
BLIT CLOSE #1
BLIT READ #1, 0, 0, 8, 8
BLIT CLOSE #1

' a read off the top-left clips and keeps the clipped size
BLIT READ #2, -4, -4, 32, 16
BLIT WRITE #2, 0, 0, 7
BLIT CLOSE #2

' a read entirely off-screen allocates nothing
BLIT READ #3, 400, 400, 8, 8
ON ERROR SKIP 1
BLIT CLOSE #3
PRINT "4: " MM.ERRMSG$

' mode 8 is out of range
BLIT READ #3, 0, 0, 4, 4
ON ERROR SKIP 1
BLIT WRITE #3, 0, 0, 8
PRINT "5: " MM.ERRMSG$
BLIT CLOSE #3

' buffer number 65 is out of range
ON ERROR SKIP 1
BLIT READ #65, 0, 0, 4, 4
PRINT "6: " MM.ERRMSG$

' zero width is the reference's silent return, not an error
BLIT READ #4, 0, 0, 0, 8
ON ERROR SKIP 1
BLIT CLOSE #4
PRINT "7: " MM.ERRMSG$

PRINT "blit surface ok"
