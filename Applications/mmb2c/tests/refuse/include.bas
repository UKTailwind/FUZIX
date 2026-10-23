' REFUSE: #INCLUDE is not translated
' The loader reads an #INCLUDE file in at that line (FileIO.c); dropped
' as a statement it could not parse, the file's SUBs were simply missing.
#INCLUDE "lib.inc"
Print "after"
