' TAB() is 1 to 255 with no clamp short of that (FUZIX issue #10).
' Past the column already it is CR LF and col - 1 spaces, so only
' TAB(255) from beyond column 255 is too long for a string.
Print Len(Tab(255))
Print String$(254, "-");
Print Len(Tab(254))
Print String$(255, "-");
On Error Skip 1
t$ = Tab(255)
Print
Print MM.ErrMsg$
