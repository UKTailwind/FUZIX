' BY VAL and BY REF as two words, as CheckByKeyword takes them.
Sub S(By Val a, By Ref b)
  a = a + 1 : b = b + 1
End Sub
x = 1 : y = 1
S x, y
Print x; y
