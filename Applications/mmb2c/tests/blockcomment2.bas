10 ' /* */ blocks, the second review (FUZIX issue #24): a numbered */ ends
20 ' the block, and after THEN a statement starts, so IF 1 THEN /* opens
30 ' one, even when the IF is false - the tokeniser decides, not the run.
40 Print "a"
50 /*
60 Print "b"
70 */
80 Print "c"
90 If 1 Then /* the IF's statement, and a block
100 Print "d"
110 */ : Print "e"
120 If 0 Then /* never runs, but the block is a block all the same
130 Print "f"
140 */
150 Print "g"
