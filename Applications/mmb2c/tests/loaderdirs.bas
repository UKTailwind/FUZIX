' The PicoMite's loader takes every line that starts with '#' for itself
' (FileIO.c): #COMMENT START to #COMMENT END makes the lines between
' comments, whatever they hold, and by default (#MMDEBUG OFF) an MMDEBUG
' statement is a comment too.  A '#' line it does not know is dropped.
' Run on a real PicoMite, loaded from a file: the output must be identical.
Print "before"
#COMMENT START
Print "inside the comment"
#DEFINE "A", "B"
x = 1 / 0
  #comment   end
Print "after"
MMDEBUG Print "debug"
	#nothing we know
Print "done"
