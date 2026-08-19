' POS and FLUSH.
'
' POS is the column the next character will go in - 1 at the start of a
' line, which is MMBasic's convention (its own code tests MMCharPos > 1
' for "am I at the beginning").  The runtime has tracked it all along
' for TAB; POS only gives it a name.
'
' FLUSH #n pushes a file's buffer out.  What it does is not visible from
' inside the program that wrote it, so what is checked here is that it
' runs, that channel 0 is accepted and does nothing, and that the data
' is readable afterwards.
Dim s$
Dim integer p

Print "POS"
' captured BEFORE anything else is printed on the line, or the label
' itself moves the column and the number means nothing
p = Pos
Print "  at line start   "; p; " (1 expected)"
Print "abc";
p = Pos
Print "   after abc      "; p; " (4 expected)"
Print "abcdefghij";
p = Pos
Print "   after 10 more  "; p; " (11 expected)"
Print

Print "FLUSH"
Open "posflush.tmp" For Output As #1
Print #1, "first line"
Flush #1
Print #1, "second line"
Flush #1
Close #1
Print "  wrote and flushed twice"

Flush #0
Print "  FLUSH #0 accepted (the console: does nothing)"

Open "posflush.tmp" For Input As #2
Line Input #2, s$
Print "  read back      "; s$
Line Input #2, s$
Print "  read back      "; s$
Close #2
Kill "posflush.tmp"
Print "  done"
