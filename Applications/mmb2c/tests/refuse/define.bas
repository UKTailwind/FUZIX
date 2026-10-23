' REFUSE: #DEFINE is not translated
' The loader substitutes a #DEFINE through the lines after it (FileIO.c),
' so dropping the line - as a statement it could not parse - lost every
' substitution with rc 0.
#DEFINE "LIMIT", "10"
For i = 1 To LIMIT
Next
Print i
