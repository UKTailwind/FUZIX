' "# DEFINE" is a #DEFINE too: the loader reads it with checkstring,
' which steps over a blank (FileIO.c).  Both forms were refused.
# DEFINE "LIMIT", "10"
#DEFINE "Twice", "2 *"
For i = 1 To LIMIT
Next
Print i; twice i
