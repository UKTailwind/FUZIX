' REFUSE: Invalid syntax
' cmd_let takes the statement's first = as the assignment: one inside the
' target's subscript is "Invalid syntax" (22cd802), not a comparison.
Dim a(3)
i = 1
a(i = 1) = 5
