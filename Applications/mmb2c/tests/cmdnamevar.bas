' A command's name as a variable (PicoMite #149, 07baeff): at the start
' of a statement NAME = x is an assignment.  Suffixed names never were
' commands; the bare ones were refused until now.
flash = 1
out% = 2
fm$ = "s"
line = 3
font! = 4.5
box = flash + out% + line
Print flash; out%; fm$; line; font!; box
