' REFUSE: an array of a TYPE needs constant bounds
' a TYPE array DIMmed again after ERASE with other bounds: refused, not
' kept at its first size (the PicoMite session's #31 review)
Type T
  x As Integer
End Type
Dim p(2) As T
Erase p
Dim p(5) As T
p(5).x = 5
Print p(5).x
