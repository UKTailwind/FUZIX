' ON ERROR IGNORE anywhere arms the checked forms for the whole program,
' subscripts included, so once ABORT is back in force an access past the
' end stops it with the reference's "Index out of bounds" - and with no
' ON ERROR in a program the same access is not checked at all, which is
' why this lives in a program of its own.
Dim a%(3)
ON ERROR IGNORE
a(5) = 1
Print "ignored: " MM.ERRMSG$
ON ERROR ABORT
a(3) = 2 : Print "a(3) ok "; a(3)
a(10) = 1
Print "NOT REACHED"
