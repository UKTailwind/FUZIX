' (x + c1) + c2 in floating point.  cc1 regrouped it as x + (c1 + c2)
' and added the two constants' IEEE bit patterns, which came to a tiny
' negative number, so the sum compiled as x: a pixel's address worked
' out as MM.INFO(WRITEBUFF) + 220 * 160 + 2 came out as the buffer's.
x = 1000.5
PRINT x + 35200 + 2
PRINT x + 220 * 160 + 2
PRINT (x + 1.25) + 2.5
y = 3
PRINT y + 0.5 + 0.25
a% = 7
PRINT a% + 1 + 2
