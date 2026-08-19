' t8.bas - LONGSTRING, and GOSUB/RETURN
OPTION BASE 0

' a long string variable is an INTEGER array; each element holds 8 bytes
DIM INTEGER buf(63)          ' up to 504 bytes
DIM INTEGER tmp(63)
DIM INTEGER other(63)
DIM s$, i, n

PRINT "-- LONGSTRING basics --"
LONGSTRING CLEAR buf()
PRINT "empty len ="; LLEN(buf())

LONGSTRING APPEND buf(), "Hello, "
LONGSTRING APPEND buf(), "long string world!"
PRINT "len ="; LLEN(buf())
PRINT "text = ["; LGETSTR$(buf(), 1, LLEN(buf())); "]"

' build something longer than an ordinary MMBasic string
LONGSTRING CLEAR buf()
FOR i = 1 TO 30
  LONGSTRING APPEND buf(), "0123456789"
NEXT i
PRINT "300 chars, len ="; LLEN(buf())
PRINT "first 12 = ["; LGETSTR$(buf(), 1, 12); "]"
PRINT "last  12 = ["; LGETSTR$(buf(), LLEN(buf()) - 11, 12); "]"

PRINT "-- COPY / CONCAT / LEFT / RIGHT / MID --"
LONGSTRING CLEAR buf()
LONGSTRING APPEND buf(), "abcdefghij"
LONGSTRING COPY tmp(), buf()
PRINT "copy = ["; LGETSTR$(tmp(), 1, LLEN(tmp())); "]"
LONGSTRING CONCAT tmp(), buf()
PRINT "concat len ="; LLEN(tmp()); " = ["; LGETSTR$(tmp(), 1, LLEN(tmp())); "]"

LONGSTRING LEFT other(), buf(), 4
PRINT "left4  = ["; LGETSTR$(other(), 1, LLEN(other())); "]"
LONGSTRING RIGHT other(), buf(), 3
PRINT "right3 = ["; LGETSTR$(other(), 1, LLEN(other())); "]"
LONGSTRING MID other(), buf(), 3, 4
PRINT "mid3,4 = ["; LGETSTR$(other(), 1, LLEN(other())); "]"
LONGSTRING MID other(), buf(), 8
PRINT "mid8   = ["; LGETSTR$(other(), 1, LLEN(other())); "]"

PRINT "-- LOAD / REPLACE / TRIM / RESIZE / SETBYTE --"
LONGSTRING LOAD tmp(), 5, "abcdefghij"
PRINT "load5  = ["; LGETSTR$(tmp(), 1, LLEN(tmp())); "]"
LONGSTRING CLEAR tmp()
LONGSTRING APPEND tmp(), "abcdefghij"
LONGSTRING REPLACE tmp(), "XYZ", 4
PRINT "replace= ["; LGETSTR$(tmp(), 1, LLEN(tmp())); "]"
LONGSTRING TRIM tmp(), 3
PRINT "trim3  = ["; LGETSTR$(tmp(), 1, LLEN(tmp())); "]"
LONGSTRING RESIZE tmp(), 4
PRINT "resize4= ["; LGETSTR$(tmp(), 1, LLEN(tmp())); "]"
LONGSTRING SETBYTE tmp(), 0, 90
PRINT "setbyte= ["; LGETSTR$(tmp(), 1, LLEN(tmp())); "]"

PRINT "-- UCASE / LCASE / LGETBYTE / LINSTR / LCOMPARE --"
LONGSTRING CLEAR buf()
LONGSTRING APPEND buf(), "Mixed Case Text"
LONGSTRING UCASE buf()
PRINT "ucase  = ["; LGETSTR$(buf(), 1, LLEN(buf())); "]"
LONGSTRING LCASE buf()
PRINT "lcase  = ["; LGETSTR$(buf(), 1, LLEN(buf())); "]"
PRINT "byte 0 ="; LGETBYTE(buf(), 0); " (m)"
PRINT "instr 'case' ="; LINSTR(buf(), "case")
PRINT "instr 'case' from 8 ="; LINSTR(buf(), "case", 8)
PRINT "instr 'zzz'  ="; LINSTR(buf(), "zzz")

LONGSTRING CLEAR tmp()
LONGSTRING APPEND tmp(), "mixed case text"
PRINT "compare equal ="; LCOMPARE(buf(), tmp())
LONGSTRING APPEND tmp(), "!"
PRINT "compare shorter ="; LCOMPARE(buf(), tmp())

PRINT "-- LONGSTRING PRINT and file round trip --"
LONGSTRING CLEAR buf()
LONGSTRING APPEND buf(), "written through a long string"
OPEN "ls.txt" FOR OUTPUT AS #1
LONGSTRING PRINT #1, buf()
CLOSE #1

OPEN "ls.txt" FOR INPUT AS #1
n = LINPUT(other(), 1, 500)
CLOSE #1
PRINT "read back"; n; "bytes:"
LONGSTRING PRINT other();
PRINT
KILL "ls.txt"

' ---------------- GOSUB / RETURN ----------------
PRINT "-- GOSUB / RETURN --"
count = 0
GOSUB simple
PRINT "back from simple, count ="; count

' nested GOSUB
GOSUB outer
PRINT "back from outer, count ="; count

' GOSUB inside a loop, and from inside an IF
FOR i = 1 TO 3
  GOSUB bump
NEXT i
PRINT "after loop, count ="; count

IF count > 0 THEN GOSUB bump
PRINT "after IF, count ="; count

' ON n GOSUB
FOR i = 1 TO 3
  ON i GOSUB first, second, third
NEXT i
PRINT "after ON GOSUB, tally$ = ["; tally$; "]"

GOSUB inside_sub_test
PRINT "done"
END

simple:
  count = count + 1
  RETURN

outer:
  count = count + 10
  GOSUB inner
  count = count + 100
  RETURN

inner:
  count = count + 1000
  RETURN

bump:
  count = count + 1
  RETURN

first:
  tally$ = tally$ + "1"
  RETURN
second:
  tally$ = tally$ + "2"
  RETURN
third:
  tally$ = tally$ + "3"
  RETURN

inside_sub_test:
  ShowIt
  RETURN

SUB ShowIt
  ' a GOSUB entirely inside a SUB is fine - it never crosses the boundary
  LOCAL INTEGER k
  k = 0
  GOSUB local_target
  PRINT "gosub inside a SUB gave k ="; k
  EXIT SUB
local_target:
  k = 42
  RETURN
END SUB
