10 ' /* */ blocks with line numbers (FUZIX issues #24 and #33): the
20 ' tokeniser takes the number off first, so a numbered /* or */ is still
30 ' the first thing on its line, and a GOTO to a */ line lands after it.
40 Print "a"
50 /*
60 Print "b"
70 */
80 Print "c"
90 GoTo 120
100 /* the GOTO steps over this block
110 Print "d"
120 */ the end of the block
130 Print "e"
