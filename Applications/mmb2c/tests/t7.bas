' t7.bas - Tier A: DATA/READ/RESTORE, SORT, CONTINUE, INC, CAT,
'           ON GOTO, ERROR, PAUSE, TIMER=, ARRAY/MATH, ERASE
OPTION BASE 0

DIM INTEGER i, j, n
DIM f, total
DIM s$, t$

' ---------------- DATA / READ ----------------
PRINT "-- DATA / READ --"
DATA 10, 20, 30
DATA "alpha", beta, "gamma"
numbers:
DATA 1.5, 2.5, 3.5
words:
DATA "one", "two"

READ i, j, n
PRINT "ints:"; i; j; n
READ s$, t$
PRINT "strs: ["; s$; "]["; t$; "]"
READ s$
PRINT "third: ["; s$; "]"

RESTORE numbers
READ f
PRINT "after RESTORE numbers: "; f

RESTORE words
READ s$, t$
PRINT "after RESTORE words: ["; s$; "]["; t$; "]"

' READ into a whole array, and the READ SAVE / RESTORE pair
DIM three(2)
RESTORE numbers
READ three()
PRINT "array read:"; three(0); three(1); three(2)

RESTORE numbers
READ f
READ SAVE
READ f
PRINT "inner read ="; f
READ RESTORE
READ f
PRINT "after READ RESTORE ="; f

' a number READ as a string, and text READ as a number
RESTORE
READ s$
PRINT "10 as a string: ["; s$; "]"

' ---------------- SORT ----------------
PRINT "-- SORT --"
DIM INTEGER v(5) = (50, 20, 40, 10, 30, 60)
DIM INTEGER ix(5)
SORT v(), ix()
PRINT "sorted:";
FOR i = 0 TO 5 : PRINT v(i); : NEXT i
PRINT
PRINT "index :";
FOR i = 0 TO 5 : PRINT ix(i); : NEXT i
PRINT

DIM INTEGER w(4) = (3, 1, 4, 1, 5)
SORT w(), , 1                       ' reverse
PRINT "reverse:";
FOR i = 0 TO 4 : PRINT w(i); : NEXT i
PRINT

DIM names$(4)
names$(0) = "delta" : names$(1) = "Alpha" : names$(2) = ""
names$(3) = "charlie" : names$(4) = "bravo"
SORT names$(), , 2 + 4              ' case independent, empties last
PRINT "names:";
FOR i = 0 TO 4 : PRINT " ["; names$(i); "]"; : NEXT i
PRINT

DIM INTEGER p(5) = (9, 8, 7, 3, 2, 1)
SORT p(), , 0, 2, 3                 ' sort three elements from index 2
PRINT "partial:";
FOR i = 0 TO 5 : PRINT p(i); : NEXT i
PRINT

' ---------------- CONTINUE ----------------
PRINT "-- CONTINUE --"
total = 0
FOR i = 1 TO 6
  IF i = 3 THEN CONTINUE FOR
  total = total + i
NEXT i
PRINT "sum skipping 3 ="; total

i = 0 : total = 0
DO
  i = i + 1
  IF i = 2 THEN CONTINUE DO
  total = total + i
LOOP UNTIL i >= 4
PRINT "do sum skipping 2 ="; total

' ---------------- INC and CAT ----------------
PRINT "-- INC / CAT --"
n = 5
INC n
INC n, 10
PRINT "n ="; n
f = 1.5
INC f, -0.25
PRINT "f ="; f
s$ = "abc"
INC s$, "def"
CAT s$, "ghi"
PRINT "s$ = ["; s$; "]"

' ---------------- ON n GOTO ----------------
PRINT "-- ON GOTO --"
FOR i = 1 TO 3
  ON i GOTO one, two, three
  PRINT "fell through"
  GOTO after
one:
  PRINT "  target one"
  GOTO after
two:
  PRINT "  target two"
  GOTO after
three:
  PRINT "  target three"
after:
NEXT i

' ---------------- ARRAY / MATH whole array ----------------
PRINT "-- ARRAY / MATH --"
DIM a(4), b(4)
ARRAY SET 2.5, a()
PRINT "set:"; a(0); a(4)
MATH SCALE a(), 4, b()
PRINT "scaled:"; b(0); b(4)
ARRAY ADD b(), 1, b()
PRINT "added:"; b(0); b(4)

DIM stat(6) = (4, 8, 15, 16, 23, 42, 4)
PRINT "sum ="; MATH(SUM stat())
PRINT "mean ="; MATH(MEAN stat())
PRINT "max ="; MATH(MAX stat(), i)
PRINT "  at index"; i
PRINT "min ="; MATH(MIN stat(), i)
PRINT "  at index"; i
PRINT "median ="; MATH(MEDIAN stat())
PRINT "sd ="; MATH(SD stat())

' ---------------- TIMER and PAUSE ----------------
PRINT "-- TIMER / PAUSE --"
TIMER = 0
PAUSE 30
PRINT "timer advanced: "; CHOICE(TIMER >= 0, "yes", "no")

' ---------------- ERASE ----------------
PRINT "-- ERASE --"
ERASE stat(), s$
PRINT "after erase: stat(0) ="; stat(0); " s$ = ["; s$; "]"

PRINT "done"
END
