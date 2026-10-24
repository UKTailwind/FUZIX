' LIBRARY LOAD: the library's source is translated with the program.
' Its top level (CONSTs, DIMs and anything else) runs at RUN before the
' program's first line, and its SUBs and FUNCTIONs are the program's.
' Comments, blank lines and OPTIONs may come before it (Elite's form).
OPTION EXPLICIT

LIBRARY LOAD MM.INFO(PATH) + "lib/libload_a.bas", "lib/libload_b.bas", O
PRINT "program starts"
PRINT "LIBMAX"; LIBMAX
libcount = libcount + 1
PRINT "count"; libcount
PRINT "twice 21 ="; twice(21)
greet "PicoMite"
PRINT shout$("done")
END
