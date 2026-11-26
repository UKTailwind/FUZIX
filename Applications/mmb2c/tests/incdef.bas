' #INCLUDE and #DEFINE, as the CMM2 loader has them (FileIO.c; PicoMite
' 043f914 and 1179345).  Both were refused.  A #DEFINE changes every later
' line, an include's too: a whole word in any case, not in quotes or a
' comment, the latest #DEFINE first.  incdef.inc is beside this file.
#DEFINE "MAXN", "10"
#DEFINE "Count", "TOTAL"
#define "twice", "2 *"
#DEFINE "PRINT DOUBLE", "PRINT 2*"
#INCLUDE "incdef"
Dim total = 0, account = 5, maxn2 = 3
For i = 1 To MAXN
  count = count + i       ' Count, MAXN and HALF are left alone here
Next
Print total; account; maxn2
Print "MAXN Count"; maxn
Print Greet$("x")
Print HALF
#COMMENT START
#DEFINE "TOTAL", "99"
#COMMENT END
Print total
Read a, b$ : Print a; b$
REM maxn is a comment
Print twice 21
Print   double 4
Data maxn, "maxn"
