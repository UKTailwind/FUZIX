' DIR$ with a directory in the spec - FileIO.c fun_dir splits it at its
' last /: the directory searched, then the pattern, "*" when nothing
' follows the /; the name alone comes back.  Elite looks for its title
' picture as DIR$(MM.INFO(PATH) + "title.jpg", FILE).
MKDIR "dirpath.d"
OPEN "dirpath.d/one.txt" FOR OUTPUT AS #1
CLOSE #1
PRINT DIR$("dirpath.d/*.txt", FILE)
PRINT DIR$("dirpath.d/one.txt", FILE)
PRINT "[" + DIR$("dirpath.d/two.txt", FILE) + "]"
PRINT DIR$("dirpath.d/", FILE)
PRINT DIR$("./dirpath.d", DIR)
KILL "dirpath.d/one.txt"
RMDIR "dirpath.d"
