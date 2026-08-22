' Drive-letter path mapping - the WebMite spelling of a file next to
' the program: "A:/x" (and "A:\x", and drive-relative "A:x") all map
' to the plain name.  A BARE leading / stays the machine's own
' absolute path - /etc/gmail.conf is load-bearing (PLAN-web.md 12.4).
Dim n%, s$
Open "A:/pm_t.dat" For Output As #1
Print #1, "hello"
Close #1
Print Mm.Info(EXISTS FILE "pm_t.dat")
Print Mm.Info(FILESIZE "A:\pm_t.dat")
Open "a:pm_t.dat" For Input As #1
Line Input #1, s$
Close #1
Print s$
Kill "A:/pm_t.dat"
Print Mm.Info(EXISTS FILE "pm_t.dat")
End
