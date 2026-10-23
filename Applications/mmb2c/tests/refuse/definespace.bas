' REFUSE: #DEFINE is not translated
' The loader reads #DEFINE with checkstring, which steps over a blank:
' "# DEFINE" is one too, and it was dropped without a word.
# DEFINE "A", "B"
Print "A"
