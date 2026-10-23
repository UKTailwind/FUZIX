' CMM2 LOAD and CMM2 RUN's loader (FileIO.c FileLoadCMM2Program) takes
' every line that starts with '#' for itself: #COMMENT START to #COMMENT
' END makes the lines between comments, whatever they hold, and by
' default (#MMDEBUG OFF) an MMDEBUG statement is a comment too.  A '#'
' line it does not know is dropped.  Plain LOAD and RUN refuse a '#'
' line ("Invalid character"), so it is that loader this follows.
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
