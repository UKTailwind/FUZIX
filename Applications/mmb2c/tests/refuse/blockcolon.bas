' REFUSE: /* must be the first thing on its line
' A /* after a ':' breaks the manual's rule (FUZIX #33).
x = 1 : /* a block from the middle of a line
Print "inside"
*/
Print x
