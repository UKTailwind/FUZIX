' REFUSE: CSUB is not translated: it is PicoMite machine code
' A CSUB is machine code for the PicoMite's interpreter.  Its hex was
' read as BASIC - 1C0569DB tokenises as line number 1 - so the block
' gave "Duplicate label", or before that was commented out with rc 0.
CSUB Twice integer
  00000000
  1C0569DB 1C056823 4770BF00
End CSub
Twice 3
Print "after"
