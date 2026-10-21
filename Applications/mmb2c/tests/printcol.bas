' PRINT's column (753ace5): a comma moves it to the next 8-column stop,
' where a terminal shows the tab - not a 14-column zone (#37) - and a
' file has a column of its own: TAB() and POS inside PRINT #n count from
' it, and the console's is left as it was (#38).
Print 1, 2; Tab(30); "e"
Print "abc",; : Print Pos
Open "pcol.txt" For Output As #1
Print "xy";
Print #1, "data"
Print #1, "d"; Tab(5); "y"; Pos;
Print #1, ""
Print Tab(10); "con at "; Pos
Close #1
Open "pcol.txt" For Input As #1
Line Input #1, a$ : Print "[" a$ "]"
Line Input #1, a$ : Print "[" a$ "]"
Close #1
Kill "pcol.txt"
' TAB() outside 1..255 is the reference's range error, not a clamp
On Error Skip 1
Print Tab(0)
Print MM.ErrMsg$
