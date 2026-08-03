' Every stage of the heap split, one at a time, each announced BEFORE it
' runs.  The last line printed names the stage that killed the machine,
' which a single register dump cannot.  Do NOT pipe this through tail.
Print "stage 1: alive"

Dim Float a(4)
Print "stage 2: float array declared"
For i = 0 To 4
  a(i) = i * 1.5
Next i
Print "stage 3: float array written"
Print "  a(0)="; a(0); " a(4)="; a(4)

Dim Integer n(4)
For i = 0 To 4
  n(i) = i * 7
Next i
Print "stage 4: integer array"; n(0); n(4)

Dim s$
s$ = "hello"
Print "stage 5: scalar string ["; s$; "]"

s$ = s$ + " world"
Print "stage 6: concat ["; s$; "]"

Print "stage 7: len"; Len(s$); " mid ["; Mid$(s$, 2, 3); "]"

Dim t$(3)
For i = 0 To 3
  t$(i) = "e" + Str$(i)
Next i
Print "stage 8: string array ["; t$(0); "]["; t$(3); "]"

Dim Float d(3)
For i = 0 To 3
  Read d(i)
Next i
Print "stage 9: READ float"; d(0); d(3)

Dim u$
Read u$
Print "stage 10: READ string ["; u$; "]"

Dim Float m(2, 2)
For i = 0 To 2
  For j = 0 To 2
    m(i, j) = i * 10 + j
  Next j
Next i
Print "stage 11: 2d array"; m(0, 0); m(2, 2)

Print "stage 12: all done"

Data 1.5, 2.5, 3.5, 4.5
Data "datastring"
