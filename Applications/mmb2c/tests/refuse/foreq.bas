' REFUSE: FOR with misplaced = or TO
' cmd_for splits on =, TO and STEP outside brackets (112ce55): a second
' = there is an error, and only inside brackets a comparison.
k = 2
For i = k = 2 To 3
Next
