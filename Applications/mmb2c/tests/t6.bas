' t6.bas - file handling
DIM s$, a$, b$
DIM INTEGER n, i
DIM f

' ---- write a file ----
OPEN "work.txt" FOR OUTPUT AS #1
PRINT #1, "first line"
PRINT #1, "second line"
PRINT #1, "alpha, 42, 3.5"
PRINT #1, "one"; " "; "two"
PRINT #1, 100; 200
CLOSE #1

' ---- append ----
OPEN "work.txt" FOR APPEND AS #2
PRINT #2, "appended"
CLOSE #2

' ---- read it back line by line ----
PRINT "-- LINE INPUT --"
OPEN "work.txt" FOR INPUT AS #1
n = 0
DO WHILE NOT EOF(#1)
  LINE INPUT #1, s$
  n = n + 1
  PRINT n; ": ["; s$; "]"
LOOP
CLOSE #1
PRINT "lines ="; n

' ---- read comma separated fields ----
PRINT "-- INPUT # --"
OPEN "work.txt" FOR INPUT AS #3
LINE INPUT #3, s$          ' skip line 1
LINE INPUT #3, s$          ' skip line 2
INPUT #3, a$, i, f
PRINT "a$=["; a$; "] i="; i; " f="; f
CLOSE #3

' ---- LOF, LOC, SEEK and INPUT$ ----
PRINT "-- random access --"
OPEN "work.txt" FOR RANDOM AS #4
PRINT "lof ="; LOF(#4)
SEEK #4, 1
PRINT "loc after seek 1 ="; LOC(#4)
PRINT "first5 = ["; INPUT$(5, #4); "]"
PRINT "loc now ="; LOC(#4)
SEEK #4, 7
PRINT "next4 = ["; INPUT$(4, #4); "]"
CLOSE #4

' ---- rewrite a record in place ----
OPEN "work.txt" FOR RANDOM AS #5
SEEK #5, 1
PRINT #5, "FIRST"
CLOSE #5
OPEN "work.txt" FOR INPUT AS #6
LINE INPUT #6, s$
PRINT "after patch: ["; s$; "]"
CLOSE #6

' ---- copy, rename, kill ----
PRINT "-- housekeeping --"
COPY "work.txt" TO "copy.txt"
OPEN "copy.txt" FOR INPUT AS #1
PRINT "copy lof ="; LOF(#1)
CLOSE #1
RENAME "copy.txt" AS "moved.txt"
OPEN "moved.txt" FOR INPUT AS #1
LINE INPUT #1, s$
PRINT "moved first line: ["; s$; "]"
CLOSE #1

' ---- directories and DIR$ ----
MKDIR "subdir"
OPEN "subdir/inner.txt" FOR OUTPUT AS #1
PRINT #1, "inside"
CLOSE #1
CHDIR "subdir"
PRINT "cwd ends with subdir: "; CHOICE(RIGHT$(CWD$, 6) = "subdir", "yes", "no")
a$ = DIR$("*.txt", FILE)
DO WHILE a$ <> ""
  PRINT "found ["; a$; "]"
  a$ = DIR$()
LOOP
CHDIR ".."

' ---- tidy up ----
KILL "subdir/inner.txt"
RMDIR "subdir"
KILL "moved.txt"
KILL "work.txt"
PRINT "cleaned up"
END
