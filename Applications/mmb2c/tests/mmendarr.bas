' SUB MM.END is called with no arguments, so an array parameter cannot
' be filled: the program runs, then stops at its end with "Incompatible
' type" (a PicoMitePC3 adds stray argument text).  FUZIX issue #17: this
' crashed the translator.
Sub MM.End(a())
  Print "not reached"
End Sub
Print "main"
