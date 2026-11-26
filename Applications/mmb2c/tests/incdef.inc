' incdef.bas's include: a FUNCTION, and a #DEFINE that applies after it,
' in this file and in the program, the earlier #DEFINEs applied to it
#DEFINE "HALF", "(MAXN / 2)"
Function Greet$(s$)
  Greet$ = "hi " + s$ + Str$(MAXN)
End Function
