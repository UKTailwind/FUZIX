' dir is a variable: DIR$() is matched with its bracket, so it reserves
' no name - Elite's MoveShips has LOCAL INTEGER n, i, mag, dir, gone,
' and under OPTION EXPLICIT dir and gone went undeclared.
OPTION EXPLICIT
DIM INTEGER dir = 3
SUB t
  LOCAL INTEGER n, dir, gone
  dir = -1 : gone = 2
  PRINT dir; gone
END SUB
t
PRINT dir
