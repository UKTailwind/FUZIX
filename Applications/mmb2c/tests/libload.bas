' LIBRARY LOAD: the library's source is translated with the program.
' Its top level (CONSTs, DIMs and anything else) runs at RUN before the
' program's first line, and its SUBs and FUNCTIONs are the program's.
' It must be the program's first statement: only comments, blank lines and
' OPTION SYMBOLS OFF may come before it.  OPTIONs follow it, as in Elite
' and Exile.

LIBRARY LOAD MM.INFO(PATH) + "lib/libload_a.bas", "lib/libload_b.bas", O
OPTION EXPLICIT
PRINT "program starts"
PRINT "LIBMAX"; LIBMAX
libcount = libcount + 1
PRINT "count"; libcount
PRINT "twice 21 ="; twice(21)
greet "PicoMite"
PRINT shout$("done")
END
