' tests/libload.bas's first library file: a constant, a variable, a
' start-up line, and a function
CONST LIBMAX = 99
DIM libcount = 5
PRINT "library a runs first"

FUNCTION twice(n)
  twice = n * 2
END FUNCTION
