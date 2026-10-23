' SUB MM.END runs when the program runs off its end (8a9e3b6) - it ran
' only at END there before, and never here.  Run on a real PicoMite: the
' output must be identical.  (mmendend, mmendnoend and mmenderr are the
' END, END NOEND and error-exit cases.)
Sub MM.End
  Print "MM.END ran"
End Sub
Print "main"
