' VAL, and a CHOICE of an integer and a float, have the type they get
' when they run, and PRINT and STR$ show it: 2000000, not 2e+06 (FUZIX
' issues #2 and #3).  An integer target takes either exactly.
Print Val("2000000"); Val("2000000.0"); Val("-3")
Print Str$(Val("2000000")); " "; Str$(Val("2.5e6"))
b% = -Val("9007199254740993")
Print b%
Print Choice(1, 2000000, 0.5); Choice(0, 2000000, 0.5)
Print Str$(Choice(1, 2000000, 0.5)); " "; Str$(Choice(0, 0.5, 3000000))
k% = Choice(1, 9007199254740993, 0.5)
Print k%
Print Choice(1, 0.5, 2000000);
Print Choice(0, 0.5, 2000000);
Print
Print Choice(1, 2.0, 2.5); Choice(1, 2000000.0, 2.5)
