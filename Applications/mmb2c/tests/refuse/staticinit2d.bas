' REFUSE: an initialiser list on a run-time DIM is only supported for 1-D arrays
' a 2-D STATIC with a run-time bound and a list: refused, not commented
' out and then crashed on (the PicoMite session's #32 review)
n = 1
S
Sub S
  Static w(n, 1) = (1, 2, 3, 4)
  Print w(1, 1)
End Sub
