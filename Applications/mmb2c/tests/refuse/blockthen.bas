' REFUSE: /* must be the first thing on its line
' By the manual /* is the first thing on its line (FUZIX #33).  RC5's
' tokeniser takes it after THEN all the same, and with the IF false the
' block's lines run: here they would be "Unknown command".
If 0 Then /* never runs
this is not BASIC
*/
Print "end"
